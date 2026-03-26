#ifndef SCRIPTPARSER_H
#define SCRIPTPARSER_H

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QList>

// 文件信息结构
struct UpdateFile {
    QString source;          // 源路径（升级包内）
    QString destination;     // 目标路径（系统中）
    QString permission;     // 权限（777）
    QString owner;           // 所有者（root）
    bool backup;            // 是否备份
    QString md5;            // MD5校验和
    qint64 size;            // 文件大小
};

// 包信息结构
struct PackageInfo {
    QString name;           // 包名称
    QString buildDate;      // 构建日期
    QString description;    // 描述
};

class ScriptParser
{
public:
    ScriptParser();
    ~ScriptParser();
    
    // 解析XML清单文件
    bool parse(const QString &xmlPath);
    
    // 获取信息
    PackageInfo packageInfo() const { return m_packageInfo; }
    QList<UpdateFile> files() const { return m_files; }
    QStringList preUpdateCommands() const { return m_preUpdateCommands; }
    QStringList postUpdateCommands() const { return m_postUpdateCommands; }
    QStringList rollbackCommands() const { return m_rollbackCommands; }
    
    // 验证清单有效性
    bool validate() const;
    
    // 验证文件MD5
    bool verifyChecksum(const QString &filePath, const QString &expectedMd5) const;

private:
    PackageInfo m_packageInfo;
    QList<UpdateFile> m_files;
    QStringList m_preUpdateCommands;
    QStringList m_postUpdateCommands;
    QStringList m_rollbackCommands;
    
    bool parsePackageInfo(const QString &xmlContent);
    bool parseFiles(const QString &xmlContent);
    bool parseCommands(const QString &xmlContent);
};

#endif // SCRIPTPARSER_H

