// gyvm - crosvm 后端：把 VmConfig 变成 crosvm 命令行，并管理进程生命周期
#pragma once

#include "vmconfig.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

class QDBusInterface;

// 运行时统计，从 crosvm 进程本身读取（/proc），不依赖客户机
struct VmStats {
    bool valid = false;
    qint64 rssBytes = 0;    // 常驻内存
    qint64 peakBytes = 0;   // 峰值内存
    qint64 cpuTicks = 0;    // 累计 CPU 时间（jiffies）
    int threads = 0;        // 线程数
    qint64 uptimeMs = 0;    // 本进程已运行时间
    int pid = 0;
};

class Backend : public QObject {
    Q_OBJECT

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;

    QString crosvmPath() const { return m_crosvm; }
    void setCrosvmPath(const QString &p) { m_crosvm = p; }

    QStringList launchPrefix() const { return m_prefix; }
    void setLaunchPrefix(const QStringList &p) { m_prefix = p; }

    // 纯函数，便于测试 / “显示将要执行什么”
    // rwFlag：读写磁盘用的开关（不同 crosvm 版本是 --block 或 --rwdisk），
    // 空串表示沿用 --block。
    static QStringList buildArgs(const VmConfig &vm,
                                 const QString &rwFlag = QString());

    // 探测当前 crosvm 接受哪个读写盘开关，结果缓存。
    // 新版是 --block，不少发行版只有弃用别名 --rwdisk。
    QString diskFlag() const;
    static QString waylandSocket();

    // 当前 crosvm 是否带 gpu 功能（跑 `crosvm run --help` 看有没有 --gpu）
    bool gpuSupported() const;
    // crosvm run 是否支持某个开关（用于自动适配不同 crosvm 版本）
    bool supportsRunFlag(const QString &flag) const;
    // buildArgs 之后再按 crosvm 能力补上必要开关（如 --no-balloon）
    QStringList fullArgs(const VmConfig &vm) const;

    // 系统总线上是否有特权 daemon（gyvmd）；有则用它启动，免密码框
    bool daemonAvailable() const;
    bool setShmemThp(const QString &mode); // 经 daemon 写 shmem 大页

    bool running(const QString &name) const;
    bool start(const VmConfig &vm, QString *err);
    void stop(const QString &name);
    void sendInput(const QString &name, const QByteArray &data);

    // 读取该 VM 的 crosvm 进程统计；未运行时返回 invalid。
    VmStats stats(const QString &name) const;
    int pidOf(const QString &name) const;

signals:
    void output(const QString &name, const QByteArray &data);
    void stateChanged(const QString &name, bool running);

private slots:
    void onDaemonOutput(const QString &name, const QByteArray &data);
    void onDaemonState(const QString &name, bool running);

private:
    QString m_crosvm = QStringLiteral("crosvm");
    QStringList m_prefix; // 例如 {"sudo"} 或 {"pkexec"}；空表示直接运行
    QHash<QString, QProcess *> m_procs;
    QDBusInterface *m_daemon = nullptr;
};
