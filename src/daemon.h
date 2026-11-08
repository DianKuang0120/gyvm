// gyvm - 特权守护进程 gyvmd：以 root 运行，替 GUI 拉起 crosvm。
// 通过系统总线 org.gyvm.Daemon1（对象 /org/gyvm/Daemon）暴露。
#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

class Daemon : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.gyvm.Daemon1")

public:
    explicit Daemon(QObject *parent = nullptr);
    ~Daemon() override;

public slots:
    // 启动成功返回空串，失败返回错误说明
    QString Start(const QString &name, const QStringList &args, const QStringList &env);
    void Stop(const QString &name);
    void Write(const QString &name, const QByteArray &data); // 写客户机串口（stdin）
    bool Running(const QString &name) const;
    QStringList RunningVms() const;
    int Pid(const QString &name) const; // crosvm 进程 PID，0 表示未运行

    // 打开/关闭 shmem 大页（内存 >1G 的 VM 必需）
    bool SetShmemThp(const QString &mode); // never / advise / always

    QString CrosvmPath() const;
    QString Version() const;

signals:
    void Output(const QString &name, const QByteArray &data);
    void StateChanged(const QString &name, bool running);

private:
    QString m_crosvm;
    QHash<QString, QProcess *> m_procs;
};
