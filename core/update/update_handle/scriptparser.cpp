#include "scriptparser.h"
#include <QtCore/QFile>
#include <QtXml/QDomDocument>
#include <QtXml/QDomElement>
#include <QtXml/QDomNode>
#include <QtCore/QCryptographicHash>
#include <QtCore/QFileInfo>

ScriptParser::ScriptParser()
{
}

ScriptParser::~ScriptParser()
{
}

bool ScriptParser::parse(const QString &xmlPath)
{
    QFile file(xmlPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    
    QDomDocument doc;
    QString errorMsg;
    int errorLine, errorColumn;
    
    if (!doc.setContent(&file, false, &errorMsg, &errorLine, &errorColumn)) {
        file.close();
        return false;
    }
    file.close();
    
    QDomElement root = doc.documentElement();
    if (root.tagName() != "package") {
        return false;
    }
    
    // 解析包信息
    QDomElement packageInfoElem = root.firstChildElement("packageInfo");
    if (!packageInfoElem.isNull()) {
        m_packageInfo.name = packageInfoElem.firstChildElement("name").text();
        m_packageInfo.buildDate = packageInfoElem.firstChildElement("buildDate").text();
        m_packageInfo.description = packageInfoElem.firstChildElement("description").text();
    }
    
    // 解析文件列表
    QDomElement filesElem = root.firstChildElement("files");
    if (!filesElem.isNull()) {
        QDomNodeList fileNodes = filesElem.elementsByTagName("file");
        for (int i = 0; i < fileNodes.size(); ++i) {
            QDomElement fileElem = fileNodes.at(i).toElement();
            if (fileElem.isNull()) continue;
            
            UpdateFile file;
            file.source = fileElem.firstChildElement("source").text();
            file.destination = fileElem.firstChildElement("destination").text();
            file.permission = fileElem.firstChildElement("permission").text();
            file.owner = fileElem.firstChildElement("owner").text();
            file.backup = (fileElem.firstChildElement("backup").text().toLower() == "true");
            file.md5 = fileElem.firstChildElement("md5").text();
            file.size = fileElem.firstChildElement("size").text().toLongLong();
            
            m_files.append(file);
        }
    }
    
    // 解析命令
    QDomElement commandsElem = root.firstChildElement("commands");
    if (!commandsElem.isNull()) {
        // 预更新命令
        QDomElement preUpdateElem = commandsElem.firstChildElement("preUpdate");
        if (!preUpdateElem.isNull()) {
            QDomNodeList cmdNodes = preUpdateElem.elementsByTagName("command");
            for (int i = 0; i < cmdNodes.size(); ++i) {
                QString cmd = cmdNodes.at(i).toElement().text();
                if (!cmd.isEmpty()) {
                    m_preUpdateCommands.append(cmd);
                }
            }
        }
        
        // 后更新命令
        QDomElement postUpdateElem = commandsElem.firstChildElement("postUpdate");
        if (!postUpdateElem.isNull()) {
            QDomNodeList cmdNodes = postUpdateElem.elementsByTagName("command");
            for (int i = 0; i < cmdNodes.size(); ++i) {
                QString cmd = cmdNodes.at(i).toElement().text();
                if (!cmd.isEmpty()) {
                    m_postUpdateCommands.append(cmd);
                }
            }
        }
        
        // 回滚命令
        QDomElement rollbackElem = commandsElem.firstChildElement("rollback");
        if (!rollbackElem.isNull()) {
            QDomNodeList cmdNodes = rollbackElem.elementsByTagName("command");
            for (int i = 0; i < cmdNodes.size(); ++i) {
                QString cmd = cmdNodes.at(i).toElement().text();
                if (!cmd.isEmpty()) {
                    m_rollbackCommands.append(cmd);
                }
            }
        }
    }
    
    return true;
}

bool ScriptParser::validate() const
{
    // 检查基本字段
    if (m_files.isEmpty()) {
        return false;
    }
    
    // 检查每个文件的必要字段
    for (int i = 0; i < m_files.size(); ++i) {
        const UpdateFile &file = m_files.at(i);
        if (file.source.isEmpty() || file.destination.isEmpty()) {
            return false;
        }
        if (file.destination[0] != '/') {
            return false; // 目标路径必须是绝对路径
        }
    }
    
    return true;
}

bool ScriptParser::verifyChecksum(const QString &filePath, const QString &expectedMd5) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QCryptographicHash hash(QCryptographicHash::Md5);
    const qint64 bufferSize = 8192;
    char buffer[bufferSize];
    qint64 bytesRead;
    
    while ((bytesRead = file.read(buffer, bufferSize)) > 0) {
        hash.addData(buffer, bytesRead);
    }
    file.close();
    
    if (file.error() != QFile::NoError) {
        return false;
    }
    
    QString actualMd5 = hash.result().toHex().toLower();
    return (actualMd5 == expectedMd5.toLower());
}

