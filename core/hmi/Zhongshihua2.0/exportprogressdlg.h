#ifndef TCU_HMI_EXPORT_PROGRESS_DLG_H
#define TCU_HMI_EXPORT_PROGRESS_DLG_H

#include <QDialog>

class QLabel;
class QPushButton;
class QProgressBar;
class QTimer;

class ExportProgressDlg : public QDialog
{
    Q_OBJECT

public:
    explicit ExportProgressDlg(QWidget *parent = 0);
    ~ExportProgressDlg();

    void setExportInfo(qint64 pid, const QString &filePath, const QString &path, qint64 sourceSize = 0);

private slots:
    void onCheckProgress();
    void onBtnStopClicked();

private:
    QProgressBar *m_progressBar;
    QLabel *m_labelStatus;
    QLabel *m_labelFileInfo;
    QPushButton *m_btnStop;
    QTimer *m_checkTimer;

    qint64 m_exportPid;
    QString m_tarFilePath;
    QString m_targetPath;
    qint64 m_lastFileSize;
    qint64 m_sourceTotalSize;
    int m_checkCount;
    bool m_isCompleted;
    int m_processEndCheckCount;
};

#endif
