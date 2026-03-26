#include "backupmanager.h"
#include "configmanager.h"
#include "logger.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QFileInfo>
#include <QtCore/QDateTime>
#include <QtCore/QProcess>
#include <QtCore/QtAlgorithms>
#include <QtCore/QStringList>
#include <QtCore/QCryptographicHash>  // BY ZF: 用于计算MD5
#include <QtCore/QDirIterator>  // BY ZF: 用于遍历目录

BackupManager::BackupManager()
    : m_rollbackPackagePath("")
{
}

BackupManager::~BackupManager()
{
}

QString BackupManager::createBackupDir()
{
    QString baseDir = ConfigManager::instance().backupDir();
    
    // 确保备份根目录存在
    QDir dir;
    if (!dir.exists(baseDir)) {
        if (!dir.mkpath(baseDir)) {
            Logger::error(QString("无法创建备份目录: %1").arg(baseDir));
            return "";
        }
    }
    
    // 生成备份目录名（格式：backup_YYYYMMDD_HHMMSS）
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString backupDir = QString("%1/backup_%2").arg(baseDir).arg(timestamp);
    
    // 创建备份目录
    if (!dir.mkpath(backupDir)) {
        Logger::error(QString("无法创建备份目录: %1").arg(backupDir));
        return "";
    }
    
    m_currentBackupDir = backupDir;
    return backupDir;
}

// BY ZF: 执行备份操作（复制文件到备份目录）
bool BackupManager::executeBackup(const QString &backupDir, const QList<UpdateFile> &files)
{
    if (backupDir.isEmpty()) {
        Logger::error("备份目录为空，无法执行备份");
        return false;
    }
    
    QString filesDir = QString("%1/files").arg(backupDir);
    QDir dir;
    if (!dir.mkpath(filesDir)) {
        Logger::error(QString("无法创建备份文件目录: %1").arg(filesDir));
        return false;
    }
    
    // BY ZF: 复制需要备份的文件到files目录
    for (int i = 0; i < files.size(); ++i) {
        const UpdateFile &file = files.at(i);
        
        if (!file.backup) {
            continue; // 跳过不需要备份的文件
        }
        
        // 检查源文件是否存在
        if (!QFileInfo(file.destination).exists()) {
            Logger::warning(QString("文件不存在，跳过备份: %1").arg(file.destination));
            continue;
        }
        
        // BY ZF: 构建备份文件路径（保持目录结构）
        QFileInfo destInfo(file.destination);
        QString destDir = destInfo.absolutePath();
        QString destFile = destInfo.fileName();
        
        // 创建备份目录结构（去掉开头的/）
        QString backupSubDir = destDir;
        if (backupSubDir.startsWith("/")) {
            backupSubDir = backupSubDir.mid(1);  // 去掉开头的/
        }
        backupSubDir = QString("%1/%2").arg(filesDir).arg(backupSubDir);
        
        if (!dir.mkpath(backupSubDir)) {
            Logger::error(QString("无法创建备份子目录: %1").arg(backupSubDir));
            return false;
        }
        
        QString backupFilePath = QString("%1/%2").arg(backupSubDir).arg(destFile);
        
        // BY ZF: 使用cp命令复制文件（确保二进制文件正确复制）
        QProcess cpProcess;
        cpProcess.start("cp", QStringList() << file.destination << backupFilePath);
        if (!cpProcess.waitForFinished(30000) || cpProcess.exitCode() != 0) {
            Logger::error(QString("备份文件失败: %1 -> %2").arg(file.destination).arg(backupFilePath));
            return false;
        }
    }
    
    return true;
}

// BY ZF: 生成回滚包（类似install.tar.gz的结构）
bool BackupManager::generateRollbackPackage(const QString &backupDir, const QList<UpdateFile> &files)
{
    if (backupDir.isEmpty()) {
        Logger::error("备份目录为空，无法生成回滚包");
        return false;
    }
    
    QString filesDir = QString("%1/files").arg(backupDir);
    QString installDir = QString("%1/install").arg(backupDir);
    
    // BY ZF: 创建install目录结构
    QDir dir;
    if (!dir.mkpath(installDir)) {
        Logger::error(QString("无法创建install目录: %1").arg(installDir));
        return false;
    }
    
    // BY ZF: 移动files目录到install目录下
    if (dir.exists(filesDir)) {
        QProcess mvProcess;
        mvProcess.start("mv", QStringList() << filesDir << QString("%1/files").arg(installDir));
        if (!mvProcess.waitForFinished(30000) || mvProcess.exitCode() != 0) {
            Logger::error("无法移动files目录到install目录");
            return false;
        }
    }
    
    // BY ZF: 生成manifest.xml
    QString manifestPath = QString("%1/manifest.xml").arg(installDir);
    QString manifestContent = generateManifestXml(files, QString("%1/files").arg(installDir));
    
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::error(QString("无法创建manifest.xml: %1").arg(manifestPath));
        return false;
    }
    QTextStream manifestOut(&manifestFile);
    manifestOut.setCodec("UTF-8");
    manifestOut << manifestContent;
    manifestFile.close();
    
    // BY ZF: 生成checksum.md5
    QString checksumPath = QString("%1/checksum.md5").arg(installDir);
    if (!generateChecksumFile(QString("%1/files").arg(installDir), checksumPath)) {
        Logger::error("无法生成checksum.md5");
        return false;
    }
    
    // BY ZF: 打包成rollback.tar.gz
    m_rollbackPackagePath = QString("%1/rollback.tar.gz").arg(backupDir);
    
    QProcess tarProcess;
    tarProcess.setWorkingDirectory(backupDir);
    tarProcess.start("tar", QStringList() << "-czf" << m_rollbackPackagePath << "install");
    if (!tarProcess.waitForFinished(300000) || tarProcess.exitCode() != 0) {
        QString errorOutput = QString::fromLocal8Bit(tarProcess.readAllStandardError());
        Logger::error(QString("打包回滚包失败: %1").arg(errorOutput));
        return false;
    }
    
    return true;
}


void BackupManager::cleanupOldBackups(int keepCount)
{
    QStringList backupDirs = getBackupDirs();
    
    if (backupDirs.size() <= keepCount) {
        return; // 不需要清理
    }
    
    // 按时间排序（最新的在前）
    // 删除最旧的备份
    for (int i = keepCount; i < backupDirs.size(); ++i) {
        QString oldBackup = backupDirs.at(i);
        QDir dir(oldBackup);
        if (dir.exists()) {
            // 删除整个备份目录（Qt 4.8.6 兼容方式）
            QProcess rmProcess;
            rmProcess.start("rm", QStringList() << "-rf" << oldBackup);
            if (rmProcess.waitForFinished(30000) && rmProcess.exitCode() == 0) {
                Logger::warning(QString("已删除旧备份: %1").arg(oldBackup));
            } else {
                Logger::warning(QString("无法删除旧备份: %1").arg(oldBackup));
            }
        }
    }
}

qint64 BackupManager::calculateBackupSize(const QList<UpdateFile> &files)
{
    qint64 totalSize = 0;
    
    for (int i = 0; i < files.size(); ++i) {
        const UpdateFile &file = files.at(i);
        
        // 只计算需要备份的文件
        if (file.backup) {
            QFileInfo fileInfo(file.destination);
            if (fileInfo.exists()) {
                totalSize += fileInfo.size();
            }
        }
    }
    
    return totalSize;
}

// BY ZF: 生成manifest.xml内容（回滚包的清单文件）
QString BackupManager::generateManifestXml(const QList<UpdateFile> &files, const QString &filesDir)
{
    QString content;
    QTextStream out(&content);
    
    QString buildDate = QDateTime::currentDateTime().toString("yyyy-MM-ddThh:mm:ss");
    
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<package packageVersion=\"1.0\">\n";
    out << "  <packageInfo>\n";
    out << "    <name>Rollback Package</name>\n";
    out << "    <buildDate>" << buildDate << "</buildDate>\n";
    out << "    <description>Auto-generated rollback package</description>\n";
    out << "  </packageInfo>\n";
    out << "  <files>\n";
    
    // BY ZF: 生成文件列表（只包含需要备份的文件）
    for (int i = 0; i < files.size(); ++i) {
        const UpdateFile &file = files.at(i);
        
        if (!file.backup) {
            continue; // 跳过不需要备份的文件
        }
        
        // BY ZF: 检查备份文件是否存在
        QFileInfo destInfo(file.destination);
        QString destDir = destInfo.absolutePath();
        QString destFile = destInfo.fileName();
        
        // 构建备份文件路径（去掉开头的/）
        QString backupSubDir = destDir;
        if (backupSubDir.startsWith("/")) {
            backupSubDir = backupSubDir.mid(1);
        }
        QString backupFilePath = QString("%1/%2/%3").arg(filesDir).arg(backupSubDir).arg(destFile);
        
        if (!QFileInfo(backupFilePath).exists()) {
            continue; // 跳过不存在的备份文件
        }
        
        // BY ZF: 计算备份文件的MD5和大小
        QFile backupFile(backupFilePath);
        if (!backupFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        
        QCryptographicHash hash(QCryptographicHash::Md5);
        const qint64 bufferSize = 8192;
        char buffer[bufferSize];
        qint64 bytesRead;
        
        while ((bytesRead = backupFile.read(buffer, bufferSize)) > 0) {
            hash.addData(buffer, bytesRead);
        }
        backupFile.close();
        
        QString md5 = hash.result().toHex().toLower();
        qint64 size = QFileInfo(backupFilePath).size();
        
        // BY ZF: 构建源路径（相对于install/files）
        QString sourcePath = QString("files/%1/%2").arg(backupSubDir).arg(destFile);
        
        // BY ZF: 转义XML特殊字符
        QString escapedDest = file.destination;
        escapedDest.replace("&", "&amp;");
        escapedDest.replace("<", "&lt;");
        escapedDest.replace(">", "&gt;");
        
        out << "    <file>\n";
        out << "      <source>" << sourcePath << "</source>\n";
        out << "      <destination>" << escapedDest << "</destination>\n";
        out << "      <permission>" << file.permission << "</permission>\n";
        out << "      <owner>" << file.owner << "</owner>\n";
        out << "      <backup>false</backup>\n";  // BY ZF: 回滚时不再备份
        out << "      <md5>" << md5 << "</md5>\n";
        out << "      <size>" << size << "</size>\n";
        out << "    </file>\n";
    }
    
    out << "  </files>\n";
    out << "  <commands>\n";
    out << "    <preUpdate>\n";
    out << "    </preUpdate>\n";
    out << "    <postUpdate>\n";
    out << "    </postUpdate>\n";
    out << "    <rollback>\n";
    out << "    </rollback>\n";
    out << "  </commands>\n";
    out << "</package>\n";
    
    return content;
}

// BY ZF: 生成checksum.md5文件
bool BackupManager::generateChecksumFile(const QString &filesDir, const QString &checksumPath)
{
    QFile checksumFile(checksumPath);
    if (!checksumFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::error(QString("无法创建checksum.md5: %1").arg(checksumPath));
        return false;
    }
    
    QTextStream out(&checksumFile);
    
    // BY ZF: 计算files目录下所有文件的MD5
    QDir dir(filesDir);
    if (!dir.exists()) {
        checksumFile.close();
        return false;
    }
    
    // BY ZF: 使用递归方式遍历目录（Qt 4.8.6 兼容方式）
    QDirIterator it(filesDir, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    
    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fileInfo(filePath);
        
        // BY ZF: 跳过.DS_Store文件
        if (fileInfo.fileName() == ".DS_Store") {
            continue;
        }
        
        QFile file(filePath);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        
        QCryptographicHash hash(QCryptographicHash::Md5);
        const qint64 bufferSize = 8192;
        char buffer[bufferSize];
        qint64 bytesRead;
        
        while ((bytesRead = file.read(buffer, bufferSize)) > 0) {
            hash.addData(buffer, bytesRead);
        }
        file.close();
        
        QString md5 = hash.result().toHex().toLower();
        QString relPath = filePath;
        relPath.replace(filesDir + "/", "");
        relPath = "files/" + relPath;
        
        out << md5 << "  " << relPath << "\n";
    }
    
    // BY ZF: 添加manifest.xml的MD5
    QString manifestPath = QString("%1/../manifest.xml").arg(filesDir);
    QFile manifestFile(manifestPath);
    if (manifestFile.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        const qint64 bufferSize = 8192;
        char buffer[bufferSize];
        qint64 bytesRead;
        
        while ((bytesRead = manifestFile.read(buffer, bufferSize)) > 0) {
            hash.addData(buffer, bytesRead);
        }
        manifestFile.close();
        
        QString md5 = hash.result().toHex().toLower();
        out << md5 << "  manifest.xml\n";
    }
    
    checksumFile.close();
    return true;
}

QStringList BackupManager::getBackupDirs()
{
    QStringList dirs;
    QStringList fullPaths;
    QString baseDir = ConfigManager::instance().backupDir();
    
    QDir dir(baseDir);
    if (!dir.exists()) {
        return fullPaths;
    }
    
    // 获取所有备份目录
    QStringList filters;
    filters << "backup_*";
    dirs = dir.entryList(filters, QDir::Dirs | QDir::NoDotAndDotDot);
    
    // 转换为完整路径
    for (int i = 0; i < dirs.size(); ++i) {
        fullPaths.append(QString("%1/%2").arg(baseDir).arg(dirs.at(i)));
    }
    
    // 按修改时间排序（最新的在前）- Qt 4.8.6 兼容方式
    // 使用静态比较函数，因为Qt 4.8.6不支持局部类型作为模板参数
    qSort(fullPaths.begin(), fullPaths.end(), compareBackupDirs);
    
    return fullPaths;
}

bool BackupManager::compareBackupDirs(const QString &a, const QString &b)
{
    QFileInfo infoA(a);
    QFileInfo infoB(b);
    return infoA.lastModified() > infoB.lastModified();
}

QString BackupManager::getLatestBackupDir()
{
    QStringList dirs = getBackupDirs();
    if (dirs.isEmpty()) {
        return QString();
    }
    return dirs.first();
}

