#include "exportprogressdlg.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>

ExportProgressDlg::ExportProgressDlg(QWidget *parent)
    : QDialog(parent)
    , m_progressBar(0)
    , m_labelStatus(0)
    , m_labelFileInfo(0)
    , m_btnStop(0)
    , m_checkTimer(0)
    , m_exportPid(0)
    , m_lastFileSize(0)
    , m_sourceTotalSize(0)
    , m_checkCount(0)
    , m_isCompleted(false)
    , m_processEndCheckCount(0)
{
    setWindowTitle(QString::fromUtf8("日志导出进度"));
    setModal(true);
    resize(600, 300);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setGeometry(50, 80, 500, 40);
    m_progressBar->setMinimum(0);
    m_progressBar->setMaximum(100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat("%p%");

    m_labelStatus = new QLabel(this);
    m_labelStatus->setGeometry(50, 140, 500, 40);
    m_labelStatus->setAlignment(Qt::AlignCenter);
    m_labelStatus->setText(QString::fromUtf8("正在导出日志，请稍候..."));
    QFont statusFont = m_labelStatus->font();
    statusFont.setPointSize(16);
    m_labelStatus->setFont(statusFont);

    m_labelFileInfo = new QLabel(this);
    m_labelFileInfo->setGeometry(50, 190, 500, 30);
    m_labelFileInfo->setAlignment(Qt::AlignCenter);
    QFont infoFont = m_labelFileInfo->font();
    infoFont.setPointSize(12);
    m_labelFileInfo->setFont(infoFont);

    m_btnStop = new QPushButton(QString::fromUtf8("停止导出"), this);
    m_btnStop->setGeometry(250, 240, 100, 40);
    QFont btnFont = m_btnStop->font();
    btnFont.setPointSize(16);
    m_btnStop->setFont(btnFont);
    connect(m_btnStop, SIGNAL(clicked()), this, SLOT(onBtnStopClicked()));

    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(1000);
    connect(m_checkTimer, SIGNAL(timeout()), this, SLOT(onCheckProgress()));
}

ExportProgressDlg::~ExportProgressDlg()
{
    if (m_checkTimer) {
        m_checkTimer->stop();
    }
}

void ExportProgressDlg::setExportInfo(qint64 pid, const QString &filePath, const QString &path, qint64 sourceSize)
{
    m_exportPid = pid;
    m_tarFilePath = filePath;
    m_targetPath = path;
    m_lastFileSize = 0;
    m_sourceTotalSize = sourceSize;
    m_checkCount = 0;
    m_isCompleted = false;
    m_processEndCheckCount = 0;

    const QFileInfo fileInfo(m_tarFilePath);
    m_labelFileInfo->setText(QString::fromUtf8("目标文件: %1").arg(fileInfo.fileName()));
    m_progressBar->setValue(0);
    if (m_sourceTotalSize > 0) {
        m_labelStatus->setText(QString::fromUtf8("正在导出日志，请稍候...\n源目录大小: %1 MB")
                               .arg(m_sourceTotalSize / 1024.0 / 1024.0, 0, 'f', 2));
    } else {
        m_labelStatus->setText(QString::fromUtf8("正在导出日志，请稍候..."));
    }
    m_btnStop->setText(QString::fromUtf8("停止导出"));
    m_btnStop->setEnabled(true);
    m_checkTimer->start();
}

void ExportProgressDlg::onCheckProgress()
{
    ++m_checkCount;

    bool processRunning = false;
    if (m_exportPid > 0) {
        QProcess checkProcess;
        checkProcess.start(QString::fromUtf8("sh"),
                           QStringList() << QString::fromUtf8("-c")
                                         << QString::fromUtf8("kill -0 %1 2>/dev/null").arg(m_exportPid));
        checkProcess.waitForFinished(100);
        processRunning = (checkProcess.exitCode() == 0);
    }

    QFileInfo fileInfo(m_tarFilePath);
    const bool fileExists = fileInfo.exists();
    const qint64 currentFileSize = fileExists ? fileInfo.size() : 0;

    if (fileExists) {
        if (currentFileSize > m_lastFileSize) {
            if (m_sourceTotalSize > 0) {
                qint64 estimatedFinalSize = m_sourceTotalSize / 9;
                if (estimatedFinalSize < m_sourceTotalSize / 20) {
                    estimatedFinalSize = m_sourceTotalSize / 20;
                }
                int progress = static_cast<int>((currentFileSize * 90) / estimatedFinalSize);
                if (progress > 90) {
                    progress = 90;
                }
                if (progress < m_progressBar->value()) {
                    progress = m_progressBar->value();
                }
                m_progressBar->setValue(progress);
                m_labelStatus->setText(QString::fromUtf8("正在导出... (已导出: %1 MB / 预计: %2 MB)")
                                       .arg(currentFileSize / 1024.0 / 1024.0, 0, 'f', 2)
                                       .arg(estimatedFinalSize / 1024.0 / 1024.0, 0, 'f', 2));
            } else {
                int progress = m_progressBar->value();
                if (progress < 90) {
                    progress += 2;
                    if (progress > 90) {
                        progress = 90;
                    }
                    m_progressBar->setValue(progress);
                }
                m_labelStatus->setText(QString::fromUtf8("正在导出... (已导出: %1 MB)")
                                       .arg(currentFileSize / 1024.0 / 1024.0, 0, 'f', 2));
            }
            m_lastFileSize = currentFileSize;
        } else if (processRunning) {
            int progress = m_progressBar->value();
            if (progress < 95) {
                ++progress;
                if (progress > 95) {
                    progress = 95;
                }
                m_progressBar->setValue(progress);
            }
            m_labelStatus->setText(QString::fromUtf8("正在压缩整理..."));
        }
    }

    if (!processRunning && fileExists && currentFileSize > 0) {
        if (m_processEndCheckCount == 0) {
            m_processEndCheckCount = 1;
            if (m_progressBar->value() < 90) {
                m_progressBar->setValue(90);
            }
        } else {
            ++m_processEndCheckCount;
            if (m_processEndCheckCount >= 4) {
                const QFileInfo finalFileInfo(m_tarFilePath);
                if (finalFileInfo.exists() && finalFileInfo.size() > 0) {
                    m_isCompleted = true;
                    m_checkTimer->stop();
                    m_progressBar->setValue(100);
                    m_labelStatus->setText(QString::fromUtf8("导出完成 | 文件大小: %1 MB | 保存位置: %2")
                                           .arg(finalFileInfo.size() / 1024.0 / 1024.0, 0, 'f', 2)
                                           .arg(m_targetPath));
                    m_btnStop->setText(QString::fromUtf8("关闭"));
                    m_btnStop->setEnabled(true);
                    return;
                }
            } else {
                int targetProgress = 90 + (m_processEndCheckCount * 3);
                if (targetProgress > 100) {
                    targetProgress = 100;
                }
                if (m_progressBar->value() < targetProgress) {
                    m_progressBar->setValue(targetProgress);
                }
            }
        }
    } else if (processRunning) {
        m_processEndCheckCount = 0;
    }

    if (!processRunning && !fileExists && m_checkCount > 5) {
        m_checkTimer->stop();
        m_progressBar->setValue(0);
        m_labelStatus->setText(QString::fromUtf8("导出失败，文件未生成"));
        m_btnStop->setText(QString::fromUtf8("关闭"));
        m_btnStop->setEnabled(true);
    }
}

void ExportProgressDlg::onBtnStopClicked()
{
    m_btnStop->setEnabled(false);
    if (m_checkTimer) {
        m_checkTimer->stop();
    }

    if (m_exportPid > 0) {
        QProcess killProcess;
        killProcess.start(QString::fromUtf8("sh"),
                          QStringList() << QString::fromUtf8("-c")
                                        << QString::fromUtf8("kill -9 %1 2>/dev/null").arg(m_exportPid));
        killProcess.waitForFinished(1000);
    }

    if (!m_isCompleted) {
        QFileInfo fileInfo(m_tarFilePath);
        if (fileInfo.exists()) {
            QFile::remove(m_tarFilePath);
        }
    }

    accept();
}
