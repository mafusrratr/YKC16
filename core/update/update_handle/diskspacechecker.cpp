#include "diskspacechecker.h"
#include "configmanager.h"
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QSet>
#include <QtCore/QDebug>
#include <sys/statvfs.h>

DiskSpaceChecker::DiskSpaceChecker()
{
}

QList<SpaceInfo> DiskSpaceChecker::checkSpace(
    const QString &packagePath,
    const QList<UpdateFile> &files,
    qint64 backupSize,
    int marginPercent)
{
    QList<SpaceInfo> results;
    
    // 获取升级包大小
    QFileInfo packageInfo(packagePath);
    qint64 packageSize = packageInfo.size();
    
    // 计算所需空间
    qint64 totalFileSize = 0;
    for (int i = 0; i < files.size(); ++i) {
        totalFileSize += files.at(i).size;
    }
    
    // 所需空间 = max(解压空间×2, 备份空间, 新文件空间) + 安全余量
    qint64 extractSpace = packageSize * 2; // 解压临时空间
    qint64 requiredSpace = qMax(qMax(extractSpace, backupSize), totalFileSize);
    
    // 添加安全余量（百分比）
    qint64 margin = (requiredSpace * marginPercent) / 100;
    requiredSpace += margin;
    
    // 获取所有唯一分区
    QSet<QString> partitions = getUniquePartitions(files);
    
    // 添加备份目录所在分区
    QString backupPartition = getPartitionForPath(ConfigManager::instance().backupDir());
    if (!backupPartition.isEmpty()) {
        partitions.insert(backupPartition);
    }
    
    // 添加临时目录所在分区
    QString tempPartition = getPartitionForPath(ConfigManager::instance().tempDir());
    if (!tempPartition.isEmpty()) {
        partitions.insert(tempPartition);
    }
    
    // 检查每个分区
    QSet<QString>::const_iterator it;
    for (it = partitions.begin(); it != partitions.end(); ++it) {
        const QString &partition = *it;
        SpaceInfo info;
        info.partition = partition;
        info.required = requiredSpace;
        info.available = getAvailableSpace(partition);
        info.total = getTotalSpace(partition);
        info.sufficient = (info.available >= info.required);
        
        results.append(info);
    }
    
    return results;
}

QString DiskSpaceChecker::resolveExistingPath(const QString &path)
{
    QString realPath = path;
    QFileInfo fileInfo(path);
    
    // BY ZF: 目标通常是文件路径，不存在时先回退到父目录
    if (fileInfo.exists()) {
        if (fileInfo.isFile()) {
            realPath = fileInfo.absolutePath();
        } else if (fileInfo.isDir()) {
            realPath = fileInfo.absoluteFilePath();
        }
    } else {
        realPath = fileInfo.absolutePath();
    }
    
    // BY ZF: 持续向上查找，直到找到真实存在的目录
    QDir dir(realPath);
    while (!dir.exists() && dir.absolutePath() != "/") {
        if (!dir.cdUp()) {
            break;
        }
    }
    
    if (!dir.exists()) {
        return "/";
    }
    
    return dir.absolutePath();
}

qint64 DiskSpaceChecker::getAvailableSpace(const QString &path)
{
    struct statvfs vfs;
    QString realPath = resolveExistingPath(path);
    
    if (statvfs(realPath.toLocal8Bit().constData(), &vfs) == 0) {
        return (qint64)vfs.f_bavail * vfs.f_frsize;
    }
    
    return -1;
}

qint64 DiskSpaceChecker::getTotalSpace(const QString &path)
{
    struct statvfs vfs;
    QString realPath = resolveExistingPath(path);
    
    if (statvfs(realPath.toLocal8Bit().constData(), &vfs) == 0) {
        return (qint64)vfs.f_blocks * vfs.f_frsize;
    }
    
    return -1;
}

QString DiskSpaceChecker::getPartitionForPath(const QString &path)
{
    QString realPath = resolveExistingPath(path);
    
    // 规范化路径（解析符号链接）
    QFileInfo info(realPath);
    QString canonicalPath = info.canonicalFilePath();
    if (canonicalPath.isEmpty()) {
        canonicalPath = info.absoluteFilePath();
    }
    
    // 返回规范化路径（实际分区路径）
    return canonicalPath;
}

QString DiskSpaceChecker::formatSize(qint64 bytes)
{
    const qint64 KB = 1024;
    const qint64 MB = KB * 1024;
    const qint64 GB = MB * 1024;
    
    if (bytes >= GB) {
        return QString("%1 GB").arg(bytes / (double)GB, 0, 'f', 2);
    } else if (bytes >= MB) {
        return QString("%1 MB").arg(bytes / (double)MB, 0, 'f', 2);
    } else if (bytes >= KB) {
        return QString("%1 KB").arg(bytes / (double)KB, 0, 'f', 2);
    } else {
        return QString("%1 B").arg(bytes);
    }
}

QSet<QString> DiskSpaceChecker::getUniquePartitions(const QList<UpdateFile> &files)
{
    QSet<QString> partitions;
    
    for (int i = 0; i < files.size(); ++i) {
        QString partition = getPartitionForPath(files.at(i).destination);
        if (!partition.isEmpty()) {
            partitions.insert(partition);
        }
    }
    
    return partitions;
}
