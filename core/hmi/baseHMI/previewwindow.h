#ifndef TCU_HMI_PREVIEWWINDOW_H
#define TCU_HMI_PREVIEWWINDOW_H

#include <QMainWindow>
#include <QMap>

class QPushButton;
class QLabel;
class QStackedWidget;
class QWidget;

class PreviewWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PreviewWindow(QWidget *parent = 0);

private slots:
    void showPage(int index);
    void showPrevPage();
    void showNextPage();

private:
    QWidget *loadPage(const QString &path);
    void applyDemoData(QWidget *page, const QString &screenTitle);
    void updateHeader();

    QStackedWidget *m_stack;
    QLabel *m_title;
    QLabel *m_desc;
    QPushButton *m_prevButton;
    QPushButton *m_nextButton;
    int m_currentIndex;
    QMap<QString, QWidget *> m_pages;
};

#endif
