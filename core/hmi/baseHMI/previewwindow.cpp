#include "previewwindow.h"

#include <QBrush>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
#include <QTextBrowser>
#include <QToolButton>

#include "customwidgets.h"
#include "uipages.h"

namespace {

struct ScreenDef {
    const char *title;
    const char *path;
    const char *desc;
};

const ScreenDef kScreens[] = {
    {"B1 Idle", ":/ui/b1idle.ui", "待机主界面"},
    {"C6 Authorize", ":/ui/c6flushcad.ui", "鉴权/启动方式选择"},
    {"E1 Charging", ":/ui/e1chargeinfo.ui", "充电中界面"},
    {"F7 Checkout Ok", ":/ui/f7checkoutok.ui", "结算完成"},
    {"A3 About", ":/ui/a3about.ui", "关于/配置页"},
    {"NumInput", ":/ui/numinputfdlg.ui", "A3About 依赖的数字输入键盘"},
    {"NumInputDlg", ":/ui/numinputdlg.ui", "A3About 依赖的简版输入对话框"}
};

QString sampleTextFor(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains("datetime")) return "2026-03-17 14:26:00";
    if (lower.contains("kwh")) return QString::fromUtf8("123.456 kWh");
    if (lower.contains("money")) return QString::fromUtf8("56.78 元");
    if (lower.contains("time")) return QString::fromUtf8("01小时 23分钟");
    if (lower.contains("soc")) return "78 %";
    if (lower.contains("vol") || lower == "lblu" || lower.contains("label_2")) return "750.0 V";
    if (lower.contains("cur") || lower == "lbli" || lower.contains("label_3")) return "126.0 A";
    if (lower.contains("tips")) return QString::fromUtf8("UI 预览模式，已剥离原始数据接口");
    if (lower.contains("title")) return QString::fromUtf8("TCU HMI UI Preview");
    if (lower.contains("pole")) return QString::fromUtf8("A枪");
    if (lower.contains("id")) return "TCU-HMI-DEMO-0001";
    if (lower.contains("reason")) return QString::fromUtf8("停机原因");
    if (lower.contains("msg")) return QString::fromUtf8("仅保留原始界面表现");
    return QString();
}

void fillLineEdit(QLineEdit *edit)
{
    const QString name = edit->objectName().toLower();
    if (name.contains("ip")) {
        edit->setText("192.168.1.88");
    } else if (name.contains("code") || name.contains("sec")) {
        edit->setText("1234");
    } else {
        edit->setText("20260317");
    }
}

} // namespace

PreviewWindow::PreviewWindow(QWidget *parent) :
    QMainWindow(parent),
    m_stack(new QStackedWidget(this)),
    m_title(new QLabel(this)),
    m_desc(new QLabel(this)),
    m_prevButton(new QPushButton(QString::fromUtf8("上一页"), this)),
    m_nextButton(new QPushButton(QString::fromUtf8("下一页"), this)),
    m_currentIndex(0)
{
    setWindowTitle(QString::fromUtf8("tcu_hmi UI 提取版"));
    setFixedSize(800, 480);
    setWindowFlags(Qt::FramelessWindowHint);

    QWidget *central = new QWidget(this);
    central->setStyleSheet("QWidget{background:#08131d;color:white;}");
    central->setFixedSize(800, 480);
    setCentralWidget(central);

    m_stack->setParent(central);
    m_stack->setGeometry(0, 0, 800, 480);

    m_title->setParent(central);
    m_title->setGeometry(12, 8, 460, 22);
    m_title->setStyleSheet("QLabel{background:rgba(8,19,29,150);color:#ffffff;font:16px 'WenQuanYi Zen Hei';font-weight:bold;padding-left:6px;}");

    m_desc->setParent(central);
    m_desc->setGeometry(12, 30, 560, 18);
    m_desc->setStyleSheet("QLabel{background:rgba(8,19,29,130);color:#9fc3df;font:11px 'WenQuanYi Zen Hei';padding-left:6px;}");

    m_prevButton->setParent(central);
    m_nextButton->setParent(central);
    m_prevButton->setGeometry(12, 432, 92, 36);
    m_nextButton->setGeometry(696, 432, 92, 36);
    m_prevButton->setStyleSheet("QPushButton{background:rgba(31,79,112,200);border:1px solid #3f84b0;border-radius:6px;color:white;}");
    m_nextButton->setStyleSheet("QPushButton{background:rgba(31,79,112,200);border:1px solid #3f84b0;border-radius:6px;color:white;}");
    m_title->raise();
    m_desc->raise();
    m_prevButton->raise();
    m_nextButton->raise();

    for (int i = 0; i < int(sizeof(kScreens) / sizeof(kScreens[0])); ++i) {
        QWidget *page = loadPage(QString::fromLatin1(kScreens[i].path));
        m_pages.insert(QString::fromLatin1(kScreens[i].path), page);
        m_stack->addWidget(page);
    }

    connect(m_prevButton, SIGNAL(clicked()), this, SLOT(showPrevPage()));
    connect(m_nextButton, SIGNAL(clicked()), this, SLOT(showNextPage()));
    showPage(0);
}

void PreviewWindow::showPage(int index)
{
    const int count = int(sizeof(kScreens) / sizeof(kScreens[0]));
    if (index < 0 || index >= count) {
        return;
    }

    m_currentIndex = index;
    const QString path = QString::fromLatin1(kScreens[index].path);
    QWidget *page = m_pages.value(path, 0);
    m_stack->setCurrentWidget(page);
    updateHeader();
}

void PreviewWindow::showPrevPage()
{
    const int count = int(sizeof(kScreens) / sizeof(kScreens[0]));
    showPage((m_currentIndex + count - 1) % count);
}

void PreviewWindow::showNextPage()
{
    const int count = int(sizeof(kScreens) / sizeof(kScreens[0]));
    showPage((m_currentIndex + 1) % count);
}

void PreviewWindow::updateHeader()
{
    const int count = int(sizeof(kScreens) / sizeof(kScreens[0]));
    m_title->setText(QString::fromUtf8("%1 / %2  %3")
                     .arg(m_currentIndex + 1)
                     .arg(count)
                     .arg(QString::fromLatin1(kScreens[m_currentIndex].title)));
    m_desc->setText(QString::fromUtf8("%1").arg(QString::fromUtf8(kScreens[m_currentIndex].desc)));
}

QWidget *PreviewWindow::loadPage(const QString &path)
{
    QWidget *page = createPreviewPage(path, this);
    if (!page) {
        QLabel *error = new QLabel(QString::fromUtf8("无法加载 %1").arg(path));
        error->setStyleSheet("QLabel{color:white;}");
        return error;
    }

    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setFixedSize(800, 480);
    page->setAutoFillBackground(true);
    QPalette palette = page->palette();
    palette.setBrush(QPalette::Window, QBrush(QPixmap(":/cui/Resources/Main.png")));
    page->setPalette(palette);
    applyDemoData(page, QFileInfo(path).baseName());
    return page;
}

void PreviewWindow::applyDemoData(QWidget *page, const QString &screenTitle)
{
    const QList<QLabel *> labels = page->findChildren<QLabel *>();
    for (int i = 0; i < labels.size(); ++i) {
        QLabel *label = labels.at(i);
        if (label->pixmap() && !label->pixmap()->isNull()) {
            continue;
        }
        if (!label->text().trimmed().isEmpty()) {
            continue;
        }
        const QString text = sampleTextFor(label->objectName());
        if (!text.isEmpty()) {
            label->setText(text);
        }
    }

    const QList<QLineEdit *> edits = page->findChildren<QLineEdit *>();
    for (int i = 0; i < edits.size(); ++i) {
        fillLineEdit(edits.at(i));
    }

    const QList<QRWidget *> qrWidgets = page->findChildren<QRWidget *>();
    for (int i = 0; i < qrWidgets.size(); ++i) {
        qrWidgets.at(i)->setQRData(QString("TCU-HMI-%1-%2").arg(screenTitle).arg(i + 1));
    }

    const QList<CCellWidget *> cells = page->findChildren<CCellWidget *>();
    for (int i = 0; i < cells.size(); ++i) {
        cells.at(i)->setPort(i);
        if (i == 0) {
            cells.at(i)->setChargeState(123.456f, 56.78f, 83, 750.0f, 126.0f, 78);
        } else {
            cells.at(i)->setTips(QString::fromUtf8("枪口空闲\n请连接充电枪"));
        }
    }

    const QList<QTextBrowser *> browsers = page->findChildren<QTextBrowser *>();
    for (int i = 0; i < browsers.size(); ++i) {
        browsers.at(i)->setText(QString::fromUtf8("这是提取后的纯 UI 预览页。\n\n原始项目中的通信、线程、数据库、设备接口均已剥离。"));
    }

    if (screenTitle == "b1idle") {
        QLabel *leftBg = page->findChild<QLabel *>("label_zq");
        QLabel *rightBg = page->findChild<QLabel *>("label_yq");
        QLabel *leftState = page->findChild<QLabel *>("label_gth");
        QLabel *rightState = page->findChild<QLabel *>("label_gth_3");
        QLabel *leftPanel = page->findChild<QLabel *>("label_tk_zq");
        QLabel *rightPanel = page->findChild<QLabel *>("label_tk_zq_2");
        if (leftBg) {
            leftBg->setPixmap(QPixmap(":/cui/Resources/qt_zq.png"));
            leftBg->setScaledContents(true);
        }
        if (rightBg) {
            rightBg->setPixmap(QPixmap(":/cui/Resources/qt_yq.png"));
            rightBg->setScaledContents(true);
        }
        if (leftState) {
            leftState->hide();
        }
        if (rightState) {
            rightState->setPixmap(QPixmap(":/cui/Resources/gth.png"));
            rightState->setScaledContents(true);
            rightState->show();
        }
        if (leftPanel) {
            leftPanel->setStyleSheet("background:transparent;border:none;");
            leftPanel->hide();
        }
        if (rightPanel) {
            rightPanel->setStyleSheet("background:transparent;border:none;");
            rightPanel->show();
        }
    }

    if (screenTitle == "c6flushcad") {
        QLabel *ewmBg = page->findChild<QLabel *>("label_ewm");
        QLabel *qcq = page->findChild<QLabel *>("qcq");
        if (ewmBg) {
            ewmBg->setPixmap(QPixmap(":/cui/Resources/ewm.png"));
            ewmBg->setScaledContents(true);
        }
        if (qcq) {
            qcq->setPixmap(QPixmap(":/cui/Resources/qcq.png"));
            qcq->setScaledContents(true);
        }
    }

    QLabel *titleLabel = page->findChild<QLabel *>("lblTitle");
    if (titleLabel && titleLabel->text().trimmed().isEmpty()) {
        titleLabel->setText(screenTitle);
    }
}
