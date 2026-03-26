#include "updateexecutor.h"
#include "logger.h"
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QProcess>
#include <QtCore/QDebug>

UpdateExecutor::UpdateExecutor()
{
}

UpdateExecutor::~UpdateExecutor()
{
}

bool UpdateExecutor::executePreUpdateCommands(const QStringList &commands)
{
    for (int i = 0; i < commands.size(); ++i) {
        QString command = commands.at(i);
        if (command.isEmpty()) {
            continue;
        }
        
        if (!executeCommand(command)) {
            Logger::warning(QString("预更新命令执行失败: %1").arg(command));
            // 继续执行其他命令（不中断）
        }
    }
    
    // 即使有错误也返回true（只记录警告，不中断）
    return true;
}

UpdateResult UpdateExecutor::executeFileUpdate(
    const QString &extractDir,
    const QList<UpdateFile> &files)
{
    UpdateResult result;
    result.success = true;
    
    for (int i = 0; i < files.size(); ++i) {
        const UpdateFile &file = files.at(i);
        
        QString sourcePath = QString("%1/%2").arg(extractDir).arg(file.source);
        QString destPath = file.destination;
        
        // 检查源文件是否存在
        if (!QFileInfo(sourcePath).exists()) {
            QString error = QString("源文件不存在: %1").arg(sourcePath);
            Logger::error(error);
            result.success = false;
            result.failedFiles.append(destPath);
            result.errorMessages.append(error);
            continue;
        }
        
        // 确保目标目录存在
        QFileInfo destInfo(destPath);
        QDir destDir = destInfo.absoluteDir();
        if (!destDir.exists()) {
            if (!destDir.mkpath(".")) {
                QString error = QString("无法创建目标目录: %1").arg(destDir.absolutePath());
                Logger::error(error);
                result.success = false;
                result.failedFiles.append(destPath);
                result.errorMessages.append(error);
                continue;
            }
        }
        
        // 复制文件
        if (!copyFile(sourcePath, destPath)) {
            QString error = QString("文件复制失败: %1 -> %2").arg(sourcePath).arg(destPath);
            Logger::error(error);
            result.success = false;
            result.failedFiles.append(destPath);
            result.errorMessages.append(error);
            continue;
        }
        
        // 设置权限和所有者
        if (!setFilePermissions(destPath, file.permission, file.owner)) {
            QString error = QString("设置文件权限失败: %1").arg(destPath);
            Logger::warning(error); // 警告，不中断
        }
    }
    
    return result;
}

bool UpdateExecutor::executePostUpdateCommands(const QStringList &commands)
{
    for (int i = 0; i < commands.size(); ++i) {
        QString command = commands.at(i);
        if (command.isEmpty()) {
            continue;
        }
        
        if (!executeCommand(command)) {
            Logger::error(QString("后更新命令执行失败: %1").arg(command));
            return false;
        }
    }
    
    return true;
}

bool UpdateExecutor::setFilePermissions(const QString &filePath, const QString &permission, const QString &owner)
{
    // 设置权限（chmod）
    if (!permission.isEmpty()) {
        QProcess chmodProcess;
        chmodProcess.start("chmod", QStringList() << permission << filePath);
        if (!chmodProcess.waitForFinished(5000)) {
            return false;
        }
        if (chmodProcess.exitCode() != 0) {
            return false;
        }
    }
    
    // 设置所有者（chown）
    if (!owner.isEmpty()) {
        QProcess chownProcess;
        chownProcess.start("chown", QStringList() << owner << filePath);
        if (!chownProcess.waitForFinished(5000)) {
            return false;
        }
        if (chownProcess.exitCode() != 0) {
            return false;
        }
    }
    
    return true;
}

bool UpdateExecutor::copyFile(const QString &source, const QString &destination)
{
    // BY ZF: 如果目标文件已存在，先删除
    if (QFileInfo(destination).exists()) {
        if (!QFile::remove(destination)) {
            Logger::error(QString("无法删除已存在的目标文件: %1").arg(destination));
            return false;
        }
    }
    
    // BY ZF: 使用系统命令cp复制文件，确保二进制文件正确复制（Qt 4.8.6的QFile::copy对二进制文件可能有问题）
    QProcess process;
    process.start("cp", QStringList() << source << destination);
    
    if (!process.waitForFinished(300000)) {  // 5分钟超时
        Logger::error(QString("文件复制超时: %1 -> %2").arg(source).arg(destination));
        return false;
    }
    
    if (process.exitCode() != 0) {
        QString errorOutput = QString::fromLocal8Bit(process.readAllStandardError());
        Logger::error(QString("文件复制失败: %1 -> %2, 错误: %3").arg(source).arg(destination).arg(errorOutput));
        return false;
    }
    
    // BY ZF: 验证文件大小是否一致
    QFileInfo sourceInfo(source);
    QFileInfo destInfo(destination);
    if (sourceInfo.size() != destInfo.size()) {
        Logger::error(QString("文件大小不一致: 源文件 %1 字节, 目标文件 %2 字节").arg(sourceInfo.size()).arg(destInfo.size()));
        return false;
    }
    
    return true;
}

bool UpdateExecutor::executeCommand(const QString &command)
{
    if (command.isEmpty()) {
        return true;
    }
    
    QProcess process;
    process.start("/bin/bash", QStringList() << "-c" << command);
    
    if (!process.waitForFinished(300000)) { // 5分钟超时
        Logger::error(QString("命令执行超时: %1").arg(command));
        return false;
    }
    
    if (process.exitCode() != 0) {
        QString errorOutput = QString::fromLocal8Bit(process.readAllStandardError());
        Logger::error(QString("命令执行失败: %1, 错误: %2").arg(command).arg(errorOutput));
        return false;
    }
    
    return true;
}

