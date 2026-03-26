#ifndef UPDATEEXECUTOR_H
#define UPDATEEXECUTOR_H

#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QList>
#include "scriptparser.h"

// 更新结果
struct UpdateResult {
    bool success;              // 是否成功
    QStringList failedFiles;   // 失败的文件列表
    QStringList errorMessages; // 错误消息列表
};

class UpdateExecutor
{
public:
    UpdateExecutor();
    ~UpdateExecutor();
    
    // 执行预更新命令
    bool executePreUpdateCommands(const QStringList &commands);
    
    // 执行文件更新
    UpdateResult executeFileUpdate(
        const QString &extractDir,
        const QList<UpdateFile> &files
    );
    
    // 执行后更新命令
    bool executePostUpdateCommands(const QStringList &commands);
    
    // 设置文件权限和所有者
    static bool setFilePermissions(const QString &filePath, const QString &permission, const QString &owner);
    
    // 复制文件
    static bool copyFile(const QString &source, const QString &destination);

private:
    // 执行单个命令
    bool executeCommand(const QString &command);
};

#endif // UPDATEEXECUTOR_H

