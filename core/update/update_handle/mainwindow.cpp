#include "mainwindow.h"
#include "updateengine.h"
#include <QtGui/QLabel>
#include <QtGui/QProgressBar>
#include <QtGui/QTextEdit>
#include <QtGui/QPushButton>
#include <QtGui/QVBoxLayout>
#include <QtGui/QHBoxLayout>
#include <QtGui/QWidget>
#include <QtGui/QMessageBox>
#include <QtGui/QFont>
#include <QtGui/QApplication>
#include <QtCore/QDateTime>
#include <QtCore/QTextCodec>
#include <QtCore/QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_statusLabel(0)
    , m_progressBar(0)
    , m_logTextEdit(0)
    , m_rollbackButton(0)
    , m_closeButton(0)
    , m_updateEngine(0)
    , m_isRollbackMode(false)
    , m_countdownTimer(0)
    , m_countdownSeconds(9)
{
    setupUI();
    setupConnections();
    
    setWindowTitle("TCU 升级程序");
    // BY ZF: 设置窗口大小为 800x400（目标设备分辨率）
    resize(800, 400);
    setMinimumSize(800, 400);
    setMaximumSize(800, 400);
}

MainWindow::~MainWindow()
{
    if (m_updateEngine) {
        delete m_updateEngine;
    }
}

void MainWindow::setupUI()
{
    QWidget *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setSpacing(5);  // BY ZF: 调整间距以适应400像素高度
    mainLayout->setContentsMargins(10, 10, 10, 10);  // 调整边距
    
    // BY ZF: 状态标签（优化显示）
    m_statusLabel = new QLabel("准备就绪", this);
    // 使用系统默认字体
    QFont statusFont = QApplication::font();
    statusFont.setPointSize(12);  // BY ZF: 调整字体大小
    statusFont.setBold(true);
    m_statusLabel->setFont(statusFont);
    m_statusLabel->setStyleSheet("font-size: 12px; font-weight: bold; padding: 5px;");
    m_statusLabel->setWordWrap(true);  // 允许文本换行
    m_statusLabel->setMaximumHeight(40);  // BY ZF: 调整最大高度
    mainLayout->addWidget(m_statusLabel);
    
    // BY ZF: 进度条
    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setMinimumHeight(25);  // BY ZF: 调整高度
    m_progressBar->setMaximumHeight(25);
    mainLayout->addWidget(m_progressBar);
    
    // BY ZF: 日志显示区域（设置最小高度，确保有足够显示空间）
    m_logTextEdit = new QTextEdit(this);
    m_logTextEdit->setReadOnly(true);
    // 使用系统默认字体，设置为等宽字体样式
    QFont logFont = QApplication::font();
    logFont.setStyleHint(QFont::Monospace);  // 等宽字体
    logFont.setPointSize(10);  // BY ZF: 增大字体到10pt，提高可读性
    m_logTextEdit->setFont(logFont);
    m_logTextEdit->setMinimumHeight(280);  // BY ZF: 调整最小高度以适应400像素屏幕
    // 确保文本编码正确
    m_logTextEdit->setAcceptRichText(false);  // 纯文本模式，避免编码问题
    mainLayout->addWidget(m_logTextEdit, 1);  // 使用拉伸因子，让日志区域占据剩余空间
    
    // 按钮区域
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    // BY ZF: 使用系统默认字体
    QFont buttonFont = QApplication::font();
    buttonFont.setPointSize(12);  // BY ZF: 调整字体大小
    
    m_rollbackButton = new QPushButton("执行回滚", this);
    m_rollbackButton->setEnabled(false);
    m_rollbackButton->setFont(buttonFont);
    m_rollbackButton->setStyleSheet("background-color: #ff6b6b; color: white; padding: 8px 20px; font-size: 12px;");
    m_rollbackButton->setMinimumHeight(35);  // BY ZF: 调整高度
    m_rollbackButton->setMaximumHeight(35);
    buttonLayout->addWidget(m_rollbackButton);
    
    m_closeButton = new QPushButton("关闭", this);
    m_closeButton->setEnabled(false);
    m_closeButton->setFont(buttonFont);
    m_closeButton->setMinimumHeight(35);  // BY ZF: 调整高度
    m_closeButton->setMaximumHeight(35);
    buttonLayout->addWidget(m_closeButton);
    
    mainLayout->addLayout(buttonLayout);
}

void MainWindow::setupConnections()
{
    connect(m_rollbackButton, SIGNAL(clicked()), this, SLOT(onRollbackClicked()));
    connect(m_closeButton, SIGNAL(clicked()), this, SLOT(onCloseButtonClicked()));
    
    // BY ZF: 初始化倒计时定时器
    m_countdownTimer = new QTimer(this);
    connect(m_countdownTimer, SIGNAL(timeout()), this, SLOT(onCountdownTimer()));
}

void MainWindow::setPackagePath(const QString &packagePath)
{
    m_packagePath = packagePath;
    m_isRollbackMode = false;
    
    // 创建更新引擎
    if (m_updateEngine) {
        delete m_updateEngine;
    }
    
    m_updateEngine = new UpdateEngine(this);
    
    // 连接信号槽
    connect(m_updateEngine, SIGNAL(progress(int, QString)), 
            this, SLOT(onUpdateProgress(int, QString)));
    connect(m_updateEngine, SIGNAL(finished(bool, QString)), 
            this, SLOT(onUpdateFinished(bool, QString)));
    connect(m_updateEngine, SIGNAL(error(QString)), 
            this, SLOT(onUpdateError(QString)));
    connect(m_updateEngine, SIGNAL(logMessage(QString)), 
            this, SLOT(onLogMessage(QString)));
    connect(m_updateEngine, SIGNAL(started()), 
            this, SLOT(onUpdateStarted()));
    
    // 开始更新
    m_updateEngine->startUpdate(m_packagePath);
}

void MainWindow::setRollbackMode()
{
    m_isRollbackMode = true;
    m_statusLabel->setText("准备执行回滚操作...");
    
    // 创建更新引擎
    if (m_updateEngine) {
        delete m_updateEngine;
    }
    
    m_updateEngine = new UpdateEngine(this);
    
    // 连接信号槽
    connect(m_updateEngine, SIGNAL(progress(int, QString)), 
            this, SLOT(onUpdateProgress(int, QString)));
    connect(m_updateEngine, SIGNAL(finished(bool, QString)), 
            this, SLOT(onUpdateFinished(bool, QString)));
    connect(m_updateEngine, SIGNAL(error(QString)), 
            this, SLOT(onUpdateError(QString)));
    connect(m_updateEngine, SIGNAL(logMessage(QString)), 
            this, SLOT(onLogMessage(QString)));
    connect(m_updateEngine, SIGNAL(started()), 
            this, SLOT(onUpdateStarted()));
    
    // 开始回滚
    m_updateEngine->startRollback();
}

void MainWindow::onUpdateProgress(int percentage, const QString &status)
{
    m_progressBar->setValue(percentage);
    m_statusLabel->setText(status);
}

void MainWindow::onUpdateFinished(bool success, const QString &message)
{
    m_progressBar->setValue(100);
    
    if (success) {
        m_statusLabel->setText("操作完成: " + message);
        m_statusLabel->setStyleSheet("font-size: 12px; font-weight: bold; padding: 5px; color: green;");
        
        // BY ZF: 升级成功，启动9秒倒计时
        m_countdownSeconds = 9;
        m_closeButton->setEnabled(true);
        m_closeButton->setText(QString("关闭 (%1s)").arg(m_countdownSeconds));
        m_countdownTimer->start(1000);  // 每秒触发一次
    } else {
        m_statusLabel->setText("操作失败: " + message);
        m_statusLabel->setStyleSheet("font-size: 12px; font-weight: bold; padding: 5px; color: red;");
        
        // 如果更新失败，启用回滚按钮
        if (!m_isRollbackMode) {
            m_rollbackButton->setEnabled(true);
        }
        
        // BY ZF: 失败时不启动倒计时，直接显示关闭按钮
        m_closeButton->setEnabled(true);
        m_closeButton->setText("关闭");
    }
}

void MainWindow::onUpdateError(const QString &error)
{
    m_statusLabel->setText("错误: " + error);
    m_statusLabel->setStyleSheet("font-size: 12px; font-weight: bold; padding: 5px; color: red;");
    
    // 如果更新失败，启用回滚按钮
    if (!m_isRollbackMode) {
        m_rollbackButton->setEnabled(true);
    }
}

void MainWindow::onLogMessage(const QString &message)
{
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString logLine = QString("[%1] %2").arg(timestamp).arg(message);
    
    // 确保文本以UTF-8编码显示
    // QTextEdit在Qt 4.8.6中应该自动处理UTF-8，但为了保险起见，我们确保编码正确
    m_logTextEdit->append(logLine);
    
    // 自动滚动到底部
    QTextCursor cursor = m_logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logTextEdit->setTextCursor(cursor);
}

void MainWindow::onRollbackClicked()
{
    int ret = QMessageBox::question(this, "确认回滚", 
        "确定要执行回滚操作吗？\n这将恢复到更新前的状态。",
        QMessageBox::Yes | QMessageBox::No,
        QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        m_rollbackButton->setEnabled(false);
        m_closeButton->setEnabled(false);
        m_progressBar->setValue(0);
        m_logTextEdit->clear();
        
        setRollbackMode();
    }
}

void MainWindow::onUpdateStarted()
{
    m_rollbackButton->setEnabled(false);
    m_closeButton->setEnabled(false);
    m_closeButton->setText("关闭");  // BY ZF: 重置按钮文本
    m_progressBar->setValue(0);
    m_logTextEdit->clear();
    
    // BY ZF: 停止倒计时定时器（如果正在运行）
    if (m_countdownTimer && m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
    }
}

void MainWindow::onCountdownTimer()
{
    m_countdownSeconds--;
    
    if (m_countdownSeconds > 0) {
        // BY ZF: 更新按钮文本显示倒计时
        m_closeButton->setText(QString("关闭 (%1s)").arg(m_countdownSeconds));
    } else {
        // BY ZF: 倒计时结束，停止定时器并自动关闭窗口
        m_countdownTimer->stop();
        m_closeButton->setText("关闭");
        close();
    }
}

void MainWindow::onCloseButtonClicked()
{
    // BY ZF: 如果倒计时正在运行，停止它
    if (m_countdownTimer && m_countdownTimer->isActive()) {
        m_countdownTimer->stop();
        m_closeButton->setText("关闭");
    }
    
    // 关闭窗口
    close();
}

