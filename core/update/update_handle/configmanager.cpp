#include "configmanager.h"
#include <QtCore/QFileInfo>
#include <QtCore/QDir>

const QString ConfigManager::DEFAULT_BACKUP_DIR = "/mnt/nandflash/ota_back";
const QString ConfigManager::DEFAULT_TEMP_DIR = "/tmp/tcu_update_extract";
const QString ConfigManager::DEFAULT_LOG_FILE = "/var/log/tcu_update.log";
const QString ConfigManager::DEFAULT_STATUS_FILE = "/usr/app/data/update_status.flag";  // BY ZF: 默认状态文件路径
const int ConfigManager::DEFAULT_BACKUP_KEEP_COUNT = 3;
const int ConfigManager::DEFAULT_DISK_SPACE_MARGIN_PERCENT = 10;

ConfigManager::ConfigManager()
    : m_settings(0)
{
}

ConfigManager::~ConfigManager()
{
    if (m_settings) {
        delete m_settings;
        m_settings = 0;
    }
}

ConfigManager& ConfigManager::instance()
{
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::loadConfig(const QString &configPath)
{
    m_configPath = configPath;
    
    // 检查配置文件是否存在
    if (!QFileInfo(configPath).exists()) {
        // 配置文件不存在，使用默认值
        return true;
    }
    
    // 读取配置文件
    if (m_settings) {
        delete m_settings;
    }
    
    m_settings = new QSettings(configPath, QSettings::IniFormat);
    m_settings->setIniCodec("UTF-8");
    
    return true;
}

QString ConfigManager::backupDir() const
{
    if (m_settings) {
        QString dir = m_settings->value("General/backup_dir", DEFAULT_BACKUP_DIR).toString();
        if (!dir.isEmpty()) {
            return dir;
        }
    }
    return DEFAULT_BACKUP_DIR;
}

QString ConfigManager::tempDir() const
{
    if (m_settings) {
        QString dir = m_settings->value("General/temp_dir", DEFAULT_TEMP_DIR).toString();
        if (!dir.isEmpty()) {
            return dir;
        }
    }
    return DEFAULT_TEMP_DIR;
}

QString ConfigManager::logFile() const
{
    if (m_settings) {
        QString file = m_settings->value("General/log_file", DEFAULT_LOG_FILE).toString();
        if (!file.isEmpty()) {
            return file;
        }
    }
    return DEFAULT_LOG_FILE;
}

int ConfigManager::backupKeepCount() const
{
    if (m_settings) {
        return m_settings->value("General/backup_keep_count", DEFAULT_BACKUP_KEEP_COUNT).toInt();
    }
    return DEFAULT_BACKUP_KEEP_COUNT;
}

int ConfigManager::diskSpaceMarginPercent() const
{
    if (m_settings) {
        return m_settings->value("General/disk_space_margin_percent", DEFAULT_DISK_SPACE_MARGIN_PERCENT).toInt();
    }
    return DEFAULT_DISK_SPACE_MARGIN_PERCENT;
}

QString ConfigManager::statusFile() const
{
    // BY ZF: 读取状态文件路径
    if (m_settings) {
        QString file = m_settings->value("General/status_file", DEFAULT_STATUS_FILE).toString();
        if (!file.isEmpty()) {
            return file;
        }
    }
    return DEFAULT_STATUS_FILE;
}

