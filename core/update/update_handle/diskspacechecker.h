#ifndef DISKSPACECHECKER_H
#define DISKSPACECHECKER_H

#include <QtCore/QString>
#include <QtCore/QList>
#include <QtCore/QSet>
#include "scriptparser.h"

struct SpaceInfo {
    QString partition;      // 分区路径
    qint64 required;       // 所需空间（字节）
    qint64 available;      // 可用空间（字节）
    qint64 total;          // 总空间（字节）
    bool sufficient;       // 是否充足
};

class DiskSpaceChecker
{
public:
    DiskSpaceChecker();
    
    // 检查磁盘空间
    // packageSize: 升级包文件大小
    // files: 文件列表
    // backupSize: 备份所需空间
    // marginPercent: 安全余量百分比
    static QList<SpaceInfo> checkSpace(
        const QString &packagePath,
        const QList<UpdateFile> &files,
        qint64 backupSize,
        int marginPercent
    );
    
    // 获取路径所在分区的可用空间
    static qint64 getAvailableSpace(const QString &path);
    
    // 获取路径所在分区的总空间
    static qint64 getTotalSpace(const QString &path);
    
    // 获取路径所在分区
    static QString getPartitionForPath(const QString &path);
    
    // 格式化空间大小（用于显示）
    static QString formatSize(qint64 bytes);

private:
    static QSet<QString> getUniquePartitions(const QList<UpdateFile> &files);
    static QString resolveExistingPath(const QString &path);
};

#endif // DISKSPACECHECKER_H
