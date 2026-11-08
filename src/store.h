// gyvm - VM 定义存储：$XDG_DATA_HOME/gyvm/vms/<name>/vm.json
#pragma once

#include "vmconfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QString>
#include <QStringList>

namespace Store {

inline QString rootDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/gyvm";
}

inline QString vmsDir() { return rootDir() + "/vms"; }

inline QString vmDir(const QString &name) { return vmsDir() + "/" + name; }

inline QString jsonPath(const QString &name) { return vmDir(name) + "/vm.json"; }

inline QString consoleLog(const QString &name) { return vmDir(name) + "/console.log"; }

inline QStringList listNames()
{
    QDir d(vmsDir());
    QStringList out;
    const QFileInfoList es = d.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : es)
        if (QFile::exists(fi.absoluteFilePath() + "/vm.json"))
            out << fi.fileName();
    out.sort();
    return out;
}

inline bool exists(const QString &name) { return QFile::exists(jsonPath(name)); }

inline VmConfig load(const QString &name)
{
    QFile f(jsonPath(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return VmConfig::fromJson(QJsonDocument::fromJson(f.readAll()).object());
}

inline bool save(const VmConfig &vm)
{
    QDir().mkpath(vmDir(vm.name));
    QFile f(jsonPath(vm.name));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(vm.toJson()).toJson(QJsonDocument::Indented));
    return true;
}

inline bool remove(const QString &name) { return QDir(vmDir(name)).removeRecursively(); }

} // namespace Store
