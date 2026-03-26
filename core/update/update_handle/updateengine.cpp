#include "updateengine.h"
#include "configmanager.h"
#include "logger.h"
#include "updateexecutor.h"
#include "diskspacechecker.h"
#include "updatestatusmanager.h"  // BY ZF: 状态管理器
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QProcess>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QDebug>

UpdateEngine::UpdateEngine(QObject *parent)
    : QThread(parent)
    , m_operation(Update)
    , m_lastError("")
{
}

UpdateEngine::~UpdateEngine()
{
}

void UpdateEngine::startUpdate(const QString &packagePath)
{
    m_operation = Update;
    m_packagePath = packagePath;
    QThread::start();
}

void UpdateEngine::startRollback()
{
    m_operation = Rollback;
    m_packagePath.clear();
    QThread::start();
}

void UpdateEngine::run()
{
    emit started();
    
    // BY ZF: 初始化错误信息
    m_lastError.clear();
    
    // 初始化配置和日志
    ConfigManager::instance().loadConfig();
    Logger::init(ConfigManager::instance().logFile());
    
    // BY ZF: 初始化状态管理器（从配置文件读取状态文件路径）
    UpdateStatusManager::instance().setStatusFilePath(ConfigManager::instance().statusFile());
    
    bool success = false;
    QString message;
    
    if (m_operation == Update) {
        // BY ZF: 设置状态为更新中
        UpdateStatusManager::instance().setStatus(UpdateStatusManager::UPDATING, m_packagePath);
        
        success = performUpdate();
        // BY ZF: 如果失败且有错误信息，使用具体错误信息；否则使用默认消息
        if (success) {
            message = "更新成功";
            // BY ZF: 设置状态为更新完成
            UpdateStatusManager::instance().setStatus(UpdateStatusManager::UPDATED, m_packagePath);
        } else {
            message = m_lastError.isEmpty() ? "更新失败" : QString("更新失败: %1").arg(m_lastError);
            // BY ZF: 设置状态为错误
            UpdateStatusManager::instance().setStatus(UpdateStatusManager::ERROR, m_packagePath);
        }
    } else if (m_operation == Rollback) {
        // BY ZF: 回滚也使用UPDATING状态（回滚就是逆安装操作）
        UpdateStatusManager::instance().setStatus(UpdateStatusManager::UPDATING);
        
        success = performRollback();
        // BY ZF: 如果失败且有错误信息，使用具体错误信息；否则使用默认消息
        if (success) {
            message = "回滚成功";
            // BY ZF: 回滚成功也使用UPDATED状态（回滚就是逆安装操作）
            UpdateStatusManager::instance().setStatus(UpdateStatusManager::UPDATED);
        } else {
            message = m_lastError.isEmpty() ? "回滚失败" : QString("回滚失败: %1").arg(m_lastError);
            // BY ZF: 设置状态为错误
            UpdateStatusManager::instance().setStatus(UpdateStatusManager::ERROR);
        }
    }
    
    emit finished(success, message);
    Logger::close();
}

bool UpdateEngine::performUpdate()
{
    emitProgress(5, "开始更新流程...");
    
    // [1] 验证升级包
    if (!verifyPackage()) {
        return false;
    }
    emitProgress(10, "升级包验证通过");
    
    // [2] 解压升级包
    if (!extractPackage()) {
        return false;
    }
    emitProgress(20, "升级包解压完成");
    
    // [3] 验证MD5校验和
    if (!verifyChecksum()) {
        return false;
    }
    emitProgress(30, "MD5校验通过");
    
    // [4] 解析manifest.xml
    QString tempDir = ConfigManager::instance().tempDir();
    
    // 检查manifest.xml路径：可能在根目录或install子目录下
    QString manifestPath1 = QString("%1/manifest.xml").arg(tempDir);
    QString manifestPath2 = QString("%1/install/manifest.xml").arg(tempDir);
    
    QString manifestPath;
    if (QFileInfo(manifestPath1).exists()) {
        manifestPath = manifestPath1;
    } else if (QFileInfo(manifestPath2).exists()) {
        manifestPath = manifestPath2;
    } else {
        emitError("未找到manifest.xml文件");
        Logger::error("未找到manifest.xml文件");
        return false;
    }
    
    ScriptParser parser;
    if (!parser.parse(manifestPath)) {
        emitError("清单文件解析失败");
        Logger::error("清单文件解析失败");
        return false;
    }
    
    if (!parser.validate()) {
        emitError("清单文件验证失败");
        Logger::error("清单文件验证失败");
        return false;
    }
    emitProgress(40, "清单文件解析完成");
    
    // [5] 检查磁盘空间
    qint64 backupSize = BackupManager::calculateBackupSize(parser.files());
    QList<SpaceInfo> spaceInfos = DiskSpaceChecker::checkSpace(
        m_packagePath,
        parser.files(),
        backupSize,
        ConfigManager::instance().diskSpaceMarginPercent()
    );
    
    bool spaceSufficient = true;
    for (int i = 0; i < spaceInfos.size(); ++i) {
        const SpaceInfo &info = spaceInfos.at(i);
        if (!info.sufficient) {
            spaceSufficient = false;
            QString error = QString("分区空间不足: %1\n需要: %2, 可用: %3")
                .arg(info.partition)
                .arg(DiskSpaceChecker::formatSize(info.required))
                .arg(DiskSpaceChecker::formatSize(info.available));
            emitError(error);
            Logger::error(error);
        }
    }
    
    if (!spaceSufficient) {
        return false;
    }
    emitProgress(50, "磁盘空间检查通过");
    
    // [6] 创建备份并生成回滚包
    BackupManager backupManager;
    QString backupDir = backupManager.createBackupDir();
    if (backupDir.isEmpty()) {
        emitError("无法创建备份目录");
        Logger::error("无法创建备份目录");
        return false;
    }
    
    // BY ZF: 执行备份（复制文件到备份目录）
    if (!backupManager.executeBackup(backupDir, parser.files())) {
        emitError("备份执行失败");
        Logger::error("备份执行失败");
        return false;
    }
    
    // BY ZF: 生成回滚包（rollback.tar.gz）
    if (!backupManager.generateRollbackPackage(backupDir, parser.files())) {
        emitError("无法生成回滚包");
        Logger::error("无法生成回滚包");
        return false;
    }
    
    // 清理旧备份
    backupManager.cleanupOldBackups(ConfigManager::instance().backupKeepCount());
    emitProgress(60, "备份完成");
    
    // [7] 执行预更新命令
    UpdateExecutor executor;
    if (!executor.executePreUpdateCommands(parser.preUpdateCommands())) {
        // 预更新命令失败只记录警告，不中断
        emitLog("警告: 部分预更新命令执行失败，继续执行");
    }
    emitProgress(65, "预更新命令执行完成");
    
    // [8] 执行文件更新
    // 确定解压后的实际目录（可能在install子目录下）
    QString extractDir = tempDir;
    if (QFileInfo(QString("%1/install").arg(tempDir)).exists()) {
        extractDir = QString("%1/install").arg(tempDir);
    }
    UpdateResult result = executor.executeFileUpdate(extractDir, parser.files());
    
    if (!result.success) {
        // 部分文件失败，显示错误信息
        QString errorMsg = "部分文件更新失败:\n";
        for (int i = 0; i < result.failedFiles.size(); ++i) {
            errorMsg += QString("- %1: %2\n")
                .arg(result.failedFiles.at(i))
                .arg(result.errorMessages.at(i));
        }
        emitError(errorMsg);
        Logger::error(errorMsg);
        
        // 等待用户选择回滚（通过UI）
        emitProgress(70, "部分文件更新失败，等待用户操作");
        return false;
    }
    emitProgress(80, "文件更新完成");
    
    // [9] 执行后更新命令
    if (!executor.executePostUpdateCommands(parser.postUpdateCommands())) {
        emitError("后更新命令执行失败");
        Logger::error("后更新命令执行失败");
        // 后更新命令失败，需要回滚
        return false;
    }
    emitProgress(90, "后更新命令执行完成");
    
    // [10] 清理临时文件
    if (!cleanup()) {
        emitLog("警告: 清理临时文件失败");
    }
    emitProgress(100, "更新完成");
    
    return true;
}

bool UpdateEngine::performRollback()
{
    emitProgress(10, "开始回滚流程...");
    emitLog("开始回滚流程");
    
    // BY ZF: 获取最新的备份目录和回滚包
    BackupManager backupManager;
    QString latestBackup = backupManager.getLatestBackupDir();
    
    if (latestBackup.isEmpty()) {
        emitError("没有找到备份目录");
        Logger::error("没有找到备份目录");
        return false;
    }
    
    QString rollbackPackage = QString("%1/rollback.tar.gz").arg(latestBackup);
    
    if (!QFileInfo(rollbackPackage).exists()) {
        emitError("回滚包不存在");
        Logger::error("回滚包不存在");
        return false;
    }
    
    emitProgress(20, "找到回滚包，开始执行回滚");
    
    // BY ZF: 使用回滚包路径，复用更新流程
    m_packagePath = rollbackPackage;
    
    // BY ZF: 调用更新流程（回滚就是逆更新）
    return performUpdate();
}

bool UpdateEngine::verifyPackage()
{
    if (m_packagePath.isEmpty()) {
        emitError("升级包路径为空");
        Logger::error("升级包路径为空");
        return false;
    }
    
    QFileInfo fileInfo(m_packagePath);
    if (!fileInfo.exists()) {
        emitError(QString("升级包文件不存在: %1").arg(m_packagePath));
        Logger::error(QString("升级包文件不存在: %1").arg(m_packagePath));
        return false;
    }
    
    if (!fileInfo.isFile()) {
        emitError(QString("升级包路径不是文件: %1").arg(m_packagePath));
        Logger::error(QString("升级包路径不是文件: %1").arg(m_packagePath));
        return false;
    }
    
    emitLog(QString("升级包验证通过: %1").arg(m_packagePath));
    return true;
}

bool UpdateEngine::extractPackage()
{
    QString tempDir = ConfigManager::instance().tempDir();
    
    // 清理临时目录（Qt 4.8.6 兼容方式）
    QDir dir(tempDir);
    if (dir.exists()) {
        QProcess rmProcess;
        rmProcess.start("rm", QStringList() << "-rf" << tempDir);
        if (!rmProcess.waitForFinished(30000) || rmProcess.exitCode() != 0) {
            emitLog("警告: 无法清理临时目录，继续执行");
        }
    }
    
    // 创建临时目录
    if (!dir.mkpath(tempDir)) {
        emitError(QString("无法创建临时目录: %1").arg(tempDir));
        Logger::error(QString("无法创建临时目录: %1").arg(tempDir));
        return false;
    }
    
    // 解压tar.gz文件
    QProcess process;
    process.setWorkingDirectory(tempDir);
    process.start("tar", QStringList() << "-xzf" << m_packagePath);
    
    if (!process.waitForFinished(300000)) {
        emitError("解压超时");
        Logger::error("解压超时");
        return false;
    }
    
    if (process.exitCode() != 0) {
        QString errorOutput = QString::fromLocal8Bit(process.readAllStandardError());
        emitError(QString("解压失败: %1").arg(errorOutput));
        Logger::error(QString("解压失败: %1").arg(errorOutput));
        return false;
    }
    
    // BY ZF: 解压成功日志由performUpdate中的emitProgress统一输出，避免重复
    return true;
}

bool UpdateEngine::verifyChecksum()
{
    QString tempDir = ConfigManager::instance().tempDir();
    
    // BY ZF: 确定解压后的实际目录（可能在install子目录下）
    QString extractDir = tempDir;
    if (QFileInfo(QString("%1/install").arg(tempDir)).exists()) {
        extractDir = QString("%1/install").arg(tempDir);
    }
    
    // 检查checksum.md5文件路径
    QString checksumFile1 = QString("%1/checksum.md5").arg(tempDir);
    QString checksumFile2 = QString("%1/install/checksum.md5").arg(tempDir);
    
    QString checksumFile;
    if (QFileInfo(checksumFile1).exists()) {
        checksumFile = checksumFile1;
    } else if (QFileInfo(checksumFile2).exists()) {
        checksumFile = checksumFile2;
    } else {
        emitLog("警告: 未找到checksum.md5文件，跳过校验");
        return true; // 允许没有校验文件
    }
    
    // BY ZF: 使用md5sum -c命令验证所有文件的MD5（标准方式）
    QProcess process;
    process.setWorkingDirectory(extractDir);
    
    // 使用md5sum -c命令验证checksum.md5中的所有文件
    // -c: 从文件中读取MD5并验证
    process.start("md5sum", QStringList() << "-c" << checksumFile);
    
    if (!process.waitForFinished(300000)) {  // 5分钟超时
        emitError("MD5校验超时");
        Logger::error("MD5校验超时");
        return false;
    }
    
    if (process.exitCode() != 0) {
        QString errorOutput = QString::fromLocal8Bit(process.readAllStandardError());
        QString standardOutput = QString::fromLocal8Bit(process.readAllStandardOutput());
        
        // BY ZF: 提取失败的MD5信息
        QString errorMsg = "MD5校验失败\n";
        if (!standardOutput.isEmpty()) {
            // md5sum -c 的输出格式：文件名: FAILED 或 文件名: OK
            QStringList lines = standardOutput.split("\n", QString::SkipEmptyParts);
            for (int i = 0; i < lines.size(); ++i) {
                QString line = lines.at(i).trimmed();
                if (line.contains("FAILED") || line.contains("失败")) {
                    errorMsg += line + "\n";
                }
            }
        }
        if (!errorOutput.isEmpty()) {
            errorMsg += "错误信息: " + errorOutput;
        }
        
        emitError(errorMsg);
        Logger::error(errorMsg);
        return false;
    }
    
    emitLog("MD5校验通过");
    return true;
}

bool UpdateEngine::parseManifest()
{
    QString tempDir = ConfigManager::instance().tempDir();
    QString manifestPath = QString("%1/manifest.xml").arg(tempDir);
    
    if (!QFileInfo(manifestPath).exists()) {
        emitError("清单文件不存在");
        Logger::error("清单文件不存在");
        return false;
    }
    
    ScriptParser parser;
    if (!parser.parse(manifestPath)) {
        emitError("清单文件解析失败");
        Logger::error("清单文件解析失败");
        return false;
    }
    
    emitLog("清单文件解析完成");
    return true;
}

bool UpdateEngine::checkDiskSpace()
{
    // 此方法已在performUpdate中实现
    return true;
}

bool UpdateEngine::createBackup()
{
    // 此方法已在performUpdate中实现
    return true;
}

bool UpdateEngine::executePreUpdateCommands()
{
    // 此方法已在performUpdate中实现
    return true;
}

bool UpdateEngine::executeFileUpdate()
{
    // 此方法已在performUpdate中实现
    return true;
}

bool UpdateEngine::executePostUpdateCommands()
{
    // 此方法已在performUpdate中实现
    return true;
}

bool UpdateEngine::cleanup()
{
    QString tempDir = ConfigManager::instance().tempDir();
    
    QDir dir(tempDir);
    if (dir.exists()) {
        // Qt 4.8.6 兼容方式
        QProcess rmProcess;
        rmProcess.start("rm", QStringList() << "-rf" << tempDir);
        if (!rmProcess.waitForFinished(30000) || rmProcess.exitCode() != 0) {
            Logger::warning(QString("无法删除临时目录: %1").arg(tempDir));
            return false;
        }
    }
    
    emitLog("临时文件清理完成");
    return true;
}

void UpdateEngine::emitProgress(int percentage, const QString &status)
{
    emit progress(percentage, status);
    emitLog(status);
}

void UpdateEngine::emitLog(const QString &message)
{
    emit logMessage(message);
}

void UpdateEngine::emitError(const QString &errorMsg)
{
    // BY ZF: 保存最后一次错误信息，用于在finished信号中传递
    m_lastError = errorMsg;
    
    emit error(errorMsg);
    Logger::error(errorMsg);
}

