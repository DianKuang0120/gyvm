#include "backend.h"
#include "store.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QProcessEnvironment>
#include <QRegularExpression>

#include <unistd.h>

Backend::Backend(QObject *parent) : QObject(parent)
{
    // 系统总线上有 gyvmd 就用它（免密码提权）；否则回退到本地 sudo -A。
    QDBusConnection bus = QDBusConnection::systemBus();
    if (bus.isConnected()) {
        QDBusConnectionInterface *ci = bus.interface();
        if (ci && ci->isServiceRegistered(QStringLiteral("org.gyvm.Daemon1"))) {
            m_daemon = new QDBusInterface(QStringLiteral("org.gyvm.Daemon1"),
                                          QStringLiteral("/org/gyvm/Daemon"),
                                          QStringLiteral("org.gyvm.Daemon1"), bus, this);
            bus.connect(QStringLiteral("org.gyvm.Daemon1"), QStringLiteral("/org/gyvm/Daemon"),
                        QStringLiteral("org.gyvm.Daemon1"), QStringLiteral("Output"), this,
                        SLOT(onDaemonOutput(QString, QByteArray)));
            bus.connect(QStringLiteral("org.gyvm.Daemon1"), QStringLiteral("/org/gyvm/Daemon"),
                        QStringLiteral("org.gyvm.Daemon1"), QStringLiteral("StateChanged"), this,
                        SLOT(onDaemonState(QString, bool)));
        }
    }
}

bool Backend::daemonAvailable() const
{
    return m_daemon != nullptr;
}

bool Backend::setShmemThp(const QString &mode)
{
    if (!m_daemon)
        return false;
    QDBusReply<bool> rep = m_daemon->call(QStringLiteral("SetShmemThp"), mode);
    return rep.isValid() && rep.value();
}

// 把一条命令包装进提权前缀（sudo -A / pkexec）并执行。
// 返回 {是否成功, 错误说明}
QPair<bool, QString> Backend::runPrivileged(const QString &script)
{
    if (m_prefix.isEmpty())
        return {false, QStringLiteral("没有配置提权前缀，无法执行需要 root 的操作")};

    QProcess p;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (m_prefix.contains(QStringLiteral("sudo")) &&
        QFile::exists(QStringLiteral("/usr/bin/ksshaskpass")))
        env.insert(QStringLiteral("SUDO_ASKPASS"), QStringLiteral("/usr/bin/ksshaskpass"));
    p.setProcessEnvironment(env);

    QStringList full = m_prefix;
    full << QStringLiteral("/bin/sh") << QStringLiteral("-c") << script;

    p.start(full.first(), full.mid(1));
    if (!p.waitForStarted(5000))
        return {false, QStringLiteral("无法启动提权程序：%1").arg(p.errorString())};
    if (!p.waitForFinished(60000)) {
        p.kill();
        return {false, QStringLiteral("提权命令超时")};
    }
    if (p.exitCode() != 0) {
        const QString err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
        return {false, err.isEmpty() ? QStringLiteral("提权命令失败（退出码 %1）").arg(p.exitCode())
                                     : err};
    }
    return {true, {}};
}

QString Backend::setupNetwork(const QString &tap, const QString &subnet, const QString &uplink)
{
    if (m_daemon) {
        QDBusReply<QString> rep =
            m_daemon->call(QStringLiteral("SetupNetwork"), tap, subnet, uplink);
        if (!rep.isValid())
            return rep.error().message();
        return rep.value();
    }

    // 没有 daemon：退回到提权前缀（sudo -A + ksshaskpass / pkexec），
    // 执行的命令和 daemon 里做的完全一致。
    if (tap.isEmpty() ||
        !QRegularExpression(QStringLiteral("^[a-zA-Z0-9_.-]{1,15}$")).match(tap).hasMatch())
        return QStringLiteral("接口名不合法：%1").arg(tap);

    const QStringList parts = subnet.split(QLatin1Char('/'));
    if (parts.size() != 2)
        return QStringLiteral("子网格式不对：%1").arg(subnet);
    const QString addr = parts.at(0);
    const int prefix = parts.at(1).toInt();
    if (prefix < 16 || prefix > 29)
        return QStringLiteral("前缀长度不合理：%1").arg(prefix);
    const int slash = addr.lastIndexOf(QLatin1Char('.'));
    if (slash < 0)
        return QStringLiteral("子网地址不合法：%1").arg(subnet);
    const QString gateway = addr.left(slash) + QStringLiteral(".1");

    QString up = uplink;
    if (up.isEmpty()) {
        QProcess r;
        r.start(QStringLiteral("ip"),
                {QStringLiteral("-4"), QStringLiteral("route"), QStringLiteral("show"),
                 QStringLiteral("default")});
        if (r.waitForFinished(5000)) {
            const QString out = QString::fromLocal8Bit(r.readAllStandardOutput());
            for (const QString &line : out.split(QLatin1Char('\n'))) {
                if (!line.startsWith(QLatin1String("default")))
                    continue;
                const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                const int i = f.indexOf(QStringLiteral("dev"));
                if (i >= 0 && i + 1 < f.size()) {
                    up = f.at(i + 1);
                    break;
                }
            }
        }
    }
    if (up.isEmpty())
        return QStringLiteral("找不到默认路由所在的出口接口，无法配 NAT");

    // 一次性脚本：建 tap、配地址、开转发、加 NAT。
    // 幂等：tap 已存在就复用，NAT 规则先删再加。
    const QString script =
        QStringLiteral(
            "set -e\n"
            "ip link show dev %1 >/dev/null 2>&1 || ip tuntap add dev %1 mode tap\n"
            "ip addr flush dev %1 2>/dev/null || true\n"
            "ip addr add %2/%3 dev %1\n"
            "ip link set dev %1 up\n"
            "sysctl -qw net.ipv4.ip_forward=1\n"
            "iptables -t nat -D POSTROUTING -s %4 -o %5 -j MASQUERADE 2>/dev/null || true\n"
            "iptables -t nat -A POSTROUTING -s %4 -o %5 -j MASQUERADE\n")
            .arg(tap, gateway)
            .arg(prefix)
            .arg(subnet, up);

    const auto r = runPrivileged(script);
    return r.first ? QString() : r.second;
}

void Backend::teardownNetwork(const QString &tap, const QString &subnet, const QString &uplink)
{
    if (!m_daemon)
        return;
    m_daemon->call(QStringLiteral("TeardownNetwork"), tap, subnet, uplink);
}

QString Backend::defaultUplink() const
{
    if (!m_daemon)
        return {};
    QDBusReply<QString> rep = m_daemon->call(QStringLiteral("DefaultUplink"));
    return rep.isValid() ? rep.value() : QString();
}

void Backend::onDaemonOutput(const QString &name, const QByteArray &data)
{
    emit output(name, data);
}

void Backend::onDaemonState(const QString &name, bool running)
{
    emit stateChanged(name, running);
}

Backend::~Backend()
{
    const auto procs = m_procs;
    for (QProcess *p : procs) {
        p->terminate();
        if (!p->waitForFinished(2000)) {
            p->kill();
            p->waitForFinished(1000);
        }
    }
}

QString Backend::waylandSocket()
{
    const QString runtime = QString::fromLocal8Bit(qgetenv("XDG_RUNTIME_DIR"));
    if (runtime.isEmpty())
        return {};
    QString disp = QString::fromLocal8Bit(qgetenv("WAYLAND_DISPLAY"));
    if (disp.isEmpty())
        disp = QStringLiteral("wayland-0");
    return runtime + QLatin1Char('/') + disp;
}

QStringList Backend::buildArgs(const VmConfig &vm, const QString &rwFlag)
{
    QStringList a;
    a << QStringLiteral("--no-syslog") << QStringLiteral("run")
      << QStringLiteral("--hypervisor") << QStringLiteral("gunyah")
      << QStringLiteral("--disable-sandbox");

    if (vm.protectedVm)
        a << QStringLiteral("--protected-vm-without-firmware");

    a << QStringLiteral("--mem") << QString::number(vm.memoryMiB)
      << QStringLiteral("--cpus") << QString::number(vm.cpus);

    if (vm.hugepages)
        a << QStringLiteral("--hugepages");

    if (vm.gpu) {
        a << QStringLiteral("--gpu")
          << QStringLiteral("backend=2d,displays=[[mode=windowed[%1,%2]]]")
                 .arg(vm.gpuWidth)
                 .arg(vm.gpuHeight);
        const QString ws = waylandSocket();
        if (!ws.isEmpty())
            a << QStringLiteral("--wayland-sock") << ws;
        // 让显示窗口捕获键鼠（否则窗口只显示、不能交互）
        a << QStringLiteral("--display-window-keyboard")
          << QStringLiteral("--display-window-mouse");
    }

    if (vm.net) {
        const QString tap = vm.tapName.isEmpty() ? QStringLiteral("gyvm0") : vm.tapName;
        a << QStringLiteral("--net") << (QStringLiteral("tap-name=") + tap);
    }

    // 内核命令行：默认串口控制台 + 不 panic，用户可在「内核命令行」里追加
    QString cmdline = QStringLiteral("console=tty0 console=ttyS0 panic=0");
    if (!vm.rootDevice.isEmpty()) {
        // 允许用户直接填 UUID=... 或 /dev/vda1，也允许填完整的 "root=..."
        if (vm.rootDevice.startsWith(QLatin1String("root=")))
            cmdline += QLatin1Char(' ') + vm.rootDevice;
        else
            cmdline += QStringLiteral(" root=") + vm.rootDevice;
    }
    if (!vm.extraCmdline.trimmed().isEmpty())
        cmdline += QLatin1Char(' ') + vm.extraCmdline.trimmed();

    a << QStringLiteral("--serial")
      << QStringLiteral("type=stdout,hardware=serial,console=true,stdin=true")
      << QStringLiteral("--params") << cmdline;

    if (!vm.initrd.isEmpty())
        a << QStringLiteral("--initrd") << vm.initrd;

    for (const DiskAttach &d : vm.disks) {
        if (d.cdrom)
            a << QStringLiteral("--scsi-block") << (d.path + QStringLiteral(",ro=true"));
        else if (d.readOnly)
            a << QStringLiteral("--block") << (d.path + QStringLiteral(",ro=true,lock=false"));
        else
            a << QStringLiteral("--block") << (d.path + QStringLiteral(",lock=false"));
    }

    // UEFI 变量存储（UEFI 启动需要）
    if (!vm.pflash.isEmpty())
        a << QStringLiteral("--pflash")
          << QStringLiteral("path=%1,block_size=4096").arg(vm.pflash);

    // 客户机内核日志跨重启保留
    if (vm.pstore)
        a << QStringLiteral("--pstore")
          << QStringLiteral("path=%1/pstore.bin,size=1048576").arg(Store::vmDir(vm.name));

    // virtio-fs 共享目录：<宿主目录>:<tag>:type=fs:cache=always
    for (const SharedDir &s : vm.sharedDirs) {
        if (s.path.isEmpty() || s.tag.isEmpty())
            continue;
        QString spec = s.path + QLatin1Char(':') + s.tag + QStringLiteral(":type=fs:cache=always");
        if (s.readOnly)
            spec += QStringLiteral(":writeback=never");
        a << QStringLiteral("--shared-dir") << spec;
    }

    a << vm.extraArgs;
    a << vm.kernel; // 位置参数：客户机内核，必须放最后
    return a;
}

bool Backend::gpuSupported() const
{
    return supportsRunFlag(QStringLiteral("--gpu"));
}

bool Backend::supportsRunFlag(const QString &flag) const
{
    if (m_crosvm.isEmpty())
        return false;
    QProcess p;
    p.start(m_crosvm, {QStringLiteral("run"), QStringLiteral("--help")});
    if (!p.waitForFinished(4000))
        return false;
    const QByteArray out = p.readAllStandardOutput() + p.readAllStandardError();
    return out.contains(flag.toLatin1());
}

QString Backend::diskFlag() const
{
    // 探测结果按 crosvm 路径缓存：探测要起一次进程，不该每建一次命令行都做。
    static QHash<QString, QString> cache;
    const QString key = m_crosvm.isEmpty() ? QStringLiteral("crosvm") : m_crosvm;

    const auto it = cache.constFind(key);
    if (it != cache.constEnd())
        return it.value();

    QString flag = QStringLiteral("--block");

    if (!supportsRunFlag(QStringLiteral("--block"))) {
        // 没有 --block 但可能有它的前身 --rwdisk（很多发行版 crosvm 都停在这一版）
        if (supportsRunFlag(QStringLiteral("--rwdisk")))
            flag = QStringLiteral("--rwdisk");
    }

    cache.insert(key, flag);
    return flag;
}

QStringList Backend::fullArgs(const VmConfig &vm) const
{
    QStringList a = buildArgs(vm, diskFlag());
    // virtio-balloon 在本平台会让 crosvm 收到 SIGBUS；若该 crosvm 带 balloon 则禁掉
    if (supportsRunFlag(QStringLiteral("--no-balloon"))) {
        const int i = a.indexOf(QStringLiteral("--disable-sandbox"));
        a.insert(i >= 0 ? i + 1 : 1, QStringLiteral("--no-balloon"));
    }
    return a;
}

bool Backend::running(const QString &name) const
{
    if (m_daemon) {
        QDBusReply<bool> rep = m_daemon->call(QStringLiteral("Running"), name);
        return rep.isValid() && rep.value();
    }
    QProcess *p = m_procs.value(name, nullptr);
    return p && p->state() != QProcess::NotRunning;
}

bool Backend::start(const VmConfig &vm, QString *err)
{
    if (running(vm.name)) {
        if (err)
            *err = QStringLiteral("「%1」已经在运行").arg(vm.name);
        return false;
    }

    if (m_daemon) {
        // 走特权 daemon：免密码框
        QDir().mkpath(Store::vmDir(vm.name));
        QStringList env;
        const QByteArray rt = qgetenv("XDG_RUNTIME_DIR");
        const QByteArray wd = qgetenv("WAYLAND_DISPLAY");
        if (!rt.isEmpty())
            env << QStringLiteral("XDG_RUNTIME_DIR=") + QString::fromLocal8Bit(rt);
        if (!wd.isEmpty())
            env << QStringLiteral("WAYLAND_DISPLAY=") + QString::fromLocal8Bit(wd);
        QDBusReply<QString> rep =
            m_daemon->call(QStringLiteral("Start"), vm.name, fullArgs(vm), env);
        if (!rep.isValid()) {
            if (err)
                *err = rep.error().message();
            return false;
        }
        if (!rep.value().isEmpty()) {
            if (err)
                *err = rep.value();
            return false;
        }
        return true;
    }

    if (m_crosvm.isEmpty()) {
        if (err)
            *err = QStringLiteral("没有配置 crosvm 路径");
        return false;
    }

    QDir().mkpath(Store::vmDir(vm.name));

    auto *p = new QProcess(this);
    p->setWorkingDirectory(Store::vmDir(vm.name));

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // sudo -A：用图形 askpass（KDE 的 ksshaskpass）代替终端密码提示
    if (m_prefix.contains(QStringLiteral("sudo")) &&
        QFile::exists(QStringLiteral("/usr/bin/ksshaskpass")))
        env.insert(QStringLiteral("SUDO_ASKPASS"), QStringLiteral("/usr/bin/ksshaskpass"));
    p->setProcessEnvironment(env);

    const QString name = vm.name;
    connect(p, &QProcess::readyReadStandardOutput, this,
            [this, p, name]() { emit output(name, p->readAllStandardOutput()); });
    connect(p, &QProcess::readyReadStandardError, this,
            [this, p, name]() { emit output(name, p->readAllStandardError()); });
    connect(p, &QProcess::finished, this, [this, name](int, QProcess::ExitStatus) {
        emit stateChanged(name, false);
        if (QProcess *pp = m_procs.take(name))
            pp->deleteLater();
    });

    QStringList full = m_prefix;
    if (!full.isEmpty()) {
        // 经 sudo/pkexec 提权时默认会清掉环境；而 crosvm 弹窗既要 --wayland-sock，
        // 也要 XDG_RUNTIME_DIR/WAYLAND_DISPLAY。用 env 显式把它们带给 crosvm。
        full << QStringLiteral("/usr/bin/env");
        const QByteArray rt = qgetenv("XDG_RUNTIME_DIR");
        const QByteArray wd = qgetenv("WAYLAND_DISPLAY");
        if (!rt.isEmpty())
            full << QStringLiteral("XDG_RUNTIME_DIR=") + QString::fromLocal8Bit(rt);
        if (!wd.isEmpty())
            full << QStringLiteral("WAYLAND_DISPLAY=") + QString::fromLocal8Bit(wd);
    }
    full << m_crosvm;
    full += fullArgs(vm);

    p->start(full.first(), full.mid(1));
    if (!p->waitForStarted(8000)) {
        if (err)
            *err = p->errorString();
        p->deleteLater();
        return false;
    }

    m_procs.insert(name, p);
    emit stateChanged(name, true);
    return true;
}

void Backend::stop(const QString &name)
{
    if (m_daemon) {
        m_daemon->call(QStringLiteral("Stop"), name);
        return;
    }
    QProcess *p = m_procs.value(name, nullptr);
    if (!p)
        return;
    p->terminate();
    if (!p->waitForFinished(4000)) {
        p->kill();
        p->waitForFinished(2000);
    }
}

void Backend::sendInput(const QString &name, const QByteArray &data)
{
    if (m_daemon) {
        m_daemon->call(QStringLiteral("Write"), name, data);
        return;
    }
    QProcess *p = m_procs.value(name, nullptr);
    if (p && p->state() == QProcess::Running)
        p->write(data);
}

int Backend::pidOf(const QString &name) const
{
    if (m_daemon) {
        QDBusReply<int> rep = m_daemon->call(QStringLiteral("Pid"), name);
        return rep.isValid() ? rep.value() : 0;
    }
    QProcess *p = m_procs.value(name, nullptr);
    if (!p || p->state() == QProcess::NotRunning)
        return 0;
    return int(p->processId());
}

VmStats Backend::stats(const QString &name) const
{
    VmStats s;
    const int pid = pidOf(name);
    if (pid <= 0)
        return s;

    s.pid = pid;

    QFile status(QStringLiteral("/proc/%1/status").arg(pid));
    if (status.open(QIODevice::ReadOnly)) {
        // 字段顺序不保证，逐行按 key 取
        while (!status.atEnd()) {
            const QByteArray line = status.readLine();
            if (line.startsWith("VmRSS:"))
                s.rssBytes = line.mid(6).trimmed().split(' ').value(0).toLongLong() * 1024;
            else if (line.startsWith("VmHWM:"))
                s.peakBytes = line.mid(6).trimmed().split(' ').value(0).toLongLong() * 1024;
            else if (line.startsWith("Threads:"))
                s.threads = line.mid(8).trimmed().toInt();
        }
    }

    // /proc/pid/stat：comm 字段可能含空格和括号，从最后一个 ')' 之后切
    QFile statFile(QStringLiteral("/proc/%1/stat").arg(pid));
    if (statFile.open(QIODevice::ReadOnly)) {
        const QByteArray line = statFile.readAll();
        const int close = line.lastIndexOf(')');
        if (close > 0) {
            const QList<QByteArray> f = line.mid(close + 2).split(' ');
            // f[0]=state（第 3 字段），所以第 N 字段 => f[N-3]
            if (f.size() > 12)
                s.cpuTicks = f.at(11).toLongLong() + f.at(12).toLongLong(); // utime+stime

            if (f.size() > 19) {
                const long hz = sysconf(_SC_CLK_TCK);
                const double startSec = f.at(19).toLongLong() / double(hz); // starttime
                QFile up(QStringLiteral("/proc/uptime"));
                if (up.open(QIODevice::ReadOnly)) {
                    const double upSec = up.readAll().split(' ').value(0).toDouble();
                    s.uptimeMs = qint64((upSec - startSec) * 1000);
                }
            }
        }
    }

    s.valid = s.rssBytes > 0;
    return s;
}
