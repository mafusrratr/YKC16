#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QtGui/QMainWindow>
#include <QtCore/QString>

class QLabel;
class QProgressBar;
class QTextEdit;
class QPushButton;
class QVBoxLayout;
class QHBoxLayout;
class QTimer;
class UpdateEngine;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    // 设置升级包路径（更新模式）
    void setPackagePath(const QString &packagePath);
    
    // 设置回滚模式
    void setRollbackMode();

private slots:
    void onUpdateProgress(int percentage, const QString &status);
    void onUpdateFinished(bool success, const QString &message);
    void onUpdateError(const QString &error);
    void onLogMessage(const QString &message);
    void onRollbackClicked();
    void onUpdateStarted();
    void onCountdownTimer();
    void onCloseButtonClicked();

private:
    void setupUI();
    void setupConnections();
    
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QTextEdit *m_logTextEdit;
    QPushButton *m_rollbackButton;
    QPushButton *m_closeButton;
    
    UpdateEngine *m_updateEngine;
    QString m_packagePath;
    bool m_isRollbackMode;
    
    QTimer *m_countdownTimer;
    int m_countdownSeconds;
};

#endif // MAINWINDOW_H

