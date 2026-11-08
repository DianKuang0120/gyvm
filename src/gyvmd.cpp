// gyvm 特权守护进程：gyvmd
#include "daemon.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QFile>
#include <QLoggingCategory>
#include <QProcess>
#include <QProcessEnvironment>

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
