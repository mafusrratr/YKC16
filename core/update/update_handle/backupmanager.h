#ifndef BACKUPMANAGER_H
#define BACKUPMANAGER_H

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QList>
#include "scriptparser.h"

class BackupManager
{
public:
    BackupManager();
    ~BackupManager();
    
    // 创建备份目录
    QString createBackupDir();
    
    // BY ZF: 生成回滚包（类似install.tar.gz的结构）
    bool generateRollbackPackage(const QString &backupDir, const QList<UpdateFile> &files);
    
    // BY ZF: 执行备份操作（复制文件到备份目录）
    bool executeBackup(const QString &backupDir, const QList<UpdateFile> &files);
    
    // 清理旧备份（保留最近N次）
    void cleanupOldBackups(int keepCount);
    
    // BY ZF: 获取回滚包路径
    QString rollbackPackagePath() const { return m_rollbackPackagePath; }
    
    // 获取当前备份目录
    QString currentBackupDir() const { return m_currentBackupDir; }
    
    // 计算备份所需空间
    static qint64 calculateBackupSize(const QList<UpdateFile> &files);
    
    // 获取最新备份目录
    QString getLatestBackupDir();

private:
    // 备份目录排序比较函数（需要在类外部定义，Qt 4.8.6 不支持局部类型作为模板参数）
    static bool compareBackupDirs(const QString &a, const QString &b);
    QString m_currentBackupDir;
    QString m_rollbackPackagePath;  // BY ZF: 回滚包路径
    
    // BY ZF: 生成manifest.xml内容
    QString generateManifestXml(const QList<UpdateFile> &files, const QString &filesDir);
    
    // BY ZF: 生成checksum.md5文件
    bool generateChecksumFile(const QString &filesDir, const QString &checksumPath);
    
    // 获取所有备份目录（按时间排序）
    QStringList getBackupDirs();
};

#endif // BACKUPMANAGER_H

