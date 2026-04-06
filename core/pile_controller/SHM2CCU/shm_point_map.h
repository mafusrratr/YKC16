#ifndef SHM2CCU_SHM_POINT_MAP_H
#define SHM2CCU_SHM_POINT_MAP_H

// BY ZF: Centralized SHM point mapping assumptions for SHM2CCU review.
// BY ZF: This file intentionally contains only point indices and MQTT-facing
// BY ZF: mapping notes. Runtime read/write logic should be added later.

namespace SHM2CCU {

// BY ZF: Gun-scoped SHM YC/YX points use left-gun base indices.
// BY ZF: Right-gun points are addressed by adding SHM_GUN_POINT_OFFSET.
// BY ZF: Example: left YX99 -> right YX355, left YC100 -> right YC356.
static const int SHM_GUN_POINT_OFFSET = 256;
static const int SHM_MAX_GUN_COUNT = 2;

enum GunChannel {
    GUN_LEFT = 0,
    GUN_RIGHT = 1
};

enum ShmPointSource {
    SHM_SOURCE_CHARGE_PORT = 0,
    SHM_SOURCE_YC = 1,
    SHM_SOURCE_YX = 2,
    SHM_SOURCE_DD = 3,
    SHM_SOURCE_EVS_REMOTE_START = 4,
    SHM_SOURCE_REMOTE_PUBLIC = 5,
    SHM_SOURCE_EVS_TRADE_RECORD = 6
};

struct MqttFieldBinding {
    const char* mqttField;
    ShmPointSource source;
    int index;
    double scale;
};

struct MqttCommandBinding {
    const char* cmd;
    int triggerYxIndex;
    int paramYcIndex;
    bool supported;
    const char* note;
};

enum CoverageStatus {
    SHM_COVER_DIRECT = 0,
    SHM_COVER_COMPOSED = 1,
    SHM_COVER_MISSING = 2,
    SHM_COVER_IGNORED = 3
};

struct StructFieldCoverage {
    const char* fieldName;
    CoverageStatus status;
    ShmPointSource source;
    int index;
    const char* note;
};

inline int getGunScopedYxIndex(int baseIndex, int gunIndex)
{
    return baseIndex + gunIndex * SHM_GUN_POINT_OFFSET;
}

inline int getGunScopedYcIndex(int baseIndex, int gunIndex)
{
    return baseIndex + gunIndex * SHM_GUN_POINT_OFFSET;
}

enum ChargePortField {
    CHARGE_PORT_FIELD_PORT_ID = 0,
    CHARGE_PORT_FIELD_WORK_STATUS = 1,
    CHARGE_PORT_FIELD_QR_CODE = 2
};

enum StartChargeDesnameOffset {
    START_CHARGE_OFFSET_START_MODE = 0,
    START_CHARGE_OFFSET_CHARGE_DISCHARGE_MODE = 1,
    START_CHARGE_OFFSET_AUX_POWER_VOLTAGE = 2,
    START_CHARGE_OFFSET_PULSE_MODE = 3,
    START_CHARGE_OFFSET_V2G_MODE = 4
};

enum StartCompleteDesnameOffset {
    START_COMPLETE_OFFSET_SUCCESS_FLAG = 0,
    START_COMPLETE_OFFSET_FAIL_REASON = 1
};

enum StopCompleteDesnameOffset {
    STOP_COMPLETE_OFFSET_STOP_REASON = 0,
    STOP_COMPLETE_OFFSET_BMS_CHARGE_FAULT_REASON = 1,
    STOP_COMPLETE_OFFSET_BMS_STOP_ERROR_REASON = 2,
    STOP_COMPLETE_OFFSET_EXTRA_3 = 3,
    STOP_COMPLETE_OFFSET_EXTRA_4 = 4
};

enum ShmYcPoint {
    YC_PORT_ID = 100,
    YC_OUTPUT_VOLTAGE = 101,
    YC_OUTPUT_CURRENT = 102,
    YC_SOC = 103,
    YC_BATTERY_MIN_TEMP = 104,
    YC_BATTERY_MAX_TEMP = 105,
    YC_CELL_MAX_VOLTAGE = 106,
    YC_CELL_MIN_VOLTAGE = 107,
    YC_PILE_ENV_TEMP = 108,
    YC_GUIDE_VOLTAGE = 109,
    YC_BMS_REQ_VOLTAGE = 110,
    YC_BMS_REQ_CURRENT = 111,
    YC_CHARGE_MODE = 112,
    YC_BMS_MEASURED_VOLTAGE = 113,
    YC_BMS_MEASURED_CURRENT = 114,
    YC_ESTIMATED_REMAIN_TIME = 115,
    YC_INTERFACE_TEMP_1 = 116,
    YC_INTERFACE_TEMP_2 = 117,
    YC_INTERFACE_TEMP_3 = 118,
    YC_INTERFACE_TEMP_4 = 119,
    YC_BATTERY_TYPE = 120,
    YC_MAX_ALLOW_TEMP = 121,
    YC_BMS_MAX_CHARGE_VOLTAGE = 122,
    YC_CELL_MAX_CHARGE_VOLTAGE = 123,
    YC_MAX_ALLOW_CHARGE_CURRENT = 124,
    YC_RATED_TOTAL_VOLTAGE = 125,
    YC_CURRENT_TOTAL_VOLTAGE = 126,
    YC_RATED_CAPACITY = 127,
    YC_NOMINAL_ENERGY = 128,
    YC_START_COMPLETE_SOC = 129,
    YC_VIN = 130,
    YC_BATTERY_CHARGE_COUNT = 131,
    YC_BATTERY_MANUFACTURER = 132,
    YC_BATTERY_PROPERTY_FLAG = 133,
    YC_BMS_SOFTWARE_VERSION = 134,
    YC_SUPPORT_CHARGE_METHOD = 135,
    YC_CHARGE_ENERGY = 136,
    YC_CHARGE_ENERGY_J = 137,
    YC_CHARGE_ENERGY_F = 138,
    YC_CHARGE_ENERGY_P = 139,
    YC_CHARGE_ENERGY_G = 140,
    YC_CHARGE_START_J = 141,
    YC_CHARGE_START_F = 142,
    YC_CHARGE_START_P = 143,
    YC_CHARGE_START_G = 144,
    YC_CHARGE_END_J = 145,
    YC_CHARGE_END_F = 146,
    YC_CHARGE_END_P = 147,
    YC_CHARGE_END_G = 148,
    YC_CHARGE_SUM_START = 149,
    YC_CHARGE_SUM_END = 150,
    YC_DISCHARGE_ENERGY = 151,
    YC_DISCHARGE_ENERGY_J = 152,
    YC_DISCHARGE_ENERGY_F = 153,
    YC_DISCHARGE_ENERGY_P = 154,
    YC_DISCHARGE_ENERGY_G = 155,
    YC_DISCHARGE_START_J = 156,
    YC_DISCHARGE_START_F = 157,
    YC_DISCHARGE_START_P = 158,
    YC_DISCHARGE_START_G = 159,
    YC_DISCHARGE_END_J = 160,
    YC_DISCHARGE_END_F = 161,
    YC_DISCHARGE_END_P = 162,
    YC_DISCHARGE_END_G = 163,
    YC_DISCHARGE_SUM_START = 164,
    YC_DISCHARGE_SUM_END = 165,
    YC_CURRENT_TRANSFORM_RATIO = 166,
    YC_CURRENT_AMOUNT = 167,
    YC_CUMULATIVE_CHARGE_SECONDS = 168,
    YC_AMOUNT_DELTA_TO_FULL = 169,
    YC_DC_INSTANT_VOLTAGE = 170,
    YC_METER_NO = 190,
    YC_METER_ASSET_NO = 191,
    YC_METER_BASE_J = 192,
    YC_METER_BASE_F = 193,
    YC_METER_BASE_P = 194,
    YC_METER_BASE_G = 195,
    YC_START_RESULT = 196,
    YC_POWER_CTRL_PARAM = 197,
    YC_POWER_CTRL_RESULT = 198,
    YC_MAX_VOLTAGE_CELL_NO = 199,
    YC_MAX_TEMP_POINT_NO = 200,
    YC_MIN_TEMP_POINT_NO = 201,
    YC_INLET_TEMP = 202,
    YC_OUTLET_TEMP = 203,
    YC_ENV_HUMIDITY = 204,
    YC_AUX_POWER_VOLTAGE = 205,
    YC_PLUG_AND_CHARGE_STOP_CODE = 208,
    YC_HMI_MAIN_STATE = 209,
    YC_PLUG_AND_CHARGE_STATE = 210
};

enum ShmYxPoint {
    YX_PORT_TOTAL_FAULT = 99,
    YX_TOTAL_FAULT = 100,
    YX_TOTAL_ALARM = 101,
    YX_EMERGENCY_STOP_FAULT = 102,
    YX_SMOKE_FAULT = 103,
    YX_AC_INPUT_BREAKER_FAULT = 104,
    YX_DC_BUS_CONTACTOR_FAULT = 105,
    YX_DC_BUS_FUSE_FAULT = 106,
    YX_ELECTRONIC_LOCK_FAULT = 107,
    YX_FAN_FAULT = 108,
    YX_LIGHTNING_ARRESTER_FAULT = 109,
    YX_INSULATION_MONITOR_FAULT = 110,
    YX_BATTERY_REVERSE_FAULT = 111,
    YX_GUIDE_FAULT = 112,
    YX_PILE_OVER_TEMP_FAULT = 113,
    YX_INTERFACE_OVER_TEMP_FAULT = 114,
    YX_GUN_NOT_RETURN_ALARM = 115,
    YX_BMS_COMM_FAULT = 116,
    YX_INPUT_OVER_VOLTAGE_FAULT = 117,
    YX_INPUT_UNDER_VOLTAGE_FAULT = 118,
    YX_DC_BUS_OVER_VOLTAGE_FAULT = 119,
    YX_DC_BUS_UNDER_VOLTAGE_FAULT = 120,
    YX_DC_BUS_OVER_CURRENT_FAULT = 121,
    YX_MODULE_FAULT = 122,
    YX_MODULE_AC_INPUT_FAULT = 123,
    YX_MODULE_AC_OVER_VOLTAGE_FAULT = 124,
    YX_MODULE_AC_UNDER_VOLTAGE_FAULT = 125,
    YX_MODULE_AC_PHASE_LOSS_FAULT = 126,
    YX_MODULE_DC_SHORT_FAULT = 127,
    YX_MODULE_DC_OVER_CURRENT_FAULT = 128,
    YX_MODULE_DC_OVER_VOLTAGE_FAULT = 129,
    YX_MODULE_DC_UNDER_VOLTAGE_FAULT = 130,
    YX_MODULE_OVER_TEMP_FAULT = 131,
    YX_MODULE_COMM_FAULT = 132,
    YX_MODULE_FAN_FAULT = 133,
    YX_VEHICLE_CONNECT_STATUS = 134,
    YX_GUN_SEAT_STATUS = 135,
    YX_ELECTRONIC_LOCK_STATUS = 136,
    YX_DC_CONTACTOR_STATUS = 137,
    YX_PILE_DOOR_FAULT = 138,
    YX_DC_CONTACTOR_STICK_FAULT = 139,
    YX_INSULATION_MONITOR_ALARM = 140,
    YX_DISCHARGE_FAULT = 141,
    YX_PILE_OVER_TEMP_ALARM = 142,
    YX_GUN_OVER_TEMP_ALARM = 143,
    YX_AC_INPUT_CONTACTOR_FAULT = 144,
    YX_AC_INPUT_CONTACTOR_STICK_FAULT = 145,
    YX_AUX_POWER_FAULT = 146,
    YX_BRIDGE_CONTACTOR_FAULT = 147,
    YX_BRIDGE_CONTACTOR_STICK_FAULT = 148,
    YX_BRIDGE_CONTACTOR_STATUS = 149,
    YX_MODULE_EMERGENCY_STOP_STATUS = 150,
    YX_PILE_WATER_FAULT = 151,
    YX_CABINET_DOOR_FAULT = 152,
    YX_CABINET_WATER_FAULT = 153,
    YX_CABINET_SMOKE_FAULT = 154,
    YX_POWER_CTRL_COMM_FAULT = 155,
    YX_POWER_CTRL_COMM_ALARM = 156,
    YX_POWER_CTRL_FAULT = 157,
    YX_POWER_CTRL_ALARM = 158,
    YX_SWITCH_MODULE_COMM_FAULT = 159,
    YX_SWITCH_MODULE_COMM_ALARM = 160,
    YX_SWITCH_MODULE_FAULT = 161,
    YX_SWITCH_MODULE_ALARM = 162,
    YX_BMS_FAULT_BY_CTRL = 163,
    YX_BMS_SEND_FAULT_INFO = 164,
    YX_MODULE_DISCHARGE_FAULT = 165,
    YX_METER_DATA_ABNORMAL = 166,
    YX_PILE_OTHER_FAULT = 167,
    YX_CAN_VERSION_CHECK_FAULT = 168,
    YX_TCU_OTHER_FAULT = 169,
    YX_OTHER_FAULT_VALUE = 170,
    YX_APPOINTMENT_START_FLAG = 171,
    YX_FIRST_AUTH_OK = 172,
    YX_SECOND_AUTH_OK = 173,
    YX_METER_START_FLAG = 174,
    YX_CAN_START_CHARGE = 175,
    YX_FIRST_TRADE_STORE = 176,
    YX_AUTH_OK_DURING_CHARGE = 177,
    YX_SECOND_TRADE_STORE = 178,
    YX_CAN_STOP_CHARGE = 179,
    YX_SETTLEMENT_POPUP = 180,
    YX_AMOUNT_FULL = 181,
    YX_STOP_COMPLETE = 182,
    YX_UNSETTLED_UNPLUG = 183,
    YX_NOTIFY_CARD_NOT_UNPLUG = 184,
    YX_PILE_STARTED = 185,
    YX_PILE_START_FAILED = 186,
    YX_HMI_START_TIMEOUT_90S = 187,
    YX_HMI_CURRENT_PAGE = 188,
    YX_POWER_CTRL_REQUEST = 189,
    YX_POWER_CTRL_RESPONSE = 190,
    YX_POWER_CTRL_ACTIVE = 191,
    YX_SERVICE_CTRL_REQUEST = 192,
    YX_SERVICE_CTRL_RESPONSE = 193,
    YX_TRICKLE_TIMEOUT_STOP = 194,
    YX_PNC_AUTH_OK = 195,
    YX_PNC_START_REQUEST = 196,
    YX_AC_INPUT_CONTACTOR_STATUS = 197,
    YX_AC_CONTACTOR_CTRL_STATUS = 198,
    YX_PORT_K1_CTRL_STATUS = 199,
    YX_PORT_K2_CTRL_STATUS = 200,
    YX_PORT_AUX_POWER_OUTPUT_STATUS = 201,
    YX_PORT_AUX_POWER_CTRL_STATUS = 202,
    YX_CABINET_FAN_CTRL_STATUS = 203,
    YX_PORT_LOCK_CTRL_STATUS = 204,
    YX_PE_BREAK_FAULT = 205,
    YX_GUIDE_VOLTAGE_ABNORMAL = 206,
    YX_MODULE_START_FAIL = 207,
    YX_MODULE_SHUTDOWN_FAIL = 208,
    YX_MODULE_ADDR_ABNORMAL = 209,
    YX_COOLING_SYSTEM_FAULT = 210,
    YX_CABINET_OVER_TEMP_ALARM = 211,
    YX_CABINET_OVER_TEMP_FAULT = 212,
    YX_POWER_DIST_STRATEGY_FAIL = 213,
    YX_NO_IDLE_MODULE_FAULT = 214,
    YX_PRECHARGE_VOLTAGE_FAIL = 215,
    YX_POWER_CTRL_COMM_TIMEOUT = 216,
    YX_ENV_HUMIDITY_ALARM = 217,
    YX_QR_READY_OR_DOUBLE_GUN_REPLY = 218,
    YX_SCREEN_UNLOCK_CTRL = 219,
    YX_CAN_TIMEOUT_BITMAP = 220,
    YX_HMI_LOCAL_PNC_START = 222,
    YX_TCU_POWER_CTRL_RESPONSE_TIMEOUT = 228,
    YX_TCU_CHARGE_PARAM_RESPONSE_TIMEOUT = 230,
    YX_TCU_WAIT_START_COMPLETE_TIMEOUT = 232,
    YX_TCU_WAIT_STOP_COMPLETE_TIMEOUT = 234,
    YX_TCU_SERVICE_CTRL_RESPONSE_TIMEOUT = 236,
    YX_TCU_ELOCK_RESPONSE_TIMEOUT = 237,
    YX_LOCK_REMOTE_CONTROL = 340,
    YX_LOCK_REMOTE_RESULT = 341,
    YX_START_COMPLETE = 343,
    YX_CAR_PLATE_AUTH_RESULT = 345
};

enum ShmDdPoint {
    DD_DC_FORWARD_ENERGY = 0,
    DD_DC_REVERSE_ENERGY = 1,
    DD_CHARGE_ENERGY = 2,
    DD_CHARGE_ENERGY_J = 3,
    DD_CHARGE_ENERGY_F = 4,
    DD_CHARGE_ENERGY_P = 5,
    DD_CHARGE_ENERGY_G = 6,
    DD_CHARGE_START_J = 7,
    DD_CHARGE_START_F = 8,
    DD_CHARGE_START_P = 9,
    DD_CHARGE_START_G = 10,
    DD_CHARGE_END_J = 11,
    DD_CHARGE_END_F = 12,
    DD_CHARGE_END_P = 13,
    DD_CHARGE_END_G = 14,
    DD_CHARGE_SUM_START = 15,
    DD_CHARGE_SUM_END = 16,
    DD_DISCHARGE_ENERGY = 17,
    DD_DISCHARGE_ENERGY_J = 18,
    DD_DISCHARGE_ENERGY_F = 19,
    DD_DISCHARGE_ENERGY_P = 20,
    DD_DISCHARGE_ENERGY_G = 21,
    DD_DISCHARGE_START_J = 22,
    DD_DISCHARGE_START_F = 23,
    DD_DISCHARGE_START_P = 24,
    DD_DISCHARGE_START_G = 25,
    DD_DISCHARGE_END_J = 26,
    DD_DISCHARGE_END_F = 27,
    DD_DISCHARGE_END_P = 28,
    DD_DISCHARGE_END_G = 29,
    DD_DISCHARGE_SUM_START = 30,
    DD_DISCHARGE_SUM_END = 31,
    DD_CHARGE_TOTAL_POWER_FEE = 32,
    DD_CHARGE_TOTAL_SERVICE_FEE = 33,
    DD_CHARGE_POWER_FEE_J = 34,
    DD_CHARGE_POWER_FEE_F = 35,
    DD_CHARGE_POWER_FEE_P = 36,
    DD_CHARGE_POWER_FEE_G = 37,
    DD_CHARGE_SERVICE_FEE_J = 38,
    DD_CHARGE_SERVICE_FEE_F = 39,
    DD_CHARGE_SERVICE_FEE_P = 40,
    DD_CHARGE_SERVICE_FEE_G = 41,
    DD_DISCHARGE_TOTAL_POWER_FEE = 42,
    DD_DISCHARGE_TOTAL_SERVICE_FEE = 43,
    DD_DISCHARGE_POWER_FEE_J = 44,
    DD_DISCHARGE_POWER_FEE_F = 45,
    DD_DISCHARGE_POWER_FEE_P = 46,
    DD_DISCHARGE_POWER_FEE_G = 47,
    DD_DISCHARGE_SERVICE_FEE_J = 48,
    DD_DISCHARGE_SERVICE_FEE_F = 49,
    DD_DISCHARGE_SERVICE_FEE_P = 50,
    DD_DISCHARGE_SERVICE_FEE_G = 51,
    DD_CHARGE_TOTAL_COST = 52,
    DD_DISCHARGE_TOTAL_COST = 53
};

// BY ZF: MQTT "yc" compatibility plan for SHM2CCU.
static const MqttFieldBinding kMqttYcBindings[] = {
    { "outputVoltage", SHM_SOURCE_YC, YC_OUTPUT_VOLTAGE, 0.1 },
    { "outputCurrent", SHM_SOURCE_YC, YC_OUTPUT_CURRENT, 0.1 },
    { "soc", SHM_SOURCE_YC, YC_SOC, 1.0 },
    { "batteryMinTemp", SHM_SOURCE_YC, YC_BATTERY_MIN_TEMP, 1.0 },
    { "batteryMaxTemp", SHM_SOURCE_YC, YC_BATTERY_MAX_TEMP, 1.0 },
    { "cellMaxVoltage", SHM_SOURCE_YC, YC_CELL_MAX_VOLTAGE, 0.1 },
    { "cellMinVoltage", SHM_SOURCE_YC, YC_CELL_MIN_VOLTAGE, 0.1 },
    { "pileEnvTemp", SHM_SOURCE_YC, YC_PILE_ENV_TEMP, 1.0 },
    { "guideVoltage", SHM_SOURCE_YC, YC_GUIDE_VOLTAGE, 0.1 },
    { "bmsReqVoltage", SHM_SOURCE_YC, YC_BMS_REQ_VOLTAGE, 0.1 },
    { "bmsReqCurrent", SHM_SOURCE_YC, YC_BMS_REQ_CURRENT, 0.1 },
    { "chargeMode", SHM_SOURCE_YC, YC_CHARGE_MODE, 1.0 },
    { "bmsMeasuredVoltage", SHM_SOURCE_YC, YC_BMS_MEASURED_VOLTAGE, 0.1 },
    { "bmsMeasuredCurrent", SHM_SOURCE_YC, YC_BMS_MEASURED_CURRENT, 0.1 },
    { "estimatedRemainTime", SHM_SOURCE_YC, YC_ESTIMATED_REMAIN_TIME, 1.0 },
    { "interfaceTemp1", SHM_SOURCE_YC, YC_INTERFACE_TEMP_1, 1.0 },
    { "interfaceTemp2", SHM_SOURCE_YC, YC_INTERFACE_TEMP_2, 1.0 },
    { "interfaceTemp3", SHM_SOURCE_YC, YC_INTERFACE_TEMP_3, 1.0 },
    { "interfaceTemp4", SHM_SOURCE_YC, YC_INTERFACE_TEMP_4, 1.0 },
    { "maxVoltageCellNo", SHM_SOURCE_YC, YC_MAX_VOLTAGE_CELL_NO, 1.0 },
    { "maxTempPointNo", SHM_SOURCE_YC, YC_MAX_TEMP_POINT_NO, 1.0 },
    { "minTempPointNo", SHM_SOURCE_YC, YC_MIN_TEMP_POINT_NO, 1.0 },
    { "inletTemp", SHM_SOURCE_YC, YC_INLET_TEMP, 1.0 },
    { "outletTemp", SHM_SOURCE_YC, YC_OUTLET_TEMP, 1.0 },
    { "envHumidity", SHM_SOURCE_YC, YC_ENV_HUMIDITY, 1.0 }
};

// BY ZF: MQTT "yx" compatibility plan for SHM2CCU.
static const MqttFieldBinding kMqttYxBindings[] = {
    { "workStatus", SHM_SOURCE_CHARGE_PORT, CHARGE_PORT_FIELD_WORK_STATUS, 1.0 },
    { "totalFault", SHM_SOURCE_YX, YX_TOTAL_FAULT, 1.0 },
    { "totalAlarm", SHM_SOURCE_YX, YX_TOTAL_ALARM, 1.0 },
    { "emergencyStopFault", SHM_SOURCE_YX, YX_EMERGENCY_STOP_FAULT, 1.0 },
    { "vehicleConnectStatus", SHM_SOURCE_YX, YX_VEHICLE_CONNECT_STATUS, 1.0 },
    { "vinReq", SHM_SOURCE_YX, YX_PNC_START_REQUEST, 1.0 },
    { "gunSeatStatus", SHM_SOURCE_YX, YX_GUN_SEAT_STATUS, 1.0 },
    { "electronicLockStatus", SHM_SOURCE_YX, YX_ELECTRONIC_LOCK_STATUS, 1.0 },
    { "dcContactorStatus", SHM_SOURCE_YX, YX_DC_CONTACTOR_STATUS, 1.0 },
    { "otherFault", SHM_SOURCE_YX, YX_OTHER_FAULT_VALUE, 1.0 }
};

// BY ZF: SHM writeback plan for MQTT commands.
static const MqttCommandBinding kMqttCommandBindings[] = {
    { "start_charge", YX_CAN_START_CHARGE, -1, true, "write YX175.desname[0]=start mode, [1]=charge/discharge, [2]=aux power, [3]=pulse, [4]=V2G; and evs_getRemoteStart(port)" },
    { "stop_charge", YX_CAN_STOP_CHARGE, -1, true, "write YX179.desname[0] stop reason; tcuStopCode not found" },
    { "power_ctrl", YX_POWER_CTRL_REQUEST, YC_POWER_CTRL_PARAM, true, "write YC197 then set YX189" },
    { "outputVA_ctrl", -1, -1, false, "no confirmed SHM point" },
    { "clear_fault", -1, -1, false, "no confirmed SHM point" }
};

// BY ZF: SHM status points used to synthesize MQTT events.
static const MqttFieldBinding kEventKeyPointBindings[] = {
    { "start_complete_flag", SHM_SOURCE_YX, YX_START_COMPLETE, 1.0 },
    { "start_fail_flag", SHM_SOURCE_YX, YX_PILE_START_FAILED, 1.0 },
    { "stop_complete_flag", SHM_SOURCE_YX, YX_STOP_COMPLETE, 1.0 },
    { "power_ctrl_response", SHM_SOURCE_YX, YX_POWER_CTRL_RESPONSE, 1.0 },
    { "power_ctrl_active", SHM_SOURCE_YX, YX_POWER_CTRL_ACTIVE, 1.0 },
    { "pile_other_fault", SHM_SOURCE_YX, YX_PILE_OTHER_FAULT, 1.0 },
    { "can_timeout_bitmap", SHM_SOURCE_YX, YX_CAN_TIMEOUT_BITMAP, 1.0 },
    { "start_result", SHM_SOURCE_YC, YC_START_RESULT, 1.0 },
    { "power_ctrl_result", SHM_SOURCE_YC, YC_POWER_CTRL_RESULT, 1.0 },
    { "cumulative_charge_seconds", SHM_SOURCE_YC, YC_CUMULATIVE_CHARGE_SECONDS, 1.0 },
    { "charge_total_cost", SHM_SOURCE_DD, DD_CHARGE_TOTAL_COST, 0.0001 },
    { "discharge_total_cost", SHM_SOURCE_DD, DD_DISCHARGE_TOTAL_COST, 0.0001 }
};

// BY ZF: Coverage audit for TCU2CCU_CmdStartChargeData.
static const StructFieldCoverage kCmdStartChargeCoverage[] = {
    { "loadControlSwitch", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "use default value in SHM mode; user requested to ignore" },
    { "plugAndChargeFlag", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_CAN_START_CHARGE, "YX175.desname[0]: 01 normal, 02 plug-and-charge, 05/06 double-gun one-car mode" },
    { "auxPowerVoltage", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_CAN_START_CHARGE, "YX175.desname[2]: 12/24" },
    { "mergeChargeFlag", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_CAN_START_CHARGE, "YX175.desname[0]: 05/06 indicate double-gun one-car mode" },
    { "v2g", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_CAN_START_CHARGE, "YX175.desname[1]: 1 charge 2 discharge; desname[4]: 1 V2G 2 normal charge" }
};

// BY ZF: Coverage audit for TCU2CCU_StartChargeResponseData.
static const StructFieldCoverage kStartChargeResponseCoverage[] = {
    { "loadControlSwitch", SHM_COVER_MISSING, SHM_SOURCE_YX, -1, "no SHM response field found" },
    { "confirmFlag", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "shared memory delivery is reliable; user requested not to model confirmFlag" },
    { "startFailReason", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_START_COMPLETE, "no explicit start-response point; nearest usable reason is YX343.desname[1], fallback YC196" },
    { "plugAndChargeFlag", SHM_COVER_MISSING, SHM_SOURCE_YX, -1, "no SHM response field found" },
    { "auxPowerVoltage", SHM_COVER_MISSING, SHM_SOURCE_YX, -1, "no SHM response field found" }
};

// BY ZF: Coverage audit for TCU2CCU_CmdStopChargeData / StopChargeResponseData.
static const StructFieldCoverage kCmdStopChargeCoverage[] = {
    { "stopReason", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_CAN_STOP_CHARGE, "YX179.desname[0]" },
    { "tcuStopCode", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_CAN_STOP_CHARGE, "reuse YX179.desname[0] stop category: 1 normal, 2 amount full, 3 fault, 4 appointment stop" }
};

static const StructFieldCoverage kStopChargeResponseCoverage[] = {
    { "confirmFlag", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "shared memory delivery is reliable; user requested not to model confirmFlag" }
};

// BY ZF: Coverage audit for version / charge parameter / pile id / config style commands.
static const StructFieldCoverage kVersionCheckCoverage[] = {
    { "cmd.versionMajorMinor", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_HMI_MAIN_STATE, "local config YC65 PileVersion and YC221 can provide reference value, not an SHM command point" },
    { "resp.versionMajorMinor", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 221, "YC221/YC220/YC222 and deviceInfo.sPileVersion can reconstruct versions; no explicit handshake response point" }
};

static const StructFieldCoverage kChargeParamCoverage[] = {
    { "cmd.pileId", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 36, "YC36 and deviceInfo.Dev_Charging_Number provide pile identity, not exact CAN frame semantics" },
    { "resp.confirmFlag", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "CAN-specific response concept; user requested to skip in SHM mode" },
    { "resp.failReason", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "CAN-specific response concept; user requested to skip in SHM mode" }
};

static const StructFieldCoverage kPileConfigCoverage[] = {
    { "manufacturerCode", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.szFactorySN" },
    { "controllerSerial", SHM_COVER_MISSING, SHM_SOURCE_YC, -1, "no controller serial SHM field found" },
    { "softwareVersion", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 218, "YC218 or deviceInfo.pileSoftwareVer" },
    { "softwareDate", SHM_COVER_MISSING, SHM_SOURCE_YC, -1, "no dedicated software date SHM field found" },
    { "maxOutputVoltage", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMaxOutVolt" },
    { "minOutputVoltage", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMinOutVolt" },
    { "maxOutputCurrent", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMaxOutCurr" },
    { "minOutputCurrent", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMinOutCurr" }
};

static const StructFieldCoverage kOtherCommandCoverage[] = {
    { "serviceCmd", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_SERVICE_CTRL_REQUEST, "YX192 request and YX193 response exist, but command payload encoding is not documented" },
    { "lockCmd", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_LOCK_REMOTE_CONTROL, "YX340.desname[0]=10 lock, 11 unlock" },
    { "lockConfirmFlag", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_LOCK_REMOTE_RESULT, "YX341.desname[0]=10 success, 11/12/13/14 failure kinds" },
    { "adjustType", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_POWER_CTRL_PARAM, "YC197 high byte type" },
    { "adjustParam", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_POWER_CTRL_PARAM, "YC197 low bits parameter; note doc says 1kW / 1%% semantics, not 0.1kW raw CAN semantics" },
    { "setParams.voltageCurrent", SHM_COVER_IGNORED, SHM_SOURCE_YC, -1, "CAN-specific; user requested to skip in SHM mode" },
    { "outputVA.demandVoltageCurrent", SHM_COVER_MISSING, SHM_SOURCE_YC, -1, "user confirmed not to implement" },
    { "clearFault.faultCode", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "CAN-specific; user requested to skip in SHM mode" }
};

// BY ZF: Coverage audit for TCU2CCU_StatusStartCompleteData.
static const StructFieldCoverage kStatusStartCompleteCoverage[] = {
    { "successFlag", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_START_COMPLETE, "YX343.desname[0]" },
    { "chargeFailReason", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_START_COMPLETE, "YX343.desname[1]" },
    { "pileBmsVersion", SHM_COVER_IGNORED, SHM_SOURCE_YC, -1, "CAN-specific; user requested to skip in SHM mode" },
    { "bmsPileVersion", SHM_COVER_IGNORED, SHM_SOURCE_YC, -1, "CAN-specific; user requested to skip in SHM mode" },
    { "handshakeResult", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "CAN-specific; user requested to skip in SHM mode" },
    { "batteryType", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BATTERY_TYPE, "YC120 / desname[0]" },
    { "maxAllowTemp", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_MAX_ALLOW_TEMP, "" },
    { "bmsMaxChargeVoltage", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BMS_MAX_CHARGE_VOLTAGE, "" },
    { "cellMaxChargeVoltage", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_CELL_MAX_CHARGE_VOLTAGE, "" },
    { "maxAllowChargeCurrent", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_MAX_ALLOW_CHARGE_CURRENT, "" },
    { "ratedTotalVoltage", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_RATED_TOTAL_VOLTAGE, "" },
    { "currentTotalVoltage", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_CURRENT_TOTAL_VOLTAGE, "" },
    { "ratedCapacity", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_RATED_CAPACITY, "" },
    { "nominalEnergy", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_NOMINAL_ENERGY, "" },
    { "soc", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_START_COMPLETE_SOC, "" },
    { "pileMaxOutputVoltage", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMaxOutVolt" },
    { "pileMinOutputVoltage", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMinOutVolt" },
    { "pileMaxOutputCurrent", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMaxOutCurr" },
    { "pileMinOutputCurrent", SHM_COVER_COMPOSED, SHM_SOURCE_YC, 220, "prefer deviceInfo.nChargerMinOutCurr" },
    { "vin", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_VIN, "YC130.desname" },
    { "batteryManufacturer", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BATTERY_MANUFACTURER, "YC132.desname" },
    { "batterySerial", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_BATTERY_CHARGE_COUNT, "YC131.desname stores battery serial" },
    { "batteryProdYearMonthDay", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_BATTERY_MANUFACTURER, "YC132.value stores production date combination" },
    { "batteryChargeCount", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BATTERY_CHARGE_COUNT, "" },
    { "batteryPropertyFlag", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BATTERY_PROPERTY_FLAG, "" },
    { "bmsSoftwareVersion", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BMS_SOFTWARE_VERSION, "YC134.desname" }
};

// BY ZF: Coverage audit for TCU2CCU_StatusStopCompleteData.
static const StructFieldCoverage kStatusStopCompleteCoverage[] = {
    { "stopReason", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_STOP_COMPLETE, "YX182.desname[0]" },
    { "stopSuccessFlag", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "SHM stop-complete only needs completion edge; explicit success flag is not required in current design" },
    { "bmsStopReason", SHM_COVER_IGNORED, SHM_SOURCE_YX, -1, "no confirmed dedicated SHM point; user requested to ignore" },
    { "bmsChargeFaultReason", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_STOP_COMPLETE, "YX182.desname[1]" },
    { "bmsStopErrorReason", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_STOP_COMPLETE, "YX182.desname[2]" },
    { "stopSoc", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_START_COMPLETE_SOC, "fallback to YC129 or evs trade record endSoc; no dedicated stop snapshot point confirmed" },
    { "cellMinVoltage", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_CELL_MIN_VOLTAGE, "fallback to current YC107; stop snapshot not confirmed" },
    { "cellMaxVoltage", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_CELL_MAX_VOLTAGE, "fallback to current YC106; stop snapshot not confirmed" },
    { "batteryMinTemp", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_BATTERY_MIN_TEMP, "fallback to current YC104; stop snapshot not confirmed" },
    { "batteryMaxTemp", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_BATTERY_MAX_TEMP, "fallback to current YC105; stop snapshot not confirmed" },
    { "timeoutInfo", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_CAN_TIMEOUT_BITMAP, "YX220.desname bitmap and timeout points YX228/230/232/234/236/237; cannot fully restore original 2-bit fields" },
    { "pileOtherError", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_OTHER_FAULT_VALUE, "combine YX167/YX170/YX220 as needed" }
};

// BY ZF: Coverage audit for state/data structs used by current CAN2CCU flow.
static const StructFieldCoverage kStatusPileStateCoverage[] = {
    { "pileStatus", SHM_COVER_COMPOSED, SHM_SOURCE_CHARGE_PORT, CHARGE_PORT_FIELD_WORK_STATUS, "prefer chargePort.WorkStatus" },
    { "vin", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_VIN, "YC130.desname" },
    { "batteryChargeCount", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BATTERY_CHARGE_COUNT, "" },
    { "soc", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_START_COMPLETE_SOC, "" },
    { "currentBatteryVoltage", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_CURRENT_TOTAL_VOLTAGE, "" }
};

static const StructFieldCoverage kStatusVehicleIdCoverage[] = {
    { "vin", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_VIN, "YC130.desname" },
    { "batteryChargeCount", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_BATTERY_CHARGE_COUNT, "" },
    { "soc", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_START_COMPLETE_SOC, "" },
    { "currentBatteryVoltage", SHM_COVER_DIRECT, SHM_SOURCE_YC, YC_CURRENT_TOTAL_VOLTAGE, "" },
    { "vinAuthResult", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_PNC_AUTH_OK, "prefer YX195.desname[0]/[1]; generic VIN auth result not fully standardized in doc" }
};

static const StructFieldCoverage kDataYC21Coverage[] = {
    { "currentEnergy", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_CHARGE_ENERGY, "prefer YC136; DD2 provides higher precision 0.0001 value" },
    { "chargeDuration", SHM_COVER_COMPOSED, SHM_SOURCE_YC, YC_CUMULATIVE_CHARGE_SECONDS, "YC168 is seconds; requires unit conversion to minutes" }
};

static const StructFieldCoverage kDataYX22Coverage[] = {
    { "workStatus", SHM_COVER_COMPOSED, SHM_SOURCE_CHARGE_PORT, CHARGE_PORT_FIELD_WORK_STATUS, "from chargePort.WorkStatus" },
    { "faultBits102to137", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_EMERGENCY_STOP_FAULT, "direct one-to-one mapping through YX137" },
    { "otherFault", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_OTHER_FAULT_VALUE, "YX170.value with auxiliary fault detail in desname" }
};

static const StructFieldCoverage kDataYX23Coverage[] = {
    { "faultBits138to165", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_PILE_DOOR_FAULT, "direct one-to-one mapping through YX165" },
    { "vinReq", SHM_COVER_COMPOSED, SHM_SOURCE_YX, YX_PNC_START_REQUEST, "mapped to YX196 request plug-and-charge start" },
    { "statusBits197to217", SHM_COVER_DIRECT, SHM_SOURCE_YX, YX_AC_INPUT_CONTACTOR_STATUS, "direct one-to-one mapping through YX217" }
};

}  // namespace SHM2CCU

#endif  // SHM2CCU_SHM_POINT_MAP_H
