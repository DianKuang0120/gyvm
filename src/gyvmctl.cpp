// gyvm 命令行：gyvmctl
//   gyvmctl list
//   gyvmctl show   <name>            # 打印将要执行的 crosvm 命令
//   gyvmctl start  <name>
//   gyvmctl stop   <name>
//   gyvmctl rm     <name>
//   gyvmctl create <name> --kernel <Image> [--initrd <f>] [--mem N] [--cpus N]
//                        [--gpu] [--net] [--iso <f>] [--disk <f>]
#include "backend.h"
#include "store.h"

#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QTextStream>

static QTextStream out(stdout);

static VmConfig loadOrDie(const QString &name)
{
    if (!Store::exists(name)) {
        out << "没有名为「" << name << "」的虚拟机\n";
        exit(2);
    }
    return Store::load(name);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("gyvmctl"));

    QCommandLineParser p;
    p.setApplicationDescription(QStringLiteral("gyvm — Gunyah 虚拟机命令行"));
    p.addHelpOption();
    p.addPositionalArgument(QStringLiteral("command"),
                            QStringLiteral("list | show | start | stop | rm | create"));
    p.addPositionalArgument(QStringLiteral("name"), QStringLiteral("虚拟机名"), QStringLiteral("[name]"));

    QCommandLineOption oKernel({QStringLiteral("k"), QStringLiteral("kernel")},
                               QStringLiteral("客户机内核 Image / UEFI 固件"), QStringLiteral("path"));
    QCommandLineOption oInitrd(QStringLiteral("initrd"), QStringLiteral("initramfs"),
                               QStringLiteral("path"));
    QCommandLineOption oMem(QStringLiteral("mem"), QStringLiteral("内存 MiB"),
                            QStringLiteral("MiB"), QStringLiteral("1024"));
    QCommandLineOption oCpus(QStringLiteral("cpus"), QStringLiteral("vCPU 数"),
                             QStringLiteral("N"), QStringLiteral("2"));
    QCommandLineOption oGpu(QStringLiteral("gpu"), QStringLiteral("开启图形窗口"));
    QCommandLineOption oIso(QStringLiteral("iso"), QStringLiteral("挂载 ISO（只读）"),
                            QStringLiteral("path"));
    QCommandLineOption oDisk(QStringLiteral("disk"), QStringLiteral("挂载磁盘（读写）"),
                             QStringLiteral("path"));
    for (auto *o : {&oKernel, &oInitrd, &oMem, &oCpus, &oGpu, &oIso, &oDisk})
        p.addOption(*o);
    p.process(app);

    const QStringList pos = p.positionalArguments();
    if (pos.isEmpty()) {
        p.showHelp(1);
        return 1;
    }
    const QString cmd = pos.value(0);
    const QString name = pos.value(1);

    Backend backend;

    if (cmd == QLatin1String("list")) {
        const QStringList names = Store::listNames();
        if (names.isEmpty())
            out << "（还没有虚拟机）\n";
        for (const QString &n : names) {
            const VmConfig v = Store::load(n);
            out << (backend.running(n) ? "● " : "○ ") << n
                << "   mem=" << v.memoryMiB << "MiB cpus=" << v.cpus
                << (v.gpu ? " gpu" : "") << (v.net ? " net" : "")
                << "  kernel=" << v.kernel << "\n";
        }
        out << (backend.daemonAvailable() ? "（特权 daemon: 可用，免密码）\n"
                                          : "（特权 daemon: 未运行，将用提权前缀）\n");
        return 0;
    }

    if (cmd == QLatin1String("show")) {
        const VmConfig v = loadOrDie(name);
        QStringList full = backend.launchPrefix();
        full << backend.crosvmPath();
        full += backend.fullArgs(v);
        out << full.join(QLatin1Char(' ')) << "\n";
        return 0;
    }

    if (cmd == QLatin1String("start")) {
        const VmConfig v = loadOrDie(name);
        QString err;
        if (!backend.start(v, &err)) {
            out << "启动失败：" << err << "\n";
            return 1;
        }
        out << "已启动 " << name << "\n";
        return 0;
    }

    if (cmd == QLatin1String("stop")) {
        backend.stop(name);
        out << "已停止 " << name << "\n";
        return 0;
    }

    if (cmd == QLatin1String("rm")) {
        if (backend.running(name)) {
            out << "请先停止 " << name << "\n";
            return 1;
        }
        Store::remove(name);
        out << "已删除 " << name << "\n";
        return 0;
    }

    if (cmd == QLatin1String("create")) {
        if (name.isEmpty() || p.value(oKernel).isEmpty()) {
            out << "用法：gyvmctl create <name> --kernel <Image>\n";
            return 1;
        }
        if (Store::exists(name)) {
            out << "已存在：" << name << "\n";
            return 1;
        }
        VmConfig v;
        v.name = name;
        v.kernel = p.value(oKernel);
        v.initrd = p.value(oInitrd);
        v.memoryMiB = p.value(oMem).toInt();
        v.cpus = p.value(oCpus).toInt();
        v.gpu = p.isSet(oGpu);
        if (!p.value(oIso).isEmpty()) {
            DiskAttach d;
            d.path = p.value(oIso);
            d.readOnly = true;
            d.cdrom = true;
            v.disks.append(d);
        }
        if (!p.value(oDisk).isEmpty()) {
            DiskAttach d;
            d.path = p.value(oDisk);
            v.disks.append(d);
        }
        if (!Store::save(v)) {
            out << "保存失败\n";
            return 1;
        }
        out << "已创建 " << name << "\n";
        return 0;
    }

    out << "未知命令：" << cmd << "\n";
    p.showHelp(1);
    return 1;
}
