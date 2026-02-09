/**
 * 轻量级配置管理器（不依赖Qt）
 * BY ZF
 */

#ifndef CONFIG_MANAGER_LITE_H
#define CONFIG_MANAGER_LITE_H

#include <string>
#include <map>
#include <fstream>
#include <sstream>

/**
 * 轻量级配置管理器
 * 使用标准C++实现INI文件解析，不依赖Qt
 */
class ConfigManagerLite {
public:
    /**
     * 获取单例实例
     */
    static ConfigManagerLite& getInstance();
    
    /**
     * 加载配置文件
     * @param configPath 配置文件路径
     * @return true成功
     */
    bool loadConfig(const std::string& configPath);
    
    /**
     * 获取字符串配置
     * @param section 节名
     * @param key 键名
     * @param defaultValue 默认值
     * @return 配置值
     */
    std::string getString(const std::string& section, const std::string& key, const std::string& defaultValue = "") const;
    
    /**
     * 获取整数配置
     * @param section 节名
     * @param key 键名
     * @param defaultValue 默认值
     * @return 配置值
     */
    int getInt(const std::string& section, const std::string& key, int defaultValue = 0) const;
    
    /**
     * 获取布尔配置
     * @param section 节名
     * @param key 键名
     * @param defaultValue 默认值
     * @return 配置值
     */
    bool getBool(const std::string& section, const std::string& key, bool defaultValue = false) const;
    
    /**
     * 设置配置值
     * @param section 节名
     * @param key 键名
     * @param value 值
     */
    void setString(const std::string& section, const std::string& key, const std::string& value);
    
    /**
     * 保存配置到文件
     * @param configPath 配置文件路径
     * @return true成功
     */
    bool saveConfig(const std::string& configPath) const;
    
    /**
     * 重新加载配置
     * @return true成功
     */
    bool reloadConfig();
    
private:
    ConfigManagerLite() = default;
    ~ConfigManagerLite() = default;
    ConfigManagerLite(const ConfigManagerLite&) = delete;
    ConfigManagerLite& operator=(const ConfigManagerLite&) = delete;
    
    /**
     * 解析INI文件
     */
    void parseIniFile(const std::string& content);
    
    /**
     * 去除字符串前后空白
     */
    std::string trim(const std::string& str) const;
    
    /**
     * 检查字符串是否为注释
     */
    bool isComment(const std::string& line) const;
    
    // 配置数据：section -> (key -> value)
    std::map<std::string, std::map<std::string, std::string>> m_config;
    std::string m_configPath;
};

#endif // CONFIG_MANAGER_LITE_H
