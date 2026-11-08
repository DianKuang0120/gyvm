// gyvm - VM 定义与（反）序列化
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariant>

struct DiskAttach {
    QString path;
    bool readOnly = false;
    bool cdrom = false;   // ISO / 只读 SCSI 盘（--scsi-block ...,ro=true）
};

// virtio-fs 共享目录（客户机里以 mount -t virtiofs <tag> 访问）
struct SharedDir {
    QString path;   // 宿主目录
    QString tag;    // 客户机看到的标签
    bool readOnly = false;
};

struct VmConfig {
    QString name;
    QString kernel;          // 客户机内核 Image
    QString initrd;          // 可选：initramfs
    int memoryMiB = 1024;
    int cpus = 2;
    bool protectedVm = true; // Gunyah 受保护 VM（本机必须）
    bool hugepages = true;   // crosvm --hugepages（配合 shmem 大页）
    bool net = false;        // virtio-net
    QString tapName;         // 宿主 tap 名，例如 gyvm0
    bool gpu = false;        // virtio-gpu 独立窗口
    int gpuWidth = 1280;
    int gpuHeight = 720;
    QString pflash;          // UEFI 变量存储（--pflash）；UEFI 启动用
    bool pstore = false;     // --pstore：客户机内核日志跨重启保留
    QList<DiskAttach> disks;
    QList<SharedDir> sharedDirs; // virtio-fs
    QStringList extraArgs;   // 追加到 crosvm 后的额外参数

    QJsonObject toJson() const
    {
        QJsonObject o;
        o["name"] = name;
        o["kernel"] = kernel;
        o["initrd"] = initrd;
        o["memoryMiB"] = memoryMiB;
        o["cpus"] = cpus;
        o["protectedVm"] = protectedVm;
        o["hugepages"] = hugepages;
        o["net"] = net;
        o["tapName"] = tapName;
        o["gpu"] = gpu;
        o["gpuWidth"] = gpuWidth;
        o["gpuHeight"] = gpuHeight;
        o["pflash"] = pflash;
        o["pstore"] = pstore;
        QJsonArray ds;
        for (const DiskAttach &d : disks) {
            QJsonObject j;
            j["path"] = d.path;
            j["readOnly"] = d.readOnly;
            j["cdrom"] = d.cdrom;
            ds.append(j);
        }
        o["disks"] = ds;
        QJsonArray sd;
        for (const SharedDir &s : sharedDirs) {
            QJsonObject j;
            j["path"] = s.path;
            j["tag"] = s.tag;
            j["readonly"] = s.readOnly;
            sd.append(j);
        }
        o["sharedDirs"] = sd;
        o["extraArgs"] = QJsonArray::fromStringList(extraArgs);
        return o;
    }

    static VmConfig fromJson(const QJsonObject &o)
    {
        VmConfig v;
        v.name = o.value("name").toString();
        v.kernel = o.value("kernel").toString();
        v.initrd = o.value("initrd").toString();
        v.memoryMiB = o.value("memoryMiB").toInt(1024);
        v.cpus = o.value("cpus").toInt(2);
        v.protectedVm = o.value("protectedVm").toBool(true);
        v.hugepages = o.value("hugepages").toBool(true);
        v.net = o.value("net").toBool(false);
        v.tapName = o.value("tapName").toString();
        v.gpu = o.value("gpu").toBool(false);
        v.gpuWidth = o.value("gpuWidth").toInt(1280);
        v.gpuHeight = o.value("gpuHeight").toInt(720);
        v.pflash = o.value("pflash").toString();
        v.pstore = o.value("pstore").toBool(false);
        const QJsonArray ds = o.value("disks").toArray();
        for (const QJsonValue &jv : ds) {
            const QJsonObject j = jv.toObject();
            DiskAttach d;
            d.path = j.value("path").toString();
            d.readOnly = j.value("readOnly").toBool(false);
            d.cdrom = j.value("cdrom").toBool(false);
            v.disks.append(d);
        }
        const QJsonArray sd = o.value("sharedDirs").toArray();
        for (const QJsonValue &jv : sd) {
            const QJsonObject j = jv.toObject();
            SharedDir s;
            s.path = j.value("path").toString();
            s.tag = j.value("tag").toString();
            s.readOnly = j.value("readonly").toBool(false);
            v.sharedDirs.append(s);
        }
        v.extraArgs = o.value("extraArgs").toVariant().toStringList();
        return v;
    }
};
