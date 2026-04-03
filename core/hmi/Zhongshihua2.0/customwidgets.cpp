#include "customwidgets.h"

#include <QDateTime>
#include <QPaintEvent>
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

static int hhmmToMinutesLocal(const QString &hhmm)
{
    if (hhmm.size() < 4) {
        return -1;
    }
    bool okHour = false;
    bool okMinute = false;
    const int hour = hhmm.mid(0, 2).toInt(&okHour);
    const int minute = hhmm.mid(2, 2).toInt(&okMinute);
    if (!okHour || !okMinute) {
        return -1;
    }
    return hour * 60 + minute;
}

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

FeeModelChartWidget::FeeModelChartWidget(QWidget *parent)
    : QWidget(parent)
    , m_currentIndex(-1)
{
    // BY ZF: 分时信息页改为自绘柱状图，后续调整样式只需要改这一个控件。
    setMinimumSize(640, 220);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("background:transparent;");
}

void FeeModelChartWidget::setBars(const QVector<FeeBarData> &bars, int currentIndex)
{
    m_bars = bars;
    m_currentIndex = currentIndex;
    update();
}

void FeeModelChartWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(rect(), Qt::transparent);

    QRect outerRect = rect().adjusted(6, 8, -6, -6);
    painter.setPen(QPen(QColor(150, 150, 150), 1));
    painter.setBrush(QColor(244, 246, 247));
    painter.drawRoundedRect(outerRect, 12, 12);

    QRect chartRect = rect().adjusted(56, 42, -28, -34);
    if (chartRect.width() <= 0 || chartRect.height() <= 0) {
        return;
    }

    painter.setPen(QPen(QColor(90, 90, 90), 1));
    painter.drawLine(chartRect.bottomLeft(), chartRect.topLeft());
    painter.drawLine(chartRect.bottomLeft(), chartRect.bottomRight());

    if (m_bars.isEmpty()) {
        painter.setPen(QColor(38, 45, 52));
        painter.setFont(QFont("MS Shell Dlg 2", 16, QFont::Bold));
        painter.drawText(chartRect, Qt::AlignCenter, QString::fromUtf8("暂无计费模型数据"));
        return;
    }

    double maxValue = 0.0;
    int i = 0;
    for (i = 0; i < m_bars.size(); ++i) {
        const double total = m_bars.at(i).chargeFee + m_bars.at(i).serviceFee;
        if (total > maxValue) {
            maxValue = total;
        }
    }
    if (maxValue < 0.1) {
        maxValue = 0.1;
    }

    QFont axisFont("MS Shell Dlg 2", 12);
    QFont labelFont("MS Shell Dlg 2", 13, QFont::Bold);
    QFont totalFont("MS Shell Dlg 2", 12, QFont::Bold);

    const double pxPerMinute = chartRect.width() / 1440.0;
    const int count = m_bars.size();
    QVector<QRect> barRects;
    QVector<double> barTotals;
    barRects.reserve(count);
    barTotals.reserve(count);
    for (i = 0; i < count; ++i) {
        const FeeBarData &bar = m_bars.at(i);
        QString startText = bar.startTime;
        QString endText = bar.endTime;
        startText.remove(":");
        endText.remove(":");
        const int startMinute = hhmmToMinutesLocal(startText);
        int endMinute = hhmmToMinutesLocal(endText);
        if (endMinute <= startMinute) {
            endMinute = 1440;
        }

        int x = chartRect.x() + static_cast<int>(startMinute * pxPerMinute + 0.5);
        int nextX = chartRect.x() + static_cast<int>(endMinute * pxPerMinute + 0.5);
        int barWidth = qMax(1, nextX - x);
        const double chargeHeightRatio = bar.chargeFee / maxValue;
        const double totalHeightRatio = (bar.chargeFee + bar.serviceFee) / maxValue;
        const int totalHeight = qMax(1, static_cast<int>(chartRect.height() * totalHeightRatio));
        const int chargeHeight = qMax(1, static_cast<int>(chartRect.height() * chargeHeightRatio));
        const int y = chartRect.bottom() - totalHeight;

        QColor chargeColor(0, 176, 102);
        QColor serviceColor(18, 108, 245);
        if (i == m_currentIndex) {
            chargeColor = QColor(0, 148, 86);
            serviceColor = QColor(0, 82, 214);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 210, 80, 45));
            painter.drawRect(QRect(x, chartRect.y(), barWidth, chartRect.height()));
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(chargeColor);
        painter.drawRect(QRect(x, chartRect.bottom() - chargeHeight, barWidth, chargeHeight));

        const int serviceHeight = qMax(1, totalHeight - chargeHeight);
        painter.setBrush(serviceColor);
        painter.drawRect(QRect(x, y, barWidth, serviceHeight));
        barRects.append(QRect(x, y, barWidth, totalHeight));
        barTotals.append(bar.chargeFee + bar.serviceFee);
    }

    painter.setPen(QColor(38, 45, 52));
    painter.setFont(totalFont);
    int groupStart = 0;
    while (groupStart < count) {
        int groupEnd = groupStart;
        const double groupTotal = barTotals.at(groupStart);
        while (groupEnd + 1 < count &&
               qAbs(barTotals.at(groupEnd + 1) - groupTotal) < 0.0001) {
            ++groupEnd;
        }

        const QRect firstRect = barRects.at(groupStart);
        const QRect lastRect = barRects.at(groupEnd);
        const int textLeft = firstRect.left();
        const int textWidth = lastRect.right() - firstRect.left() + 1;
        const int textTop = qMin(firstRect.top(), lastRect.top()) - 18;
        painter.drawText(QRect(textLeft, textTop, textWidth, 16),
                         Qt::AlignCenter,
                         QString::number(groupTotal, 'f', 2));
        groupStart = groupEnd + 1;
    }

    painter.setPen(QColor(38, 45, 52));
    painter.setFont(labelFont);
    painter.drawText(QRect(2, chartRect.y() - 2, 42, 20), Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(maxValue, 'f', 2));
    painter.drawText(QRect(2, chartRect.bottom() - 10, 42, 20), Qt::AlignRight | Qt::AlignVCenter,
                     QString::number(0.0, 'f', 2));
    painter.setFont(QFont("MS Shell Dlg 2", 12, QFont::Bold));
    painter.drawText(QRect(8, chartRect.y() - 20, 60, 16),
                     Qt::AlignLeft | Qt::AlignVCenter,
                     QString::fromUtf8("价格(元)"));

    painter.setFont(axisFont);
    const int hourWidth = chartRect.width() / 24;
    for (i = 0; i <= 24; ++i) {
        const int tickX = (i == 24) ? chartRect.right() : (chartRect.x() + i * hourWidth);
        painter.setPen(QPen(QColor(90, 90, 90), 1));
        painter.drawLine(tickX, chartRect.bottom(), tickX, chartRect.bottom() + 4);
        if (i <= 24) {
            painter.setPen(QColor(38, 45, 52));
            int textX = tickX - 12;
            if (i == 0) {
                textX = tickX;
            } else if (i == 24) {
                textX = tickX - 24;
            }
            painter.drawText(QRect(textX, chartRect.bottom() + 8, 24, 14),
                             (i == 0) ? (Qt::AlignLeft | Qt::AlignTop) :
                             (i == 24) ? (Qt::AlignRight | Qt::AlignTop) :
                             (Qt::AlignHCenter | Qt::AlignTop),
                             QString("%1").arg(i, 2, 10, QChar('0')));
        }
    }
    painter.setPen(QColor(38, 45, 52));
    painter.setFont(QFont("MS Shell Dlg 2", 12, QFont::Bold));
    painter.drawText(QRect(chartRect.right() - 54, chartRect.bottom() + 22, 54, 14),
                     Qt::AlignRight | Qt::AlignVCenter,
                     QString::fromUtf8("时刻(h)"));
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
