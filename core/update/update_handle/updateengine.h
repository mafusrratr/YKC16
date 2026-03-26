#ifndef UPDATEENGINE_H
#define UPDATEENGINE_H

#include <QtCore/QObject>
#include <QtCore/QThread>
#include <QtCore/QString>
#include "scriptparser.h"
#include "backupmanager.h"

class UpdateEngine : public QThread
{
    Q_OBJECT

public:
    explicit UpdateEngine(QObject *parent = 0);
    ~UpdateEngine();
    
    // 开始更新
    void startUpdate(const QString &packagePath);
    
    // 开始回滚
    void startRollback();

signals:
    void progress(int percentage, const QString &status);
    void finished(bool success, const QString &message);
    void error(const QString &error);
    void logMessage(const QString &message);
    void started();

protected:
    void run();

private:
    enum Operation {
        Update,
        Rollback
    };
    
    Operation m_operation;
    QString m_packagePath;
    QString m_lastError;  // BY ZF: 保存最后一次错误信息
    
    // 更新流程
    bool performUpdate();
    
    // 回滚流程
    bool performRollback();
    
    // 更新步骤
    bool verifyPackage();
    bool extractPackage();
    bool verifyChecksum();
    bool parseManifest();
    bool checkDiskSpace();
    bool createBackup();
    bool executePreUpdateCommands();
    bool executeFileUpdate();
    bool executePostUpdateCommands();
    bool cleanup();
    
    // 辅助方法
    void emitProgress(int percentage, const QString &status);
    void emitLog(const QString &message);
    void emitError(const QString &error);
};

#endif // UPDATEENGINE_H

