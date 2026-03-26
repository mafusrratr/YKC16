#ifndef UPDATESTATUSMANAGER_H
#define UPDATESTATUSMANAGER_H

#include <QtCore/QString>
#include <QtCore/QtGlobal>  // BY ZF: 用于qint64类型

// BY ZF: 更新状态管理器
class UpdateStatusManager
{
public:
    enum Status {
        UNKNOWN,      // 未知状态
        UPDATING,     // 更新中（包括回滚）
        UPDATED,      // 更新完成（包括回滚完成）
        ERROR         // 错误
    };
    
    // BY ZF: 获取单例实例
    static UpdateStatusManager& instance();
    
    // BY ZF: 设置状态文件路径
    void setStatusFilePath(const QString &filePath);
    
    // BY ZF: 读取当前状态
    Status getCurrentStatus() const;
    
    // BY ZF: 读取包路径
    QString getPackagePath() const;
    
    // BY ZF: 读取时间戳
    qint64 getTimestamp() const;
    
    // BY ZF: 设置状态
    bool setStatus(Status status, const QString &packagePath = QString());
    
    // BY ZF: 清除状态文件
    bool clearStatus();
    
    // BY ZF: 状态转字符串
    static QString statusToString(Status status);
    
    // BY ZF: 字符串转状态
    static Status stringToStatus(const QString &statusStr);

private:
    UpdateStatusManager();
    ~UpdateStatusManager();
    
    // BY ZF: 禁止拷贝
    UpdateStatusManager(const UpdateStatusManager&);
    UpdateStatusManager& operator=(const UpdateStatusManager&);
    
    QString m_statusFilePath;
    
    // BY ZF: 读取状态文件内容
    bool readStatusFile(QString &status, QString &packagePath, qint64 &timestamp) const;
    
    // BY ZF: 写入状态文件
    bool writeStatusFile(const QString &status, const QString &packagePath, qint64 timestamp) const;
};

#endif // UPDATESTATUSMANAGER_H

