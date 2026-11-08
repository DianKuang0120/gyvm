// gyvm 特权守护进程：gyvmd
#include "daemon.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QFile>
#include <QLoggingCategory>
#include <QPair>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>

Daemon::Daemon(QObject *parent) : QObject(parent)
{
    m_crosvm = qEnvironmentVariable("GYVM_CROSVM", QStringLiteral("/opt/crosvm/crosvm"));
}

Daemon::~Daemon()
{
    const auto procs = m_procs;
    for (QProcess *p : procs) {
        p->terminate();
        if (!p->waitForFinished(3000)) {
            p->kill();
            p->waitForFinished(1000);
        }
    }
}

QString Daemon::Version() const { return QStringLiteral("gyvmd 0.1.0"); }
QString Daemon::CrosvmPath() const { return m_crosvm; }

QStringList Daemon::RunningVms() const
{
    QStringList out;
    for (auto it = m_procs.cbegin(); it != m_procs.cend(); ++it)
        if (it.value()->state() != QProcess::NotRunning)
            out << it.key();
    return out;
}

bool Daemon::Running(const QString &name) const
{
    QProcess *p = m_procs.value(name, nullptr);
    return p && p->state() != QProcess::NotRunning;
}

int Daemon::Pid(const QString &name) const
{
    QProcess *p = m_procs.value(name, nullptr);
    if (!p || p->state() == QProcess::NotRunning)
        return 0;
    return int(p->processId());
}

QString Daemon::Start(const QString &name, const QStringList &args, const QStringList &env)
{
    if (name.isEmpty())
        return QStringLiteral("名字为空");
    if (Running(name))
        return QStringLiteral("「%1」已经在运行").arg(name);
    if (!QFile::exists(m_crosvm))
        return QStringLiteral("找不到 crosvm：%1").arg(m_crosvm);

    auto *p = new QProcess(this);
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    for (const QString &kv : env) {
        const int i = kv.indexOf(QLatin1Char('='));
        if (i > 0)
            e.insert(kv.left(i), kv.mid(i + 1));
    }
    p->setProcessEnvironment(e);

    connect(p, &QProcess::readyReadStandardOutput, this,
            [this, p, name]() { emit Output(name, p->readAllStandardOutput()); });
    connect(p, &QProcess::readyReadStandardError, this,
            [this, p, name]() { emit Output(name, p->readAllStandardError()); });
    connect(p, &QProcess::finished, this, [this, name](int, QProcess::ExitStatus) {
        emit StateChanged(name, false);
        if (QProcess *pp = m_procs.take(name))
            pp->deleteLater();
    });

    qInfo("gyvmd: 启动 %s : %s %s", qUtf8Printable(name), qUtf8Printable(m_crosvm),
          qUtf8Printable(args.join(QLatin1Char(' '))));
    p->start(m_crosvm, args);
    if (!p->waitForStarted(8000)) {
        const QString err = p->errorString();
        p->deleteLater();
        return QStringLiteral("启动失败：%1").arg(err);
    }
    m_procs.insert(name, p);
    emit StateChanged(name, true);
    return {};
}

void Daemon::Stop(const QString &name)
{
    QProcess *p = m_procs.value(name, nullptr);
    if (!p)
        return;
    p->terminate();
    if (!p->waitForFinished(4000)) {
        p->kill();
        p->waitForFinished(2000);
    }
}

void Daemon::Write(const QString &name, const QByteArray &data)
{
    QProcess *p = m_procs.value(name, nullptr);
    if (p && p->state() == QProcess::Running)
        p->write(data);
}

bool Daemon::SetShmemThp(const QString &mode)
{
    if (mode != QLatin1String("never") && mode != QLatin1String("advise")
        && mode != QLatin1String("always") && mode != QLatin1String("within_size"))
        return false;
    QFile f(QStringLiteral("/sys/kernel/mm/transparent_hugepage/shmem_enabled"));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(mode.toLatin1()) < 0)
        return false;
    return true;
}

// 跑一条命令，返回 {成功, 输出}
static QPair<bool, QString> run(const QString &prog, const QStringList &args)
{
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(15000))
        return {false, QStringLiteral("超时：%1 %2").arg(prog, args.join(QLatin1Char(' ')))};
    const QString err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
    const bool ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return {ok, ok ? QString() : err};
}

QString Daemon::DefaultUplink() const
{
    // 默认路由所在接口就是出口
    QProcess p;
    p.start(QStringLiteral("ip"), {QStringLiteral("-4"), QStringLiteral("route"),
                                   QStringLiteral("show"), QStringLiteral("default")});
    if (p.waitForFinished(5000)) {
        const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
        for (const QString &line : out.split(QLatin1Char('\n')))
            if (line.startsWith(QLatin1String("default"))) {
                const QStringList f = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                const int i = f.indexOf(QStringLiteral("dev"));
                if (i >= 0 && i + 1 < f.size())
                    return f.at(i + 1);
            }
    }
    return {};
}

bool Daemon::NetworkReady(const QString &tap) const
{
    if (tap.isEmpty())
        return false;
    QProcess p;
    p.start(QStringLiteral("ip"), {QStringLiteral("link"), QStringLiteral("show"),
                                   QStringLiteral("dev"), tap});
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return false;
    return QString::fromLocal8Bit(p.readAllStandardOutput())
        .contains(QLatin1String("state UP"));
}

QString Daemon::SetupNetwork(const QString &tap, const QString &subnet,
                             const QString &uplink)
{
    if (tap.isEmpty() || !tap.contains(QRegularExpression(QStringLiteral("^[a-zA-Z0-9_.-]{1,15}$"))))
        return QStringLiteral("接口名不合法：%1").arg(tap);

    // subnet 形如 192.168.77.0/24，我们从中取出网关地址（.1）
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

    const QString up = uplink.isEmpty() ? DefaultUplink() : uplink;

    // 1) tap 不存在才建；已存在就直接用
    if (!QFile::exists(QStringLiteral("/sys/class/net/") + tap)) {
        auto r = run(QStringLiteral("ip"),
                     {QStringLiteral("tuntap"), QStringLiteral("add"),
                      QStringLiteral("dev"), tap, QStringLiteral("mode"),
                      QStringLiteral("tap")});
        if (!r.first)
            return QStringLiteral("创建 tap 失败：%1").arg(r.second);
    }

    // 2) 配地址和 up
    run(QStringLiteral("ip"), {QStringLiteral("addr"), QStringLiteral("flush"),
                               QStringLiteral("dev"), tap});
    auto r = run(QStringLiteral("ip"),
                 {QStringLiteral("addr"), QStringLiteral("add"),
                  QStringLiteral("%1/%2").arg(gateway).arg(prefix),
                  QStringLiteral("dev"), tap});
    if (!r.first)
        return QStringLiteral("配地址失败：%1").arg(r.second);
    r = run(QStringLiteral("ip"),
            {QStringLiteral("link"), QStringLiteral("set"), QStringLiteral("dev"),
             tap, QStringLiteral("up")});
    if (!r.first)
        return QStringLiteral("启用 tap 失败：%1").arg(r.second);

    // 3) 开转发
    QFile fwd(QStringLiteral("/proc/sys/net/ipv4/ip_forward"));
    if (fwd.open(QIODevice::WriteOnly)) {
        fwd.write("1");
        fwd.close();
    }

    if (up.isEmpty())
        return QStringLiteral("找不到默认路由所在的出口接口，无法配 NAT");

    // 4) NAT。priority 固定，便于重复添加时替换而不是叠加
    const QString prio = QStringLiteral("100");
    QProcess del;
    del.start(QStringLiteral("iptables"),
              {QStringLiteral("-t"), QStringLiteral("nat"), QStringLiteral("-D"),
               QStringLiteral("POSTROUTING"), QStringLiteral("-s"), subnet,
               QStringLiteral("-o"), up, QStringLiteral("-j"), QStringLiteral("MASQUERADE")});
    del.waitForFinished(5000);

    r = run(QStringLiteral("iptables"),
            {QStringLiteral("-t"), QStringLiteral("nat"), QStringLiteral("-A"),
             QStringLiteral("POSTROUTING"), QStringLiteral("-s"), subnet,
             QStringLiteral("-o"), up, QStringLiteral("-j"), QStringLiteral("MASQUERADE")});
    if (!r.first) {
        // iptables 不可用时退回 nft
        QProcess nd;
        nd.start(QStringLiteral("nft"),
                 {QStringLiteral("add"), QStringLiteral("rule"),
                  QStringLiteral("ip"), QStringLiteral("nat"), QStringLiteral("postrouting"),
                  QStringLiteral("ip"), QStringLiteral("saddr"), subnet,
                  QStringLiteral("oifname"), QStringLiteral("\"%1\"").arg(up),
                  QStringLiteral("masquerade")});
        nd.waitForFinished(8000);
        if (nd.exitCode() != 0)
            return QStringLiteral("配 NAT 失败：iptables 和 nft 都不可用");
    }

    qInfo("gyvmd: 网络就绪 tap=%s 网关=%s 出口=%s",
          qUtf8Printable(tap), qUtf8Printable(gateway), qUtf8Printable(up));
    return {};
}

void Daemon::TeardownNetwork(const QString &tap, const QString &subnet,
                             const QString &uplink)
{
    if (tap.isEmpty())
        return;
    const QString up = uplink.isEmpty() ? DefaultUplink() : uplink;
    if (!up.isEmpty())
        run(QStringLiteral("iptables"),
            {QStringLiteral("-t"), QStringLiteral("nat"), QStringLiteral("-D"),
             QStringLiteral("POSTROUTING"), QStringLiteral("-s"), subnet,
             QStringLiteral("-o"), up, QStringLiteral("-j"), QStringLiteral("MASQUERADE")});
    run(QStringLiteral("ip"),
        {QStringLiteral("link"), QStringLiteral("del"), QStringLiteral("dev"), tap});
    qInfo("gyvmd: 网络已拆除 tap=%s", qUtf8Printable(tap));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("gyvmd"));

    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        qCritical("gyvmd: 无法连接系统总线：%s", qUtf8Printable(bus.lastError().message()));
        return 1;
    }

    Daemon daemon;
    if (!bus.registerObject(QStringLiteral("/org/gyvm/Daemon"), &daemon,
                            QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllSignals)) {
        qCritical("gyvmd: 注册对象失败");
        return 1;
    }
    if (!bus.registerService(QStringLiteral("org.gyvm.Daemon1"))) {
        qCritical("gyvmd: 注册服务名失败：%s",
                  qUtf8Printable(bus.lastError().message()));
        return 1;
    }

    qInfo("gyvmd: 就绪（crosvm=%s）", qUtf8Printable(daemon.CrosvmPath()));
    return app.exec();
}
