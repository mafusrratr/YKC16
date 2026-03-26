#include "logger.h"
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>

QString Logger::m_logFile;
bool Logger::m_initialized = false;

Logger::Logger()
{
}

bool Logger::init(const QString &logFile)
{
    m_logFile = logFile;
    
    // 确保日志目录存在
    QFileInfo fileInfo(logFile);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            return false;
        }
    }
    
    m_initialized = true;
    return true;
}

void Logger::log(LogLevel level, const QString &message)
{
    if (!m_initialized || m_logFile.isEmpty()) {
        return;
    }
    
    QString formattedMessage = formatMessage(level, message);
    
    QFile file(m_logFile);
    if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&file);
        out.setCodec("UTF-8");
        out << formattedMessage << "\n";
        file.close();
    }
}

void Logger::error(const QString &message)
{
    log(ERROR, message);
}

void Logger::warning(const QString &message)
{
    log(WARNING, message);
}

void Logger::close()
{
    m_initialized = false;
    m_logFile.clear();
}

QString Logger::formatMessage(LogLevel level, const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    QString levelStr = levelToString(level);
    return QString("[%1] [%2] %3").arg(timestamp).arg(levelStr).arg(message);
}

QString Logger::levelToString(LogLevel level)
{
    switch (level) {
        case ERROR:
            return "ERROR";
        case WARNING:
            return "WARNING";
        default:
            return "UNKNOWN";
    }
}

