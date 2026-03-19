#include "customwidgets.h"

#include <QDateTime>
#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QByteArray>

extern "C" {
#include "qrencode.h"
}

namespace {

static QPixmap buildCellTextPixmap(const QSize &size, const QString &text, int pointSize)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRect box = pixmap.rect().adjusted(4, 4, -4, -4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 110));
    painter.drawRoundedRect(box, 10, 10);

    QFont font("MS Shell Dlg 2", pointSize, QFont::Bold);
    painter.setFont(font);
    painter.setPen(Qt::white);

    QStringList lines = text.split("\n");
    QFontMetrics fm(font);
    const int baseLineHeight = fm.height();
    const int lineHeight = (baseLineHeight * 3) / 2;
    const int textHeight = lineHeight * lines.size();
    int y = box.y() + (box.height() - textHeight) / 2 + fm.ascent();

    for (int i = 0; i < lines.size(); ++i) {
        painter.drawText(QRect(box.x() + 12, y - fm.ascent(), box.width() - 24, baseLineHeight),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         lines.at(i));
        y += lineHeight;
    }

    return pixmap;
}

static QPixmap buildStoppedTextPixmap(const QSize &size, const QString &text)
{
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRect box = pixmap.rect().adjusted(4, 4, -4, -4);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 110));
    painter.drawRoundedRect(box, 10, 10);

    QStringList lines = text.split("\n");
    if (lines.isEmpty()) {
        return pixmap;
    }

    QFont titleFont("MS Shell Dlg 2", 22, QFont::Bold);
    QFont bodyFont("MS Shell Dlg 2", 16, QFont::Bold);
    QFontMetrics titleFm(titleFont);
    QFontMetrics bodyFm(bodyFont);
    const int titleHeight = titleFm.height();
    const int bodyLineHeight = (bodyFm.height() * 3) / 2;
    const int bodyCount = qMax(0, lines.size() - 1);
    const int textHeight = titleHeight + (bodyCount > 0 ? 8 : 0) + bodyLineHeight * bodyCount;
    int y = box.y() + (box.height() - textHeight) / 2;

    painter.setPen(Qt::white);
    painter.setFont(titleFont);
    painter.drawText(QRect(box.x() + 12, y, box.width() - 24, titleHeight),
                     Qt::AlignHCenter | Qt::AlignVCenter,
                     lines.at(0));
    y += titleHeight + 8;

    painter.setFont(bodyFont);
    for (int i = 1; i < lines.size(); ++i) {
        painter.drawText(QRect(box.x() + 12, y, box.width() - 24, bodyFm.height()),
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         lines.at(i));
        y += bodyLineHeight;
    }

    return pixmap;
}

} // namespace

CClickLabel::CClickLabel(QWidget *parent) : QLabel(parent)
{
}

void CClickLabel::mouseReleaseEvent(QMouseEvent *event)
{
    emit click();
    QLabel::mouseReleaseEvent(event);
}

BaseDialog::BaseDialog(QWidget *parent) : QDialog(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog);
    resize(800, 480);
    connect(&m_timer, SIGNAL(timeout()), this, SLOT(refreshHeader()));
    m_timer.start(1000);
}

void BaseDialog::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);
    refreshHeader();
}

void BaseDialog::refreshHeader()
{
    QLabel *datetime = findChild<QLabel *>("lblDatetime");
    if (datetime) {
        datetime->setText(QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    }

    QLabel *host = findChild<QLabel *>("label_host");
    if (host && host->pixmap() == 0) {
        host->setPixmap(QPixmap(":/cui/Resources/host_0.png"));
    }

    QLabel *bottom = findChild<QLabel *>("base_bottom");
    if (bottom && bottom->pixmap() == 0) {
        bottom->setPixmap(QPixmap(":/cui/Resources/base_bottom.png").scaled(bottom->size(),
                                                                            Qt::IgnoreAspectRatio,
                                                                            Qt::SmoothTransformation));
    }
}

WNumEdit::WNumEdit(QWidget *parent) : QLineEdit(parent), m_type(PASSWORD)
{
}

void WNumEdit::setEdtType(EDITTYPE edttype)
{
    m_type = edttype;
}

WNumEdit::EDITTYPE WNumEdit::edttype() const
{
    return m_type;
}

void WNumEdit::mousePressEvent(QMouseEvent *event)
{
    QLineEdit::mousePressEvent(event);
    selectAll();
}

QRWidget::QRWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(120, 120);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAutoFillBackground(false);
}

void QRWidget::setQRData(const QString &data)
{
    m_data = data;
    update();
}

void QRWidget::setQRData1(const QString &data1)
{
    m_data1 = data1;
    update();
}

void QRWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), Qt::transparent);
    if (m_data.isEmpty()) {
        return;
    }

    const QByteArray encoded = m_data.toUtf8();
    QRcode *code = QRcode_encodeString8bit(encoded.constData(), 0, QR_ECLEVEL_M);
    if (!code || !code->data || code->width <= 0) {
        if (code) {
            QRcode_free(code);
        }
        return;
    }

    const int quietZone = 4;
    const int modules = code->width + quietZone * 2;
    const QRect area = rect().adjusted(8, 8, -8, -8);
    const int cell = qMax(1, qMin(area.width(), area.height()) / modules);
    const int qrSize = cell * modules;
    const int startX = area.x() + (area.width() - qrSize) / 2;
    const int startY = area.y() + (area.height() - qrSize) / 2;
    const QColor qrColor(0, 0, 0, 235);

    unsigned char *data = code->data;
    for (int row = 0; row < code->width; ++row) {
        for (int col = 0; col < code->width; ++col) {
            if (data[row * code->width + col] & 0x01) {
                painter.fillRect(startX + (col + quietZone) * cell,
                                 startY + (row + quietZone) * cell,
                                 cell,
                                 cell,
                                 qrColor);
            }
        }
    }

    QRcode_free(code);
}

CCellWidget::CCellWidget(QWidget *parent) :
    QWidget(parent),
    m_port(0),
    m_frame(new QFrame(this)),
    m_lblKwh(new CClickLabel(m_frame)),
    m_lblMoney(new CClickLabel(m_frame)),
    m_lblTime(new CClickLabel(m_frame)),
    m_lblVA(new CClickLabel(m_frame)),
    m_lblSoc(new CClickLabel(m_frame)),
    m_lblHint(new CClickLabel(this)),
    m_progress(new QProgressBar(m_frame))
{
    setMinimumSize(360, 260);

    m_frame->setGeometry(rect());
    m_frame->setStyleSheet("QFrame{background:rgba(10,32,48,150);border:1px solid rgba(120,200,255,120);"
                           "border-radius:14px;}");

    const QFont infoFont("MS Shell Dlg 2", 18);
    m_lblKwh->setGeometry(76, 28, 220, 26);
    m_lblMoney->setGeometry(76, 60, 220, 26);
    m_lblTime->setGeometry(76, 92, 220, 26);
    m_lblVA->setGeometry(76, 124, 220, 26);
    m_lblSoc->setGeometry(76, 156, 220, 26);

    m_lblKwh->setFont(infoFont);
    m_lblMoney->setFont(infoFont);
    m_lblTime->setFont(infoFont);
    m_lblVA->setFont(infoFont);
    m_lblSoc->setFont(infoFont);
    m_lblKwh->setWordWrap(true);
    m_lblMoney->setWordWrap(true);
    m_lblTime->setWordWrap(true);
    m_lblVA->setWordWrap(true);
    m_lblSoc->setWordWrap(true);
    m_lblKwh->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblMoney->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblTime->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblVA->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_lblSoc->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    // m_lblKwh->setStyleSheet("color:white;background:transparent;");
    // m_lblMoney->setStyleSheet("color:white;background:transparent;");
    // m_lblTime->setStyleSheet("color:white;background:transparent;");
    // m_lblVA->setStyleSheet("color:white;background:transparent;");
    // m_lblSoc->setStyleSheet("color:white;background:transparent;");

    // BY ZF: lbl 背景设为透明，避免出现不透明叠加效果
    m_lblKwh->setStyleSheet("color:white;background:transparent;");
    m_lblMoney->setStyleSheet("color:white;background:transparent;");
    m_lblTime->setStyleSheet("color:white;background:transparent;");
    m_lblVA->setStyleSheet("color:white;background:transparent;");
    m_lblSoc->setStyleSheet("color:white;background:transparent;");



    m_progress->setGeometry(28, 196, 260, 18);
    m_progress->setRange(0, 100);
    m_progress->setValue(78);
    m_progress->hide();

    m_lblHint->setGeometry(64, 58, 250, 180);
    m_lblHint->setWordWrap(true);
    m_lblHint->setAlignment(Qt::AlignCenter);
    m_lblHint->setStyleSheet("color:white;background:transparent;font:28px 'MS Shell Dlg 2';font-weight:700;");

    connect(m_lblKwh, SIGNAL(click()), this, SLOT(emitClick()));
    connect(m_lblMoney, SIGNAL(click()), this, SLOT(emitClick()));
    connect(m_lblTime, SIGNAL(click()), this, SLOT(emitClick()));
    connect(m_lblVA, SIGNAL(click()), this, SLOT(emitClick()));
    connect(m_lblSoc, SIGNAL(click()), this, SLOT(emitClick()));
    connect(m_lblHint, SIGNAL(click()), this, SLOT(emitClick()));

    setChargeState(123.456f, 56.78f, 83, 750.0f, 126.0f, 78);
}

void CCellWidget::setPort(int port)
{
    m_port = port;
}

void CCellWidget::setTips(const QString &msg)
{
    m_frame->hide();
    m_lblHint->show();
    m_lblHint->setText(QString());
    if (msg.startsWith(QString::fromUtf8("充电已停止"))) {
        m_lblHint->setPixmap(buildStoppedTextPixmap(m_lblHint->size(), msg));
        return;
    }
    const int lineCount = msg.split("\n").size();
    const int pointSize = lineCount >= 5 ? 16 : (lineCount >= 3 ? 20 : 28);
    m_lblHint->setPixmap(buildCellTextPixmap(m_lblHint->size(), msg, pointSize));
}

void CCellWidget::setChargeState(float kwh, float money, int minute, float volt, float current, int soc)
{
    m_lblKwh->setText(QString());
    m_lblMoney->setText(QString());
    m_lblTime->setText(QString());
    m_lblVA->setText(QString());
    m_lblSoc->setText(QString());
    m_lblKwh->setPixmap(buildCellTextPixmap(m_lblKwh->size(), QString::fromUtf8("充电电量: %1kWh").arg(kwh, 0, 'f', 3), 14));
    m_lblMoney->setPixmap(buildCellTextPixmap(m_lblMoney->size(), QString::fromUtf8("充电金额: %1元").arg(money, 0, 'f', 2), 14));
    m_lblTime->setPixmap(buildCellTextPixmap(m_lblTime->size(), QString::fromUtf8("充电时长: %1小时%2分钟").arg(minute / 60).arg(minute % 60), 14));
    m_lblVA->setPixmap(buildCellTextPixmap(m_lblVA->size(), QString::fromUtf8("充电信息: %1V %2A").arg(volt, 0, 'f', 0).arg(current, 0, 'f', 0), 14));
    m_lblSoc->setPixmap(buildCellTextPixmap(m_lblSoc->size(), QString::fromUtf8("SOC: %1%").arg(soc >= 0 ? soc : 0), 14));
    m_progress->hide();
    m_frame->show();
    m_lblHint->hide();
}

void CCellWidget::emitClick()
{
    emit cellclick(m_port);
}

void CCellWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    m_frame->setGeometry(rect());
}
