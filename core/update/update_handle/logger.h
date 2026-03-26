#ifndef LOGGER_H
#define LOGGER_H

#include <QtCore/QString>

class Logger
{
public:
    enum LogLevel {
        ERROR,
        WARNING
    };
    
    // 初始化日志系统
    static bool init(const QString &logFile);
    
    // 记录日志
    static void log(LogLevel level, const QString &message);
    static void error(const QString &message);
    static void warning(const QString &message);
    
    // 关闭日志系统
    static void close();

private:
    Logger();
    static QString formatMessage(LogLevel level, const QString &message);
    static QString levelToString(LogLevel level);
    
    static QString m_logFile;
    static bool m_initialized;
};

#endif // LOGGER_H

