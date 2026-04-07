/**
 * Shared-memory pile controller implementation.
 * BY ZF
 */

#include "shm_pile_controller.h"
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <cmath>

namespace {

static unsigned int packChargeCount(unsigned int value, int byteIndex)
{
    return (value >> (byteIndex * 8)) & 0xFFU;
}

}

SHMPileController::SHMPileController()
    : m_shm(nullptr)
    , m_gunNo(1)
    , m_gunIndex(0)
    , m_shmKey(85000)
{
    zeroMemory(&m_startChargeData, sizeof(m_startChargeData));
    zeroMemory(&m_stopChargeData, sizeof(m_stopChargeData));
    zeroMemory(&m_powerAdjustData, sizeof(m_powerAdjustData));
}

SHMPileController::~SHMPileController()
{
    cleanup();
}

bool SHMPileController::initialize(const char* commType, const char* config)
{
    if (commType == nullptr || std::string(commType) != "shm") {
        return false;
    }
    if (!parseConfig(config)) {
        return false;
    }
    if (m_shm == nullptr) {
        m_shm = new CShm();
    }
    return m_shm != nullptr;
}

void SHMPileController::cleanup()
{
    if (m_shm != nullptr) {
        delete m_shm;
        m_shm = nullptr;
    }
}

int SHMPileController::startCharge()
{
    if (m_shm == nullptr) {
        return -1;
    }

    YX* point = getYxPoint(SHM2CCU::YX_CAN_START_CHARGE);
    if (point == nullptr) {
        return -1;
    }

    memset(point->desname, 0, sizeof(point->desname));
    point->desname[SHM2CCU::START_CHARGE_OFFSET_START_MODE] =
        (m_startChargeData.mergeChargeFlag != 0)
            ? static_cast<char>((m_startChargeData.plugAndChargeFlag == 0x02) ? 0x06 : 0x05)
            : static_cast<char>((m_startChargeData.plugAndChargeFlag == 0x02) ? 0x02 : 0x01);
    point->desname[SHM2CCU::START_CHARGE_OFFSET_CHARGE_DISCHARGE_MODE] =
        static_cast<char>((m_startChargeData.v2g != 0) ? 0x02 : 0x01);
    point->desname[SHM2CCU::START_CHARGE_OFFSET_AUX_POWER_VOLTAGE] =
        static_cast<char>((m_startChargeData.auxPowerVoltage == 0x18) ? 24 : 12);
    point->desname[SHM2CCU::START_CHARGE_OFFSET_PULSE_MODE] = 0x01;
    point->desname[SHM2CCU::START_CHARGE_OFFSET_V2G_MODE] =
        static_cast<char>((m_startChargeData.v2g != 0) ? 0x01 : 0x02);
    point->value = true;

    _evs_service_startCharge* remoteStart = m_shm->evs_getRemoteStart(m_gunIndex);
    if (remoteStart != nullptr) {
        memset(remoteStart, 0, sizeof(*remoteStart));
        remoteStart->gunNo = static_cast<unsigned char>(m_gunNo);
        remoteStart->startType = static_cast<unsigned char>(
            (m_startChargeData.plugAndChargeFlag == 0x02) ? 0x02 : 0x01);
        remoteStart->chargeMode = static_cast<unsigned char>(
            (m_startChargeData.v2g != 0) ? 0x02 : 0x01);
        remoteStart->startMode = static_cast<unsigned char>(
            (m_startChargeData.mergeChargeFlag != 0) ? 0x02 : 0x01);
    }

    return 0;
}

int SHMPileController::stopCharge()
{
    if (m_shm == nullptr) {
        return -1;
    }

    YX* point = getYxPoint(SHM2CCU::YX_CAN_STOP_CHARGE);
    if (point == nullptr) {
        return -1;
    }

    memset(point->desname, 0, sizeof(point->desname));
    uint8_t stopReason = m_stopChargeData.stopReason;
    if (stopReason < 1 || stopReason > 4) {
        stopReason = 3;
    }
    point->desname[0] = static_cast<char>(stopReason);
    point->value = true;
    return 0;
}

int SHMPileController::getStatus(uint8_t gunNo, PileStatus* status)
{
    if (status == nullptr || m_shm == nullptr || gunNo != m_gunNo) {
        return -1;
    }

    memset(status, 0, sizeof(*status));
    status->gunNo = m_gunNo;
    status->outputVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_OUTPUT_VOLTAGE));
    status->outputCurrent = getYcSignedValue(SHM2CCU::YC_OUTPUT_CURRENT);
    status->outputPower = static_cast<uint32_t>(
        (static_cast<double>(status->outputVoltage) / 10.0) *
        (static_cast<double>(status->outputCurrent) / 10.0));
    status->cumulativeEnergy = static_cast<uint32_t>(getYcValue(SHM2CCU::YC_CHARGE_ENERGY) * 100U);
    status->workStatus = getChargePortWorkStatus();
    status->faultCode = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_START_RESULT));
    status->soc = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_SOC));
    status->temperature = static_cast<uint16_t>(getYcSignedValue(SHM2CCU::YC_PILE_ENV_TEMP) * 10);
    return 0;
}

void SHMPileController::setStartChargeData(const TCU2CCU_CmdStartChargeData* data)
{
    if (data != nullptr) {
        m_startChargeData = *data;
    }
}

void SHMPileController::setStopChargeData(const TCU2CCU_CmdStopChargeData* data)
{
    if (data != nullptr) {
        m_stopChargeData = *data;
    }
}

void SHMPileController::setPowerAdjustData(const TCU2CCU_CmdPowerAdjustData* data)
{
    if (data != nullptr) {
        m_powerAdjustData = *data;
    }
}

int SHMPileController::powerAdjust()
{
    if (m_shm == nullptr) {
        return -1;
    }
    YC* paramPoint = getYcPoint(SHM2CCU::YC_POWER_CTRL_PARAM);
    YX* triggerPoint = getYxPoint(SHM2CCU::YX_POWER_CTRL_REQUEST);
    if (paramPoint == nullptr || triggerPoint == nullptr) {
        return -1;
    }

    paramPoint->value = (static_cast<unsigned int>(m_powerAdjustData.adjustType & 0xFFU) << 8) |
                        static_cast<unsigned int>(m_powerAdjustData.adjustParam & 0xFFU);
    triggerPoint->value = true;
    return 0;
}

bool SHMPileController::getYC20Data(TCU2CCU_DataYC20* data) const
{
    if (data == nullptr || m_shm == nullptr) {
        return false;
    }
    zeroMemory(data, sizeof(*data));
    data->outputVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_OUTPUT_VOLTAGE));
    data->outputCurrent = getYcSignedValue(SHM2CCU::YC_OUTPUT_CURRENT);
    data->soc = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_SOC));
    data->batteryMinTemp = getYcSignedValue(SHM2CCU::YC_BATTERY_MIN_TEMP);
    data->batteryMaxTemp = getYcSignedValue(SHM2CCU::YC_BATTERY_MAX_TEMP);
    data->cellMaxVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_CELL_MAX_VOLTAGE));
    data->cellMinVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_CELL_MIN_VOLTAGE));
    data->pileEnvTemp = getYcSignedValue(SHM2CCU::YC_PILE_ENV_TEMP);
    data->guideVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_GUIDE_VOLTAGE));
    data->bmsReqVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_BMS_REQ_VOLTAGE));
    data->bmsReqCurrent = getYcSignedValue(SHM2CCU::YC_BMS_REQ_CURRENT);
    data->chargeMode = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_CHARGE_MODE));
    data->bmsMeasuredVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_BMS_MEASURED_VOLTAGE));
    data->bmsMeasuredCurrent = getYcSignedValue(SHM2CCU::YC_BMS_MEASURED_CURRENT);
    data->estimatedRemainTime = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_ESTIMATED_REMAIN_TIME));
    data->interfaceTemp1 = getYcSignedValue(SHM2CCU::YC_INTERFACE_TEMP_1);
    data->interfaceTemp2 = getYcSignedValue(SHM2CCU::YC_INTERFACE_TEMP_2);
    data->interfaceTemp3 = getYcSignedValue(SHM2CCU::YC_INTERFACE_TEMP_3);
    data->interfaceTemp4 = getYcSignedValue(SHM2CCU::YC_INTERFACE_TEMP_4);
    data->maxVoltageCellNo = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_MAX_VOLTAGE_CELL_NO));
    data->maxTempPointNo = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_MAX_TEMP_POINT_NO));
    data->minTempPointNo = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_MIN_TEMP_POINT_NO));
    data->inletTemp = getYcSignedValue(SHM2CCU::YC_INLET_TEMP);
    data->outletTemp = getYcSignedValue(SHM2CCU::YC_OUTLET_TEMP);
    data->envHumidity = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_ENV_HUMIDITY));
    return true;
}

bool SHMPileController::getYX22Data(TCU2CCU_DataYX22* data) const
{
    if (data == nullptr || m_shm == nullptr) {
        return false;
    }
    zeroMemory(data, sizeof(*data));
    data->workStatus = static_cast<uint8_t>(getChargePortWorkStatus() & 0x03U);
    data->totalFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_TOTAL_FAULT));
    data->totalAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_TOTAL_ALARM));
    data->emergencyStopFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_EMERGENCY_STOP_FAULT));
    data->smokeFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_SMOKE_FAULT));
    data->acInputBreakerFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_AC_INPUT_BREAKER_FAULT));
    data->dcBusContactorFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DC_BUS_CONTACTOR_FAULT));
    data->dcBusFuseFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DC_BUS_FUSE_FAULT));
    data->electronicLockFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_ELECTRONIC_LOCK_FAULT));
    data->fanFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_FAN_FAULT));
    data->lightningArresterFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_LIGHTNING_ARRESTER_FAULT));
    data->insulationMonitorFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_INSULATION_MONITOR_FAULT));
    data->batteryReverseFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_BATTERY_REVERSE_FAULT));
    data->guideFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_GUIDE_FAULT));
    data->pileOverTempFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PILE_OVER_TEMP_FAULT));
    data->interfaceOverTempFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_INTERFACE_OVER_TEMP_FAULT));
    data->gunNotReturnAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_GUN_NOT_RETURN_ALARM));
    data->bmsCommFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_BMS_COMM_FAULT));
    data->inputOverVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_INPUT_OVER_VOLTAGE_FAULT));
    data->inputUnderVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_INPUT_UNDER_VOLTAGE_FAULT));
    data->dcBusOverVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DC_BUS_OVER_VOLTAGE_FAULT));
    data->dcBusUnderVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DC_BUS_UNDER_VOLTAGE_FAULT));
    data->dcBusOverCurrentFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DC_BUS_OVER_CURRENT_FAULT));
    data->moduleFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_FAULT));
    data->moduleAcInputFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_AC_INPUT_FAULT));
    data->moduleAcOverVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_AC_OVER_VOLTAGE_FAULT));
    data->moduleAcUnderVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_AC_UNDER_VOLTAGE_FAULT));
    data->moduleAcPhaseLossFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_AC_PHASE_LOSS_FAULT));
    data->moduleDcShortFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_DC_SHORT_FAULT));
    data->moduleDcOverCurrentFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_DC_OVER_CURRENT_FAULT));
    data->moduleDcOverVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_DC_OVER_VOLTAGE_FAULT));
    data->moduleDcUnderVoltageFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_DC_UNDER_VOLTAGE_FAULT));
    data->moduleOverTempFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_OVER_TEMP_FAULT));
    data->moduleCommFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_COMM_FAULT));
    data->moduleFanFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_FAN_FAULT));
    data->vehicleConnectStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_VEHICLE_CONNECT_STATUS));
    data->gunSeatStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_GUN_SEAT_STATUS));
    data->electronicLockStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_ELECTRONIC_LOCK_STATUS));
    data->dcContactorStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DC_CONTACTOR_STATUS));
    YX* otherFaultPoint = getYxPoint(SHM2CCU::YX_OTHER_FAULT_VALUE);
    data->otherFault = (otherFaultPoint == nullptr)
        ? 0U
        : static_cast<uint16_t>(static_cast<unsigned char>(otherFaultPoint->desname[0]));
    return true;
}

bool SHMPileController::getYX23Data(TCU2CCU_DataYX23* data) const
{
    if (data == nullptr || m_shm == nullptr) {
        return false;
    }
    zeroMemory(data, sizeof(*data));
    data->pileDoorFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PILE_DOOR_FAULT));
    data->dcContactorStickFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DC_CONTACTOR_STICK_FAULT));
    data->insulationMonitorAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_INSULATION_MONITOR_ALARM));
    data->dischargeFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_DISCHARGE_FAULT));
    data->pileOverTempAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PILE_OVER_TEMP_ALARM));
    data->gunOverTempAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_GUN_OVER_TEMP_ALARM));
    data->acInputContactorFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_AC_INPUT_CONTACTOR_FAULT));
    data->acInputContactorStickFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_AC_INPUT_CONTACTOR_STICK_FAULT));
    data->auxPowerFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_AUX_POWER_FAULT));
    data->bridgeContactorFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_BRIDGE_CONTACTOR_FAULT));
    data->bridgeContactorStickFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_BRIDGE_CONTACTOR_STICK_FAULT));
    data->bridgeContactorStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_BRIDGE_CONTACTOR_STATUS));
    data->moduleEmergencyStopStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_EMERGENCY_STOP_STATUS));
    data->pileWaterFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PILE_WATER_FAULT));
    data->cabinetDoorFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_CABINET_DOOR_FAULT));
    data->cabinetWaterFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_CABINET_WATER_FAULT));
    data->cabinetSmokeFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_CABINET_SMOKE_FAULT));
    data->powerCtrlCommFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_POWER_CTRL_COMM_FAULT));
    data->powerCtrlCommAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_POWER_CTRL_COMM_ALARM));
    data->powerCtrlFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_POWER_CTRL_FAULT));
    data->powerCtrlAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_POWER_CTRL_ALARM));
    data->switchModuleCommFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_SWITCH_MODULE_COMM_FAULT));
    data->switchModuleCommAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_SWITCH_MODULE_COMM_ALARM));
    data->switchModuleFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_SWITCH_MODULE_FAULT));
    data->switchModuleAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_SWITCH_MODULE_ALARM));
    data->bmsFaultByCtrl = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_BMS_FAULT_BY_CTRL));
    data->bmsSendFaultInfo = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_BMS_SEND_FAULT_INFO));
    data->moduleDischargeFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_DISCHARGE_FAULT));
    data->vinReq = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PNC_START_REQUEST));
    data->acInputContactorStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_AC_INPUT_CONTACTOR_STATUS));
    data->acContactorCtrlStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_AC_CONTACTOR_CTRL_STATUS));
    data->portK1CtrlStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PORT_K1_CTRL_STATUS));
    data->portK2CtrlStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PORT_K2_CTRL_STATUS));
    data->portAuxPowerCtrlStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PORT_AUX_POWER_OUTPUT_STATUS));
    data->portAuxPowerCtrlStatus2 = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PORT_AUX_POWER_CTRL_STATUS));
    data->cabinetFanCtrlStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_CABINET_FAN_CTRL_STATUS));
    data->portLockCtrlStatus = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PORT_LOCK_CTRL_STATUS));
    data->peBreakFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PE_BREAK_FAULT));
    data->guideVoltageAbnormal = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_GUIDE_VOLTAGE_ABNORMAL));
    data->moduleStartFail = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_START_FAIL));
    data->moduleShutdownFail = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_SHUTDOWN_FAIL));
    data->moduleAddrAbnormal = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_MODULE_ADDR_ABNORMAL));
    data->coolingSystemFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_COOLING_SYSTEM_FAULT));
    data->cabinetOverTempAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_CABINET_OVER_TEMP_ALARM));
    data->cabinetOverTempFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_CABINET_OVER_TEMP_FAULT));
    data->powerDistStrategyFail = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_POWER_DIST_STRATEGY_FAIL));
    data->noIdleModuleFault = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_NO_IDLE_MODULE_FAULT));
    data->prechargeVoltageFail = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_PRECHARGE_VOLTAGE_FAIL));
    data->powerCtrlCommTimeout = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_POWER_CTRL_COMM_TIMEOUT));
    data->envHumidityAlarm = static_cast<uint8_t>(getYxValue(SHM2CCU::YX_ENV_HUMIDITY_ALARM));
    return true;
}

bool SHMPileController::getStartCompleteData(TCU2CCU_StatusStartCompleteData* data) const
{
    if (data == nullptr || m_shm == nullptr) {
        return false;
    }
    zeroMemory(data, sizeof(*data));
    const char* desname = getYxDesname(SHM2CCU::YX_START_COMPLETE);
    if (desname != nullptr) {
        data->successFlag = static_cast<uint8_t>(static_cast<unsigned char>(desname[SHM2CCU::START_COMPLETE_OFFSET_SUCCESS_FLAG]));
        data->chargeFailReason = static_cast<uint8_t>(static_cast<unsigned char>(desname[SHM2CCU::START_COMPLETE_OFFSET_FAIL_REASON]));
    }
    data->batteryType = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_BATTERY_TYPE));
    data->maxAllowTemp = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_MAX_ALLOW_TEMP));
    data->bmsMaxChargeVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_BMS_MAX_CHARGE_VOLTAGE));
    data->cellMaxChargeVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_CELL_MAX_CHARGE_VOLTAGE));
    data->maxAllowChargeCurrent = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_MAX_ALLOW_CHARGE_CURRENT));
    data->ratedTotalVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_RATED_TOTAL_VOLTAGE));
    data->currentTotalVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_CURRENT_TOTAL_VOLTAGE));
    data->ratedCapacity = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_RATED_CAPACITY));
    data->nominalEnergy = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_NOMINAL_ENERGY));
    data->soc = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_START_COMPLETE_SOC));
    if (m_shm->getDeviceInfo() != nullptr) {
        _tagDeviceInfo* info = m_shm->getDeviceInfo();
        data->pileMaxOutputVoltage = static_cast<uint16_t>(info->nChargerMaxOutVolt);
        data->pileMinOutputVoltage = static_cast<uint16_t>(info->nChargerMinOutVolt);
        data->pileMaxOutputCurrent = static_cast<uint16_t>(info->nChargerMaxOutCurr);
        data->pileMinOutputCurrent = static_cast<uint16_t>(info->nChargerMinOutCurr);
    }
    copyAsciiChars(data->vin, sizeof(data->vin), getYcDesname(SHM2CCU::YC_VIN));
    copyAsciiChars(data->batteryManufacturer, sizeof(data->batteryManufacturer), getYcDesname(SHM2CCU::YC_BATTERY_MANUFACTURER));
    copyAsciiBytes(data->batterySerial, sizeof(data->batterySerial), getYcDesname(SHM2CCU::YC_BATTERY_CHARGE_COUNT));
    fillBatteryProdDate(data->batteryProdYear, data->batteryProdMonth, data->batteryProdDay);
    unsigned int chargeCount = getYcValue(SHM2CCU::YC_BATTERY_CHARGE_COUNT);
    for (int i = 0; i < 3; ++i) {
        data->batteryChargeCount[i] = static_cast<uint8_t>(packChargeCount(chargeCount, i));
    }
    data->batteryPropertyFlag = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_BATTERY_PROPERTY_FLAG));
    copyAsciiBytes(data->bmsSoftwareVersion, sizeof(data->bmsSoftwareVersion), getYcDesname(SHM2CCU::YC_BMS_SOFTWARE_VERSION));
    return true;
}

bool SHMPileController::getStopCompleteData(TCU2CCU_StatusStopCompleteData* data) const
{
    if (data == nullptr || m_shm == nullptr) {
        return false;
    }
    zeroMemory(data, sizeof(*data));
    const char* desname = getYxDesname(SHM2CCU::YX_STOP_COMPLETE);
    if (desname != nullptr) {
        data->stopReason = static_cast<uint8_t>(static_cast<unsigned char>(desname[SHM2CCU::STOP_COMPLETE_OFFSET_STOP_REASON]));
        data->bmsChargeFaultReason = static_cast<uint16_t>(static_cast<unsigned char>(desname[SHM2CCU::STOP_COMPLETE_OFFSET_BMS_CHARGE_FAULT_REASON]));
        data->bmsStopErrorReason = static_cast<uint8_t>(static_cast<unsigned char>(desname[SHM2CCU::STOP_COMPLETE_OFFSET_BMS_STOP_ERROR_REASON]));
    }
    data->stopSoc = static_cast<uint8_t>(getYcValue(SHM2CCU::YC_START_COMPLETE_SOC));
    data->cellMinVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_CELL_MIN_VOLTAGE));
    data->cellMaxVoltage = static_cast<uint16_t>(getYcValue(SHM2CCU::YC_CELL_MAX_VOLTAGE));
    data->batteryMinTemp = getYcSignedValue(SHM2CCU::YC_BATTERY_MIN_TEMP);
    data->batteryMaxTemp = getYcSignedValue(SHM2CCU::YC_BATTERY_MAX_TEMP);
    return true;
}

bool SHMPileController::hasStartCompleteEvent() const
{
    return getYxValue(SHM2CCU::YX_START_COMPLETE);
}

bool SHMPileController::hasStopCompleteEvent() const
{
    return getYxValue(SHM2CCU::YX_STOP_COMPLETE);
}

bool SHMPileController::hasPowerCtrlResponseEvent() const
{
    return getYxValue(SHM2CCU::YX_POWER_CTRL_RESPONSE);
}

unsigned int SHMPileController::getYcRawValueByBase(int baseIndex) const
{
    return getYcValue(baseIndex);
}

void SHMPileController::clearStartCompleteEvent()
{
    clearYxPoint(SHM2CCU::YX_START_COMPLETE);
}

void SHMPileController::clearStopCompleteEvent()
{
    clearYxPoint(SHM2CCU::YX_STOP_COMPLETE);
}

void SHMPileController::clearPowerCtrlResponseEvent()
{
    clearYxPoint(SHM2CCU::YX_POWER_CTRL_RESPONSE);
}

uint8_t SHMPileController::getWorkStatus() const
{
    return getChargePortWorkStatus();
}

uint8_t SHMPileController::getTotalFault() const
{
    return static_cast<uint8_t>(getYxValue(SHM2CCU::YX_TOTAL_FAULT));
}

uint8_t SHMPileController::getTotalAlarm() const
{
    return static_cast<uint8_t>(getYxValue(SHM2CCU::YX_TOTAL_ALARM));
}

bool SHMPileController::parseConfig(const char* config)
{
    if (config == nullptr) {
        return false;
    }
    std::stringstream ss(config);
    std::string item;
    while (std::getline(ss, item, ',')) {
        size_t pos = item.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = item.substr(0, pos);
        std::string value = item.substr(pos + 1);
        if (key == "gun_no") {
            m_gunIndex = std::atoi(value.c_str());
            m_gunNo = static_cast<uint8_t>(m_gunIndex + 1);
        } else if (key == "shm_key") {
            m_shmKey = std::atoi(value.c_str());
        }
    }
    return m_gunIndex >= 0 && m_gunIndex < SHM2CCU::SHM_MAX_GUN_COUNT;
}

int SHMPileController::yxIndex(int baseIndex) const
{
    return SHM2CCU::getGunScopedYxIndex(baseIndex, m_gunIndex);
}

int SHMPileController::ycIndex(int baseIndex) const
{
    return SHM2CCU::getGunScopedYcIndex(baseIndex, m_gunIndex);
}

YX* SHMPileController::getYxPoint(int baseIndex) const
{
    return (m_shm == nullptr) ? nullptr : m_shm->getYx(yxIndex(baseIndex));
}

YC* SHMPileController::getYcPoint(int baseIndex) const
{
    return (m_shm == nullptr) ? nullptr : m_shm->getYc(ycIndex(baseIndex));
}

const char* SHMPileController::getYxDesname(int baseIndex) const
{
    YX* point = getYxPoint(baseIndex);
    return (point == nullptr) ? nullptr : point->desname;
}

const char* SHMPileController::getYcDesname(int baseIndex) const
{
    YC* point = getYcPoint(baseIndex);
    return (point == nullptr) ? nullptr : point->desname;
}

bool SHMPileController::getYxValue(int baseIndex) const
{
    YX* point = getYxPoint(baseIndex);
    return point != nullptr && point->value;
}

unsigned int SHMPileController::getYcValue(int baseIndex) const
{
    YC* point = getYcPoint(baseIndex);
    return (point == nullptr) ? 0U : point->value;
}

int16_t SHMPileController::getYcSignedValue(int baseIndex) const
{
    return static_cast<int16_t>(getYcValue(baseIndex) & 0xFFFFU);
}

uint8_t SHMPileController::getChargePortWorkStatus() const
{
    if (m_shm == nullptr) {
        return 0;
    }
    _tagChargePort* port = m_shm->getChargePort(m_gunIndex);
    return (port == nullptr) ? 0 : port->WorkStatus;
}

void SHMPileController::clearYxPoint(int baseIndex)
{
    YX* point = getYxPoint(baseIndex);
    if (point == nullptr) {
        return;
    }
    memset(point->desname, 0, sizeof(point->desname));
    point->value = false;
}

void SHMPileController::zeroMemory(void* ptr, size_t size) const
{
    if (ptr != nullptr) {
        memset(ptr, 0, size);
    }
}

void SHMPileController::copyAsciiBytes(uint8_t* dest, size_t len, const char* src) const
{
    if (dest == nullptr || len == 0) {
        return;
    }
    memset(dest, 0, len);
    if (src == nullptr) {
        return;
    }
    for (size_t i = 0; i < len && src[i] != '\0'; ++i) {
        dest[i] = static_cast<uint8_t>(src[i]);
    }
}

void SHMPileController::copyAsciiChars(char* dest, size_t len, const char* src) const
{
    if (dest == nullptr || len == 0) {
        return;
    }
    memset(dest, 0, len);
    if (src == nullptr) {
        return;
    }
    for (size_t i = 0; i + 1 < len && src[i] != '\0'; ++i) {
        dest[i] = src[i];
    }
}

void SHMPileController::fillBatteryProdDate(uint8_t& year, uint8_t& month, uint8_t& day) const
{
    unsigned int value = getYcValue(SHM2CCU::YC_BATTERY_MANUFACTURER);
    unsigned int parsedYear = value / 10000U;
    unsigned int parsedMonth = (value / 100U) % 100U;
    unsigned int parsedDay = value % 100U;
    if (parsedYear >= 2000U) {
        parsedYear -= 2000U;
    }
    year = static_cast<uint8_t>(parsedYear & 0xFFU);
    month = static_cast<uint8_t>(parsedMonth & 0xFFU);
    day = static_cast<uint8_t>(parsedDay & 0xFFU);
}
