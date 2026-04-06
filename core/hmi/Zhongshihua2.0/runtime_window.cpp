#include "runtime_window.h"

#include <QApplication>
#include <QAbstractButton>
#include <QBrush>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFile>
#include <QFrame>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPalette>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QStyledItemDelegate>
#include <QStackedWidget>
#include <QStyleOptionButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include "../../base/common/config_manager_lite.h"
#include "../../base/common/fault_reason_mapper.h"
#include "../../base/common/message_queue.h"
#include "../../base/cjson/include/cjson/cJSON.h"
#include "../../logger/sql/sqlite3.h"
#include "customwidgets.h"
#include "exportprogressdlg.h"
#include "uipages.h"

namespace {

class StorageCheckboxDelegate : public QStyledItemDelegate
{
public:
    explicit StorageCheckboxDelegate(QObject *parent = 0)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        QStyleOptionViewItem textOption(option);
        textOption.decorationSize = QSize(0, 0);
        textOption.rect.setRight(option.rect.right() - 56);
        QStyledItemDelegate::paint(painter, textOption, index);

        if (index.flags() & Qt::ItemIsUserCheckable) {
            const int checkboxSize = 32;
            QStyleOptionButton checkboxOption;
            checkboxOption.rect = QRect(option.rect.right() - checkboxSize - 10,
                                        option.rect.top() + (option.rect.height() - checkboxSize) / 2,
                                        checkboxSize,
                                        checkboxSize);
            checkboxOption.state = QStyle::State_Enabled;
            if (static_cast<Qt::CheckState>(index.data(Qt::CheckStateRole).toInt()) == Qt::Checked) {
                checkboxOption.state |= QStyle::State_On;
            } else {
                checkboxOption.state |= QStyle::State_Off;
            }
            QApplication::style()->drawControl(QStyle::CE_CheckBox, &checkboxOption, painter);
        }
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        if (index.flags() & Qt::ItemIsUserCheckable) {
            size.setWidth(size.width() + 56);
        }
        return size;
    }
};

static uint64_t nowMs()
{
    return static_cast<uint64_t>(QDateTime::currentDateTime().toMSecsSinceEpoch());
}

static double getNumber(cJSON *obj, const char *key, double defVal)
{
    if (!obj) {
        return defVal;
    }
    cJSON *n = cJSON_GetObjectItem(obj, key);
    if (n && cJSON_IsNumber(n)) {
        return n->valuedouble;
    }
    return defVal;
}

static int getInt(cJSON *obj, const char *key, int defVal)
{
    if (!obj) {
        return defVal;
    }
    cJSON *n = cJSON_GetObjectItem(obj, key);
    if (n && cJSON_IsNumber(n)) {
        return n->valueint;
    }
    return defVal;
}

static std::string getString(cJSON *obj, const char *key, const std::string &defVal)
{
    if (!obj) {
        return defVal;
    }
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (!v) {
        return defVal;
    }
    if (cJSON_IsString(v) && v->valuestring) {
        return std::string(v->valuestring);
    }
    if (cJSON_IsNumber(v)) {
        char tmp[64] = {0};
        std::snprintf(tmp, sizeof(tmp), "%.0f", v->valuedouble);
        return std::string(tmp);
    }
    return defVal;
}

static QString formatMoney(double value)
{
    return QString::number(value, 'f', 2);
}

static QString formatEnergy(double value)
{
    return QString::number(value, 'f', 3);
}

static QString formatFeeValue(double value)
{
    return QString::number(value, 'f', 4);
}

static QString formatVoltage(double value)
{
    return QString::number(value, 'f', 1);
}

static QString formatCurrent(double value)
{
    return QString::number(value, 'f', 1);
}

static QString formatChargeTime(double seconds)
{
    const int total = seconds > 0.0 ? static_cast<int>(seconds + 0.5) : 0;
    const int hour = total / 3600;
    const int minute = (total % 3600) / 60;
    // const int second = total % 60;
    return QString::fromUtf8("%1小时%2分")
        .arg(hour, 2, 10, QChar('0'))
        .arg(minute, 2, 10, QChar('0'));
}

static QString formatRemainMinutes(double minutes)
{
    const int total = minutes > 0.0 ? static_cast<int>(minutes + 0.5) : 0;
    const int hour = total / 60;
    const int minute = total % 60;
    return QString::fromUtf8("%1小时%2分钟").arg(hour).arg(minute, 2, 10, QChar('0'));
}

static QString formatHourMinute(double seconds)
{
    const int total = seconds > 0.0 ? static_cast<int>(seconds + 0.5) : 0;
    const int hour = total / 3600;
    const int minute = (total % 3600) / 60;
    return QString::fromUtf8("%1:%2")
        .arg(hour, 2, 10, QChar('0'))
        .arg(minute, 2, 10, QChar('0'));
}

static QString stateText(const std::string &state)
{
    if (state == "IDLE") {
        return QString::fromUtf8("请连接\n电动汽车");
    }
    if (state == "PREPARE") {
        return QString::fromUtf8("车辆已连接\n请点击操作");
    }
    if (state == "STARTING") {
        return QString::fromUtf8("充电启动中...");
    }
    if (state == "CHARGING") {
        return QString::fromUtf8("充电进行中");
    }
    if (state == "STOPPING") {
        return QString::fromUtf8("停止充电中...");
    }
    if (state == "STOPPED") {
        return QString::fromUtf8("充电已完成");
    }
    if (state == "ERROR") {
        return QString::fromUtf8("枪口故障");
    }
    return QString::fromUtf8("等待状态");
}

static QString idleCellText(double totalEnergy,
                            double totalAmount,
                            double chargedTime,
                            double voltage,
                            double current,
                            int soc,
                            const std::string &state)
{
    if (state == "STOPPED") {
        return QString::fromUtf8("充电已停止\n充电电量: %1kWh\n充电金额: %2元\n充电时长: %3")
            .arg(formatEnergy(totalEnergy))
            .arg(formatMoney(totalAmount))
            .arg(formatChargeTime(chargedTime));
    }
    if (state == "CHARGING" || state == "STOPPING") {
        return QString::fromUtf8("充电电量: %1kWh\n充电金额: %2元\n充电时长: %3\n充电信息: %4V %5A\nSOC: %6%")
            .arg(formatEnergy(totalEnergy))
            .arg(formatMoney(totalAmount))
            .arg(formatChargeTime(chargedTime))
            .arg(formatVoltage(voltage))
            .arg(formatCurrent(current))
            .arg(soc >= 0 ? QString::number(soc) : QString::fromUtf8("--"));
    }
    return stateText(state);
}

static bool looksLikeMachineReasonText(const std::string &text)
{
    if (text.empty()) {
        return false;
    }
    bool hasUnderscore = false;
    size_t i = 0;
    for (i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c >= 0x80) {
            return false;
        }
        if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) {
            return false;
        }
        if (c == '_') {
            hasUnderscore = true;
        }
    }
    return hasUnderscore;
}

static QString stopReasonText(const RuntimeWindow::GunUiData &gun)
{
    if (gun.state == "ERROR" && !gun.lastFaultMessage.empty()) {
        return QString::fromUtf8(gun.lastFaultMessage.c_str());
    }
    const std::string &reason = gun.stopReason;
    if (!reason.empty()) {
        char *endp = 0;
        const unsigned long rawReason = std::strtoul(reason.c_str(), &endp, 0);
        const bool isNumeric = (endp && *endp == 0);
        if (!isNumeric) {
            const FaultJudgeResult pointResult = JudgeFaultPoint(reason);
            if (pointResult.valid && !pointResult.message.empty()) {
                return QString::fromUtf8(pointResult.message.c_str());
            }
            return QString::fromUtf8(reason.c_str());
        }

        if (rawReason == 0UL) {
            return QString::fromUtf8("正常停止");
        }

        const unsigned int stage = static_cast<unsigned int>(rawReason & 0xF0000UL);
        const bool hasStage =
            (stage == 0x10000U) ||
            (stage == 0x20000U) ||
            (stage == 0x30000U);
        if (hasStage) {
            const char *reasonText = QueryReasonText(static_cast<unsigned int>(rawReason));
            if (reasonText && reasonText[0] != '\0') {
                return QString::fromUtf8(reasonText);
            }
        }

        const bool looksLikeStartFail =
            (std::fabs(gun.totalEnergy) < 0.0001) &&
            (std::fabs(gun.totalAmount) < 0.0001) &&
            (std::fabs(gun.chargedTime) < 0.0001);
        FaultJudgeResult result = looksLikeStartFail
            ? JudgeStartFailPoint(MakeStartPointKey(static_cast<unsigned int>(rawReason)))
            : JudgeChargingFailPoint(MakeChargingPointKey(static_cast<unsigned int>(rawReason)));
        if (result.valid && !result.message.empty()) {
            return QString::fromUtf8(result.message.c_str());
        }
        return QString::fromUtf8("故障码%1").arg(static_cast<unsigned int>(rawReason));
    }
    return QString::fromUtf8("充电完成");
}

static QString currentFaultCodeText(const RuntimeWindow::GunUiData &gun)
{
    if (!gun.lastFaultPointKey.empty()) {
        return QString::fromUtf8(gun.lastFaultPointKey.c_str());
    }
    if (!gun.stopReason.empty()) {
        return QString::fromUtf8(gun.stopReason.c_str());
    }
    return QString::fromUtf8("--");
}

static void setLabelText(QWidget *page, const char *name, const QString &text)
{
    QLabel *label = page ? page->findChild<QLabel *>(name) : 0;
    if (label) {
        label->setText(text);
    }
}

static void setLabelPixmap(QWidget *page, const char *name, const QString &res)
{
    QLabel *label = page ? page->findChild<QLabel *>(name) : 0;
    if (label) {
        label->setPixmap(QPixmap(res));
        label->setScaledContents(true);
    }
}

static void applyTopHeaderStyle(QWidget *page)
{
    if (!page) {
        return;
    }
    QLabel *title = page->findChild<QLabel *>("label_cdz_title");
    if (title) {
        title->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
    }
    QLabel *deviceId = page->findChild<QLabel *>("label_id");
    if (deviceId) {
        deviceId->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
    }
    QLabel *gunPole = page->findChild<QLabel *>("lblPole");
    if (gunPole) {
        gunPole->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
    }
    QLabel *gunTitle = page->findChild<QLabel *>("lblTitle");
    if (gunTitle) {
        gunTitle->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
    }
}

static QStringList splitSemicolon(const std::string &text)
{
    return QString::fromStdString(text).split(";", QString::SkipEmptyParts);
}

static int hhmmToMinutes(const std::string &hhmm)
{
    if (hhmm.size() < 4) {
        return -1;
    }
    const int hour = std::atoi(hhmm.substr(0, 2).c_str());
    const int minute = std::atoi(hhmm.substr(2, 2).c_str());
    return hour * 60 + minute;
}

static QString hhmmText(const std::string &hhmm)
{
    if (hhmm.size() < 4) {
        return QString::fromStdString(hhmm);
    }
    return QString("%1:%2").arg(QString::fromStdString(hhmm.substr(0, 2)))
                           .arg(QString::fromStdString(hhmm.substr(2, 2)));
}

static QString ymdhmsText(const std::string &stamp)
{
    if (stamp.size() < 14) {
        return QString::fromStdString(stamp);
    }
    return QString("%1-%2-%3 %4:%5:%6")
        .arg(QString::fromStdString(stamp.substr(0, 4)))
        .arg(QString::fromStdString(stamp.substr(4, 2)))
        .arg(QString::fromStdString(stamp.substr(6, 2)))
        .arg(QString::fromStdString(stamp.substr(8, 2)))
        .arg(QString::fromStdString(stamp.substr(10, 2)))
        .arg(QString::fromStdString(stamp.substr(12, 2)));
}

static QString chargeRecordReasonText(const RuntimeWindow::ChargeRecordItem &item)
{
    const unsigned int rawReason = static_cast<unsigned int>(item.reason);
    if (rawReason == 0U) {
        return QString::fromUtf8("正常停止");
    }

    const unsigned int stage = rawReason & 0xF0000U;
    const bool hasStage =
        (stage == 0x10000U) ||
        (stage == 0x20000U) ||
        (stage == 0x30000U);
    if (hasStage) {
        const char *reasonText = QueryReasonText(rawReason);
        if (reasonText && reasonText[0] != '\0') {
            return QString::fromUtf8(reasonText);
        }
    }

    const bool looksLikeStartFail =
        (std::fabs(item.totalElect) < 0.0001) &&
        (std::fabs(item.totalCost) < 0.0001) &&
        (std::fabs(item.startSoc - item.endSoc) < 0.0001);

    FaultJudgeResult result = looksLikeStartFail
        ? JudgeStartFailPoint(MakeStartPointKey(rawReason))
        : JudgeChargingFailPoint(MakeChargingPointKey(rawReason));
    if (result.valid && !result.message.empty()) {
        return QString::fromUtf8(result.message.c_str());
    }

    return QString::fromUtf8("故障码%1").arg(item.reason);
}

static QString faultRecordText(const std::string &faultName, const std::string &type)
{
    if (!faultName.empty()) {
        return QString::fromUtf8(faultName.c_str());
    }
    if (type == "Error") {
        return QString::fromUtf8("故障");
    }
    if (type == "Warning") {
        return QString::fromUtf8("告警");
    }
    return QString::fromLatin1(type.c_str());
}

static QWidget *makeInfoCard(QWidget *parent, const QString &title, const QRect &rect)
{
    QFrame *frame = new QFrame(parent);
    frame->setObjectName(title);
    frame->setGeometry(rect);
    frame->setStyleSheet("QFrame{background:rgb(244,246,247);border:1px solid rgb(168,168,168);border-radius:12px;}");

    QLabel *header = new QLabel(title, frame);
    header->setGeometry(16, 10, rect.width() - 32, 28);
    header->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:20px 'MS Shell Dlg 2';font-weight:bold;}");
    return frame;
}

static QLabel *makeValueLabel(QWidget *parent, const char *name, const QRect &rect, int pointSize, bool bold)
{
    QLabel *label = new QLabel(parent);
    label->setObjectName(name);
    label->setGeometry(rect);
    label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    label->setStyleSheet(QString("QLabel{color:rgb(38,45,52);background:transparent;font:%1px 'MS Shell Dlg 2';font-weight:%2;}")
                         .arg(pointSize)
                         .arg(bold ? 700 : 500));
    return label;
}

static void setRestrictedAboutTabsEnabled(QTabWidget *tabWidget, bool enabled)
{
    if (!tabWidget) {
        return;
    }
    int i = 0;
    for (i = 0; i < tabWidget->count(); ++i) {
        QWidget *page = tabWidget->widget(i);
        if (!page) {
            continue;
        }
        const QString pageName = page->objectName();
        if (pageName == QString::fromUtf8("tab_10") || pageName == QString::fromUtf8("tab_4")) {
            tabWidget->setTabEnabled(i, enabled);
        }
    }
}

static bool readFeeModelFromStmt(sqlite3_stmt *stmt, RuntimeWindow::FeeModelData &model)
{
    if (!stmt || sqlite3_step(stmt) != SQLITE_ROW) {
        return false;
    }

    const char *feeModelId = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    const char *timeSeg = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
    const char *segFlag = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
    const char *chargeFee = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
    const char *serviceFee = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 5));
    const char *timeStamp = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 6));

    const QStringList segs = splitSemicolon(timeSeg ? timeSeg : "");
    const QStringList flags = splitSemicolon(segFlag ? segFlag : "");
    const QStringList chargeFees = splitSemicolon(chargeFee ? chargeFee : "");
    const QStringList serviceFees = splitSemicolon(serviceFee ? serviceFee : "");
    const int count = std::min(std::min(segs.size(), chargeFees.size()), serviceFees.size());
    if (count <= 0) {
        return false;
    }

    int idx = 0;
    for (idx = 0; idx < count; ++idx) {
        RuntimeWindow::FeeSegment seg;
        seg.segFlag = idx < flags.size() ? flags.at(idx).toStdString() : std::string();
        seg.startTime = segs.at(idx).toStdString();
        seg.endTime = (idx + 1 < segs.size()) ? segs.at(idx + 1).toStdString() : std::string("2400");
        // BY ZF: 计费模型单价已统一按10^-5元存库，界面展示时换算回元。
        seg.chargeFee = chargeFees.at(idx).toDouble() / 100000.0;
        seg.serviceFee = serviceFees.at(idx).toDouble() / 100000.0;
        model.segments.push_back(seg);
    }

    model.feeModelId = feeModelId ? feeModelId : "";
    model.timeStamp = timeStamp ? timeStamp : "";
    model.valid = !model.segments.empty();
    if (!model.valid) {
        return false;
    }

    const QTime nowTime = QTime::currentTime();
    const int nowMinute = nowTime.hour() * 60 + nowTime.minute();
    model.currentIndex = 0;
    for (idx = 0; idx < static_cast<int>(model.segments.size()); ++idx) {
        const int start = hhmmToMinutes(model.segments[idx].startTime);
        const int end = hhmmToMinutes(model.segments[idx].endTime);
        if (start >= 0 && end > start && nowMinute >= start && nowMinute < end) {
            model.currentIndex = idx;
            break;
        }
        if (idx == static_cast<int>(model.segments.size()) - 1 && nowMinute >= start) {
            model.currentIndex = idx;
        }
    }

    return true;
}

static QString meterConfigPath()
{
    const QString targetPath = QString::fromUtf8("/usr/app/config/tcu_meter.ini");
    const QString fallbackPath = QString::fromUtf8("/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/config/tcu_meter.ini");
    if (QFileInfo(targetPath).exists()) {
        return targetPath;
    }
    return fallbackPath;
}

static QString commConfigPath()
{
    const QString targetPath = QString::fromUtf8("/usr/app/config/tcu_comm.ini");
    const QString fallbackPath = QString::fromUtf8("/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/config/tcu_comm.ini");
    if (QFileInfo(targetPath).exists()) {
        return targetPath;
    }
    return fallbackPath;
}

static QString pileControllerConfigPath()
{
    const QString targetPath = QString::fromUtf8("/usr/app/config/pile_controller.ini");
    const QString fallbackPath = QString::fromUtf8("/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/config/pile_controller.ini");
    if (QFileInfo(targetPath).exists()) {
        return targetPath;
    }
    return fallbackPath;
}

static QString hmiConfigPath()
{
    const QString targetPath = QString::fromUtf8("/usr/app/config/tcu_hmi.ini");
    const QString fallbackPath = QString::fromUtf8("/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/config/tcu_hmi.ini");
    if (QFileInfo(targetPath).exists()) {
        return targetPath;
    }
    return fallbackPath;
}

static bool replaceConfigValue(const QString &path, const QString &section, const QString &key, const QString &value)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    QString content = QString::fromUtf8(file.readAll());
    file.close();

    QStringList lines = content.split(QRegExp(QString::fromUtf8("\r?\n")));
    bool inMeter = false;
    bool replaced = false;
    int insertPos = -1;
    int i = 0;
    for (i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith("[") && trimmed.endsWith("]")) {
            if (inMeter) {
                break;
            }
            inMeter = (trimmed == QString::fromUtf8("[%1]").arg(section));
            continue;
        }
        if (!inMeter) {
            continue;
        }
        if (insertPos < 0) {
            insertPos = i;
        }
        if (trimmed.startsWith(key + "=")) {
            lines[i] = key + "=" + value;
            replaced = true;
            break;
        }
    }

    if (!replaced) {
        if (insertPos < 0) {
            lines << QString::fromUtf8("[%1]").arg(section);
            lines << (key + "=" + value);
        } else {
            lines.insert(insertPos, key + "=" + value);
        }
    }

    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    file.write(lines.join(QString::fromUtf8("\n")).toUtf8());
    file.close();
    return true;
}

} // namespace

RuntimeWindow::HmiConfig::HmiConfig()
    : screenWidth(800)
    , screenHeight(480)
    , fullScreen(true)
    , mqttHost("127.0.0.1")
    , mqttPort(1883)
    , mqttKeepalive(60)
    , mqttClientId("tcu_hmi_zsh")
    , mqttTopicPrefix("tcu")
    , enableMergeChargeEntry(false)
    , enableVinEntry(false)
    , enableCardEntry(false)
{
}

RuntimeWindow::GunUiData::GunUiData()
    : configured(false)
    , gun(0)
    , state("IDLE")
    , voltage(0.0)
    , current(0.0)
    , reqVoltage(0.0)
    , reqCurrent(0.0)
    , remainMinutes(0.0)
    , batteryMaxTemp(0.0)
    , power(0.0)
    , totalAmount(0.0)
    , totalEnergy(0.0)
    , chargedTime(0.0)
    , soc(-1)
    , stopReason("充电完成")
    , lastStateChangeMs(0)
{
}

RuntimeWindow::ModuleVersionInfo::ModuleVersionInfo()
{
}

RuntimeWindow::MonStatusData::MonStatusData()
    : platformComm(0)
    , meterComm(0)
    , controllerComm(0)
    , activeFaultCount(0)
{
}

RuntimeWindow::FeeSegment::FeeSegment()
    : chargeFee(0.0)
    , serviceFee(0.0)
{
}

RuntimeWindow::FeeModelData::FeeModelData()
    : currentIndex(-1)
    , valid(false)
{
}

RuntimeWindow::ChargeRecordItem::ChargeRecordItem()
    : id(0)
    , gunNo(0)
    , startSoc(0.0)
    , endSoc(0.0)
    , totalElect(0.0)
    , totalCost(0.0)
    , reason(0)
{
}

RuntimeWindow::FaultRecordItem::FaultRecordItem()
    : id(0)
    , gunNo(0)
{
}

RuntimeWindow::RuntimeWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_mqttReady(false)
    , m_showAbout(false)
    , m_forceIdleView(false)
    , m_manualFocusLocked(false)
    , m_aboutPermissionGranted(false)
    , m_gunCount(1)
    , m_focusGun(0)
    , m_operatorId("0")
    , m_macAddr("000000000000000000000000")
    , m_guns(2)
    , m_lastFeeModelCheckMs(0)
    , m_lastChargeRecordCheckMs(0)
    , m_recordGunFilter(-1)
    , m_chargeRecordPage(0)
    , m_chargeRecordPageSize(8)
    , m_lastFaultRecordCheckMs(0)
    , m_faultGunFilter(-1)
    , m_faultRecordPage(0)
    , m_faultRecordPageSize(8)
    , m_lastWatchdogFeedMs(0)
    , m_lastScreenActivityMs(0)
    , m_platformOnline(false)
    , m_screenBacklightOff(true)
    , m_stack(new QStackedWidget(this))
    , m_bottomTime(new QLabel(this))
    , m_bottomHostState(new QLabel(this))
    , m_idlePage(0)
    , m_authorizePage(0)
    , m_chargingPage(0)
    , m_checkoutPage(0)
    , m_aboutPage(0)
    , m_aboutTabWidget(0)
    , m_aboutPermissionEdit(0)
    , m_aboutPermissionHint(0)
    , m_aboutPermissionPad(0)
    , m_configPileNoEdit(0)
    , m_configSecretEdit(0)
    , m_configMeterAddr1Edit(0)
    , m_configMeterAddr2Edit(0)
    , m_configKeyboardTarget(0)
    , m_configHintLabel(0)
    , m_configKeyboard(0)
    , m_configMeter2Row(0)
    , m_configShiftButton(0)
    , m_configModeButton(0)
    , m_configBackspaceButton(0)
    , m_configClearButton(0)
    , m_configConfirmButton(0)
    , m_configGunSingle(0)
    , m_configGunDual(0)
    , m_configKeyboardUppercase(false)
    , m_configKeyboardNumberMode(false)
    , m_configKeyboardNumericOnly(false)
    , m_feeChart(0)
    , m_faultRecordTable(0)
    , m_faultRecordPageLabel(0)
    , m_chargeRecordTable(0)
    , m_chargeRecordPageLabel(0)
{
    int keyIndex = 0;
    for (keyIndex = 0; keyIndex < 30; ++keyIndex) {
        m_configKeyButtons[keyIndex] = 0;
    }
    m_guns[0].gun = 0;
    m_guns[1].gun = 1;

    setCentralWidget(m_stack);
    m_bottomTime->setParent(this);
    m_bottomTime->setGeometry(220, 448, 360, 28);
    m_bottomTime->setAlignment(Qt::AlignCenter);
    m_bottomTime->setStyleSheet("QLabel{color:white;background:transparent;font:20px 'MS Shell Dlg 2';font-weight:bold;}");
    m_bottomTime->show();
    m_bottomTime->raise();
    m_bottomHostState->setParent(this);
    m_bottomHostState->setGeometry(592, 445, 42, 32);
    m_bottomHostState->setAlignment(Qt::AlignCenter);
    m_bottomHostState->setPixmap(QPixmap(":/cui/Resources/host_not.png"));
    m_bottomHostState->setScaledContents(false);
    m_bottomHostState->show();
    m_bottomHostState->raise();
    connect(&m_uiTimer, SIGNAL(timeout()), this, SLOT(refreshUi()));
    connect(&m_screenIdleTimer, SIGNAL(timeout()), this, SLOT(checkScreenIdle()));
}

RuntimeWindow::~RuntimeWindow()
{
    if (m_mqttReady) {
        m_mqtt.loopStop(true);
        m_mqtt.disconnect();
    }
    if (m_screenBacklightOff) {
        setScreenBacklight(true);
    }
}

bool RuntimeWindow::initialize()
{
    // BY ZF: 运行态窗口复用提取 UI，但数据源改为当前项目 MQTT 流程。
    if (!loadConfig()) {
        return false;
    }

    setWindowTitle(QString::fromUtf8("中石化 HMI 运行版"));
    resize(m_config.screenWidth, m_config.screenHeight);
    setMinimumSize(m_config.screenWidth, m_config.screenHeight);
    setMaximumSize(m_config.screenWidth, m_config.screenHeight);

    if (!buildPages()) {
        return false;
    }

    if (!initMqtt()) {
        return false;
    }

    // BY ZF: 复刻旧版 BaseDialog 的空闲熄屏思路，只做轮询计时，不拦截全局事件链。
    m_lastScreenActivityMs = nowMs();
    m_screenBacklightOff = false;
    m_screenIdleTimer.start(1000);
    m_uiTimer.start(1000);
    refreshUi();

    if (m_config.fullScreen) {
        showFullScreen();
    } else {
        show();
    }
    return true;
}

bool RuntimeWindow::loadConfig()
{
    ConfigManagerLite &cfg = ConfigManagerLite::getInstance();
    if (!cfg.loadConfig("/usr/app/config/tcu_hmi.ini")) {
        std::cerr << "[HMI] use default config /usr/app/config/tcu_hmi.ini not found" << std::endl;
        return true;
    }

    const std::string section = "Hmi";
    m_config.screenWidth = cfg.getInt(section, "screen_width", 800);
    m_config.screenHeight = cfg.getInt(section, "screen_height", 480);
    m_config.fullScreen = true;
    m_config.mqttHost = cfg.getString(section, "mqtt_host", "127.0.0.1");
    m_config.mqttPort = cfg.getInt(section, "mqtt_port", 1883);
    m_config.mqttKeepalive = cfg.getInt(section, "mqtt_keepalive", 60);
    m_config.mqttClientId = cfg.getString(section, "mqtt_client_id", "tcu_hmi_zsh");
    m_config.mqttTopicPrefix = cfg.getString(section, "mqtt_topic_prefix", "tcu");
    m_config.mqttUsername = cfg.getString(section, "mqtt_username", "");
    m_config.mqttPassword = cfg.getString(section, "mqtt_password", "");
    m_config.enableMergeChargeEntry = cfg.getBool(section, "enable_merge_charge_entry", false);
    m_config.enableVinEntry = cfg.getBool(section, "enable_vin_entry", false);
    m_config.enableCardEntry = cfg.getBool(section, "enable_card_entry", false);

    m_gunCount = cfg.getInt(section, "gun_count", 1);
    if (m_gunCount < 1) {
        m_gunCount = 1;
    }
    if (m_gunCount > 2) {
        m_gunCount = 2;
    }
    return true;
}

bool RuntimeWindow::initMqtt()
{
    if (!m_mqtt.init(m_config.mqttClientId, true)) {
        std::cerr << "[HMI] mqtt init failed" << std::endl;
        return false;
    }

    if (!m_config.mqttUsername.empty()) {
        m_mqtt.setUsernamePassword(m_config.mqttUsername, m_config.mqttPassword);
    }

    m_mqtt.setConnectHandler([this](int rc) { onMqttConnected(rc); });
    m_mqtt.setMessageHandler([this](const std::string &topic, const std::string &payload) {
        onMqttMessage(topic, payload);
    });

    if (!m_mqtt.connect(m_config.mqttHost, m_config.mqttPort, m_config.mqttKeepalive)) {
        std::cerr << "[HMI] mqtt connect failed " << m_config.mqttHost << ":" << m_config.mqttPort << std::endl;
        return false;
    }

    if (!m_mqtt.loopStart()) {
        std::cerr << "[HMI] mqtt loop start failed" << std::endl;
        return false;
    }

    m_mqttReady = true;
    return true;
}

bool RuntimeWindow::buildPages()
{
    m_idlePage = createPage(":/ui/b1idle.ui");
    m_authorizePage = createPage(":/ui/c6flushcad.ui");
    m_chargingPage = createPage(":/ui/e1chargeinfo.ui");
    m_checkoutPage = createPage(":/ui/f7checkoutok.ui");
    m_aboutPage = createPage(":/ui/a3about.ui");

    if (!m_idlePage || !m_authorizePage || !m_chargingPage || !m_checkoutPage || !m_aboutPage) {
        return false;
    }

    m_stack->addWidget(m_idlePage);
    m_stack->addWidget(m_authorizePage);
    m_stack->addWidget(m_chargingPage);
    m_stack->addWidget(m_checkoutPage);
    m_stack->addWidget(m_aboutPage);

    // BY ZF: 只监听顶层页面空白区域点击，用于黑屏后亮屏，不拦截业务事件。
    m_stack->installEventFilter(this);
    m_idlePage->installEventFilter(this);
    m_authorizePage->installEventFilter(this);
    m_chargingPage->installEventFilter(this);
    m_checkoutPage->installEventFilter(this);
    m_aboutPage->installEventFilter(this);

    bindStaticUi();
    return true;
}

QWidget *RuntimeWindow::createPage(const QString &path)
{
    QWidget *page = createPreviewPage(path, this);
    if (!page) {
        return 0;
    }

    page->setAttribute(Qt::WA_StyledBackground, true);
    page->setFixedSize(800, 480);
    page->setAutoFillBackground(true);

    QPalette palette = page->palette();
    palette.setBrush(QPalette::Window, QBrush(QPixmap(":/cui/Resources/Main.png")));
    page->setPalette(palette);
    return page;
}

void RuntimeWindow::bindStaticUi()
{
    // BY ZF: 页面静态图片与按钮关系在启动时一次性绑定。
    applyTopHeaderStyle(m_idlePage);
    applyTopHeaderStyle(m_authorizePage);
    applyTopHeaderStyle(m_chargingPage);
    applyTopHeaderStyle(m_checkoutPage);
    applyTopHeaderStyle(m_aboutPage);
    setLabelPixmap(m_idlePage, "label_zq", ":/cui/Resources/qt_zq.png");
    setLabelPixmap(m_idlePage, "label_yq", ":/cui/Resources/qt_yq.png");
    setLabelPixmap(m_idlePage, "label_about", ":/cui/Resources/about.png");
    setLabelPixmap(m_authorizePage, "label_about", ":/cui/Resources/about.png");
    setLabelPixmap(m_chargingPage, "label_about", ":/cui/Resources/about.png");
    setLabelPixmap(m_authorizePage, "label_ewm", ":/cui/Resources/ewm.png");
    setLabelPixmap(m_authorizePage, "qcq", ":/cui/Resources/qcq.png");
    setLabelPixmap(m_checkoutPage, "f7_ok", ":/cui/Resources/f7_ok.png");
    setLabelPixmap(m_checkoutPage, "c8_back", ":/cui/Resources/back.png");
    setLabelPixmap(m_checkoutPage, "ts_tb", ":/cui/Resources/ts.png");

    QPushButton *btnAboutIdle = m_idlePage->findChild<QPushButton *>("btnAbout");
    QPushButton *btnAboutAuthorize = m_authorizePage->findChild<QPushButton *>("btnAbout");
    QPushButton *btnAboutCharging = m_chargingPage->findChild<QPushButton *>("btnAbout");
    QPushButton *btnBackAuthorize = m_authorizePage->findChild<QPushButton *>("btnBack");
    QPushButton *btnBackCharging = m_chargingPage->findChild<QPushButton *>("btnBack");
    QPushButton *btnBackCheckout = m_checkoutPage->findChild<QPushButton *>("btnBack");
    QPushButton *btnBackAbout = m_aboutPage->findChild<QPushButton *>("btnBack");
    QLabel *aboutBackText = m_aboutPage->findChild<QLabel *>("label_8");
    QLabel *aboutBackIcon = m_aboutPage->findChild<QLabel *>("c8_back");
    QAbstractButton *aboutOldBack = m_aboutPage->findChild<QAbstractButton *>("secback");
    QAbstractButton *aboutOldConfirm = m_aboutPage->findChild<QAbstractButton *>("secback_2");
    QLabel *idleDatetime = m_idlePage->findChild<QLabel *>("lblDatetimeb");
    QPushButton *btnMergeCharge = m_authorizePage->findChild<QPushButton *>("bbms");
    QPushButton *btnVin = m_authorizePage->findChild<QPushButton *>("vin");
    QPushButton *btnCard = m_authorizePage->findChild<QPushButton *>("card");

    if (btnAboutIdle) {
        connect(btnAboutIdle, SIGNAL(clicked()), this, SLOT(showAboutPage()));
    }
    if (btnAboutAuthorize) {
        connect(btnAboutAuthorize, SIGNAL(clicked()), this, SLOT(showAboutPage()));
    }
    if (btnAboutCharging) {
        connect(btnAboutCharging, SIGNAL(clicked()), this, SLOT(showAboutPage()));
    }
    if (btnBackAuthorize) {
        btnBackAuthorize->setVisible(true);
        btnBackAuthorize->setFlat(true);
        connect(btnBackAuthorize, SIGNAL(clicked()), this, SLOT(returnToIdlePage()));
    }
    if (btnBackCharging) {
        btnBackCharging->setVisible(true);
        btnBackCharging->setFlat(true);
        connect(btnBackCharging, SIGNAL(clicked()), this, SLOT(returnToIdlePage()));
    }
    if (btnBackCheckout) {
        btnBackCheckout->setVisible(true);
        btnBackCheckout->setFlat(true);
        connect(btnBackCheckout, SIGNAL(clicked()), this, SLOT(returnToIdlePage()));
    }
    if (btnBackAbout) {
        connect(btnBackAbout, SIGNAL(clicked()), this, SLOT(leaveAboutPage()));
    }
    if (aboutBackText) {
        aboutBackText->hide();
    }
    if (aboutBackIcon) {
        aboutBackIcon->hide();
    }
    if (aboutOldBack) {
        aboutOldBack->hide();
    }
    if (aboutOldConfirm) {
        aboutOldConfirm->hide();
    }
    if (idleDatetime) {
        idleDatetime->hide();
    }
    if (btnMergeCharge) {
        btnMergeCharge->setVisible(m_config.enableMergeChargeEntry);
    }
    if (btnVin) {
        btnVin->setVisible(m_config.enableVinEntry);
    }
    if (btnCard) {
        btnCard->setVisible(m_config.enableCardEntry);
    }
    QLabel *idleId = m_idlePage->findChild<QLabel *>("label_id");
    if (idleId) {
        idleId->raise();
    }
    QLabel *authorizePole = m_authorizePage->findChild<QLabel *>("lblPole");
    QLabel *authorizeTips = m_authorizePage->findChild<QLabel *>("lblTips");
    QLabel *authorizeTips2 = m_authorizePage->findChild<QLabel *>("lblTips_2");
    QRWidget *authorizeQr = m_authorizePage->findChild<QRWidget *>("qrMain");
    if (btnAboutAuthorize) {
        btnAboutAuthorize->raise();
    }
    if (btnBackAuthorize) {
        btnBackAuthorize->raise();
    }
    if (authorizePole) {
        authorizePole->raise();
    }
    if (authorizeTips) {
        authorizeTips->raise();
    }
    if (authorizeTips2) {
        authorizeTips2->raise();
    }
    if (authorizeQr) {
        authorizeQr->raise();
    }
    QPushButton *chargingBack = m_chargingPage->findChild<QPushButton *>("btnBack");
    QLabel *chargingTitle = m_chargingPage->findChild<QLabel *>("lblTitle");
    if (chargingBack) {
        chargingBack->raise();
    }
    if (btnAboutCharging) {
        btnAboutCharging->raise();
    }
    if (chargingTitle) {
        chargingTitle->raise();
    }
    QPushButton *checkoutBack = m_checkoutPage->findChild<QPushButton *>("btnBack");
    QLabel *checkoutHint = m_checkoutPage->findChild<QLabel *>("lblHint");
    if (checkoutBack) {
        checkoutBack->raise();
    }
    if (checkoutHint) {
        checkoutHint->raise();
    }
    if (btnBackAbout) {
        btnBackAbout->raise();
    }

    CCellWidget *cell0 = m_idlePage->findChild<CCellWidget *>("cell0");
    CCellWidget *cell1 = m_idlePage->findChild<CCellWidget *>("cell1");
    if (cell0) {
        cell0->setPort(0);
        connect(cell0, SIGNAL(cellclick(int)), this, SLOT(handleCellClick(int)));
    }
    if (cell1) {
        cell1->setPort(1);
        connect(cell1, SIGNAL(cellclick(int)), this, SLOT(handleCellClick(int)));
    }

    QTabWidget *tab = m_aboutPage->findChild<QTabWidget *>("tabWidget");
    if (tab) {
        tab->setCurrentIndex(0);
        tab->installEventFilter(this);
        connect(tab, SIGNAL(currentChanged(int)), this, SLOT(handleAboutTabChanged(int)));
    }
    m_aboutTabWidget = tab;
    QLabel *aboutIcon = m_aboutPage->findChild<QLabel *>("label_cdz_icon");
    if (!aboutIcon) {
        aboutIcon = new QLabel(m_aboutPage);
        aboutIcon->setObjectName("label_cdz_icon");
        aboutIcon->setGeometry(8, 2, 32, 32);
        aboutIcon->setPixmap(QPixmap(":/cui/Resources/cdz_tb.png"));
        aboutIcon->setScaledContents(true);
        aboutIcon->show();
    }
    QLabel *aboutTitle = m_aboutPage->findChild<QLabel *>("label_cdz_title");
    if (!aboutTitle) {
        aboutTitle = new QLabel(QString::fromUtf8("直流充电桩"), m_aboutPage);
        aboutTitle->setObjectName("label_cdz_title");
        aboutTitle->setGeometry(44, 0, 180, 41);
        aboutTitle->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
        aboutTitle->show();
    }
    QLabel *aboutId = m_aboutPage->findChild<QLabel *>("label_id");
    if (!aboutId) {
        aboutId = new QLabel(m_aboutPage);
        aboutId->setObjectName("label_id");
        aboutId->setGeometry(590, 0, 211, 41);
        aboutId->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        aboutId->setStyleSheet("QLabel{color:white;background:transparent;font:25px 'MS Shell Dlg 2';font-weight:bold;}");
        aboutId->show();
    }
    setupAboutTabs();
}

void RuntimeWindow::onMqttConnected(int rc)
{
    if (rc != 0) {
        std::cerr << "[HMI] mqtt reconnect rc=" << rc << std::endl;
        return;
    }

    const std::string p = m_config.mqttTopicPrefix;
    m_mqtt.subscribe(p + "/logic/+/event", 2);
    m_mqtt.subscribe(p + "/logic/+/feeData", 1);
    m_mqtt.subscribe(p + "/pile/+/data", 0);
    m_mqtt.subscribe(p + "/plat/+/event", 1);
    m_mqtt.subscribe(p + "/plat/+/setConfig", 1);
    m_mqtt.subscribe(p + "/save/+/event", 1);
    m_mqtt.subscribe(p + "/mon/0/event", 1);
}

void RuntimeWindow::onMqttMessage(const std::string &topic, const std::string &payload)
{
    uint8_t gun = 0;
    std::string tail;

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/logic/", gun, tail)) {
        if (gun <= 1 && tail == "event") {
            handleLogicEvent(gun, payload);
        } else if (gun <= 1 && tail == "feeData") {
            handleFeeData(gun, payload);
        }
        markScreenActivity();
        return;
    }

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/pile/", gun, tail)) {
        if (gun <= 1 && tail == "data") {
            handlePileData(gun, payload);
        }
        return;
    }

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/plat/", gun, tail)) {
        if (gun <= 1 && (tail == "event" || tail == "setConfig")) {
            handlePlatEvent(gun, payload);
        }
        return;
    }

    if (parseTopicGun(topic, m_config.mqttTopicPrefix + "/save/", gun, tail)) {
        if (gun <= 1 && tail == "event") {
            handleSaveEvent(gun, payload);
        }
        markScreenActivity();
        return;
    }

    if (topic == m_config.mqttTopicPrefix + "/mon/0/event") {
        handleMonEvent(payload);
    }
    markScreenActivity();
}

void RuntimeWindow::handleLogicEvent(uint8_t gun, const std::string &payload)
{
    // BY ZF: 逻辑事件只负责刷新状态缓存，不在回调线程直接操作界面。
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *evt = cJSON_GetObjectItem(root, "event");
    cJSON *data = cJSON_GetObjectItem(root, "data");

    if (evt && cJSON_IsString(evt) && evt->valuestring &&
        std::strcmp(evt->valuestring, "state_change") == 0 &&
        cJSON_IsObject(data)) {
        cJSON *to = cJSON_GetObjectItem(data, "to");
        if (to && cJSON_IsString(to) && to->valuestring) {
            QMutexLocker locker(&m_dataMutex);
            m_guns[gun].state = to->valuestring;
            m_guns[gun].lastStateChangeMs = nowMs();
            if (m_guns[gun].state != "PREPARE" && m_guns[gun].state != "STARTING") {
                m_forceIdleView = false;
                m_manualFocusLocked = false;
            }
            if (!m_manualFocusLocked && m_guns[gun].state != "IDLE") {
                m_focusGun = gun;
            }
            const std::string reasonText = getString(data, "reasonText",
                getString(data, "stopReasonText", ""));
            const std::string numericReason = getString(data, "reason",
                getString(data, "stopReason", ""));
            if (!numericReason.empty() && looksLikeMachineReasonText(reasonText)) {
                m_guns[gun].stopReason = numericReason;
            } else if (!reasonText.empty()) {
                m_guns[gun].stopReason = reasonText;
            } else if (!numericReason.empty()) {
                m_guns[gun].stopReason = numericReason;
            }
        }
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::handleFeeData(uint8_t gun, const std::string &payload)
{
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsObject(data)) {
        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].totalEnergy = getNumber(data, "totalEnergy", m_guns[gun].totalEnergy);
        m_guns[gun].totalAmount = getNumber(data, "totalAmount", m_guns[gun].totalAmount);
        m_guns[gun].chargedTime = getNumber(data, "chargedTime", m_guns[gun].chargedTime);
        m_guns[gun].soc = getInt(data, "soc", getInt(data, "vehicleSoc", m_guns[gun].soc));
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::handlePileData(uint8_t gun, const std::string &payload)
{
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (type && cJSON_IsString(type) && type->valuestring &&
        std::strcmp(type->valuestring, "yc") == 0 &&
        cJSON_IsObject(data)) {
        const double v = getNumber(data, "outputVoltage", m_guns[gun].voltage);
        const double c = getNumber(data, "outputCurrent", m_guns[gun].current);

        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].voltage = v;
        m_guns[gun].current = c;
        m_guns[gun].reqVoltage = getNumber(data, "bmsReqVoltage", m_guns[gun].reqVoltage);
        m_guns[gun].reqCurrent = getNumber(data, "bmsReqCurrent", m_guns[gun].reqCurrent);
        m_guns[gun].remainMinutes = getNumber(data, "estimatedRemainTime", m_guns[gun].remainMinutes);
        m_guns[gun].batteryMaxTemp = getNumber(data, "batteryMaxTemp", m_guns[gun].batteryMaxTemp);
        m_guns[gun].power = (v * c) / 1000.0;
        m_guns[gun].soc = getInt(data, "soc", getInt(data, "vehicleSoc", m_guns[gun].soc));
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::handlePlatEvent(uint8_t gun, const std::string &payload)
{
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    cJSON *event = cJSON_GetObjectItem(root, "event");
    cJSON *data = cJSON_GetObjectItem(root, "data");

    const bool isSetConfigType = (type && cJSON_IsString(type) && type->valuestring &&
        std::strcmp(type->valuestring, "setConfig") == 0);
    const bool isSetConfigCmd = (cmd && cJSON_IsString(cmd) && cmd->valuestring &&
        std::strcmp(cmd->valuestring, "setConfig") == 0);
    const bool isPlatformOnline = (event && cJSON_IsString(event) && event->valuestring &&
        std::strcmp(event->valuestring, "platform_online") == 0);
    const bool isPlatformOffline = (event && cJSON_IsString(event) && event->valuestring &&
        std::strcmp(event->valuestring, "platform_offline") == 0);
    if (isPlatformOnline || isPlatformOffline) {
        QMutexLocker locker(&m_dataMutex);
        m_platformOnline = isPlatformOnline;
        cJSON_Delete(root);
        QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
        return;
    }
    if ((!isSetConfigType && !isSetConfigCmd) || !cJSON_IsObject(data)) {
        cJSON_Delete(root);
        return;
    }

    QMutexLocker locker(&m_dataMutex);

    int gc = getInt(data, "gunCount", m_gunCount);
    if (gc < 1) {
        gc = 1;
    }
    if (gc > 2) {
        gc = 2;
    }
    m_gunCount = gc;

    m_cdzId = getString(data, "cdzId", m_cdzId);
    m_cdzNo = getString(data, "cdzNo", m_cdzNo);
    m_operatorId = getString(data, "operatorId", m_operatorId);

    std::string mac = getString(data, "macAddr", "");
    if (mac.empty()) {
        mac = getString(data, "mac", m_macAddr);
    }
    if (!mac.empty()) {
        m_macAddr = mac;
    }

    if (gun < m_guns.size()) {
        const std::string gunId = getString(data, "gunId", "");
        if (!gunId.empty()) {
            m_guns[gun].gunId = gunId;
        }

        const std::string qrCode = getString(data, "qrCode",
            getString(data, "qrcode",
            getString(data, "qrPayload",
            getString(data, "payUrl", m_guns[gun].qrPayload))));
        if (!qrCode.empty()) {
            m_guns[gun].qrPayload = qrCode;
        }

        m_guns[gun].configured = true;
    }

    cJSON *guns = cJSON_GetObjectItem(data, "guns");
    if (guns && cJSON_IsArray(guns)) {
        const int n = cJSON_GetArraySize(guns);
        for (int i = 0; i < n; ++i) {
            cJSON *item = cJSON_GetArrayItem(guns, i);
            if (!cJSON_IsObject(item)) {
                continue;
            }
            const int idx = getInt(item, "gun", -1);
            if (idx < 0 || idx > 1) {
                continue;
            }
            m_guns[idx].gunId = getString(item, "gunId", m_guns[idx].gunId);
            const std::string qrCode = getString(item, "qrcode",
                getString(item, "qrCode",
                getString(item, "qrPayload",
                getString(item, "payUrl", m_guns[idx].qrPayload))));
            if (!qrCode.empty()) {
                m_guns[idx].qrPayload = qrCode;
            }
            m_guns[idx].configured = true;
            if (m_guns[idx].qrPayload.empty()) {
                rebuildQrPayload(idx);
            }
        }
    }

    for (int i = 0; i < 2; ++i) {
        if (m_guns[i].qrPayload.empty()) {
            rebuildQrPayload(i);
        }
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::handleSaveEvent(uint8_t gun, const std::string &payload)
{
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (type && cJSON_IsString(type) && type->valuestring &&
        std::strcmp(type->valuestring, "Error") == 0 &&
        cJSON_IsObject(data)) {
        QMutexLocker locker(&m_dataMutex);
        m_guns[gun].lastFaultPointKey = getString(data, "pointKey", m_guns[gun].lastFaultPointKey);
        m_guns[gun].lastFaultMessage = getString(data, "faultMessage", m_guns[gun].lastFaultMessage);
        m_guns[gun].lastFaultOccurTime = getString(data, "occurTime", m_guns[gun].lastFaultOccurTime);
    }

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::rebuildQrPayload(int gun)
{
    if (gun < 0 || gun >= static_cast<int>(m_guns.size())) {
        return;
    }

    const std::string gunId = m_guns[gun].gunId.empty() ? std::to_string(gun) : m_guns[gun].gunId;
    const std::string operatorId = m_operatorId.empty() ? std::string("0") : m_operatorId;
    const std::string macAddr = m_macAddr.empty() ? std::string("000000000000000000000000") : m_macAddr;
    m_guns[gun].qrPayload = std::string("hlht://") + gunId + "." + operatorId + "/" + macAddr;
}

void RuntimeWindow::setupAboutTabs()
{
    // BY ZF: A3 前两页改为运行态重建，避免继续维护旧项目遗留的大量静态控件。
    if (!m_aboutTabWidget) {
        return;
    }

    QWidget *feeTab = m_aboutTabWidget->widget(0);
    QWidget *deviceTab = m_aboutTabWidget->widget(1);
    QWidget *configTab = m_aboutPage->findChild<QWidget *>("tab_4");
    QWidget *permissionTab = m_aboutPage->findChild<QWidget *>("tab_6");
    if (!feeTab || !deviceTab || !configTab || !permissionTab) {
        return;
    }

    m_aboutTabWidget->setTabText(0, QString::fromUtf8("分时信息"));
    m_aboutTabWidget->setTabText(1, QString::fromUtf8("设备信息"));
    int configIdx = m_aboutTabWidget->indexOf(configTab);
    if (configIdx >= 0) {
        m_aboutTabWidget->setTabText(configIdx, QString::fromUtf8("参数配置"));
    }

    QList<QWidget *> feeChildren;
    const QObjectList feeObjects = feeTab->children();
    int i = 0;
    for (i = 0; i < feeObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(feeObjects.at(i));
        if (child && child->parent() == feeTab) {
            feeChildren.append(child);
        }
    }
    for (i = 0; i < feeChildren.size(); ++i) {
        feeChildren.at(i)->hide();
    }

    QList<QWidget *> deviceChildren;
    const QObjectList deviceObjects = deviceTab->children();
    for (i = 0; i < deviceObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(deviceObjects.at(i));
        if (child && child->parent() == deviceTab) {
            deviceChildren.append(child);
        }
    }
    for (i = 0; i < deviceChildren.size(); ++i) {
        deviceChildren.at(i)->hide();
    }

    QWidget *feeOverlay = feeTab->findChild<QWidget *>("feeInfoOverlay");
    if (!feeOverlay) {
        feeOverlay = new QWidget(feeTab);
        feeOverlay->setObjectName("feeInfoOverlay");
        feeOverlay->setGeometry(0, 0, 755, 345);
        feeOverlay->setStyleSheet("background:transparent;");

        QFrame *topCard = static_cast<QFrame *>(makeInfoCard(feeOverlay, QString(), QRect(12, 8, 731, 48)));
        topCard->setStyleSheet("QFrame{background:transparent;border:none;}");
        makeValueLabel(topCard, "lblCurrentSeg", QRect(18, 20, 170, 24), 14, true);
        makeValueLabel(topCard, "lblCurrentChargeFee", QRect(194, 20, 160, 24), 14, false);
        makeValueLabel(topCard, "lblCurrentServiceFee", QRect(364, 20, 160, 24), 14, false);
        makeValueLabel(topCard, "lblCurrentTotalFee", QRect(534, 20, 170, 24), 14, true);

        QLabel *legendCharge = new QLabel(QString::fromUtf8("电费"), feeOverlay);
        legendCharge->setGeometry(548, 64, 60, 20);
        legendCharge->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:13px 'MS Shell Dlg 2';}");
        QLabel *legendChargeColor = new QLabel(feeOverlay);
        legendChargeColor->setGeometry(522, 67, 18, 14);
        legendChargeColor->setStyleSheet("QLabel{background:rgb(0,176,102);border-radius:3px;}");
        QLabel *legendService = new QLabel(QString::fromUtf8("服务费"), feeOverlay);
        legendService->setGeometry(662, 64, 60, 20);
        legendService->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:13px 'MS Shell Dlg 2';}");
        QLabel *legendServiceColor = new QLabel(feeOverlay);
        legendServiceColor->setGeometry(636, 67, 18, 14);
        legendServiceColor->setStyleSheet("QLabel{background:rgb(18,108,245);border-radius:3px;}");

        m_feeChart = new FeeModelChartWidget(feeOverlay);
        m_feeChart->setObjectName("feeModelChart");
        m_feeChart->setGeometry(12, 74, 731, 248);
        legendChargeColor->raise();
        legendCharge->raise();
        legendServiceColor->raise();
        legendService->raise();
        feeOverlay->show();
    }

    QWidget *deviceOverlay = deviceTab->findChild<QWidget *>("deviceInfoOverlay");
    if (!deviceOverlay) {
        deviceOverlay = new QWidget(deviceTab);
        deviceOverlay->setObjectName("deviceInfoOverlay");
        deviceOverlay->setGeometry(0, 0, 755, 345);
        deviceOverlay->setStyleSheet("background:transparent;");

        QFrame *leftCard = static_cast<QFrame *>(makeInfoCard(deviceOverlay, QString::fromUtf8("软件版本信息"), QRect(12, 10, 360, 300)));
        QFrame *rightCard = static_cast<QFrame *>(makeInfoCard(deviceOverlay, QString::fromUtf8("通信状态信息"), QRect(383, 10, 360, 300)));

        const int rowY[] = {48, 84, 120, 156, 192, 228};
        const char *nameLabels[] = {
            "logic", "meter", "mon", "comm", "daemon", "hmi"
        };
        const char *valueNames[] = {
            "lblVersionLogic", "lblVersionMeter", "lblVersionMon",
            "lblVersionComm", "lblVersionDaemon", "lblVersionHmi"
        };
        for (i = 0; i < 6; ++i) {
            QLabel *name = new QLabel(QString::fromLatin1(nameLabels[i]), leftCard);
            name->setGeometry(18, rowY[i], 70, 24);
            name->setStyleSheet("QLabel{color:rgb(72,84,94);background:transparent;font:15px 'MS Shell Dlg 2';font-weight:bold;}");
            makeValueLabel(leftCard, valueNames[i], QRect(88, rowY[i], 250, 24), 14, false);
        }

        QLabel *deviceIdTitle = new QLabel(QString::fromUtf8("设备身份"), leftCard);
        deviceIdTitle->setGeometry(18, 264, 80, 20);
        deviceIdTitle->setStyleSheet("QLabel{color:rgb(72,84,94);background:transparent;font:14px 'MS Shell Dlg 2';font-weight:bold;}");
        makeValueLabel(leftCard, "lblDeviceIdentity", QRect(100, 260, 240, 26), 12, false);

        const QString commTitles[] = {
            QString::fromUtf8("平台通信状态"),
            QString::fromUtf8("电表通信状态"),
            QString::fromUtf8("主控通信状态"),
            QString::fromUtf8("当前故障数量"),
            QString::fromUtf8("最近故障名称"),
            QString::fromUtf8("最近故障时间")
        };
        const char *commValueNames[] = {
            "lblCommPlatform", "lblCommMeter", "lblCommController",
            "lblDeviceFaultCount", "lblDeviceLatestFault", "lblDeviceLatestFaultTime"
        };
        for (i = 0; i < 6; ++i) {
            QLabel *title = new QLabel(commTitles[i], rightCard);
            title->setGeometry(18, rowY[i], 120, 24);
            title->setStyleSheet("QLabel{color:rgb(72,84,94);background:transparent;font:15px 'MS Shell Dlg 2';font-weight:bold;}");
            QLabel *value = makeValueLabel(rightCard, commValueNames[i], QRect(150, rowY[i], 180, 24), 14, true);
            if (i < 3) {
                value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            }
        }
        deviceOverlay->show();
    }

    QList<QWidget *> permissionChildren;
    const QObjectList permissionObjects = permissionTab->children();
    for (i = 0; i < permissionObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(permissionObjects.at(i));
        if (child && child->parent() == permissionTab) {
            permissionChildren.append(child);
        }
    }
    for (i = 0; i < permissionChildren.size(); ++i) {
        permissionChildren.at(i)->hide();
    }

    QWidget *permissionOverlay = permissionTab->findChild<QWidget *>("permissionOverlay");
    if (!permissionOverlay) {
        permissionOverlay = new QWidget(permissionTab);
        permissionOverlay->setObjectName("permissionOverlay");
        permissionOverlay->setGeometry(0, 0, 755, 345);
        permissionOverlay->setStyleSheet("background:transparent;");

        QFrame *card = static_cast<QFrame *>(makeInfoCard(permissionOverlay, QString::fromUtf8("页面权限"), QRect(82, 42, 590, 270)));

        m_aboutPermissionEdit = new WNumEdit(card);
        m_aboutPermissionEdit->setObjectName("editAboutPermission");
        m_aboutPermissionEdit->setGeometry(24, 88, 246, 56);
        m_aboutPermissionEdit->setEchoMode(QLineEdit::Password);
        m_aboutPermissionEdit->setMaxLength(4);
        m_aboutPermissionEdit->setAlignment(Qt::AlignCenter);
        m_aboutPermissionEdit->setStyleSheet("QLineEdit{background:white;border:2px solid rgb(150,150,150);border-radius:10px;color:rgb(38,45,52);font:28px 'MS Shell Dlg 2';font-weight:bold;padding:2px 8px;}");
        m_aboutPermissionEdit->installEventFilter(this);
        connect(m_aboutPermissionEdit, SIGNAL(returnPressed()), this, SLOT(submitAboutPermission()));

        QPushButton *editClickMask = new QPushButton(card);
        editClickMask->setObjectName("btnAboutPermissionMask");
        editClickMask->setGeometry(m_aboutPermissionEdit->geometry());
        editClickMask->setFlat(true);
        editClickMask->setStyleSheet("QPushButton{background:transparent;border:none;}");
        connect(editClickMask, SIGNAL(clicked()), this, SLOT(showAboutPermissionPad()));
        editClickMask->raise();

        QPushButton *submitBtn = new QPushButton(QString::fromUtf8("确认"), card);
        submitBtn->setObjectName("btnAboutPermissionSubmit");
        submitBtn->setGeometry(96, 166, 104, 36);
        submitBtn->setStyleSheet("QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:8px;color:white;font:14px 'MS Shell Dlg 2';font-weight:bold;}"
                                 "QPushButton:pressed{background:rgb(58,128,176);}");
        connect(submitBtn, SIGNAL(clicked()), this, SLOT(submitAboutPermission()));

        m_aboutPermissionHint = new QLabel(QString::fromUtf8("未授权"), card);
        m_aboutPermissionHint->setObjectName("lblAboutPermissionHint");
        m_aboutPermissionHint->setGeometry(24, 214, 246, 22);
        m_aboutPermissionHint->setAlignment(Qt::AlignCenter);
        m_aboutPermissionHint->setStyleSheet("QLabel{color:rgb(160,60,60);background:transparent;font:14px 'MS Shell Dlg 2';font-weight:bold;}");

        m_aboutPermissionPad = new QFrame(permissionOverlay);
        m_aboutPermissionPad->setObjectName("aboutPermissionPad");
        m_aboutPermissionPad->setGeometry(362, 74, 270, 214);
        m_aboutPermissionPad->setStyleSheet("QFrame{background:rgb(236,238,239);border:1px solid rgb(160,160,160);border-radius:10px;}");
        const char *keys[] = {"1","2","3","4","5","6","7","8","9","清空","0","退格"};
        for (i = 0; i < 12; ++i) {
            QPushButton *key = new QPushButton(QString::fromLatin1(keys[i]), m_aboutPermissionPad);
            key->setObjectName(QString::fromUtf8("btnPad_%1").arg(i));
            const int row = i / 3;
            const int col = i % 3;
            key->setGeometry(12 + col * 82, 12 + row * 48, 74, 40);
            key->setStyleSheet("QPushButton{background:white;border:1px solid rgb(150,150,150);border-radius:6px;color:rgb(38,45,52);font:18px 'MS Shell Dlg 2';font-weight:bold;}"
                               "QPushButton:pressed{background:rgb(214,217,219);}");
            connect(key, SIGNAL(clicked()), this, SLOT(submitAboutPermission()));
        }
        m_aboutPermissionPad->hide();
        std::cerr << "[A3] permission pad setup geometry="
                  << m_aboutPermissionPad->x() << ","
                  << m_aboutPermissionPad->y() << ","
                  << m_aboutPermissionPad->width() << ","
                  << m_aboutPermissionPad->height() << std::endl;

        permissionOverlay->show();
    }

    setRestrictedAboutTabsEnabled(m_aboutTabWidget, m_aboutPermissionGranted);
    setupFaultRecordTab();
    setupChargeRecordTab();
    setupConfigTab();
    setupExternalStorageTab();
}

void RuntimeWindow::setupFaultRecordTab()
{
    // BY ZF: 故障记录按与充电记录一致的交互方式展示，支持 A/B/全部筛选与分页。
    if (!m_aboutTabWidget) {
        return;
    }

    QWidget *faultTab = m_aboutPage->findChild<QWidget *>("tab_3");
    if (!faultTab) {
        return;
    }

    int idx = m_aboutTabWidget->indexOf(faultTab);
    if (idx >= 0) {
        m_aboutTabWidget->setTabText(idx, QString::fromUtf8("故障记录"));
    }

    QList<QWidget *> tabChildren;
    const QObjectList tabObjects = faultTab->children();
    int i = 0;
    for (i = 0; i < tabObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(tabObjects.at(i));
        if (child && child->parent() == faultTab) {
            tabChildren.append(child);
        }
    }
    for (i = 0; i < tabChildren.size(); ++i) {
        tabChildren.at(i)->hide();
    }

    QWidget *recordOverlay = faultTab->findChild<QWidget *>("faultRecordOverlay");
    if (recordOverlay) {
        return;
    }

    recordOverlay = new QWidget(faultTab);
    recordOverlay->setObjectName("faultRecordOverlay");
    recordOverlay->setGeometry(0, 0, 755, 345);
    recordOverlay->setStyleSheet("background:transparent;");

    QFrame *filterBar = static_cast<QFrame *>(makeInfoCard(recordOverlay, QString(), QRect(12, 8, 731, 50)));
    filterBar->setStyleSheet("QFrame{background:transparent;border:none;}");

    QPushButton *btnAll = new QPushButton(QString::fromUtf8("全部"), filterBar);
    btnAll->setObjectName("btnFaultRecordAll");
    btnAll->setGeometry(18, 12, 92, 28);
    btnAll->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}"
                          "QPushButton:pressed{background:rgb(205,208,210);}");
    connect(btnAll, SIGNAL(clicked()), this, SLOT(showAllFaultRecords()));

    QPushButton *btnA = new QPushButton(QString::fromUtf8("A枪"), filterBar);
    btnA->setObjectName("btnFaultRecordA");
    btnA->setGeometry(122, 12, 92, 28);
    btnA->setStyleSheet(btnAll->styleSheet());
    connect(btnA, SIGNAL(clicked()), this, SLOT(showAFaultRecords()));

    QPushButton *btnB = new QPushButton(QString::fromUtf8("B枪"), filterBar);
    btnB->setObjectName("btnFaultRecordB");
    btnB->setGeometry(226, 12, 92, 28);
    btnB->setStyleSheet(btnAll->styleSheet());
    connect(btnB, SIGNAL(clicked()), this, SLOT(showBFaultRecords()));

    m_faultRecordPageLabel = new QLabel(filterBar);
    m_faultRecordPageLabel->setObjectName("lblFaultRecordPage");
    m_faultRecordPageLabel->setGeometry(432, 12, 92, 28);
    m_faultRecordPageLabel->setAlignment(Qt::AlignCenter);
    m_faultRecordPageLabel->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");

    QPushButton *btnPrev = new QPushButton(QString::fromUtf8("上一页"), filterBar);
    btnPrev->setObjectName("btnFaultRecordPrev");
    btnPrev->setGeometry(536, 12, 92, 28);
    btnPrev->setStyleSheet(btnAll->styleSheet());
    connect(btnPrev, SIGNAL(clicked()), this, SLOT(showPrevFaultRecordPage()));

    QPushButton *btnNext = new QPushButton(QString::fromUtf8("下一页"), filterBar);
    btnNext->setObjectName("btnFaultRecordNext");
    btnNext->setGeometry(640, 12, 92, 28);
    btnNext->setStyleSheet(btnAll->styleSheet());
    connect(btnNext, SIGNAL(clicked()), this, SLOT(showNextFaultRecordPage()));

    m_faultRecordTable = new QTableWidget(recordOverlay);
    m_faultRecordTable->setObjectName("faultRecordTable");
    m_faultRecordTable->setGeometry(12, 52, 731, 270);
    m_faultRecordTable->setColumnCount(3);
    m_faultRecordTable->setRowCount(0);
    QStringList headers;
    headers << QString::fromUtf8("枪号")
            << QString::fromUtf8("发生时间")
            << QString::fromUtf8("故障名称");
    m_faultRecordTable->setHorizontalHeaderLabels(headers);
    m_faultRecordTable->verticalHeader()->hide();
    m_faultRecordTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_faultRecordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_faultRecordTable->setFocusPolicy(Qt::NoFocus);
    m_faultRecordTable->setShowGrid(true);
    m_faultRecordTable->setAlternatingRowColors(true);
    m_faultRecordTable->setStyleSheet(
        "QTableWidget{background:rgb(244,246,247);alternate-background-color:rgb(236,238,239);"
        "gridline-color:rgb(188,188,188);border:1px solid rgb(168,168,168);color:rgb(38,45,52);font:13px 'MS Shell Dlg 2';}"
        "QHeaderView::section{background:rgb(214,217,219);color:rgb(38,45,52);border:1px solid rgb(168,168,168);font:13px 'MS Shell Dlg 2';font-weight:bold;}");
    m_faultRecordTable->setColumnWidth(0, 72);
    m_faultRecordTable->setColumnWidth(1, 220);
    m_faultRecordTable->setColumnWidth(2, 417);
    recordOverlay->show();
}

void RuntimeWindow::setupChargeRecordTab()
{
    // BY ZF: 充电记录统一收敛为单页，A/B/全部通过筛选按钮切换。
    if (!m_aboutTabWidget) {
        return;
    }

    QWidget *summaryTab = m_aboutPage->findChild<QWidget *>("tab_9");
    QWidget *aTab = m_aboutPage->findChild<QWidget *>("tab_7");
    QWidget *bTab = m_aboutPage->findChild<QWidget *>("tab_8");

    int idx = -1;
    if (summaryTab) {
        idx = m_aboutTabWidget->indexOf(summaryTab);
        if (idx >= 0) {
            m_aboutTabWidget->removeTab(idx);
        }
    }
    if (bTab) {
        idx = m_aboutTabWidget->indexOf(bTab);
        if (idx >= 0) {
            m_aboutTabWidget->removeTab(idx);
        }
    }
    if (!aTab) {
        return;
    }

    idx = m_aboutTabWidget->indexOf(aTab);
    if (idx >= 0) {
        m_aboutTabWidget->setTabText(idx, QString::fromUtf8("充电记录"));
    }

    QList<QWidget *> tabChildren;
    const QObjectList tabObjects = aTab->children();
    int i = 0;
    for (i = 0; i < tabObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(tabObjects.at(i));
        if (child && child->parent() == aTab) {
            tabChildren.append(child);
        }
    }
    for (i = 0; i < tabChildren.size(); ++i) {
        tabChildren.at(i)->hide();
    }

    QWidget *recordOverlay = aTab->findChild<QWidget *>("chargeRecordOverlay");
    if (recordOverlay) {
        return;
    }

    recordOverlay = new QWidget(aTab);
    recordOverlay->setObjectName("chargeRecordOverlay");
    recordOverlay->setGeometry(0, 0, 755, 345);
    recordOverlay->setStyleSheet("background:transparent;");

    QFrame *filterBar = static_cast<QFrame *>(makeInfoCard(recordOverlay, QString(), QRect(12, 8, 731, 50)));
    filterBar->setStyleSheet("QFrame{background:transparent;border:none;}");

    QPushButton *btnAll = new QPushButton(QString::fromUtf8("全部"), filterBar);
    btnAll->setObjectName("btnChargeRecordAll");
    btnAll->setGeometry(18, 12, 92, 28);
    btnAll->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}"
                          "QPushButton:pressed{background:rgb(205,208,210);}");
    connect(btnAll, SIGNAL(clicked()), this, SLOT(showAllChargeRecords()));

    QPushButton *btnA = new QPushButton(QString::fromUtf8("A枪"), filterBar);
    btnA->setObjectName("btnChargeRecordA");
    btnA->setGeometry(122, 12, 92, 28);
    btnA->setStyleSheet(btnAll->styleSheet());
    connect(btnA, SIGNAL(clicked()), this, SLOT(showAGunChargeRecords()));

    QPushButton *btnB = new QPushButton(QString::fromUtf8("B枪"), filterBar);
    btnB->setObjectName("btnChargeRecordB");
    btnB->setGeometry(226, 12, 92, 28);
    btnB->setStyleSheet(btnAll->styleSheet());
    connect(btnB, SIGNAL(clicked()), this, SLOT(showBGunChargeRecords()));

    m_chargeRecordPageLabel = new QLabel(filterBar);
    m_chargeRecordPageLabel->setObjectName("lblChargeRecordPage");
    m_chargeRecordPageLabel->setGeometry(432, 12, 92, 28);
    m_chargeRecordPageLabel->setAlignment(Qt::AlignCenter);
    m_chargeRecordPageLabel->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");

    QPushButton *btnPrevLeft = new QPushButton(QString::fromUtf8("上一页"), filterBar);
    btnPrevLeft->setObjectName("btnChargeRecordPrevLeft");
    btnPrevLeft->setGeometry(536, 12, 92, 28);
    btnPrevLeft->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}"
                               "QPushButton:pressed{background:rgb(205,208,210);}");
    connect(btnPrevLeft, SIGNAL(clicked()), this, SLOT(showPrevChargeRecordPage()));

    QPushButton *btnNextLeft = new QPushButton(QString::fromUtf8("下一页"), filterBar);
    btnNextLeft->setObjectName("btnChargeRecordNextLeft");
    btnNextLeft->setGeometry(640, 12, 92, 28);
    btnNextLeft->setStyleSheet(btnPrevLeft->styleSheet());
    connect(btnNextLeft, SIGNAL(clicked()), this, SLOT(showNextChargeRecordPage()));

    m_chargeRecordTable = new QTableWidget(recordOverlay);
    m_chargeRecordTable->setObjectName("chargeRecordTable");
    m_chargeRecordTable->setGeometry(12, 52, 731, 270);
    m_chargeRecordTable->setColumnCount(7);
    m_chargeRecordTable->setRowCount(0);
    QStringList headers;
    headers << QString::fromUtf8("编号")
            << QString::fromUtf8("枪号")
            << QString::fromUtf8("启动时间")
            << QString::fromUtf8("结束时间")
            << QString::fromUtf8("电量(kWh)")
            << QString::fromUtf8("金额(元)")
            << QString::fromUtf8("停机原因");
    m_chargeRecordTable->setHorizontalHeaderLabels(headers);
    m_chargeRecordTable->verticalHeader()->hide();
    m_chargeRecordTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_chargeRecordTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_chargeRecordTable->setFocusPolicy(Qt::NoFocus);
    m_chargeRecordTable->setShowGrid(true);
    m_chargeRecordTable->setAlternatingRowColors(true);
    m_chargeRecordTable->setStyleSheet(
        "QTableWidget{background:rgb(244,246,247);alternate-background-color:rgb(236,238,239);"
        "gridline-color:rgb(188,188,188);border:1px solid rgb(168,168,168);color:rgb(38,45,52);font:13px 'MS Shell Dlg 2';}"
        "QHeaderView::section{background:rgb(214,217,219);color:rgb(38,45,52);border:1px solid rgb(168,168,168);font:13px 'MS Shell Dlg 2';font-weight:bold;}");
    m_chargeRecordTable->setColumnWidth(0, 54);
    m_chargeRecordTable->setColumnWidth(1, 52);
    m_chargeRecordTable->setColumnWidth(2, 156);
    m_chargeRecordTable->setColumnWidth(3, 156);
    m_chargeRecordTable->setColumnWidth(4, 96);
    m_chargeRecordTable->setColumnWidth(5, 86);
    m_chargeRecordTable->setColumnWidth(6, 122);

    QPushButton *btnPrevRight = new QPushButton(QString::fromUtf8("上一页"), recordOverlay);
    btnPrevRight->setObjectName("btnChargeRecordPrevRight");
    btnPrevRight->setGeometry(12, 324, 86, 22);
    btnPrevRight->setStyleSheet(btnPrevLeft->styleSheet());
    connect(btnPrevRight, SIGNAL(clicked()), this, SLOT(showPrevChargeRecordPage()));
    btnPrevRight->hide();

    QPushButton *btnNextRight = new QPushButton(QString::fromUtf8("下一页"), recordOverlay);
    btnNextRight->setObjectName("btnChargeRecordNextRight");
    btnNextRight->setGeometry(104, 324, 86, 22);
    btnNextRight->setStyleSheet(btnPrevLeft->styleSheet());
    connect(btnNextRight, SIGNAL(clicked()), this, SLOT(showNextChargeRecordPage()));
    btnNextRight->hide();
    recordOverlay->show();
}

void RuntimeWindow::refreshFaultRecordCache(bool forceReload)
{
    const uint64_t now = nowMs();
    if (!forceReload && now - m_lastFaultRecordCheckMs < 5000) {
        return;
    }
    m_lastFaultRecordCheckMs = now;

    const QString targetPathAbs = QString::fromUtf8("/mnt/nandflash/data/error.db");
    const QString targetPathRel = QString::fromUtf8("mnt/nandflash/data/error.db");
    const QString fallbackPath = QString::fromUtf8("/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/logger/release/error.db");
    QString dbPath;
    if (QFileInfo(targetPathAbs).exists()) {
        dbPath = targetPathAbs;
    } else if (QFileInfo(targetPathRel).exists()) {
        dbPath = targetPathRel;
    } else {
        dbPath = fallbackPath;
    }
    if (!QFileInfo(dbPath).exists()) {
        return;
    }

    sqlite3 *db = 0;
    if (sqlite3_open(dbPath.toLocal8Bit().constData(), &db) != SQLITE_OK || !db) {
        if (db) {
            sqlite3_close(db);
        }
        return;
    }

    const char *sql =
        "SELECT id,gun,occur_time,fault_message,type "
        "FROM fault_records ORDER BY occur_time DESC,id DESC LIMIT 200;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }

    std::vector<FaultRecordItem> records;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FaultRecordItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.gunNo = sqlite3_column_int(stmt, 1);
        const char *occurTime = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        const char *faultMessage = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        const char *type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        item.occurTime = occurTime ? occurTime : "";
        item.faultName = faultRecordText(faultMessage ? faultMessage : "", type ? type : "");
        records.push_back(item);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    m_faultRecords = records;
}

void RuntimeWindow::refreshFaultRecordTable()
{
    if (!m_faultRecordTable) {
        return;
    }

    QPushButton *btnAll = m_aboutPage->findChild<QPushButton *>("btnFaultRecordAll");
    QPushButton *btnA = m_aboutPage->findChild<QPushButton *>("btnFaultRecordA");
    QPushButton *btnB = m_aboutPage->findChild<QPushButton *>("btnFaultRecordB");
    const QString activeStyle =
        "QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:8px;color:white;font:14px 'MS Shell Dlg 2';font-weight:bold;}";
    const QString normalStyle =
        "QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}";
    if (btnAll) btnAll->setStyleSheet(m_faultGunFilter < 0 ? activeStyle : normalStyle);
    if (btnA) btnA->setStyleSheet(m_faultGunFilter == 0 ? activeStyle : normalStyle);
    if (btnB) btnB->setStyleSheet(m_faultGunFilter == 1 ? activeStyle : normalStyle);

    std::vector<FaultRecordItem> filtered;
    int i = 0;
    for (i = 0; i < static_cast<int>(m_faultRecords.size()); ++i) {
        const FaultRecordItem &item = m_faultRecords[i];
        if (m_faultGunFilter >= 0 && item.gunNo != m_faultGunFilter) {
            continue;
        }
        filtered.push_back(item);
    }

    const int totalPages = filtered.empty() ? 1 : ((static_cast<int>(filtered.size()) - 1) / m_faultRecordPageSize + 1);
    if (m_faultRecordPage >= totalPages) {
        m_faultRecordPage = totalPages - 1;
    }
    if (m_faultRecordPage < 0) {
        m_faultRecordPage = 0;
    }
    if (m_faultRecordPageLabel) {
        m_faultRecordPageLabel->setText(QString::fromUtf8("%1/%2").arg(m_faultRecordPage + 1).arg(totalPages));
    }

    QPushButton *btnPrev = m_aboutPage->findChild<QPushButton *>("btnFaultRecordPrev");
    QPushButton *btnNext = m_aboutPage->findChild<QPushButton *>("btnFaultRecordNext");
    const bool hasPrev = (m_faultRecordPage > 0);
    const bool hasNext = (m_faultRecordPage + 1 < totalPages);
    if (btnPrev) btnPrev->setEnabled(hasPrev);
    if (btnNext) btnNext->setEnabled(hasNext);

    m_faultRecordTable->setRowCount(0);
    const int startIndex = m_faultRecordPage * m_faultRecordPageSize;
    const int endIndex = qMin(startIndex + m_faultRecordPageSize, static_cast<int>(filtered.size()));
    int displayRow = 0;
    for (i = startIndex; i < endIndex; ++i) {
        const FaultRecordItem &item = filtered[i];
        m_faultRecordTable->insertRow(displayRow);
        m_faultRecordTable->setItem(displayRow, 0,
            new QTableWidgetItem(item.gunNo == 0 ? QString::fromUtf8("A枪") : QString::fromUtf8("B枪")));
        m_faultRecordTable->setItem(displayRow, 1, new QTableWidgetItem(ymdhmsText(item.occurTime)));
        m_faultRecordTable->setItem(displayRow, 2, new QTableWidgetItem(item.faultName));
        ++displayRow;
    }
}

void RuntimeWindow::refreshChargeRecordCache(bool forceReload)
{
    const uint64_t now = nowMs();
    if (!forceReload && now - m_lastChargeRecordCheckMs < 5000) {
        return;
    }
    m_lastChargeRecordCheckMs = now;

    const QString targetPathAbs = QString::fromUtf8("/mnt/nandflash/data/chargerecords.db");
    const QString targetPathRel = QString::fromUtf8("mnt/nandflash/data/chargerecords.db");
    const QString fallbackPath = QString::fromUtf8("/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/hmi/Zhongshihua2.0/release/chargerecords.db");
    QString dbPath;
    if (QFileInfo(targetPathAbs).exists()) {
        dbPath = targetPathAbs;
    } else if (QFileInfo(targetPathRel).exists()) {
        dbPath = targetPathRel;
    } else {
        dbPath = fallbackPath;
    }
    if (!QFileInfo(dbPath).exists()) {
        return;
    }

    sqlite3 *db = 0;
    if (sqlite3_open(dbPath.toLocal8Bit().constData(), &db) != SQLITE_OK || !db) {
        if (db) {
            sqlite3_close(db);
        }
        return;
    }

    const char *sql =
        "SELECT id,gun_no,trade_no,charge_start_time,charge_end_time,start_soc,end_soc,total_elect,total_cost,reason "
        "FROM charge_trade_info ORDER BY charge_start_time DESC,id DESC LIMIT 100;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) {
        sqlite3_close(db);
        return;
    }

    std::vector<ChargeRecordItem> records;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChargeRecordItem item;
        item.id = sqlite3_column_int(stmt, 0);
        item.gunNo = sqlite3_column_int(stmt, 1);
        const char *tradeNo = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        const char *startTime = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        const char *endTime = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        item.tradeNo = tradeNo ? tradeNo : "";
        item.startTime = startTime ? startTime : "";
        item.endTime = endTime ? endTime : "";
        item.startSoc = sqlite3_column_double(stmt, 5);
        item.endSoc = sqlite3_column_double(stmt, 6);
        item.totalElect = sqlite3_column_double(stmt, 7);
        item.totalCost = sqlite3_column_double(stmt, 8);
        item.reason = sqlite3_column_int(stmt, 9);
        records.push_back(item);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    m_chargeRecords = records;
}

void RuntimeWindow::refreshChargeRecordTable()
{
    if (!m_chargeRecordTable) {
        return;
    }

    QPushButton *btnAll = m_aboutPage->findChild<QPushButton *>("btnChargeRecordAll");
    QPushButton *btnA = m_aboutPage->findChild<QPushButton *>("btnChargeRecordA");
    QPushButton *btnB = m_aboutPage->findChild<QPushButton *>("btnChargeRecordB");
    const QString activeStyle =
        "QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:8px;color:white;font:14px 'MS Shell Dlg 2';font-weight:bold;}";
    const QString normalStyle =
        "QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}";
    if (btnAll) btnAll->setStyleSheet(m_recordGunFilter < 0 ? activeStyle : normalStyle);
    if (btnA) btnA->setStyleSheet(m_recordGunFilter == 0 ? activeStyle : normalStyle);
    if (btnB) btnB->setStyleSheet(m_recordGunFilter == 1 ? activeStyle : normalStyle);

    std::vector<ChargeRecordItem> filtered;
    int i = 0;
    for (i = 0; i < static_cast<int>(m_chargeRecords.size()); ++i) {
        const ChargeRecordItem &item = m_chargeRecords[i];
        if (m_recordGunFilter >= 0 && item.gunNo != m_recordGunFilter) {
            continue;
        }
        filtered.push_back(item);
    }

    const int totalPages = filtered.empty() ? 1 : ((static_cast<int>(filtered.size()) - 1) / m_chargeRecordPageSize + 1);
    if (m_chargeRecordPage >= totalPages) {
        m_chargeRecordPage = totalPages - 1;
    }
    if (m_chargeRecordPage < 0) {
        m_chargeRecordPage = 0;
    }
    if (m_chargeRecordPageLabel) {
        m_chargeRecordPageLabel->setText(QString::fromUtf8("%1/%2").arg(m_chargeRecordPage + 1).arg(totalPages));
    }

    QPushButton *btnPrevLeft = m_aboutPage->findChild<QPushButton *>("btnChargeRecordPrevLeft");
    QPushButton *btnNextLeft = m_aboutPage->findChild<QPushButton *>("btnChargeRecordNextLeft");
    QPushButton *btnPrevRight = m_aboutPage->findChild<QPushButton *>("btnChargeRecordPrevRight");
    QPushButton *btnNextRight = m_aboutPage->findChild<QPushButton *>("btnChargeRecordNextRight");
    const bool hasPrev = (m_chargeRecordPage > 0);
    const bool hasNext = (m_chargeRecordPage + 1 < totalPages);
    if (btnPrevLeft) btnPrevLeft->setEnabled(hasPrev);
    if (btnPrevRight) btnPrevRight->setEnabled(hasPrev);
    if (btnNextLeft) btnNextLeft->setEnabled(hasNext);
    if (btnNextRight) btnNextRight->setEnabled(hasNext);

    m_chargeRecordTable->setRowCount(0);
    int displayRow = 0;
    const int startIndex = m_chargeRecordPage * m_chargeRecordPageSize;
    const int endIndex = qMin(startIndex + m_chargeRecordPageSize, static_cast<int>(filtered.size()));
    for (i = startIndex; i < endIndex; ++i) {
        const ChargeRecordItem &item = filtered[i];

        m_chargeRecordTable->insertRow(displayRow);
        QTableWidgetItem *idItem = new QTableWidgetItem(QString::number(item.id));
        QTableWidgetItem *gunItem = new QTableWidgetItem(item.gunNo == 0 ? QString::fromUtf8("A枪") : QString::fromUtf8("B枪"));
        QTableWidgetItem *startItem = new QTableWidgetItem(ymdhmsText(item.startTime));
        QTableWidgetItem *endItem = new QTableWidgetItem(ymdhmsText(item.endTime));
        QTableWidgetItem *electItem = new QTableWidgetItem(QString::number(item.totalElect, 'f', 3));
        QTableWidgetItem *costItem = new QTableWidgetItem(QString::number(item.totalCost, 'f', 2));
        QTableWidgetItem *reasonItem = new QTableWidgetItem(chargeRecordReasonText(item));
        m_chargeRecordTable->setItem(displayRow, 0, idItem);
        m_chargeRecordTable->setItem(displayRow, 1, gunItem);
        m_chargeRecordTable->setItem(displayRow, 2, startItem);
        m_chargeRecordTable->setItem(displayRow, 3, endItem);
        m_chargeRecordTable->setItem(displayRow, 4, electItem);
        m_chargeRecordTable->setItem(displayRow, 5, costItem);
        m_chargeRecordTable->setItem(displayRow, 6, reasonItem);
        ++displayRow;
    }
}

void RuntimeWindow::refreshFeeModelCache(bool forceReload)
{
    // BY ZF: 分时模型统一从数据库最新记录读取，空库时保持旧缓存不被覆盖。
    const uint64_t now = nowMs();
    if (!forceReload && now - m_lastFeeModelCheckMs < 3000) {
        return;
    }
    m_lastFeeModelCheckMs = now;

    const QString targetPathAbs = QString::fromUtf8("/mnt/nandflash/data/feemodel.db");
    QString dbPath;
    dbPath = targetPathAbs;
    
    if (!QFileInfo(dbPath).exists()) {
        std::cerr << "[A3] feemodel db not found: " << dbPath.toStdString() << std::endl;
        return;
    }

    sqlite3 *db = 0;
    if (sqlite3_open(dbPath.toLocal8Bit().constData(), &db) != SQLITE_OK || !db) {
        std::cerr << "[A3] open feemodel db failed: " << dbPath.toStdString() << std::endl;
        if (db) {
            sqlite3_close(db);
        }
        return;
    }

    const char *sqlByTime =
        "SELECT feeModelId,timeNum,timeSeg,segFlag,chargeFee,serviceFee,timeStamp "
        "FROM tbFeeModel ORDER BY timeStamp DESC,id DESC LIMIT 1;";
    const char *sqlById =
        "SELECT feeModelId,timeNum,timeSeg,segFlag,chargeFee,serviceFee,timeStamp "
        "FROM tbFeeModel ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt *stmt = 0;
    if (sqlite3_prepare_v2(db, sqlByTime, -1, &stmt, 0) != SQLITE_OK) {
        std::cerr << "[A3] prepare feemodel query by time failed: " << sqlite3_errmsg(db) << std::endl;
        sqlite3_close(db);
        return;
    }

    FeeModelData newModel;
    bool ok = readFeeModelFromStmt(stmt, newModel);
    sqlite3_finalize(stmt);

    if (!ok) {
        if (sqlite3_prepare_v2(db, sqlById, -1, &stmt, 0) == SQLITE_OK) {
            newModel = FeeModelData();
            ok = readFeeModelFromStmt(stmt, newModel);
            sqlite3_finalize(stmt);
        } else {
            std::cerr << "[A3] prepare feemodel query by id failed: " << sqlite3_errmsg(db) << std::endl;
        }
    }

    sqlite3_close(db);

    if (!ok || !newModel.valid) {
        std::cerr << "[A3] feemodel parse empty from db: " << dbPath.toStdString() << std::endl;
        return;
    }

    if (!m_feeModel.valid || m_feeModel.timeStamp != newModel.timeStamp || m_feeModel.feeModelId != newModel.feeModelId) {
        m_feeModel = newModel;
        std::cerr << "[A3] feemodel loaded: " << m_feeModel.feeModelId
                  << " segments=" << m_feeModel.segments.size()
                  << " time=" << m_feeModel.timeStamp << std::endl;
    }
}

void RuntimeWindow::handleMonEvent(const std::string &payload)
{
    // BY ZF: 设备信息页的通信状态与模块版本由 mon 事件缓存驱动。
    cJSON *root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }

    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsObject(data)) {
        data = root;
    }

    QMutexLocker locker(&m_dataMutex);
    m_monStatus.logic.version = getString(data, "logicVersion", m_monStatus.logic.version);
    m_monStatus.logic.buildDate = getString(data, "logicDate", getString(data, "logicBuildDate", m_monStatus.logic.buildDate));
    m_monStatus.meter.version = getString(data, "meterVersion", m_monStatus.meter.version);
    m_monStatus.meter.buildDate = getString(data, "meterDate", getString(data, "meterBuildDate", m_monStatus.meter.buildDate));
    m_monStatus.mon.version = getString(data, "monVersion", m_monStatus.mon.version);
    m_monStatus.mon.buildDate = getString(data, "monDate", getString(data, "monBuildDate", m_monStatus.mon.buildDate));

    m_monStatus.platformComm = getInt(data, "platformComm", getInt(data, "platformLink", m_monStatus.platformComm));
    m_monStatus.meterComm = getInt(data, "meterComm", getInt(data, "meterLink", m_monStatus.meterComm));
    m_monStatus.controllerComm = getInt(data, "controllerComm", getInt(data, "mainCtrlComm", m_monStatus.controllerComm));
    m_monStatus.activeFaultCount = getInt(data, "faultCount", getInt(data, "activeFaultCount", m_monStatus.activeFaultCount));
    m_monStatus.latestFaultName = getString(data, "latestFaultName", getString(data, "faultName", m_monStatus.latestFaultName));
    m_monStatus.latestFaultTime = getString(data, "latestFaultTime", getString(data, "faultTime", m_monStatus.latestFaultTime));
    m_monStatus.lastEventTime = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString();

    cJSON_Delete(root);
    QMetaObject::invokeMethod(this, "refreshUi", Qt::QueuedConnection);
}

void RuntimeWindow::refreshUi()
{
    feedWatchdog();
    // BY ZF: 统一在 UI 线程做页面选择和控件刷新，避免多线程直接改界面。
    std::vector<GunUiData> guns;
    int focusGun = 0;
    bool showAbout = false;
    bool forceIdleView = false;
    const QString nowText = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss");
    {
        QMutexLocker locker(&m_dataMutex);
        guns = m_guns;
        focusGun = m_focusGun;
        showAbout = m_showAbout;
        forceIdleView = m_forceIdleView;
        const bool platformOnline = m_platformOnline;
        m_bottomHostState->setPixmap(QPixmap(platformOnline ? ":/cui/Resources/host_yes.png"
                                                            : ":/cui/Resources/host_not.png"));
    }
    m_bottomTime->setText(nowText);
    m_bottomTime->raise();
    m_bottomHostState->raise();
    if (focusGun < 0 || focusGun >= static_cast<int>(guns.size())) {
        focusGun = 0;
    }

    if (showAbout) {
        refreshAboutPage();
        m_stack->setCurrentWidget(m_aboutPage);
        return;
    }

    if (forceIdleView) {
        applyIdleLayout();
        refreshIdlePage(guns);
        m_stack->setCurrentWidget(m_idlePage);
        return;
    }

    PageId page = decidePage(guns, focusGun);
    {
        QMutexLocker locker(&m_dataMutex);
        m_focusGun = focusGun;
    }

    if (page == PageIdle) {
        applyIdleLayout();
        refreshIdlePage(guns);
        m_stack->setCurrentWidget(m_idlePage);
    } else if (page == PageAuthorize) {
        refreshAuthorizePage(guns[focusGun]);
        m_stack->setCurrentWidget(m_authorizePage);
    } else if (page == PageCharging) {
        refreshChargingPage(guns[focusGun]);
        m_stack->setCurrentWidget(m_chargingPage);
    } else if (page == PageCheckout) {
        refreshCheckoutPage(guns[focusGun]);
        m_stack->setCurrentWidget(m_checkoutPage);
    }
}

void RuntimeWindow::feedWatchdog()
{
    const uint64_t now = nowMs();
    if (m_lastWatchdogFeedMs != 0 && (now - m_lastWatchdogFeedMs) < 5000ULL) {
        return;
    }

    // BY ZF: 通过守护进程看门狗消息队列上报 tcu_hmi 存活状态。
    static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    static int queueReady = -1;
    if (queueReady == -1) {
        queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
    }
    if (queueReady == 1) {
        const char* processName = "tcu_hmi";
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
        m_lastWatchdogFeedMs = now;
    }
}

void RuntimeWindow::showAboutPage()
{
    markScreenActivity();
    {
        QMutexLocker locker(&m_dataMutex);
        m_showAbout = true;
    }
    refreshFeeModelCache(true);
    refreshFaultRecordCache(true);
    refreshFaultRecordTable();
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
    setRestrictedAboutTabsEnabled(m_aboutTabWidget, m_aboutPermissionGranted);
    if (m_aboutTabWidget && m_aboutTabWidget->currentIndex() < 0) {
        m_aboutTabWidget->setCurrentIndex(0);
    }
    m_stack->setCurrentWidget(m_aboutPage);
}

void RuntimeWindow::leaveAboutPage()
{
    markScreenActivity();
    {
        QMutexLocker locker(&m_dataMutex);
        m_showAbout = false;
        m_aboutPermissionGranted = false;
    }
    if (m_aboutPermissionEdit) {
        m_aboutPermissionEdit->clear();
    }
    if (m_aboutPermissionHint) {
        m_aboutPermissionHint->setText(QString::fromUtf8("未授权"));
        m_aboutPermissionHint->setStyleSheet("QLabel{color:rgb(160,60,60);background:transparent;font:14px 'MS Shell Dlg 2';font-weight:bold;}");
    }
    if (m_aboutPermissionPad) {
        m_aboutPermissionPad->hide();
    }
    if (m_configKeyboard) {
        m_configKeyboard->hide();
    }
    setRestrictedAboutTabsEnabled(m_aboutTabWidget, false);
    if (m_aboutTabWidget) {
        m_aboutTabWidget->setCurrentIndex(0);
    }
    refreshUi();
}

void RuntimeWindow::submitAboutPermission()
{
    markScreenActivity();
    QObject *src = sender();
    QPushButton *btn = qobject_cast<QPushButton *>(src);
    if (btn && m_aboutPermissionEdit && btn->objectName().startsWith("btnPad_")) {
        const QString key = btn->text();
        if (key == QString::fromUtf8("清空")) {
            m_aboutPermissionEdit->clear();
        } else if (key == QString::fromUtf8("退格")) {
            QString text = m_aboutPermissionEdit->text();
            if (!text.isEmpty()) {
                text.chop(1);
                m_aboutPermissionEdit->setText(text);
            }
        } else if (m_aboutPermissionEdit->text().size() < 4) {
            m_aboutPermissionEdit->setText(m_aboutPermissionEdit->text() + key);
        }
        return;
    }

    const QString password = m_aboutPermissionEdit ? m_aboutPermissionEdit->text().trimmed() : QString();
    if (password == QString::fromUtf8("6186")) {
        m_aboutPermissionGranted = true;
        setRestrictedAboutTabsEnabled(m_aboutTabWidget, true);
        if (m_aboutPermissionHint) {
            m_aboutPermissionHint->setText(QString::fromUtf8("验证通过"));
            m_aboutPermissionHint->setStyleSheet("QLabel{color:rgb(32,132,82);background:transparent;font:14px 'MS Shell Dlg 2';font-weight:bold;}");
        }
        if (m_aboutPermissionEdit) {
            m_aboutPermissionEdit->clear();
        }
        if (m_aboutPermissionPad) {
            m_aboutPermissionPad->hide();
        }
        return;
    }

    m_aboutPermissionGranted = false;
    setRestrictedAboutTabsEnabled(m_aboutTabWidget, false);
    if (m_aboutPermissionHint) {
        m_aboutPermissionHint->setText(QString::fromUtf8("密码错误"));
        m_aboutPermissionHint->setStyleSheet("QLabel{color:rgb(160,60,60);background:transparent;font:14px 'MS Shell Dlg 2';font-weight:bold;}");
    }
}

void RuntimeWindow::showAboutPermissionPad()
{
    markScreenActivity();
    std::cerr << "[A3] showAboutPermissionPad clicked" << std::endl;
    if (m_aboutPermissionPad) {
        m_aboutPermissionPad->show();
        m_aboutPermissionPad->raise();
        std::cerr << "[A3] permission pad visible="
                  << (m_aboutPermissionPad->isVisible() ? 1 : 0)
                  << " geometry="
                  << m_aboutPermissionPad->x() << ","
                  << m_aboutPermissionPad->y() << ","
                  << m_aboutPermissionPad->width() << ","
                  << m_aboutPermissionPad->height() << std::endl;
    }
    if (m_aboutPermissionEdit) {
        m_aboutPermissionEdit->setFocus();
    }
}

void RuntimeWindow::handleAboutTabChanged(int index)
{
    markScreenActivity();
    if (!m_aboutTabWidget) {
        return;
    }

    QWidget *currentTab = m_aboutTabWidget->widget(index);
    if (!currentTab) {
        return;
    }

    const QString tabName = currentTab->objectName();
    const bool restricted = (tabName == QString::fromUtf8("tab_10") || tabName == QString::fromUtf8("tab_4"));
    if (!restricted || m_aboutPermissionGranted) {
        if (tabName == QString::fromUtf8("tab_10")) {
            scanExternalStorage();
        }
        if (tabName == QString::fromUtf8("tab_4")) {
            loadMeterConfigToUi();
            loadCommConfigToUi();
        }
        if (tabName != QString::fromUtf8("tab_4") && m_configKeyboard) {
            m_configKeyboard->hide();
        }
        return;
    }

    QWidget *permissionTab = m_aboutPage ? m_aboutPage->findChild<QWidget *>("tab_6") : 0;
    const int permissionIndex = permissionTab ? m_aboutTabWidget->indexOf(permissionTab) : -1;
    if (permissionIndex >= 0 && permissionIndex != index) {
        m_aboutTabWidget->setCurrentIndex(permissionIndex);
    }
    if (m_aboutPermissionHint) {
        m_aboutPermissionHint->setText(QString::fromUtf8("请先输入密码获取权限"));
        m_aboutPermissionHint->setStyleSheet("QLabel{color:rgb(160,60,60);background:transparent;font:14px 'MS Shell Dlg 2';font-weight:bold;}");
    }
}

void RuntimeWindow::setupConfigTab()
{
    if (!m_aboutPage || !m_aboutTabWidget) {
        return;
    }

    QWidget *configTab = m_aboutPage->findChild<QWidget *>("tab_4");
    if (!configTab) {
        return;
    }

    QList<QWidget *> tabChildren;
    const QObjectList tabObjects = configTab->children();
    int i = 0;
    for (i = 0; i < tabObjects.size(); ++i) {
        QWidget *child = qobject_cast<QWidget *>(tabObjects.at(i));
        if (child && child->parent() == configTab) {
            tabChildren.append(child);
        }
    }
    for (i = 0; i < tabChildren.size(); ++i) {
        tabChildren.at(i)->hide();
    }

    QWidget *configOverlay = configTab->findChild<QWidget *>("configOverlay");
    if (configOverlay) {
        return;
    }

    configOverlay = new QWidget(configTab);
    configOverlay->setObjectName("configOverlay");
    configOverlay->setGeometry(0, 0, 755, 345);
    configOverlay->setStyleSheet("background:transparent;");

    QFrame *leftCard = static_cast<QFrame *>(makeInfoCard(configOverlay, QString::fromUtf8("参数配置"), QRect(12, 10, 300, 314)));
    QFrame *rightCard = static_cast<QFrame *>(makeInfoCard(configOverlay, QString(), QRect(320, 10, 423, 314)));

    QLabel *labelPileNo = new QLabel(QString::fromUtf8("充电桩号"), leftCard);
    labelPileNo->setGeometry(20, 48, 96, 28);
    labelPileNo->setStyleSheet("QLabel{color:rgb(38,45,52);background:transparent;font:15px 'MS Shell Dlg 2';font-weight:bold;}");

    m_configPileNoEdit = new QLineEdit(leftCard);
    m_configPileNoEdit->setGeometry(112, 46, 172, 34);
    m_configPileNoEdit->setReadOnly(true);
    m_configPileNoEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_configPileNoEdit->setStyleSheet("QLineEdit{background:white;border:1px solid rgb(160,160,160);border-radius:6px;color:rgb(38,45,52);font:15px 'MS Shell Dlg 2';padding:2px 8px;}");

    QPushButton *pileMask = new QPushButton(leftCard);
    pileMask->setObjectName("btnConfigEdit_pile");
    pileMask->setGeometry(m_configPileNoEdit->geometry());
    pileMask->setFlat(true);
    pileMask->setStyleSheet("QPushButton{background:transparent;border:none;}");
    connect(pileMask, SIGNAL(clicked()), this, SLOT(onConfigEditMaskClicked()));

    QLabel *labelGunCount = new QLabel(QString::fromUtf8("枪数"), leftCard);
    labelGunCount->setGeometry(20, 94, 96, 28);
    labelGunCount->setStyleSheet(labelPileNo->styleSheet());

    m_configGunSingle = new QRadioButton(QString::fromUtf8("单枪"), leftCard);
    m_configGunSingle->setObjectName("radioConfigSingle");
    m_configGunSingle->setGeometry(114, 94, 72, 28);
    m_configGunSingle->setStyleSheet("QRadioButton{color:rgb(38,45,52);font:15px 'MS Shell Dlg 2';font-weight:bold;background:transparent;}");
    connect(m_configGunSingle, SIGNAL(clicked()), this, SLOT(onConfigGunCountChanged()));

    m_configGunDual = new QRadioButton(QString::fromUtf8("双枪"), leftCard);
    m_configGunDual->setObjectName("radioConfigDual");
    m_configGunDual->setGeometry(198, 94, 72, 28);
    m_configGunDual->setStyleSheet(m_configGunSingle->styleSheet());
    connect(m_configGunDual, SIGNAL(clicked()), this, SLOT(onConfigGunCountChanged()));
    if (m_gunCount <= 1) {
        m_configGunSingle->setChecked(true);
    } else {
        m_configGunDual->setChecked(true);
    }

    QLabel *labelSecret = new QLabel(QString::fromUtf8("充电桩密钥"), leftCard);
    labelSecret->setGeometry(20, 140, 96, 28);
    labelSecret->setStyleSheet(labelPileNo->styleSheet());

    m_configSecretEdit = new QLineEdit(leftCard);
    m_configSecretEdit->setGeometry(112, 138, 172, 34);
    m_configSecretEdit->setReadOnly(true);
    m_configSecretEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_configSecretEdit->setStyleSheet(m_configPileNoEdit->styleSheet());
    m_configSecretEdit->setText(QString::fromUtf8("xMa3mXJG"));

    QPushButton *secretMask = new QPushButton(leftCard);
    secretMask->setObjectName("btnConfigEdit_secret");
    secretMask->setGeometry(m_configSecretEdit->geometry());
    secretMask->setFlat(true);
    secretMask->setStyleSheet("QPushButton{background:transparent;border:none;}");
    connect(secretMask, SIGNAL(clicked()), this, SLOT(onConfigEditMaskClicked()));

    QLabel *labelAddr1 = new QLabel(QString::fromUtf8("电表地址1"), leftCard);
    labelAddr1->setGeometry(20, 186, 96, 28);
    labelAddr1->setStyleSheet(labelPileNo->styleSheet());

    m_configMeterAddr1Edit = new QLineEdit(leftCard);
    m_configMeterAddr1Edit->setGeometry(112, 184, 172, 34);
    m_configMeterAddr1Edit->setReadOnly(true);
    m_configMeterAddr1Edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_configMeterAddr1Edit->setStyleSheet(m_configPileNoEdit->styleSheet());

    QPushButton *addr1Mask = new QPushButton(leftCard);
    addr1Mask->setObjectName("btnConfigEdit_meter1");
    addr1Mask->setGeometry(m_configMeterAddr1Edit->geometry());
    addr1Mask->setFlat(true);
    addr1Mask->setStyleSheet("QPushButton{background:transparent;border:none;}");
    connect(addr1Mask, SIGNAL(clicked()), this, SLOT(onConfigEditMaskClicked()));

    m_configMeter2Row = new QWidget(leftCard);
    m_configMeter2Row->setObjectName("configMeter2Row");
    m_configMeter2Row->setGeometry(0, 226, 300, 44);
    m_configMeter2Row->setStyleSheet("background:transparent;");

    QLabel *labelAddr2 = new QLabel(QString::fromUtf8("电表地址2"), m_configMeter2Row);
    labelAddr2->setGeometry(20, 6, 96, 28);
    labelAddr2->setStyleSheet(labelPileNo->styleSheet());

    m_configMeterAddr2Edit = new QLineEdit(m_configMeter2Row);
    m_configMeterAddr2Edit->setGeometry(112, 4, 172, 34);
    m_configMeterAddr2Edit->setReadOnly(true);
    m_configMeterAddr2Edit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_configMeterAddr2Edit->setStyleSheet(m_configPileNoEdit->styleSheet());

    QPushButton *addr2Mask = new QPushButton(m_configMeter2Row);
    addr2Mask->setObjectName("btnConfigEdit_meter2");
    addr2Mask->setGeometry(m_configMeterAddr2Edit->geometry());
    addr2Mask->setFlat(true);
    addr2Mask->setStyleSheet("QPushButton{background:transparent;border:none;}");
    connect(addr2Mask, SIGNAL(clicked()), this, SLOT(onConfigEditMaskClicked()));

    QPushButton *btnReset = new QPushButton(QString::fromUtf8("重置"), leftCard);
    btnReset->setGeometry(18, 274, 116, 28);
    btnReset->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:8px;color:rgb(38,45,52);font:14px 'MS Shell Dlg 2';font-weight:bold;}");
    connect(btnReset, SIGNAL(clicked()), this, SLOT(onConfigResetClicked()));

    QPushButton *btnSubmit = new QPushButton(QString::fromUtf8("确认"), leftCard);
    btnSubmit->setGeometry(168, 274, 116, 28);
    btnSubmit->setStyleSheet("QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:8px;color:white;font:14px 'MS Shell Dlg 2';font-weight:bold;}");
    connect(btnSubmit, SIGNAL(clicked()), this, SLOT(onConfigSubmitClicked()));

    m_configHintLabel = new QLabel(QString(), leftCard);
    m_configHintLabel->setGeometry(112, 230, 172, 22);
    m_configHintLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_configHintLabel->setStyleSheet("QLabel{color:rgb(160,100,40);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");
    m_configHintLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    if (m_configMeter2Row) {
        m_configMeter2Row->raise();
    }

    m_configKeyboard = new QFrame(rightCard);
    m_configKeyboard->setObjectName("configKeyboard");
    m_configKeyboard->setGeometry(10, 16, 403, 286);
    m_configKeyboard->setStyleSheet("QFrame{background:rgb(236,238,239);border:1px solid rgb(160,160,160);border-radius:10px;}");

    for (i = 0; i < 30; ++i) {
        QPushButton *key = new QPushButton(m_configKeyboard);
        key->setObjectName(QString::fromUtf8("btnConfigKey_%1").arg(i));
        key->setGeometry(10 + (i % 10) * 39, 12 + (i / 10) * 52, 35, 42);
        key->setStyleSheet("QPushButton{background:white;border:1px solid rgb(150,150,150);border-radius:5px;color:rgb(38,45,52);font:17px 'MS Shell Dlg 2';font-weight:bold;}"
                           "QPushButton:pressed{background:rgb(214,217,219);}");
        connect(key, SIGNAL(clicked()), this, SLOT(onConfigKeyboardButtonClicked()));
        m_configKeyButtons[i] = key;
    }

    m_configShiftButton = new QPushButton(QString::fromUtf8("Shift"), m_configKeyboard);
    m_configShiftButton->setObjectName("btnConfigShift");
    m_configShiftButton->setGeometry(10, 220, 74, 42);
    m_configShiftButton->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:5px;color:rgb(38,45,52);font:15px 'MS Shell Dlg 2';font-weight:bold;}");
    connect(m_configShiftButton, SIGNAL(clicked()), this, SLOT(onConfigKeyboardButtonClicked()));

    m_configModeButton = new QPushButton(QString::fromUtf8("123"), m_configKeyboard);
    m_configModeButton->setObjectName("btnConfigMode");
    m_configModeButton->setGeometry(92, 220, 74, 42);
    m_configModeButton->setStyleSheet(m_configShiftButton->styleSheet());
    connect(m_configModeButton, SIGNAL(clicked()), this, SLOT(onConfigKeyboardButtonClicked()));

    m_configClearButton = new QPushButton(QString::fromUtf8("清空"), m_configKeyboard);
    m_configClearButton->setObjectName("btnConfigClear");
    m_configClearButton->setGeometry(174, 220, 74, 42);
    m_configClearButton->setStyleSheet(m_configShiftButton->styleSheet());
    connect(m_configClearButton, SIGNAL(clicked()), this, SLOT(onConfigKeyboardButtonClicked()));

    m_configBackspaceButton = new QPushButton(QString::fromUtf8("退格"), m_configKeyboard);
    m_configBackspaceButton->setObjectName("btnConfigBackspace");
    m_configBackspaceButton->setGeometry(256, 220, 74, 42);
    m_configBackspaceButton->setStyleSheet(m_configShiftButton->styleSheet());
    connect(m_configBackspaceButton, SIGNAL(clicked()), this, SLOT(onConfigKeyboardButtonClicked()));

    m_configConfirmButton = new QPushButton(QString::fromUtf8("完成"), m_configKeyboard);
    m_configConfirmButton->setObjectName("btnConfigFinish");
    m_configConfirmButton->setGeometry(338, 220, 54, 42);
    m_configConfirmButton->setStyleSheet("QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:5px;color:white;font:15px 'MS Shell Dlg 2';font-weight:bold;}");
    connect(m_configConfirmButton, SIGNAL(clicked()), this, SLOT(onConfigKeyboardButtonClicked()));

    onConfigGunCountChanged();
    refreshConfigKeyboardLayout();
    configOverlay->show();
}

void RuntimeWindow::setupExternalStorageTab()
{
    if (!m_aboutPage) {
        return;
    }

    QWidget *storageTab = m_aboutPage->findChild<QWidget *>("tab_10");
    if (!storageTab) {
        return;
    }

    QListWidget *storageList = storageTab->findChild<QListWidget *>("listWidget_storage");
    QListWidget *fileList = storageTab->findChild<QListWidget *>("listWidget_files");
    QAbstractButton *btnRefresh = storageTab->findChild<QAbstractButton *>("btn_filebrowser_refresh");
    QAbstractButton *btnExport = storageTab->findChild<QAbstractButton *>("btn_export_log");
    QAbstractButton *btnUpgrade = storageTab->findChild<QAbstractButton *>("btn_upgrade");
    QAbstractButton *btnBack = storageTab->findChild<QAbstractButton *>("btn_filebrowser_back");
    if (!storageList || !fileList || !btnRefresh || !btnExport || !btnUpgrade || !btnBack) {
        std::cerr << "[A3] setupExternalStorageTab missing widgets"
                  << " storageList=" << (storageList ? 1 : 0)
                  << " fileList=" << (fileList ? 1 : 0)
                  << " refresh=" << (btnRefresh ? 1 : 0)
                  << " export=" << (btnExport ? 1 : 0)
                  << " upgrade=" << (btnUpgrade ? 1 : 0)
                  << " back=" << (btnBack ? 1 : 0)
                  << std::endl;
        return;
    }

    storageList->disconnect(this);
    fileList->disconnect(this);
    btnRefresh->disconnect(this);
    btnExport->disconnect(this);
    btnUpgrade->disconnect(this);
    btnBack->disconnect(this);

    connect(storageList, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(onStorageItemClicked(QListWidgetItem*)));
    connect(storageList, SIGNAL(itemPressed(QListWidgetItem*)), this, SLOT(onStorageItemClicked(QListWidgetItem*)));
    connect(fileList, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(onStorageFileItemDoubleClicked(QListWidgetItem*)));
    connect(btnRefresh, SIGNAL(clicked()), this, SLOT(onStorageRefreshClicked()));
    connect(btnExport, SIGNAL(clicked()), this, SLOT(onStorageExportLogClicked()));
    connect(btnUpgrade, SIGNAL(clicked()), this, SLOT(onStorageUpgradeClicked()));
    connect(btnBack, SIGNAL(clicked()), this, SLOT(onStorageBackClicked()));

    storageList->setAlternatingRowColors(true);
    fileList->setAlternatingRowColors(true);
    storageList->setSelectionMode(QAbstractItemView::SingleSelection);
    storageList->setItemDelegate(new StorageCheckboxDelegate(storageList));
    storageList->setStyleSheet("QListWidget{background:rgb(244,246,247);alternate-background-color:rgb(236,238,239);border:1px solid rgb(168,168,168);color:rgb(38,45,52);font:18px 'MS Shell Dlg 2';}"
                               "QListWidget::item{height:46px;padding-left:8px;}"
                               "QListWidget::item:selected{background:rgb(72,148,196);color:white;}");
    fileList->setStyleSheet(storageList->styleSheet());
    scanExternalStorage();
}

void RuntimeWindow::scanExternalStorage()
{
    if (!m_aboutPage) {
        return;
    }

    QListWidget *storageList = m_aboutPage->findChild<QListWidget *>("listWidget_storage");
    QListWidget *fileList = m_aboutPage->findChild<QListWidget *>("listWidget_files");
    QLabel *pathLabel = m_aboutPage->findChild<QLabel *>("label_filebrowser_path");
    if (!storageList || !fileList || !pathLabel) {
        return;
    }

    storageList->clear();
    fileList->clear();
    m_storageCurrentPath.clear();
    pathLabel->setText(QString::fromUtf8("当前路径: "));

    QDir mediaDir(QString::fromUtf8("/run/media"));
    if (!mediaDir.exists()) {
        std::cerr << "[A3] storage scan: /run/media missing" << std::endl;
        storageList->addItem(QString::fromUtf8("未找到/run/media目录"));
        return;
    }

    const QStringList entries = mediaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    std::cerr << "[A3] storage scan entries=" << entries.size() << std::endl;
    const QStringList excludeList = QStringList()
        << QString::fromUtf8("mmcblk1p1")
        << QString::fromUtf8("mmcblk1p3")
        << QString::fromUtf8("mmcblk1p4");

    bool found = false;
    int i = 0;
    for (i = 0; i < entries.size(); ++i) {
        const QString entry = entries.at(i);
        bool shouldExclude = false;
        int j = 0;
        for (j = 0; j < excludeList.size(); ++j) {
            if (entry.contains(excludeList.at(j))) {
                shouldExclude = true;
                break;
            }
        }
        if (shouldExclude) {
            continue;
        }

        QListWidgetItem *item = new QListWidgetItem(entry, storageList);
        item->setData(Qt::UserRole, mediaDir.absoluteFilePath(entry));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Unchecked);
        std::cerr << "[A3] storage add " << mediaDir.absoluteFilePath(entry).toLocal8Bit().constData() << std::endl;
        found = true;
    }

    if (!found) {
        std::cerr << "[A3] storage scan: no external storage found" << std::endl;
        storageList->addItem(QString::fromUtf8("未找到外置存储设备"));
    }
}

void RuntimeWindow::loadStorageFileList(const QString &path)
{
    if (!m_aboutPage) {
        return;
    }

    QListWidget *fileList = m_aboutPage->findChild<QListWidget *>("listWidget_files");
    QLabel *pathLabel = m_aboutPage->findChild<QLabel *>("label_filebrowser_path");
    if (!fileList || !pathLabel) {
        return;
    }

    fileList->clear();
    m_storageCurrentPath = path;
    pathLabel->setText(QString::fromUtf8("当前路径: ") + path);

    QDir dir(path);
    if (!dir.exists()) {
        fileList->addItem(QString::fromUtf8("路径不存在"));
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
    int i = 0;
    for (i = 0; i < entries.size(); ++i) {
        const QFileInfo info = entries.at(i);
        QString displayName = info.fileName();
        if (info.isDir()) {
            displayName = QString::fromUtf8("[目录] ") + displayName;
        } else {
            const qint64 size = info.size();
            QString sizeStr;
            if (size < 1024) {
                sizeStr = QString::number(size) + QString::fromUtf8(" B");
            } else if (size < 1024 * 1024) {
                sizeStr = QString::number(size / 1024.0, 'f', 2) + QString::fromUtf8(" KB");
            } else {
                sizeStr = QString::number(size / (1024.0 * 1024.0), 'f', 2) + QString::fromUtf8(" MB");
            }
            displayName += QString::fromUtf8(" (") + sizeStr + QString::fromUtf8(")");
        }

        QListWidgetItem *item = new QListWidgetItem(displayName, fileList);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setData(Qt::UserRole + 1, info.isDir() ? 1 : 0);
    }
}

QString RuntimeWindow::selectedStoragePath() const
{
    if (!m_aboutPage) {
        return QString();
    }

    QListWidget *storageList = m_aboutPage->findChild<QListWidget *>("listWidget_storage");
    if (!storageList) {
        return QString();
    }

    int i = 0;
    for (i = 0; i < storageList->count(); ++i) {
        QListWidgetItem *item = storageList->item(i);
        if (item && item->checkState() == Qt::Checked) {
            return item->data(Qt::UserRole).toString();
        }
    }

    QListWidgetItem *current = storageList->currentItem();
    if (current) {
        return current->data(Qt::UserRole).toString();
    }
    return QString();
}

void RuntimeWindow::onStorageItemClicked(QListWidgetItem *item)
{
    markScreenActivity();
    if (!item) {
        return;
    }

    item->setSelected(true);
    item->setCheckState(Qt::Checked);
    QListWidget *storageList = m_aboutPage ? m_aboutPage->findChild<QListWidget *>("listWidget_storage") : 0;
    if (storageList) {
        int i = 0;
        for (i = 0; i < storageList->count(); ++i) {
            QListWidgetItem *other = storageList->item(i);
            if (other && other != item && (other->flags() & Qt::ItemIsUserCheckable)) {
                other->setCheckState(Qt::Unchecked);
            }
        }
    }
    const QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        loadStorageFileList(path);
    }
}

void RuntimeWindow::onStorageFileItemDoubleClicked(QListWidgetItem *item)
{
    markScreenActivity();
    if (!item) {
        return;
    }
    if (item->data(Qt::UserRole + 1).toInt() != 1) {
        return;
    }
    const QString path = item->data(Qt::UserRole).toString();
    if (!path.isEmpty()) {
        loadStorageFileList(path);
    }
}

void RuntimeWindow::onStorageBackClicked()
{
    markScreenActivity();
    if (m_storageCurrentPath.isEmpty()) {
        return;
    }

    QDir dir(m_storageCurrentPath);
    if (!dir.cdUp()) {
        return;
    }

    const QString parentPath = dir.absolutePath();
    if (parentPath == QString::fromUtf8("/run/media")) {
        m_storageCurrentPath.clear();
        scanExternalStorage();
        return;
    }
    loadStorageFileList(parentPath);
}

void RuntimeWindow::onStorageRefreshClicked()
{
    markScreenActivity();
    if (!m_storageCurrentPath.isEmpty()) {
        loadStorageFileList(m_storageCurrentPath);
    } else {
        scanExternalStorage();
    }
}

void RuntimeWindow::onStorageExportLogClicked()
{
    markScreenActivity();
    QAbstractButton *btnExport = m_aboutPage ? m_aboutPage->findChild<QAbstractButton *>("btn_export_log") : 0;
    if (btnExport && !btnExport->isEnabled()) {
        return;
    }

    const QString targetPath = selectedStoragePath();
    if (targetPath.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先选择一个外置存储设备"));
        return;
    }
    if (!QDir(QString::fromUtf8("/mnt/nandflash/data")).exists()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("日志目录不存在: /mnt/nandflash/data"));
        return;
    }
    if (!QDir(targetPath).exists()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("选中的存储设备路径不存在"));
        return;
    }

    const QString pileId = QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo);
    const QString fileName = QString::fromUtf8("data_%1.tar.gz").arg(pileId.isEmpty() ? QString::fromUtf8("tcu") : pileId);
    const QString tarFilePath = QDir(targetPath).absoluteFilePath(fileName);
    if (QFile::exists(tarFilePath)) {
        QFile::remove(tarFilePath);
    }

    qint64 sourceTotalSize = 0;
    QProcess sizeProcess;
    sizeProcess.start(QString::fromUtf8("du"),
                      QStringList() << QString::fromUtf8("-sb") << QString::fromUtf8("/mnt/nandflash/data"));
    if (sizeProcess.waitForFinished(3000)) {
        const QString output = QString::fromLocal8Bit(sizeProcess.readAllStandardOutput()).trimmed();
        const QStringList parts = output.split(QChar('\t'));
        if (!parts.isEmpty()) {
            bool ok = false;
            sourceTotalSize = parts.at(0).toLongLong(&ok);
            if (!ok) {
                sourceTotalSize = 0;
            }
        }
    }

    qint64 pid = 0;
    const bool started = QProcess::startDetached(QString::fromUtf8("sh"),
                                                 QStringList() << QString::fromUtf8("-c")
                                                               << QString::fromUtf8("tar -czf \"%1\" -C /mnt/nandflash data && sync").arg(tarFilePath),
                                                 QDir::rootPath(),
                                                 &pid);
    if (!started || pid == 0) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("无法启动导出任务"));
        return;
    }

    if (btnExport) {
        btnExport->setEnabled(false);
    }

    ExportProgressDlg progressDlg(this);
    progressDlg.setExportInfo(pid, tarFilePath, targetPath, sourceTotalSize);
    progressDlg.exec();

    if (btnExport) {
        btnExport->setEnabled(true);
    }

    if (!m_storageCurrentPath.isEmpty()) {
        loadStorageFileList(m_storageCurrentPath);
    }
}

void RuntimeWindow::onStorageUpgradeClicked()
{
    markScreenActivity();
    const QString targetPath = selectedStoragePath();
        if (targetPath.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请先选择一个外置存储设备"));
        return;
    }
    const QString installPackagePath = QDir(targetPath).absoluteFilePath(QString::fromUtf8("install.tar.gz"));
    QFileInfo packageFile(installPackagePath);
    if (!packageFile.exists()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("未找到升级包 install.tar.gz"));
        return;
    }
    if (!packageFile.isReadable()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("升级包不可读"));
        return;
    }

    const int ret = QMessageBox::question(this, QString::fromUtf8("确认"),
                                          QString::fromUtf8("确认执行升级？\n%1").arg(installPackagePath),
                                          QMessageBox::Yes | QMessageBox::No,
                                          QMessageBox::No);
    if (ret != QMessageBox::Yes) {
        return;
    }

    MessageQueue systemQueue(MSG_KEY_SYSTEM);
    if (!systemQueue.open()) {
        QMessageBox::critical(this, QString::fromUtf8("错误"), QString::fromUtf8("无法打开系统消息队列"));
        return;
    }

    const QByteArray pathBytes = installPackagePath.toLocal8Bit();
    const int sendResult = systemQueue.send(MSG_UPDATE_REQUEST, pathBytes.constData(), pathBytes.size());
    if (sendResult != 0) {
        QMessageBox::critical(this, QString::fromUtf8("错误"), QString::fromUtf8("发送升级请求失败"));
        return;
    }

    QMessageBox::information(this, QString::fromUtf8("提示"),
                             QString::fromUtf8("升级请求已发送\n%1").arg(installPackagePath));
}

void RuntimeWindow::openConfigKeyboard(QLineEdit *target, bool numericOnly)
{
    if (!target || !m_configKeyboard) {
        return;
    }
    m_configKeyboardTarget = target;
    m_configKeyboardNumericOnly = numericOnly;
    m_configKeyboardNumberMode = numericOnly;
    if (numericOnly) {
        m_configKeyboardUppercase = false;
    }
    refreshConfigKeyboardLayout();
    m_configKeyboard->show();
    m_configKeyboard->raise();
}

void RuntimeWindow::refreshConfigKeyboardLayout()
{
    static const char *upperKeys[26] = {
        "Q","W","E","R","T","Y","U","I","O","P",
        "A","S","D","F","G","H","J","K","L",
        "Z","X","C","V","B","N","M"
    };
    static const char *lowerKeys[26] = {
        "q","w","e","r","t","y","u","i","o","p",
        "a","s","d","f","g","h","j","k","l",
        "z","x","c","v","b","n","m"
    };
    static const char *numberKeys[10] = {
        "1","2","3","4","5","6","7","8","9","0"
    };

    int i = 0;
    for (i = 0; i < 30; ++i) {
        if (!m_configKeyButtons[i]) {
            continue;
        }
        QString text;
        bool visible = false;
        if (m_configKeyboardNumberMode) {
            if (i < 10) {
                text = QString::fromLatin1(numberKeys[i]);
                visible = !text.isEmpty();
            }
        } else {
            if (i < 26) {
                text = QString::fromLatin1(m_configKeyboardUppercase ? upperKeys[i] : lowerKeys[i]);
                visible = true;
            }
        }
        m_configKeyButtons[i]->setText(text);
        m_configKeyButtons[i]->setVisible(visible);
        m_configKeyButtons[i]->setEnabled(visible);
        if (m_configKeyboardNumberMode) {
            if (visible) {
                int x = 0;
                int y = 0;
                if (i < 9) {
                    x = 56 + (i % 3) * 108;
                    y = 16 + (i / 3) * 54;
                } else {
                    x = 164;
                    y = 178;
                }
                m_configKeyButtons[i]->setGeometry(x, y, 92, 46);
            }
        } else {
            m_configKeyButtons[i]->setGeometry(10 + (i % 10) * 39, 12 + (i / 10) * 52, 35, 42);
        }
    }

    if (m_configShiftButton) {
        m_configShiftButton->setVisible(!m_configKeyboardNumberMode && !m_configKeyboardNumericOnly);
        m_configShiftButton->setText(m_configKeyboardUppercase ? QString::fromUtf8("abc") : QString::fromUtf8("ABC"));
    }
    if (m_configModeButton) {
        if (m_configKeyboardNumericOnly) {
            m_configModeButton->setVisible(false);
        } else {
            m_configModeButton->setVisible(true);
            m_configModeButton->setText(m_configKeyboardNumberMode ? QString::fromUtf8("ABC") : QString::fromUtf8("123"));
        }
    }
    if (m_configClearButton) {
        m_configClearButton->setVisible(!m_configKeyboardNumberMode);
    }
    if (m_configBackspaceButton) {
        if (m_configKeyboardNumberMode) {
            m_configBackspaceButton->setText(QString::fromUtf8("删除"));
            m_configBackspaceButton->setGeometry(56, 178, 92, 46);
            m_configBackspaceButton->setStyleSheet("QPushButton{background:rgb(232,234,236);border:1px solid rgb(150,150,150);border-radius:5px;color:rgb(38,45,52);font:15px 'MS Shell Dlg 2';font-weight:bold;}");
        } else {
            m_configBackspaceButton->setText(QString::fromUtf8("退格"));
            m_configBackspaceButton->setGeometry(256, 220, 74, 42);
            m_configBackspaceButton->setStyleSheet(m_configShiftButton ? m_configShiftButton->styleSheet() : QString());
        }
    }
    if (m_configConfirmButton) {
        if (m_configKeyboardNumberMode) {
            m_configConfirmButton->setGeometry(272, 178, 92, 46);
            m_configConfirmButton->setStyleSheet("QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:5px;color:white;font:15px 'MS Shell Dlg 2';font-weight:bold;}");
        } else {
            m_configConfirmButton->setGeometry(338, 220, 54, 42);
            m_configConfirmButton->setStyleSheet("QPushButton{background:rgb(72,148,196);border:1px solid rgb(110,110,110);border-radius:5px;color:white;font:15px 'MS Shell Dlg 2';font-weight:bold;}");
        }
    }
}

void RuntimeWindow::onConfigEditMaskClicked()
{
    markScreenActivity();
    QObject *src = sender();
    if (!src) {
        return;
    }

    if (src->objectName() == QString::fromUtf8("btnConfigEdit_pile")) {
        openConfigKeyboard(m_configPileNoEdit, true);
    } else if (src->objectName() == QString::fromUtf8("btnConfigEdit_secret")) {
        openConfigKeyboard(m_configSecretEdit, false);
    } else if (src->objectName() == QString::fromUtf8("btnConfigEdit_meter1")) {
        openConfigKeyboard(m_configMeterAddr1Edit, true);
    } else if (src->objectName() == QString::fromUtf8("btnConfigEdit_meter2")) {
        openConfigKeyboard(m_configMeterAddr2Edit, true);
    }
}

void RuntimeWindow::onConfigKeyboardButtonClicked()
{
    markScreenActivity();
    if (!m_configKeyboardTarget) {
        return;
    }

    QObject *src = sender();
    QPushButton *btn = qobject_cast<QPushButton *>(src);
    if (!btn) {
        return;
    }

    const QString objName = btn->objectName();
    if (objName == QString::fromUtf8("btnConfigShift")) {
        m_configKeyboardUppercase = !m_configKeyboardUppercase;
        refreshConfigKeyboardLayout();
        return;
    }
    if (objName == QString::fromUtf8("btnConfigMode")) {
        m_configKeyboardNumberMode = !m_configKeyboardNumberMode;
        refreshConfigKeyboardLayout();
        return;
    }
    if (objName == QString::fromUtf8("btnConfigBackspace")) {
        QString text = m_configKeyboardTarget->text();
        if (!text.isEmpty()) {
            text.chop(1);
            m_configKeyboardTarget->setText(text);
        }
        return;
    }
    if (objName == QString::fromUtf8("btnConfigClear")) {
        m_configKeyboardTarget->clear();
        return;
    }
    if (objName == QString::fromUtf8("btnConfigFinish")) {
        if (m_configKeyboard) {
            m_configKeyboard->hide();
        }
        if (m_configHintLabel) {
            m_configHintLabel->setText(QString());
            m_configHintLabel->setStyleSheet("QLabel{color:rgb(160,100,40);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");
        }
        return;
    }

    const QString keyText = btn->text();
    if (keyText.isEmpty()) {
        return;
    }
    m_configKeyboardTarget->setText(m_configKeyboardTarget->text() + keyText);
}

void RuntimeWindow::onConfigGunCountChanged()
{
    markScreenActivity();
    const bool dual = (m_configGunDual && m_configGunDual->isChecked());
    if (m_configMeter2Row) {
        m_configMeter2Row->setVisible(dual);
    }
    if (!dual && m_configMeterAddr2Edit) {
        m_configMeterAddr2Edit->clear();
    }
    if (m_configHintLabel) {
        m_configHintLabel->setText(QString());
        m_configHintLabel->setStyleSheet("QLabel{color:rgb(160,100,40);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");
    }
}

void RuntimeWindow::loadMeterConfigToUi()
{
    if (!m_configGunSingle || !m_configGunDual || !m_configMeterAddr1Edit || !m_configMeterAddr2Edit) {
        return;
    }

    ConfigManagerLite &cfg = ConfigManagerLite::getInstance();
    int gunCount = 1;
    QString meterAddr1;
    QString meterAddr2;
    if (cfg.loadConfig(meterConfigPath().toStdString())) {
        gunCount = cfg.getInt("Meter", "gun_count", 1);
        meterAddr1 = QString::fromStdString(cfg.getString("Meter", "gun1_meter_addr", ""));
        meterAddr2 = QString::fromStdString(cfg.getString("Meter", "gun2_meter_addr", ""));
    }

    if (gunCount <= 1) {
        m_configGunSingle->setChecked(true);
    } else {
        m_configGunDual->setChecked(true);
    }
    m_configMeterAddr1Edit->setText(meterAddr1);
    m_configMeterAddr2Edit->setText(meterAddr2);
    onConfigGunCountChanged();
}

void RuntimeWindow::loadCommConfigToUi()
{
    if (!m_configSecretEdit || !m_configPileNoEdit) {
        return;
    }

    ConfigManagerLite &cfg = ConfigManagerLite::getInstance();
    QString loginId = QString::fromUtf8("xMa3mXJG");
    QString pileNo;
    if (cfg.loadConfig(commConfigPath().toStdString())) {
        pileNo = QString::fromStdString(cfg.getString("Comm", "cdz_no", ""));
        const std::string value = cfg.getString("Comm", "login_id", "xMa3mXJG");
        loginId = QString::fromStdString(value);
    }
    m_configPileNoEdit->setText(pileNo);
    m_configSecretEdit->setText(loginId);
}

bool RuntimeWindow::saveMeterConfigFromUi()
{
    if (!m_configGunSingle || !m_configGunDual || !m_configMeterAddr1Edit || !m_configMeterAddr2Edit) {
        return false;
    }

    const QString meterAddr1 = m_configMeterAddr1Edit->text().trimmed();
    const QString meterAddr2 = m_configMeterAddr2Edit->text().trimmed();
    const int gunCount = (m_configGunDual && m_configGunDual->isChecked()) ? 2 : 1;
    if (meterAddr1.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请填写电表地址1"));
        return false;
    }
    if (gunCount > 1 && meterAddr2.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("双枪模式请填写电表地址2"));
        return false;
    }

    const QString cfgPath = meterConfigPath();
    if (!replaceConfigValue(cfgPath, QString::fromUtf8("Meter"), QString::fromUtf8("gun_count"), QString::number(gunCount))) {
        return false;
    }
    if (!replaceConfigValue(cfgPath, QString::fromUtf8("Meter"), QString::fromUtf8("gun1_meter_addr"), meterAddr1)) {
        return false;
    }
    if (!replaceConfigValue(cfgPath, QString::fromUtf8("Meter"), QString::fromUtf8("gun2_meter_addr"), gunCount > 1 ? meterAddr2 : QString())) {
        return false;
    }
    return true;
}

bool RuntimeWindow::saveCommConfigFromUi()
{
    if (!m_configSecretEdit || !m_configPileNoEdit || !m_configGunSingle || !m_configGunDual) {
        return false;
    }

    const QString pileNo = m_configPileNoEdit->text().trimmed();
    const QString loginId = m_configSecretEdit->text().trimmed();
    const int gunCount = (m_configGunDual && m_configGunDual->isChecked()) ? 2 : 1;
    if (pileNo.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请填写充电桩号"));
        return false;
    }
    if (loginId.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("提示"), QString::fromUtf8("请填写充电桩密钥"));
        return false;
    }

    const QString cfgPath = commConfigPath();
    if (!replaceConfigValue(cfgPath, QString::fromUtf8("Comm"), QString::fromUtf8("gun_count"), QString::number(gunCount))) {
        return false;
    }
    if (!replaceConfigValue(cfgPath, QString::fromUtf8("Comm"), QString::fromUtf8("cdz_no"), pileNo)) {
        return false;
    }
    if (!replaceConfigValue(cfgPath, QString::fromUtf8("Comm"), QString::fromUtf8("login_id"), loginId)) {
        return false;
    }
    return true;
}

bool RuntimeWindow::saveHmiConfigFromUi()
{
    if (!m_configGunSingle || !m_configGunDual) {
        return false;
    }

    const int gunCount = (m_configGunDual && m_configGunDual->isChecked()) ? 2 : 1;
    const QString cfgPath = hmiConfigPath();
    return replaceConfigValue(cfgPath, QString::fromUtf8("Hmi"), QString::fromUtf8("gun_count"), QString::number(gunCount));
}

bool RuntimeWindow::savePileConfigFromUi()
{
    if (!m_configGunSingle || !m_configGunDual) {
        return false;
    }

    const int gunCount = (m_configGunDual && m_configGunDual->isChecked()) ? 2 : 1;
    const QString cfgPath = pileControllerConfigPath();
    if (!replaceConfigValue(cfgPath, QString::fromUtf8("PileController"), QString::fromUtf8("gun_count"), QString::number(gunCount))) {
        return false;
    }
    if (gunCount <= 1) {
        if (!replaceConfigValue(cfgPath, QString::fromUtf8("PileController"), QString::fromUtf8("gun1_cdz_addr"), QString::fromUtf8("0xF6"))) {
            return false;
        }
        if (!replaceConfigValue(cfgPath, QString::fromUtf8("PileController"), QString::fromUtf8("gun2_cdz_addr"), QString::fromUtf8("0xE2"))) {
            return false;
        }
    } else {
        if (!replaceConfigValue(cfgPath, QString::fromUtf8("PileController"), QString::fromUtf8("gun1_cdz_addr"), QString::fromUtf8("0xE0"))) {
            return false;
        }
        if (!replaceConfigValue(cfgPath, QString::fromUtf8("PileController"), QString::fromUtf8("gun2_cdz_addr"), QString::fromUtf8("0xE1"))) {
            return false;
        }
    }
    return true;
}

void RuntimeWindow::onConfigSubmitClicked()
{
    markScreenActivity();
    if (m_configKeyboard) {
        m_configKeyboard->hide();
    }
    const bool meterOk = saveMeterConfigFromUi();
    const bool commOk = saveCommConfigFromUi();
    const bool hmiOk = saveHmiConfigFromUi();
    const bool pileOk = savePileConfigFromUi();
    if (meterOk && commOk && hmiOk && pileOk) {
        if (m_configHintLabel) {
            m_configHintLabel->setText(QString::fromUtf8("配置已写入，5秒后重启"));
            m_configHintLabel->setStyleSheet("QLabel{color:rgb(32,132,82);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");
        }
        QTimer::singleShot(5000, this, SLOT(rebootSystem()));
    } else if (m_configHintLabel) {
        m_configHintLabel->setText(QString::fromUtf8("写入失败"));
        m_configHintLabel->setStyleSheet("QLabel{color:rgb(160,60,60);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");
    }
}

void RuntimeWindow::onConfigResetClicked()
{
    markScreenActivity();
    loadMeterConfigToUi();
    loadCommConfigToUi();
    if (m_configKeyboard) {
        m_configKeyboard->hide();
    }
    if (m_configHintLabel) {
        m_configHintLabel->setText(QString());
        m_configHintLabel->setStyleSheet("QLabel{color:rgb(72,84,94);background:transparent;font:13px 'MS Shell Dlg 2';font-weight:bold;}");
    }
}

void RuntimeWindow::rebootSystem()
{
    // BY ZF: 参数配置确认后延时重启，使各进程按新配置完整重载。
    QProcess::startDetached(QString::fromUtf8("reboot"));
}

void RuntimeWindow::markScreenActivity()
{
    // BY ZF: 复刻旧版空闲计时逻辑，界面操作或数据变化只更新时间，并在黑屏时直接点亮背光。
    QMutexLocker locker(&m_dataMutex);
    m_lastScreenActivityMs = nowMs();
    if (m_screenBacklightOff) {
        setScreenBacklight(true);
        m_screenBacklightOff = false;
    }
}

void RuntimeWindow::setScreenBacklight(bool on)
{
    // BY ZF: 继续沿用目标机已验证的 sysfs 背光控制链路。
    const QString command = on
        ? QString::fromUtf8("echo 80 > /sys/devices/platform/backlight/backlight/backlight/brightness")
        : QString::fromUtf8("echo 0 > /sys/devices/platform/backlight/backlight/backlight/brightness");
    std::cerr << "[HMI] setScreenBacklight "
              << (on ? "ON" : "OFF")
              << " cmd=" << command.toStdString() << std::endl;
    QProcess::startDetached(QString::fromUtf8("sh"),
                            QStringList() << QString::fromUtf8("-c") << command);
}

void RuntimeWindow::checkScreenIdle()
{
    // BY ZF: 120 秒内既无用户操作也无 MQTT 驱动的数据变化时关闭背光。
    QMutexLocker locker(&m_dataMutex);
    if (m_screenBacklightOff) {
        return;
    }
    if (m_lastScreenActivityMs == 0) {
        m_lastScreenActivityMs = nowMs();
        return;
    }
    if (nowMs() - m_lastScreenActivityMs >= 120000ULL) {
        setScreenBacklight(false);
        m_screenBacklightOff = true;
    }
}

bool RuntimeWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event &&
        (watched == m_stack ||
         watched == m_idlePage ||
         watched == m_authorizePage ||
         watched == m_chargingPage ||
         watched == m_checkoutPage ||
         watched == m_aboutPage ||
         watched == m_aboutTabWidget) &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease)) {
        markScreenActivity();
    }

    if (watched == m_aboutPermissionEdit && event &&
        (event->type() == QEvent::MouseButtonPress ||
         event->type() == QEvent::MouseButtonRelease ||
         event->type() == QEvent::FocusIn)) {
        markScreenActivity();
        std::cerr << "[A3] permission edit event type=" << static_cast<int>(event->type()) << std::endl;
        if (m_aboutPermissionPad) {
            m_aboutPermissionPad->show();
            m_aboutPermissionPad->raise();
            std::cerr << "[A3] permission pad from edit visible="
                      << (m_aboutPermissionPad->isVisible() ? 1 : 0)
                      << " geometry="
                      << m_aboutPermissionPad->x() << ","
                      << m_aboutPermissionPad->y() << ","
                      << m_aboutPermissionPad->width() << ","
                      << m_aboutPermissionPad->height() << std::endl;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void RuntimeWindow::handleCellClick(int port)
{
    markScreenActivity();
    {
        QMutexLocker locker(&m_dataMutex);
        m_forceIdleView = false;
        m_manualFocusLocked = true;
        if (port >= 0 && port < static_cast<int>(m_guns.size())) {
            m_focusGun = port;
        }
    }
    refreshUi();
}

void RuntimeWindow::returnToIdlePage()
{
    markScreenActivity();
    {
        QMutexLocker locker(&m_dataMutex);
        m_forceIdleView = true;
        m_manualFocusLocked = false;
    }
    refreshUi();
}

void RuntimeWindow::showAllChargeRecords()
{
    markScreenActivity();
    m_recordGunFilter = -1;
    m_chargeRecordPage = 0;
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
}

void RuntimeWindow::showAGunChargeRecords()
{
    markScreenActivity();
    m_recordGunFilter = 0;
    m_chargeRecordPage = 0;
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
}

void RuntimeWindow::showBGunChargeRecords()
{
    markScreenActivity();
    m_recordGunFilter = 1;
    m_chargeRecordPage = 0;
    refreshChargeRecordCache(true);
    refreshChargeRecordTable();
}

void RuntimeWindow::showPrevChargeRecordPage()
{
    markScreenActivity();
    if (m_chargeRecordPage > 0) {
        --m_chargeRecordPage;
    }
    refreshChargeRecordTable();
}

void RuntimeWindow::showNextChargeRecordPage()
{
    markScreenActivity();
    ++m_chargeRecordPage;
    refreshChargeRecordTable();
}

void RuntimeWindow::showAllFaultRecords()
{
    markScreenActivity();
    m_faultGunFilter = -1;
    m_faultRecordPage = 0;
    refreshFaultRecordCache(true);
    refreshFaultRecordTable();
}

void RuntimeWindow::showAFaultRecords()
{
    markScreenActivity();
    m_faultGunFilter = 0;
    m_faultRecordPage = 0;
    refreshFaultRecordCache(true);
    refreshFaultRecordTable();
}

void RuntimeWindow::showBFaultRecords()
{
    markScreenActivity();
    m_faultGunFilter = 1;
    m_faultRecordPage = 0;
    refreshFaultRecordCache(true);
    refreshFaultRecordTable();
}

void RuntimeWindow::showPrevFaultRecordPage()
{
    markScreenActivity();
    if (m_faultRecordPage > 0) {
        --m_faultRecordPage;
    }
    refreshFaultRecordTable();
}

void RuntimeWindow::showNextFaultRecordPage()
{
    markScreenActivity();
    ++m_faultRecordPage;
    refreshFaultRecordTable();
}

void RuntimeWindow::refreshIdlePage(const std::vector<GunUiData> &guns)
{
    setLabelText(m_idlePage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));

    CCellWidget *cell0 = m_idlePage->findChild<CCellWidget *>("cell0");
    CCellWidget *cell1 = m_idlePage->findChild<CCellWidget *>("cell1");
    QLabel *leftFault = m_idlePage->findChild<QLabel *>("label_gth");
    QLabel *rightFault = m_idlePage->findChild<QLabel *>("label_gth_3");

    if (cell0 && guns.size() > 0) {
        const GunUiData &g = guns[0];
        cell0->setTips(idleCellText(g.totalEnergy, g.totalAmount, g.chargedTime,
                                    g.voltage, g.current, g.soc, g.state));
    }

    if (cell1) {
        if (m_gunCount > 1 && guns.size() > 1) {
            cell1->show();
            const GunUiData &g = guns[1];
            cell1->setTips(idleCellText(g.totalEnergy, g.totalAmount, g.chargedTime,
                                        g.voltage, g.current, g.soc, g.state));
        } else {
            cell1->hide();
        }
    }

    if (leftFault) {
        leftFault->setVisible(guns.size() > 0 && guns[0].state == "ERROR");
        if (leftFault->isVisible()) {
            leftFault->setPixmap(QPixmap(":/cui/Resources/gth.png"));
            leftFault->setScaledContents(true);
            leftFault->raise();
        }
    }
    if (rightFault) {
        rightFault->setVisible(m_gunCount > 1 && guns.size() > 1 && guns[1].state == "ERROR");
        if (rightFault->isVisible()) {
            rightFault->setPixmap(QPixmap(":/cui/Resources/gth.png"));
            rightFault->setScaledContents(true);
            rightFault->raise();
        }
    }
}

void RuntimeWindow::applyIdleLayout()
{
    CCellWidget *cell0 = m_idlePage->findChild<CCellWidget *>("cell0");
    CCellWidget *cell1 = m_idlePage->findChild<CCellWidget *>("cell1");
    QWidget *line = m_idlePage->findChild<QWidget *>("line");
    QLabel *labelA = m_idlePage->findChild<QLabel *>("label_2");
    QLabel *labelB = m_idlePage->findChild<QLabel *>("label_3");
    QLabel *leftBg = m_idlePage->findChild<QLabel *>("label_zq");
    QLabel *rightBg = m_idlePage->findChild<QLabel *>("label_yq");
    QLabel *leftFault = m_idlePage->findChild<QLabel *>("label_gth");
    QLabel *rightFault = m_idlePage->findChild<QLabel *>("label_gth_3");
    QLabel *leftMask = m_idlePage->findChild<QLabel *>("label_tk_zq");
    QLabel *rightMask = m_idlePage->findChild<QLabel *>("label_tk_zq_2");

    if (!cell0 || !cell1 || !labelA || !labelB || !leftBg || !rightBg || !leftFault || !rightFault || !leftMask || !rightMask) {
        return;
    }

    if (m_gunCount <= 1) {
        if (line) {
            line->hide();
        }
        cell0->setGeometry(235, 100, 330, 240);
        labelA->setGeometry(355, 95, 91, 61);
        labelA->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
        leftBg->setGeometry(275, 90, 290, 296);
        leftMask->setGeometry(275, 90, 290, 296);
        leftFault->setGeometry(501, 148, 48, 48);

        cell1->hide();
        labelB->hide();
        rightBg->hide();
        rightMask->hide();
        rightFault->hide();
    } else {
        if (line) {
            line->show();
            line->setGeometry(400, 90, 2, 320);
        }
        cell0->setGeometry(40, 100, 330, 240);
        labelA->setGeometry(80, 95, 91, 61);
        labelA->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
        leftBg->setGeometry(80, 90, 290, 296);
        leftMask->setGeometry(80, 90, 290, 296);
        leftFault->setGeometry(306, 148, 48, 48);

        cell1->show();
        cell1->setGeometry(400, 100, 330, 240);
        labelB->show();
        labelB->setGeometry(440, 95, 81, 51);
        labelB->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
        rightBg->show();
        rightBg->setGeometry(440, 90, 290, 296);
        rightMask->show();
        rightMask->setGeometry(440, 90, 290, 296);
        rightFault->setGeometry(666, 148, 48, 48);
    }
}

void RuntimeWindow::refreshAuthorizePage(const GunUiData &gun)
{
    const bool hasExtraEntry = m_config.enableMergeChargeEntry || m_config.enableVinEntry || m_config.enableCardEntry;
    const bool isError = (gun.state == "ERROR");
    const bool inserted = (gun.state != "IDLE" && !isError);
    setLabelText(m_authorizePage, "lblPole", QString::fromUtf8("%1枪").arg(gun.gun == 0 ? "A" : "B"));
    setLabelText(m_authorizePage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));

    QLabel *tips = m_authorizePage->findChild<QLabel *>("lblTips");
    QLabel *tips2 = m_authorizePage->findChild<QLabel *>("lblTips_2");
    QLabel *labelMs = m_authorizePage->findChild<QLabel *>("label_ms");
    QLabel *ewmBg = m_authorizePage->findChild<QLabel *>("label_ewm");
    QPushButton *btnMergeCharge = m_authorizePage->findChild<QPushButton *>("bbms");
    QPushButton *btnVin = m_authorizePage->findChild<QPushButton *>("vin");
    QPushButton *btnCard = m_authorizePage->findChild<QPushButton *>("card");

    QRWidget *qr = m_authorizePage->findChild<QRWidget *>("qrMain");
    QLabel *qcq = m_authorizePage->findChild<QLabel *>("qcq");
    if (qr) {
        if (!gun.qrPayload.empty()) {
            qr->setQRData(QString::fromStdString(gun.qrPayload));
        }
        if (inserted) {
            if (hasExtraEntry) {
                qr->setGeometry(40, 80, 280, 280);
            } else {
                qr->setGeometry(260, 80, 280, 280);
            }
            qr->show();
        } else {
            qr->hide();
        }
    }
    if (ewmBg) {
        if (inserted) {
            if (hasExtraEntry) {
                ewmBg->setGeometry(40, 84, 280, 312);
            } else {
                ewmBg->setGeometry(260, 84, 280, 312);
            }
            ewmBg->show();
            ewmBg->raise();
        } else {
            ewmBg->hide();
        }
    }
    if (btnMergeCharge) {
        if (inserted && m_config.enableMergeChargeEntry) {
            btnMergeCharge->setGeometry(448, 132, 88, 88);
            btnMergeCharge->show();
        } else {
            btnMergeCharge->hide();
        }
    }
    if (btnVin) {
        if (inserted && m_config.enableVinEntry) {
            btnVin->setGeometry(628, 132, 88, 88);
            btnVin->show();
        } else {
            btnVin->hide();
        }
    }
    if (btnCard) {
        if (inserted && m_config.enableCardEntry) {
            btnCard->setGeometry(448, 242, 88, 88);
            btnCard->show();
        } else {
            btnCard->hide();
        }
    }
    if (tips) {
        if (isError) {
            tips->setText(QString::fromUtf8("当前故障代码：%1").arg(currentFaultCodeText(gun)));
            tips->setGeometry(80, 322, 640, 38);
            tips->setAlignment(Qt::AlignCenter);
            tips->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;font:26px 'MS Shell Dlg 2';}");
            tips->show();
            tips->raise();
        } else {
            tips->hide();
        }
    }
    if (tips2) {
        if (isError) {
            tips2->setText(QString::fromUtf8("故障名称：%1").arg(stopReasonText(gun)));
            tips2->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;font:26px 'MS Shell Dlg 2';}");
            tips2->setGeometry(50, 364, 700, 44);
            tips2->setAlignment(Qt::AlignCenter);
            tips2->show();
            tips2->raise();
        } else if (inserted) {
            tips2->setText(QString::fromUtf8("扫码启动充电"));
            tips2->setStyleSheet("QLabel{color:white;font-weight:bold;background:transparent;}");
            if (hasExtraEntry) {
                tips2->setGeometry(46, 348, 268, 44);
            } else {
                tips2->setGeometry(240, 348, 320, 44);
            }
            tips2->show();
            tips2->raise();
        } else {
            tips2->hide();
        }
    }
    if (qr && qr->isVisible()) {
        qr->raise();
    }
    if (labelMs) {
        labelMs->hide();
    }
    if (qcq) {
        QPixmap qcqPixmap(isError ? ":/cui/Resources/error.png" : ":/cui/Resources/qcq.png");
        qcq->setPixmap(qcqPixmap);
        qcq->setScaledContents(false);
        if ((!inserted || isError) && !qcqPixmap.isNull()) {
            const int x = (800 - qcqPixmap.width()) / 2;
            const int y = isError ? 104 : (480 - qcqPixmap.height()) / 2;
            qcq->setGeometry(x, y, qcqPixmap.width(), qcqPixmap.height());
            qcq->show();
            qcq->raise();
        } else {
            qcq->hide();
        }
    }
}

void RuntimeWindow::refreshChargingPage(const GunUiData &gun)
{
    setLabelText(m_chargingPage, "lblTitle", QString());
    setLabelText(m_chargingPage, "lblMoney", QString::fromUtf8("%1 元").arg(formatMoney(gun.totalAmount)));
    setLabelText(m_chargingPage, "lblKwh", QString::fromUtf8("%1 kWh").arg(formatEnergy(gun.totalEnergy)));
    setLabelText(m_chargingPage, "lblChargeTime", formatChargeTime(gun.chargedTime));
    QString voltageText = QString::fromUtf8("%1 V").arg(formatVoltage(gun.voltage));
    QString currentText = QString::fromUtf8("%1 A").arg(formatCurrent(gun.current));
    if (gun.reqVoltage > 0.0) {
        voltageText += QString::fromUtf8("/%1 V").arg(formatVoltage(gun.reqVoltage));
    }
    if (gun.reqCurrent != 0.0) {
        currentText += QString::fromUtf8("/%1 A").arg(formatCurrent(gun.reqCurrent));
    }
    setLabelText(m_chargingPage, "lblU", voltageText);
    setLabelText(m_chargingPage, "lblI", currentText);
    setLabelText(m_chargingPage, "lblSoc", gun.soc >= 0 ? QString::fromUtf8("%1 %").arg(gun.soc) : "-- %");
    setLabelText(m_chargingPage, "lblRemainTime", formatRemainMinutes(gun.remainMinutes));
    setLabelText(m_chargingPage, "lblBatteryMaxTemp", QString::fromUtf8("%1 ℃").arg(QString::number(gun.batteryMaxTemp, 'f', 0)));
    setLabelText(m_chargingPage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));
}

void RuntimeWindow::refreshCheckoutPage(const GunUiData &gun)
{
    setLabelText(m_checkoutPage, "lblHint", QString::fromUtf8("本次充电已结束"));
    setLabelText(m_checkoutPage, "lblChargeTime", formatHourMinute(gun.chargedTime));
    setLabelText(m_checkoutPage, "lblMoney", QString::fromUtf8("%1元").arg(formatMoney(gun.totalAmount)));
    setLabelText(m_checkoutPage, "lblKwh", QString::fromUtf8("%1kWh").arg(formatEnergy(gun.totalEnergy)));
    setLabelText(m_checkoutPage, "lblendmoney", QString::fromUtf8("请手机中查看"));
    setLabelText(m_checkoutPage, "stop_reason", stopReasonText(gun));
}

void RuntimeWindow::refreshAboutPage()
{
    refreshFeeModelCache(false);
    refreshFaultRecordCache(false);
    refreshFaultRecordTable();
    refreshChargeRecordCache(false);
    refreshChargeRecordTable();
    setLabelText(m_aboutPage, "label_id", QString::fromStdString(m_cdzNo.empty() ? m_cdzId : m_cdzNo));
    setLabelText(m_aboutPage, "current_time", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    setLabelText(m_aboutPage, "label_timeshow", QString::fromStdString(m_cdzId));
    setLabelText(m_aboutPage, "lblVersion", QString::fromUtf8("Zhongshihua2.0"));
    setLabelText(m_aboutPage, "lblVersion_cui", QString::fromUtf8("流程适配版"));
    setLabelText(m_aboutPage, "lblVersion_pro", QString::fromUtf8("MQTT Runtime"));
    setLabelText(m_aboutPage, "lblnetstat", m_mqttReady ? QString::fromUtf8("已连接") : QString::fromUtf8("未连接"));
    setLabelText(m_aboutPage, "lblcomm", QString::fromUtf8("逻辑订阅模式"));
    setLabelText(m_aboutPage, "lblPoleId_2", QString::fromStdString(m_cdzId));
    setLabelText(m_aboutPage, "lblJzqAddr", QString::fromStdString(m_macAddr));

    setLabelText(m_aboutPage, "lblVersionLogic",
                 QString("logic    %1    %2")
                    .arg(QString::fromStdString(m_monStatus.logic.version.empty() ? "--" : m_monStatus.logic.version))
                    .arg(QString::fromStdString(m_monStatus.logic.buildDate.empty() ? "--" : m_monStatus.logic.buildDate)));
    setLabelText(m_aboutPage, "lblVersionMeter",
                 QString("meter    %1    %2")
                    .arg(QString::fromStdString(m_monStatus.meter.version.empty() ? "--" : m_monStatus.meter.version))
                    .arg(QString::fromStdString(m_monStatus.meter.buildDate.empty() ? "--" : m_monStatus.meter.buildDate)));
    setLabelText(m_aboutPage, "lblVersionMon",
                 QString("mon      %1    %2")
                    .arg(QString::fromStdString(m_monStatus.mon.version.empty() ? "--" : m_monStatus.mon.version))
                    .arg(QString::fromStdString(m_monStatus.mon.buildDate.empty() ? "--" : m_monStatus.mon.buildDate)));
    setLabelText(m_aboutPage, "lblVersionComm", QString("comm     --    --"));
    setLabelText(m_aboutPage, "lblVersionDaemon", QString("daemon   --    --"));
    setLabelText(m_aboutPage, "lblVersionHmi", QString("hmi      Zhongshihua2.0    MQTT Runtime"));

    setLabelText(m_aboutPage, "lblCommPlatform", QString::number(m_monStatus.platformComm));
    setLabelText(m_aboutPage, "lblCommMeter", QString::number(m_monStatus.meterComm));
    setLabelText(m_aboutPage, "lblCommController", QString::number(m_monStatus.controllerComm));
    setLabelText(m_aboutPage, "lblDeviceFaultCount", QString::number(m_monStatus.activeFaultCount));
    setLabelText(m_aboutPage, "lblDeviceLatestFault",
                 QString::fromStdString(m_monStatus.latestFaultName.empty() ? "--" : m_monStatus.latestFaultName));
    setLabelText(m_aboutPage, "lblDeviceLatestFaultTime",
                 QString::fromStdString(m_monStatus.latestFaultTime.empty() ? "--" : m_monStatus.latestFaultTime));
    setLabelText(m_aboutPage, "lblDeviceIdentity",
                 QString("No:%1  ID:%2  MAC:%3")
                    .arg(QString::fromStdString(m_cdzNo.empty() ? "--" : m_cdzNo))
                    .arg(QString::fromStdString(m_cdzId.empty() ? "--" : m_cdzId))
                    .arg(QString::fromStdString(m_macAddr.empty() ? "--" : m_macAddr)));

    QString currentSeg = QString::fromUtf8("当前时段：--");
    QString currentCharge = QString::fromUtf8("电费：-- 元");
    QString currentService = QString::fromUtf8("服务费：-- 元");
    QString currentTotal = QString::fromUtf8("总价：-- 元");
    QVector<FeeBarData> bars;
    if (m_feeModel.valid) {
        int idx = 0;
        for (idx = 0; idx < static_cast<int>(m_feeModel.segments.size()); ++idx) {
            FeeBarData bar;
            bar.startTime = hhmmText(m_feeModel.segments[idx].startTime);
            bar.endTime = hhmmText(m_feeModel.segments[idx].endTime);
            bar.chargeFee = m_feeModel.segments[idx].chargeFee;
            bar.serviceFee = m_feeModel.segments[idx].serviceFee;
            bars.push_back(bar);
        }

        if (m_feeModel.currentIndex >= 0 && m_feeModel.currentIndex < static_cast<int>(m_feeModel.segments.size())) {
            const FeeSegment &seg = m_feeModel.segments[m_feeModel.currentIndex];
            currentSeg = QString::fromUtf8("当前时段：%1 - %2")
                .arg(hhmmText(seg.startTime))
                .arg(hhmmText(seg.endTime));
            currentCharge = QString::fromUtf8("电费：%1 元").arg(formatFeeValue(seg.chargeFee));
            currentService = QString::fromUtf8("服务费：%1 元").arg(formatFeeValue(seg.serviceFee));
            currentTotal = QString::fromUtf8("总价：%1 元").arg(formatFeeValue(seg.chargeFee + seg.serviceFee));
        }
    }
    setLabelText(m_aboutPage, "lblCurrentSeg", currentSeg);
    setLabelText(m_aboutPage, "lblCurrentChargeFee", currentCharge);
    setLabelText(m_aboutPage, "lblCurrentServiceFee", currentService);
    setLabelText(m_aboutPage, "lblCurrentTotalFee", currentTotal);
    if (m_feeChart) {
        m_feeChart->setBars(bars, m_feeModel.currentIndex);
    }
}

RuntimeWindow::PageId RuntimeWindow::decidePage(const std::vector<GunUiData> &guns, int &focusGun) const
{
    if (guns.empty()) {
        focusGun = 0;
        return PageIdle;
    }

    if (m_manualFocusLocked && focusGun >= 0 && focusGun < static_cast<int>(guns.size())) {
        const std::string &state = guns[focusGun].state;
        if (state == "CHARGING" || state == "STOPPING") {
            return PageCharging;
        }
        if (state == "PREPARE" || state == "STARTING") {
            return PageAuthorize;
        }
        if (state == "IDLE") {
            return PageAuthorize;
        }
        if (state == "ERROR") {
            return PageAuthorize;
        }
        if (state == "STOPPED") {
            return PageCheckout;
        }
        return PageIdle;
    }

    int bestGun = focusGun;
    int bestPriority = 0;
    uint64_t bestTs = 0;
    PageId bestPage = PageIdle;

    for (int i = 0; i < m_gunCount && i < static_cast<int>(guns.size()); ++i) {
        const std::string &state = guns[i].state;
        PageId page = PageIdle;
        int priority = 0;

        if (state == "CHARGING" || state == "STOPPING") {
            page = PageCharging;
            priority = 4;
        } else if (state == "PREPARE" || state == "STARTING") {
            page = PageAuthorize;
            priority = 3;
        } else if (state == "STOPPED") {
            page = PageCheckout;
            priority = 2;
        } else if (state == "ERROR") {
            page = PageIdle;
            priority = 1;
        }

        if (priority == 0) {
            continue;
        }

        if (priority > bestPriority ||
            (priority == bestPriority && guns[i].lastStateChangeMs >= bestTs)) {
            bestPriority = priority;
            bestPage = page;
            bestGun = i;
            bestTs = guns[i].lastStateChangeMs;
        }
    }

    focusGun = bestGun;
    return bestPage;
}

bool RuntimeWindow::parseTopicGun(const std::string &topic,
                                  const std::string &prefix,
                                  uint8_t &gun,
                                  std::string &tail)
{
    if (topic.find(prefix) != 0U) {
        return false;
    }

    const std::string rest = topic.substr(prefix.size());
    const size_t p = rest.find('/');
    if (p == std::string::npos) {
        return false;
    }

    const std::string gunStr = rest.substr(0, p);
    if (gunStr.empty()) {
        return false;
    }

    const int g = std::atoi(gunStr.c_str());
    if (g < 0 || g > 255) {
        return false;
    }

    gun = static_cast<uint8_t>(g);
    tail = rest.substr(p + 1);
    return true;
}
