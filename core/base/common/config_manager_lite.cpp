/**
 * 轻量级配置管理器实现
 * BY ZF
 */

#include "config_manager_lite.h"
#include <iostream>
#include <algorithm>
#include <cctype>

ConfigManagerLite& ConfigManagerLite::getInstance() {
    static ConfigManagerLite instance;
    return instance;
}

bool ConfigManagerLite::loadConfig(const std::string& configPath) {
    m_configPath = configPath;
    
    std::ifstream file(configPath);
    if (!file.is_open()) {
        std::cout << "Failed to open config file: " << configPath << std::endl;
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    file.close();
    
    parseIniFile(buffer.str());
    
    // 调试：打印解析的配置
    std::cout << "Config loaded from: " << configPath << std::endl;
    std::cout << "Parsed sections: ";
    for (const auto& section : m_config) {
        std::cout << section.first << " ";
    }
    std::cout << std::endl;
    
    return true;
}

std::string ConfigManagerLite::getString(const std::string& section, const std::string& key, const std::string& defaultValue) const {
    auto sectionIt = m_config.find(section);
    if (sectionIt != m_config.end()) {
        auto keyIt = sectionIt->second.find(key);
        if (keyIt != sectionIt->second.end()) {
            return keyIt->second;
        }
    }
    return defaultValue;
}

int ConfigManagerLite::getInt(const std::string& section, const std::string& key, int defaultValue) const {
    std::string value = getString(section, key, "");
    if (value.empty()) {
        return defaultValue;
    }
    // BY ZF: 支持十六进制（0x/0X 前缀），ini 中如 can_id_base=0x180
    if (value.size() >= 2 && (value[0] == '0' && (value[1] == 'x' || value[1] == 'X'))) {
        try {
            return static_cast<int>(std::stoul(value, nullptr, 16));
        } catch (const std::exception& e) {
            std::cout << "Failed to parse hex int value: " << value << " for " << section << "." << key << std::endl;
            return defaultValue;
        }
    }
    try {
        return std::stoi(value);
    } catch (const std::exception& e) {
        std::cout << "Failed to parse int value: " << value << " for " << section << "." << key << std::endl;
        return defaultValue;
    }
}

bool ConfigManagerLite::getBool(const std::string& section, const std::string& key, bool defaultValue) const {
    std::string value = getString(section, key, "");
    if (value.empty()) {
        return defaultValue;
    }
    
    // 转换为小写
    std::transform(value.begin(), value.end(), value.begin(), ::tolower);
    
    return (value == "true" || value == "1" || value == "yes" || value == "on");
}

void ConfigManagerLite::setString(const std::string& section, const std::string& key, const std::string& value) {
    m_config[section][key] = value;
}

bool ConfigManagerLite::saveConfig(const std::string& configPath) const {
    std::ofstream file(configPath);
    if (!file.is_open()) {
        std::cout << "Failed to open config file for writing: " << configPath << std::endl;
        return false;
    }
    
    for (const auto& section : m_config) {
        file << "[" << section.first << "]" << std::endl;
        for (const auto& key : section.second) {
            file << key.first << "=" << key.second << std::endl;
        }
        file << std::endl;
    }
    
    file.close();
    std::cout << "Config saved to: " << configPath << std::endl;
    return true;
}

bool ConfigManagerLite::reloadConfig() {
    if (m_configPath.empty()) {
        std::cout << "No config path set for reload" << std::endl;
        return false;
    }
    
    return loadConfig(m_configPath);
}

void ConfigManagerLite::parseIniFile(const std::string& content) {
    m_config.clear();
    
    std::istringstream stream(content);
    std::string line;
    std::string currentSection;
    
    while (std::getline(stream, line)) {
        line = trim(line);
        
        // 跳过空行和注释
        if (line.empty() || isComment(line)) {
            continue;
        }
        
        // 检查节名
        if (line.front() == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.length() - 2);
            continue;
        }
        
        // 解析键值对
        size_t pos = line.find('=');
        if (pos != std::string::npos) {
            std::string key = trim(line.substr(0, pos));
            std::string value = trim(line.substr(pos + 1));
            
            if (!currentSection.empty() && !key.empty()) {
                m_config[currentSection][key] = value;
            }
        }
    }
}

std::string ConfigManagerLite::trim(const std::string& str) const {
    size_t first = str.find_first_not_of(' ');
    if (first == std::string::npos) {
        return "";
    }
    
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

bool ConfigManagerLite::isComment(const std::string& line) const {
    return line.front() == '#' || line.front() == ';';
}
