#ifndef CONFIGMANAGER_H
#define CONFIGMANAGER_H

#include <QtCore/QString>
#include <QtCore/QSettings>

class ConfigManager
{
public:
    static ConfigManager& instance();
    
    // 读取配置
    QString backupDir() const;
    QString tempDir() const;
    QString logFile() const;
    int backupKeepCount() const;
    int diskSpaceMarginPercent() const;
    QString statusFile() const;  // BY ZF: 状态文件路径
    
    // 初始化（读取配置文件）
    bool loadConfig(const QString &configPath = "/usr/app/config/tcu_update.conf");
    
private:
    ConfigManager();
    ~ConfigManager();
    ConfigManager(const ConfigManager&);
    ConfigManager& operator=(const ConfigManager&);
    
    QSettings *m_settings;
    QString m_configPath;
    
    // 默认值
    static const QString DEFAULT_BACKUP_DIR;
    static const QString DEFAULT_TEMP_DIR;
    static const QString DEFAULT_LOG_FILE;
    static const QString DEFAULT_STATUS_FILE;  // BY ZF: 默认状态文件路径
    static const int DEFAULT_BACKUP_KEEP_COUNT;
    static const int DEFAULT_DISK_SPACE_MARGIN_PERCENT;
};

#endif // CONFIGMANAGER_H

