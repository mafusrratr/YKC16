#include "updatestatusmanager.h"
#include "logger.h"
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QFileInfo>
#include <QtCore/QDir>
#include <QtCore/QDateTime>
#include <QtCore/QDebug>

// BY ZF: 更新状态管理器实现
UpdateStatusManager& UpdateStatusManager::instance()
{
    static UpdateStatusManager instance;
    return instance;
}

UpdateStatusManager::UpdateStatusManager()
    : m_statusFilePath("/usr/app/data/update_status.flag")
{
}

UpdateStatusManager::~UpdateStatusManager()
{
}

void UpdateStatusManager::setStatusFilePath(const QString &filePath)
{
    m_statusFilePath = filePath;
}

UpdateStatusManager::Status UpdateStatusManager::getCurrentStatus() const
{
    QString statusStr;
    QString packagePath;
    qint64 timestamp;
    
    if (!readStatusFile(statusStr, packagePath, timestamp)) {
        return UNKNOWN;
    }
    
    return stringToStatus(statusStr);
}

QString UpdateStatusManager::getPackagePath() const
{
    QString statusStr;
    QString packagePath;
    qint64 timestamp;
    
    if (!readStatusFile(statusStr, packagePath, timestamp)) {
        return QString();
    }
    
    return packagePath;
}

qint64 UpdateStatusManager::getTimestamp() const
{
    QString statusStr;
    QString packagePath;
    qint64 timestamp = 0;
    
    if (!readStatusFile(statusStr, packagePath, timestamp)) {
        return 0;
    }
    
    return timestamp;
}

bool UpdateStatusManager::setStatus(Status status, const QString &packagePath)
{
    QString statusStr = statusToString(status);
    qint64 timestamp = QDateTime::currentDateTime().toTime_t();
    
    // BY ZF: 如果包路径为空，尝试保持原有的包路径
    QString finalPackagePath = packagePath;
    if (finalPackagePath.isEmpty()) {
        finalPackagePath = getPackagePath();
    }
    
    bool result = writeStatusFile(statusStr, finalPackagePath, timestamp);
    
    if (result) {
        Logger::warning(QString("更新状态: %1, 包路径: %2").arg(statusStr).arg(finalPackagePath));
    } else {
        Logger::error(QString("无法写入状态文件: %1").arg(m_statusFilePath));
    }
    
    return result;
}

bool UpdateStatusManager::clearStatus()
{
    if (QFileInfo(m_statusFilePath).exists()) {
        if (!QFile::remove(m_statusFilePath)) {
            Logger::error(QString("无法删除状态文件: %1").arg(m_statusFilePath));
            return false;
        }
    }
    return true;
}

QString UpdateStatusManager::statusToString(Status status)
{
    switch (status) {
        case UPDATING:
            return "UPDATING";
        case UPDATED:
            return "UPDATED";
        case ERROR:
            return "ERROR";
        case UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

UpdateStatusManager::Status UpdateStatusManager::stringToStatus(const QString &statusStr)
{
    if (statusStr == "UPDATING") {
        return UPDATING;
    } else if (statusStr == "UPDATED") {
        return UPDATED;
    } else if (statusStr == "ERROR") {
        return ERROR;
    } else {
        return UNKNOWN;
    }
}

bool UpdateStatusManager::readStatusFile(QString &status, QString &packagePath, qint64 &timestamp) const
{
    QFile file(m_statusFilePath);
    if (!file.exists()) {
        return false;
    }
    
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        Logger::error(QString("无法读取状态文件: %1").arg(m_statusFilePath));
        return false;
    }
    
    QTextStream in(&file);
    in.setCodec("UTF-8");
    
    status.clear();
    packagePath.clear();
    timestamp = 0;
    
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        
        // BY ZF: 跳过注释和空行
        if (line.isEmpty() || line.startsWith("#")) {
            continue;
        }
        
        // BY ZF: 解析键值对
        int equalPos = line.indexOf('=');
        if (equalPos < 0) {
            continue;
        }
        
        QString key = line.left(equalPos).trimmed();
        QString value = line.mid(equalPos + 1).trimmed();
        
        if (key == "STATUS") {
            status = value;
        } else if (key == "PACKAGE_PATH") {
            packagePath = value;
        } else if (key == "TIMESTAMP") {
            bool ok;
            timestamp = value.toLongLong(&ok);
            if (!ok) {
                timestamp = 0;
            }
        }
    }
    
    file.close();
    return true;
}

bool UpdateStatusManager::writeStatusFile(const QString &status, const QString &packagePath, qint64 timestamp) const
{
    // BY ZF: 确保目录存在
    QFileInfo fileInfo(m_statusFilePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            Logger::error(QString("无法创建状态文件目录: %1").arg(dir.absolutePath()));
            return false;
        }
    }
    
    QFile file(m_statusFilePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        Logger::error(QString("无法写入状态文件: %1").arg(m_statusFilePath));
        return false;
    }
    
    QTextStream out(&file);
    out.setCodec("UTF-8");
    
    // BY ZF: 写入状态文件内容（INI格式）
    out << "# 更新状态文件\n";
    out << "STATUS=" << status << "\n";
    out << "PACKAGE_PATH=" << packagePath << "\n";
    out << "TIMESTAMP=" << timestamp << "\n";
    
    file.close();
    return true;
}

