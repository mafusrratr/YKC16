/**
 * SHM2CCU process implementation.
 * BY ZF
 */

#include "pile_controller_process.h"
#include "shm_pile_controller.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cjson/cJSON.h>

// 是否支持即插即充功能
#define SHM2CCU_PLATFORM_ONLINE_PNC 1

namespace {

#if SHM2CCU_PLATFORM_ONLINE_PNC 
static const unsigned int kPlatformOnlineYc32Value = 2U;
#else
static const unsigned int kPlatformOnlineYc32Value = 1U;
#endif

static uint16_t roundToUint16(double value)
{
    if (value <= 0.0) {
        return 0;
    }
    if (value >= 65535.0) {
        return 65535;
    }
    return static_cast<uint16_t>(value + 0.5);
}

static bool getJsonNumber(cJSON* obj, const char* key, double& out)
{
    if (obj == nullptr || key == nullptr) {
        return false;
    }
    cJSON* item = cJSON_GetObjectItem(obj, key);
    if (!cJSON_IsNumber(item)) {
        return false;
    }
    out = item->valuedouble;
    return true;
}

static void printPileCmdTrace(int gun, const char* cmdName, const std::string& topic, const std::string& payload)
{
    std::cout << "[SHM2CCU] pile cmd received, gun=" << gun
              << ", cmd=" << (cmdName == nullptr ? "unknown" : cmdName)
              << ", topic=" << topic
              << ", payload=" << payload
              << std::endl;
}

static void printPileCmdRawTrace(int gun, const std::string& topic, const std::string& payload)
{
    std::cout << "[SHM2CCU] pile cmd raw message, gun=" << gun
              << ", topic=" << topic
              << ", payload=" << payload
              << std::endl;
}

static void printPlatEventRawTrace(int gun, const std::string& topic, const std::string& payload)
{
    std::cout << "[SHM2CCU] plat event raw message, gun=" << gun
              << ", topic=" << topic
              << ", payload=" << payload
              << std::endl;
}

static std::string sanitizeVinString(const char* vin)
{
    std::string out;
    if (vin == nullptr) {
        return out;
    }
    out.reserve(17);
    for (int i = 0; i < 17 && vin[i] != '\0'; ++i) {
        unsigned char ch = static_cast<unsigned char>(vin[i]);
        if (ch >= 'a' && ch <= 'z') {
            ch = static_cast<unsigned char>(ch - 'a' + 'A');
        }
        out.push_back(static_cast<char>(ch));
    }
    return out;
}

static std::string fixedAsciiToString(const char* bytes, size_t len)
{
    std::string out;
    if (bytes == nullptr || len == 0) {
        return out;
    }
    out.reserve(len);
    for (size_t i = 0; i < len && bytes[i] != '\0'; ++i) {
        out.push_back(bytes[i]);
    }
    return out;
}

}

PileControllerProcess::PileControllerProcess()
    : BaseProcess(PROC_PILE_CONTROLLER, "SHM2CCUProcess")
    , m_cmdQueue(nullptr)
    , m_running(false)
    , m_mqttSeq(0)
{
}

PileControllerProcess::~PileControllerProcess()
{
    stop();
}

bool PileControllerProcess::doInitialize()
{
    if (!loadGunConfigs()) {
        std::cerr << "[SHM2CCU] Failed to load gun configs" << std::endl;
        return false;
    }

    m_cmdQueue = new MessageQueue(MSG_KEY_CMD);
    if (!m_cmdQueue->open()) {
        delete m_cmdQueue;
        m_cmdQueue = nullptr;
    }

    if (!createPileController()) {
        std::cerr << "[SHM2CCU] Failed to create pile controllers" << std::endl;
        return false;
    }

    m_eventCaches.resize(m_config.gunCount);
    m_plugAndChargeCaches.resize(m_config.gunCount);
    m_dataCaches.resize(m_config.gunCount);
    m_lastVinDebugPrint.resize(m_config.gunCount, std::chrono::steady_clock::now() - std::chrono::seconds(5));
    m_lastPublish = std::chrono::steady_clock::now();

    if (!initMqtt()) {
        std::cerr << "[SHM2CCU] MQTT init failed, continue without MQTT" << std::endl;
    }

    return true;
}

bool PileControllerProcess::loadGunConfigs()
{
    ConfigManagerLite& config = getConfig();
    m_config.gunCount = static_cast<uint8_t>(config.getInt("PileController", "gun_count", 2));
    if (m_config.gunCount < 1 || m_config.gunCount > SHM2CCU::SHM_MAX_GUN_COUNT) {
        return false;
    }

    m_config.gunConfigs.resize(m_config.gunCount);
    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        GunConfig& gunCfg = m_config.gunConfigs[i];
        gunCfg.gunNo = i + 1;
        std::string gunPrefix = "gun" + std::to_string(static_cast<int>(gunCfg.gunNo)) + "_";
        gunCfg.commType = config.getString("PileController", gunPrefix + "comm_type", "shm");
        gunCfg.protocolType = config.getString("PileController", gunPrefix + "protocol_type", "shm2ccu");
        gunCfg.shmKey = config.getInt("PileController", gunPrefix + "shm_key", 85000);
        if (gunCfg.commType != "shm") {
            std::cerr << "[SHM2CCU] Only shm comm_type is supported" << std::endl;
            return false;
        }
    }

    m_config.packetTimeout = config.getInt("PileController", "packet_timeout", 3);
    m_config.retryCount = config.getInt("PileController", "retry_count", 3);
    m_config.mqttHost = config.getString("PileController", "mqtt_host", "localhost");
    m_config.mqttPort = config.getInt("PileController", "mqtt_port", 1883);
    m_config.mqttKeepalive = config.getInt("PileController", "mqtt_keepalive", 60);
    m_config.mqttClientId = config.getString("PileController", "mqtt_client_id", "shm2ccu");
    m_config.mqttTopicPrefix = config.getString("PileController", "mqtt_topic_prefix", "tcu");
    m_config.biasNo = config.getInt("PileController", "bias_no", 0);
    return true;
}

bool PileControllerProcess::createPileController()
{
    m_controllers.resize(m_config.gunCount);
    for (uint8_t i = 0; i < m_config.gunCount; ++i) {
        const GunConfig& gunCfg = m_config.gunConfigs[i];
        std::unique_ptr<SHMPileController> controller(new SHMPileController());
        std::ostringstream configStream;
        configStream << "gun_no=" << static_cast<int>(gunCfg.gunNo - 1)
                     << ",shm_key=" << gunCfg.shmKey;
        if (!controller->initialize("shm", configStream.str().c_str())) {
            return false;
        }
        m_controllers[i] = std::move(controller);
    }
    return true;
}

void PileControllerProcess::doRun()
{
    m_running = true;
    m_mainThread = std::thread([this]() {
        while (m_running.load()) {
            updateStatusFromController();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    m_mainThread.join();
}

void PileControllerProcess::doCleanup()
{
    m_running = false;
    if (m_mainThread.joinable()) {
        m_mainThread.join();
    }

    for (size_t i = 0; i < m_controllers.size(); ++i) {
        if (m_controllers[i]) {
            m_controllers[i]->cleanup();
            m_controllers[i].reset();
        }
    }
    m_controllers.clear();

    if (m_cmdQueue != nullptr) {
        delete m_cmdQueue;
        m_cmdQueue = nullptr;
    }

    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

void PileControllerProcess::feedWatchdog()
{
    // BY ZF: SHM 主控看门狗每 5 秒上报一次，进程名必须与 daemon.ini 的进程键保持一致。
    static const std::chrono::seconds kWatchdogInterval(5);
    static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    static int queueReady = -1;
    static std::chrono::steady_clock::time_point lastFeedTime =
        std::chrono::steady_clock::now() - kWatchdogInterval;
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if ((now - lastFeedTime) < kWatchdogInterval) {
        return;
    }
    if (queueReady == -1) {
        queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
    }
    if (queueReady == 1) {
        const char* processName = "tcu_shmctrl";
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
        lastFeedTime = now;
        std::cout << "[SHM2CCU] Watchdog fed: " << processName << std::endl;
    }
}

void PileControllerProcess::processCommandMessage()
{
    if (m_cmdQueue == nullptr) {
        return;
    }
}

void PileControllerProcess::updateStatusFromController()
{
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const bool doPublish = (now - m_lastPublish) >= std::chrono::milliseconds(1000);
    if (doPublish) {
        m_lastPublish = now;
    }

    for (size_t i = 0; i < m_controllers.size(); ++i) {
        SHMPileController* shm = dynamic_cast<SHMPileController*>(m_controllers[i].get());
        if (shm == nullptr) {
            continue;
        }
        const uint8_t gunNo = static_cast<uint8_t>(m_config.gunConfigs[i].gunNo - 1);
        DataCache& dataCache = m_dataCaches[i];
        EventCache& cache = m_eventCaches[i];

        if ((now - m_lastVinDebugPrint[i]) >= std::chrono::seconds(5)) {
            unsigned int vinPointValue = 0U;
            std::string vinPointDesname;
            if (shm->getVinPointDebug(vinPointValue, vinPointDesname)) {
                // BY ZF: 联调时每 5 秒打印一次 YC130 原始值，便于确认 VIN 是否已经准备完成。
                std::cout << "[SHM2CCU] YC130 debug, gun=" << static_cast<int>(gunNo)
                          << ", value=" << vinPointValue
                          << ", desname=" << vinPointDesname
                          << std::endl;
            }
            m_lastVinDebugPrint[i] = now;
        }

        if (!cache.onlinePublished) {
            cache.onlinePublished = true;
            std::string payload = buildDataPayload(gunNo, "pile_online", [](cJSON* data) {
                cJSON_AddStringToObject(data, "reason", "shm_attached");
            });
            publishCmdUpset(gunNo, payload, true);
        }

        PileStatus status;
        shm->getStatus(static_cast<uint8_t>(gunNo + 1), &status);

        TCU2CCU_DataYC20 yc20;
        if (shm->getYC20Data(&yc20)) {
            dataCache.yc20 = yc20;
            dataCache.hasYc20 = true;
        }
        TCU2CCU_DataYX22 yx22;
        if (shm->getYX22Data(&yx22)) {
            dataCache.yx22 = yx22;
            dataCache.hasYx22 = true;
        }
        TCU2CCU_DataYX23 yx23;
        if (shm->getYX23Data(&yx23)) {
            dataCache.yx23 = yx23;
            dataCache.hasYx23 = true;
        }

        PlugAndChargeCache& pncCache = m_plugAndChargeCaches[i];
        const bool vinReqActive = dataCache.hasYx23 && dataCache.yx23.vinReq != 0;
        if (vinReqActive && !pncCache.lastVinReq) {
            pncCache.vinReqPublished = false;
            pncCache.vehicleIdPublished = false;
            pncCache.vehicleIdConfirmed = false;
            pncCache.authHandled = false;
            pncCache.localStartIssued = false;
            pncCache.vin.clear();

            const uint64_t startTime = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
            const std::string payload = buildCmdPayload(gunNo, "vin_req", [startTime](cJSON* data) {
                // BY ZF: SHM 模式无 HMI，检测到 CCU 即插即充请求后由 pile_controller 代发 vin_req 驱动 logic 流程。
                cJSON_AddNumberToObject(data, "startTime", static_cast<double>(startTime));
                cJSON_AddNumberToObject(data, "plugAndChargeFlag", 0x02);
                cJSON_AddNumberToObject(data, "mergeChargeFlag", 0x00);
                cJSON_AddNumberToObject(data, "loadControlSwitch", 0x02);
                cJSON_AddNumberToObject(data, "auxPowerVoltage", 0x0C);
                cJSON_AddNumberToObject(data, "v2g", 0x00);
            });
            publishLogicCmd(gunNo, payload);
            pncCache.vinReqPublished = true;
            shm->clearLocalPncStartRequest();

            TCU2CCU_CmdStartChargeData startData;
            memset(&startData, 0, sizeof(startData));
            startData.loadControlSwitch = 0x02;
            startData.plugAndChargeFlag = 0x02;
            startData.auxPowerVoltage = 0x0C;
            startData.mergeChargeFlag = 0x00;
            startData.v2g = 0x00;
            shm->setStartChargeData(&startData);
            pncCache.localStartIssued = (shm->startCharge() == 0);

            std::cout << "[SHM2CCU] plug-and-charge vin_req published, gun=" << static_cast<int>(gunNo)
                      << ", yx_pnc_request=1" << std::endl;
            std::cout << "[SHM2CCU] plug-and-charge local request consumed, gun=" << static_cast<int>(gunNo)
                      << ", yx222_cleared=1"
                      << ", yx175_sent=" << (pncCache.localStartIssued ? 1 : 0)
                      << std::endl;
        }
        pncCache.lastVinReq = vinReqActive;

        if (pncCache.vinReqPublished && !pncCache.vehicleIdPublished) {
            TCU2CCU_StatusVehicleIdData vehicleIdData;
            if (shm->getVehicleIdData(&vehicleIdData)) {
                const std::string vin = sanitizeVinString(vehicleIdData.vin);
                if (!vin.empty()) {
                    std::string payload = buildDataPayload(gunNo, "vehicle_id", [&vehicleIdData, &vin](cJSON* data) {
                        cJSON_AddStringToObject(data, "vin", vin.c_str());
                        cJSON* chargeCount = cJSON_CreateArray();
                        for (size_t idx = 0; idx < sizeof(vehicleIdData.batteryChargeCount); ++idx) {
                            cJSON_AddItemToArray(chargeCount, cJSON_CreateNumber(vehicleIdData.batteryChargeCount[idx]));
                        }
                        cJSON_AddItemToObject(data, "batteryChargeCount", chargeCount);
                        cJSON_AddNumberToObject(data, "soc", vehicleIdData.soc);
                        cJSON_AddNumberToObject(data, "currentBatteryVoltage", vehicleIdData.currentBatteryVoltage);
                    });
                    publishCmdUpset(gunNo, payload);
                    pncCache.vehicleIdPublished = true;
                    pncCache.vin = vin;
                    std::cout << "[SHM2CCU] plug-and-charge vehicle_id published, gun="
                              << static_cast<int>(gunNo)
                              << ", vin=" << vin
                              << std::endl;
                } else {
                    std::cout << "[SHM2CCU] plug-and-charge vehicle_id skipped: empty vin, gun="
                              << static_cast<int>(gunNo)
                              << std::endl;
                }
            }
        }

        if (doPublish && dataCache.hasYc20) {
            const TCU2CCU_DataYC20& current = dataCache.yc20;
            std::string payload = buildDataPayload(gunNo, "yc", [&current](cJSON* data) {
                cJSON_AddNumberToObject(data, "outputVoltage", static_cast<double>(current.outputVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "outputCurrent", static_cast<double>(current.outputCurrent) / 10.0);
                cJSON_AddNumberToObject(data, "soc", current.soc);
                cJSON_AddNumberToObject(data, "batteryMinTemp", current.batteryMinTemp);
                cJSON_AddNumberToObject(data, "batteryMaxTemp", current.batteryMaxTemp);
                cJSON_AddNumberToObject(data, "cellMaxVoltage", static_cast<double>(current.cellMaxVoltage) / 100.0);
                cJSON_AddNumberToObject(data, "cellMinVoltage", static_cast<double>(current.cellMinVoltage) / 100.0);
                cJSON_AddNumberToObject(data, "pileEnvTemp", current.pileEnvTemp);
                cJSON_AddNumberToObject(data, "guideVoltage", static_cast<double>(current.guideVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "bmsReqVoltage", static_cast<double>(current.bmsReqVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "bmsReqCurrent", static_cast<double>(current.bmsReqCurrent) / 10.0);
                cJSON_AddNumberToObject(data, "chargeMode", current.chargeMode);
                cJSON_AddNumberToObject(data, "bmsMeasuredVoltage", static_cast<double>(current.bmsMeasuredVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "bmsMeasuredCurrent", static_cast<double>(current.bmsMeasuredCurrent) / 10.0);
                cJSON_AddNumberToObject(data, "estimatedRemainTime", current.estimatedRemainTime);
                cJSON_AddNumberToObject(data, "interfaceTemp1", static_cast<double>(current.interfaceTemp1) / 10.0);
                cJSON_AddNumberToObject(data, "interfaceTemp2", static_cast<double>(current.interfaceTemp2) / 10.0);
                cJSON_AddNumberToObject(data, "interfaceTemp3", static_cast<double>(current.interfaceTemp3) / 10.0);
                cJSON_AddNumberToObject(data, "interfaceTemp4", static_cast<double>(current.interfaceTemp4) / 10.0);
                cJSON_AddNumberToObject(data, "maxVoltageCellNo", current.maxVoltageCellNo);
                cJSON_AddNumberToObject(data, "maxTempPointNo", current.maxTempPointNo);
                cJSON_AddNumberToObject(data, "minTempPointNo", current.minTempPointNo);
                cJSON_AddNumberToObject(data, "inletTemp", current.inletTemp);
                cJSON_AddNumberToObject(data, "outletTemp", current.outletTemp);
                cJSON_AddNumberToObject(data, "envHumidity", current.envHumidity);
            });
            publishData(gunNo, "yc", payload, false);
        }

        if (doPublish && dataCache.hasYx22) {
            const TCU2CCU_DataYX22& current = dataCache.yx22;
            const uint8_t vinReq = dataCache.hasYx23 ? dataCache.yx23.vinReq : 0;
            std::string payload = buildDataPayload(gunNo, "yx", [&current, vinReq](cJSON* data) {
                cJSON_AddNumberToObject(data, "workStatus", current.workStatus);
                cJSON_AddNumberToObject(data, "totalFault", current.totalFault);
                cJSON_AddNumberToObject(data, "totalAlarm", current.totalAlarm);
                cJSON_AddNumberToObject(data, "emergencyStopFault", current.emergencyStopFault);
                cJSON_AddNumberToObject(data, "vehicleConnectStatus", current.vehicleConnectStatus);
                cJSON_AddNumberToObject(data, "vinReq", vinReq);
                cJSON_AddNumberToObject(data, "gunSeatStatus", current.gunSeatStatus);
                cJSON_AddNumberToObject(data, "electronicLockStatus", current.electronicLockStatus);
                cJSON_AddNumberToObject(data, "dcContactorStatus", current.dcContactorStatus);
                cJSON_AddNumberToObject(data, "otherFault", current.otherFault);
            });
            publishData(gunNo, "yx", payload, false);
        }

        if (dataCache.hasYx22) {
            const TCU2CCU_DataYX22& current = dataCache.yx22;
            if (!cache.hasTotalFault || cache.totalFault != current.totalFault) {
                cache.hasTotalFault = true;
                cache.totalFault = current.totalFault;
                if (current.totalFault) {
                    std::string payload = buildDataPayload(gunNo, "deviceErr_on", [&current, &dataCache](cJSON* data) {
                        cJSON_AddNumberToObject(data, "totalFault", current.totalFault);
                        cJSON* arr = cJSON_CreateArray();
                        if (current.emergencyStopFault) cJSON_AddItemToArray(arr, cJSON_CreateString("emergencyStopFault"));
                        if (current.smokeFault) cJSON_AddItemToArray(arr, cJSON_CreateString("smokeFault"));
                        if (current.acInputBreakerFault) cJSON_AddItemToArray(arr, cJSON_CreateString("acInputBreakerFault"));
                        if (current.dcBusContactorFault) cJSON_AddItemToArray(arr, cJSON_CreateString("dcBusContactorFault"));
                        if (current.dcBusFuseFault) cJSON_AddItemToArray(arr, cJSON_CreateString("dcBusFuseFault"));
                        if (current.electronicLockFault) cJSON_AddItemToArray(arr, cJSON_CreateString("electronicLockFault"));
                        if (current.fanFault) cJSON_AddItemToArray(arr, cJSON_CreateString("fanFault"));
                        if (current.lightningArresterFault) cJSON_AddItemToArray(arr, cJSON_CreateString("lightningArresterFault"));
                        if (current.insulationMonitorFault) cJSON_AddItemToArray(arr, cJSON_CreateString("insulationMonitorFault"));
                        if (current.batteryReverseFault) cJSON_AddItemToArray(arr, cJSON_CreateString("batteryReverseFault"));
                        if (current.guideFault) cJSON_AddItemToArray(arr, cJSON_CreateString("guideFault"));
                        if (current.pileOverTempFault) cJSON_AddItemToArray(arr, cJSON_CreateString("pileOverTempFault"));
                        if (current.interfaceOverTempFault) cJSON_AddItemToArray(arr, cJSON_CreateString("interfaceOverTempFault"));
                        if (current.gunNotReturnAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("gunNotReturnAlarm"));
                        if (current.bmsCommFault) cJSON_AddItemToArray(arr, cJSON_CreateString("bmsCommFault"));
                        if (current.inputOverVoltageFault) cJSON_AddItemToArray(arr, cJSON_CreateString("inputOverVoltageFault"));
                        if (current.inputUnderVoltageFault) cJSON_AddItemToArray(arr, cJSON_CreateString("inputUnderVoltageFault"));
                        if (current.dcBusOverVoltageFault) cJSON_AddItemToArray(arr, cJSON_CreateString("dcBusOverVoltageFault"));
                        if (current.dcBusUnderVoltageFault) cJSON_AddItemToArray(arr, cJSON_CreateString("dcBusUnderVoltageFault"));
                        if (current.dcBusOverCurrentFault) cJSON_AddItemToArray(arr, cJSON_CreateString("dcBusOverCurrentFault"));
                        if (current.moduleFault) cJSON_AddItemToArray(arr, cJSON_CreateString("moduleFault"));
                        if (dataCache.hasYx23) {
                            const TCU2CCU_DataYX23& extra = dataCache.yx23;
                            if (extra.pileDoorFault) cJSON_AddItemToArray(arr, cJSON_CreateString("pileDoorFault"));
                            if (extra.dcContactorStickFault) cJSON_AddItemToArray(arr, cJSON_CreateString("dcContactorStickFault"));
                            if (extra.insulationMonitorAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("insulationMonitorAlarm"));
                            if (extra.dischargeFault) cJSON_AddItemToArray(arr, cJSON_CreateString("dischargeFault"));
                            if (extra.pileOverTempAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("pileOverTempAlarm"));
                            if (extra.gunOverTempAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("gunOverTempAlarm"));
                            if (extra.acInputContactorFault) cJSON_AddItemToArray(arr, cJSON_CreateString("acInputContactorFault"));
                            if (extra.acInputContactorStickFault) cJSON_AddItemToArray(arr, cJSON_CreateString("acInputContactorStickFault"));
                            if (extra.auxPowerFault) cJSON_AddItemToArray(arr, cJSON_CreateString("auxPowerFault"));
                            if (extra.bridgeContactorFault) cJSON_AddItemToArray(arr, cJSON_CreateString("bridgeContactorFault"));
                            if (extra.bridgeContactorStickFault) cJSON_AddItemToArray(arr, cJSON_CreateString("bridgeContactorStickFault"));
                            if (extra.pileWaterFault) cJSON_AddItemToArray(arr, cJSON_CreateString("pileWaterFault"));
                            if (extra.cabinetDoorFault) cJSON_AddItemToArray(arr, cJSON_CreateString("cabinetDoorFault"));
                            if (extra.cabinetWaterFault) cJSON_AddItemToArray(arr, cJSON_CreateString("cabinetWaterFault"));
                            if (extra.cabinetSmokeFault) cJSON_AddItemToArray(arr, cJSON_CreateString("cabinetSmokeFault"));
                            if (extra.powerCtrlCommFault) cJSON_AddItemToArray(arr, cJSON_CreateString("powerCtrlCommFault"));
                            if (extra.powerCtrlCommAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("powerCtrlCommAlarm"));
                            if (extra.powerCtrlFault) cJSON_AddItemToArray(arr, cJSON_CreateString("powerCtrlFault"));
                            if (extra.powerCtrlAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("powerCtrlAlarm"));
                            if (extra.switchModuleCommFault) cJSON_AddItemToArray(arr, cJSON_CreateString("switchModuleCommFault"));
                            if (extra.switchModuleCommAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("switchModuleCommAlarm"));
                            if (extra.switchModuleFault) cJSON_AddItemToArray(arr, cJSON_CreateString("switchModuleFault"));
                            if (extra.switchModuleAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("switchModuleAlarm"));
                            if (extra.bmsFaultByCtrl) cJSON_AddItemToArray(arr, cJSON_CreateString("bmsFaultByCtrl"));
                            if (extra.bmsSendFaultInfo) cJSON_AddItemToArray(arr, cJSON_CreateString("bmsSendFaultInfo"));
                            if (extra.moduleDischargeFault) cJSON_AddItemToArray(arr, cJSON_CreateString("moduleDischargeFault"));
                            if (extra.peBreakFault) cJSON_AddItemToArray(arr, cJSON_CreateString("peBreakFault"));
                            if (extra.guideVoltageAbnormal) cJSON_AddItemToArray(arr, cJSON_CreateString("guideVoltageAbnormal"));
                            if (extra.moduleStartFail) cJSON_AddItemToArray(arr, cJSON_CreateString("moduleStartFail"));
                            if (extra.moduleShutdownFail) cJSON_AddItemToArray(arr, cJSON_CreateString("moduleShutdownFail"));
                            if (extra.moduleAddrAbnormal) cJSON_AddItemToArray(arr, cJSON_CreateString("moduleAddrAbnormal"));
                            if (extra.coolingSystemFault) cJSON_AddItemToArray(arr, cJSON_CreateString("coolingSystemFault"));
                            if (extra.cabinetOverTempAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("cabinetOverTempAlarm"));
                            if (extra.cabinetOverTempFault) cJSON_AddItemToArray(arr, cJSON_CreateString("cabinetOverTempFault"));
                            if (extra.powerDistStrategyFail) cJSON_AddItemToArray(arr, cJSON_CreateString("powerDistStrategyFail"));
                            if (extra.noIdleModuleFault) cJSON_AddItemToArray(arr, cJSON_CreateString("noIdleModuleFault"));
                            if (extra.prechargeVoltageFail) cJSON_AddItemToArray(arr, cJSON_CreateString("prechargeVoltageFail"));
                            if (extra.powerCtrlCommTimeout) cJSON_AddItemToArray(arr, cJSON_CreateString("powerCtrlCommTimeout"));
                            if (extra.envHumidityAlarm) cJSON_AddItemToArray(arr, cJSON_CreateString("envHumidityAlarm"));
                        }
                        cJSON_AddItemToObject(data, "faults", arr);
                        cJSON_AddNumberToObject(data, "otherFault", current.otherFault);
                    });
                    publishCmdUpset(gunNo, payload);
                } else {
                    std::string payload = buildDataPayload(gunNo, "deviceErr_off", [&current](cJSON* data) {
                        cJSON_AddNumberToObject(data, "totalFault", current.totalFault);
                        cJSON_AddNumberToObject(data, "otherFault", current.otherFault);
                    });
                    publishCmdUpset(gunNo, payload);
                }
            }
        }

        if (shm->hasStartCompleteEvent()) {
            TCU2CCU_StatusStartCompleteData eventData;
            if (shm->getStartCompleteData(&eventData)) {
                const std::string startCompleteVin = sanitizeVinString(eventData.vin);
                const std::string batteryManufacturer = fixedAsciiToString(eventData.batteryManufacturer,
                                                                          sizeof(eventData.batteryManufacturer));
                std::string payload = buildDataPayload(gunNo, "start_complete",
                                                       [&eventData, &startCompleteVin, &batteryManufacturer](cJSON* data) {
                    auto addByteArray = [](cJSON* obj, const char* key, const uint8_t* bytes, size_t len) {
                        cJSON* arr = cJSON_CreateArray();
                        for (size_t i = 0; i < len; ++i) {
                            cJSON_AddItemToArray(arr, cJSON_CreateNumber(bytes[i]));
                        }
                        cJSON_AddItemToObject(obj, key, arr);
                    };
                    auto addProdDate = [&eventData](cJSON* obj) {
                        if (eventData.batteryProdYear == 0 || eventData.batteryProdMonth == 0 || eventData.batteryProdDay == 0) {
                            cJSON_AddStringToObject(obj, "batteryProdDate", "");
                            return;
                        }
                        char buf[16];
                        std::snprintf(buf, sizeof(buf), "20%02u-%02u-%02u",
                                      static_cast<unsigned int>(eventData.batteryProdYear),
                                      static_cast<unsigned int>(eventData.batteryProdMonth),
                                      static_cast<unsigned int>(eventData.batteryProdDay));
                        cJSON_AddStringToObject(obj, "batteryProdDate", buf);
                    };
                    cJSON_AddNumberToObject(data, "successFlag", eventData.successFlag);
                    cJSON_AddNumberToObject(data, "chargeFailReason", eventData.chargeFailReason);
                    cJSON_AddNumberToObject(data, "batteryType", eventData.batteryType);
                    cJSON_AddNumberToObject(data, "maxAllowTemp", eventData.maxAllowTemp);
                    cJSON_AddNumberToObject(data, "bmsMaxChargeVoltage", static_cast<double>(eventData.bmsMaxChargeVoltage) / 10.0);
                    cJSON_AddNumberToObject(data, "cellMaxChargeVoltage", static_cast<double>(eventData.cellMaxChargeVoltage) / 100.0);
                    cJSON_AddNumberToObject(data, "maxAllowChargeCurrent", static_cast<double>(eventData.maxAllowChargeCurrent) / 10.0);
                    cJSON_AddNumberToObject(data, "ratedTotalVoltage", static_cast<double>(eventData.ratedTotalVoltage) / 10.0);
                    cJSON_AddNumberToObject(data, "currentTotalVoltage", static_cast<double>(eventData.currentTotalVoltage) / 10.0);
                    cJSON_AddNumberToObject(data, "ratedCapacity", static_cast<double>(eventData.ratedCapacity) / 10.0);
                    cJSON_AddNumberToObject(data, "nominalEnergy", static_cast<double>(eventData.nominalEnergy) / 10.0);
                    cJSON_AddNumberToObject(data, "soc", static_cast<double>(eventData.soc) / 10.0);
                    cJSON_AddNumberToObject(data, "pileMaxOutputVoltage", eventData.pileMaxOutputVoltage);
                    cJSON_AddNumberToObject(data, "pileMinOutputVoltage", eventData.pileMinOutputVoltage);
                    cJSON_AddNumberToObject(data, "pileMaxOutputCurrent", eventData.pileMaxOutputCurrent);
                    cJSON_AddNumberToObject(data, "pileMinOutputCurrent", eventData.pileMinOutputCurrent);
                    cJSON_AddStringToObject(data, "vin", startCompleteVin.c_str());
                    cJSON_AddStringToObject(data, "batteryManufacturer", batteryManufacturer.c_str());
                    addByteArray(data, "batterySerial", eventData.batterySerial, sizeof(eventData.batterySerial));
                    cJSON_AddNumberToObject(data, "batteryProdYear", eventData.batteryProdYear);
                    cJSON_AddNumberToObject(data, "batteryProdMonth", eventData.batteryProdMonth);
                    cJSON_AddNumberToObject(data, "batteryProdDay", eventData.batteryProdDay);
                    addProdDate(data);
                    addByteArray(data, "batteryChargeCount", eventData.batteryChargeCount, sizeof(eventData.batteryChargeCount));
                    cJSON_AddNumberToObject(data, "batteryPropertyFlag", eventData.batteryPropertyFlag);
                    addByteArray(data, "bmsSoftwareVersion", eventData.bmsSoftwareVersion, sizeof(eventData.bmsSoftwareVersion));
                });
                publishCmdUpset(gunNo, payload);
            }
            shm->clearStartCompleteEvent();
        }

        if (shm->hasStopCompleteEvent()) {
            TCU2CCU_StatusStopCompleteData eventData;
            if (shm->getStopCompleteData(&eventData)) {
                std::string payload = buildDataPayload(gunNo, "stop_complete", [&eventData](cJSON* data) {
                    cJSON_AddNumberToObject(data, "stopReason", eventData.stopReason);
                    cJSON_AddNumberToObject(data, "bmsChargeFaultReason", eventData.bmsChargeFaultReason);
                    cJSON_AddNumberToObject(data, "bmsStopErrorReason", eventData.bmsStopErrorReason);
                    cJSON_AddNumberToObject(data, "stopSoc", eventData.stopSoc);
                    cJSON_AddNumberToObject(data, "cellMinVoltage", eventData.cellMinVoltage/100.0);
                    cJSON_AddNumberToObject(data, "cellMaxVoltage", eventData.cellMaxVoltage/100.0);
                    cJSON_AddNumberToObject(data, "batteryMinTemp", eventData.batteryMinTemp);
                    cJSON_AddNumberToObject(data, "batteryMaxTemp", eventData.batteryMaxTemp);
                });
                publishCmdUpset(gunNo, payload);
            }
            shm->clearStopCompleteEvent();
        }

        if (shm->hasPowerCtrlResponseEvent()) {
            std::string payload = buildDataPayload(gunNo, "power_ctrl_response", [shm](cJSON* data) {
                cJSON_AddNumberToObject(data, "result", shm->getYcRawValueByBase(SHM2CCU::YC_POWER_CTRL_RESULT));
            });
            publishCmdUpset(gunNo, payload);
            shm->clearPowerCtrlResponseEvent();
        }
    }
}

bool PileControllerProcess::initMqtt()
{
    std::ostringstream cid;
    cid << m_config.mqttClientId << "_" << static_cast<int>(m_config.gunCount);
    if (!m_mqtt.init(cid.str(), true)) {
        return false;
    }
    m_mqtt.setConnectHandler([this](int rc) {
        if (rc != 0) {
            return;
        }
        for (uint8_t i = 0; i < m_config.gunCount; ++i) {
            uint8_t gunNo = m_config.gunConfigs[i].gunNo;
            std::ostringstream t;
            t << m_config.mqttTopicPrefix << "/pile/" << (static_cast<int>(gunNo - 1) + m_config.biasNo) << "/cmd";
            m_mqtt.subscribe(t.str(), 1);
            std::ostringstream feeTopic;
            feeTopic << m_config.mqttTopicPrefix << "/logic/" << (static_cast<int>(gunNo - 1) + m_config.biasNo) << "/feeData";
            m_mqtt.subscribe(feeTopic.str(), 1);
            std::ostringstream logicEventTopic;
            logicEventTopic << m_config.mqttTopicPrefix << "/logic/" << (static_cast<int>(gunNo - 1) + m_config.biasNo) << "/event";
            m_mqtt.subscribe(logicEventTopic.str(), 2);
            std::ostringstream platEventTopic;
            platEventTopic << m_config.mqttTopicPrefix << "/plat/" << (static_cast<int>(gunNo - 1) + m_config.biasNo) << "/event";
            m_mqtt.subscribe(platEventTopic.str(), 1);
        }
    });
    if (!m_mqtt.connect(m_config.mqttHost, m_config.mqttPort, m_config.mqttKeepalive)) {
        return false;
    }
    m_mqtt.setMessageHandler([this](const std::string& topic, const std::string& payload) {
        this->onMqttMessage(topic, payload);
    });
    if (!m_mqtt.loopStart()) {
        return false;
    }
    return true;
}

void PileControllerProcess::publishData(uint8_t gunNo, const std::string& type, const std::string& payload, bool retain)
{
    std::ostringstream t;
    t << m_config.mqttTopicPrefix << "/pile/" << (static_cast<int>(gunNo) + m_config.biasNo) << "/data";
    m_mqtt.publish(t.str(), payload, 0, retain);
}

void PileControllerProcess::publishCmdUpset(uint8_t gunNo, const std::string& payload, bool retain)
{
    std::ostringstream t;
    t << m_config.mqttTopicPrefix << "/pile/" << (static_cast<int>(gunNo) + m_config.biasNo) << "/event";
    m_mqtt.publish(t.str(), payload, 2, retain);
}

void PileControllerProcess::publishLogicCmd(uint8_t gunNo, const std::string& payload, bool retain)
{
    std::ostringstream t;
    t << m_config.mqttTopicPrefix << "/logic/" << (static_cast<int>(gunNo) + m_config.biasNo) << "/cmd";
    m_mqtt.publish(t.str(), payload, 1, retain);
}

void PileControllerProcess::onMqttMessage(const std::string& topic, const std::string& payload)
{
    const bool isPlatEventTopic = topic.find("/plat/") != std::string::npos
                               && topic.find("/event") != std::string::npos;
    int gun = -1;
    if (!parseGunFromTopic(topic, gun)) {
        if (isPlatEventTopic) {
            std::cout << "[SHM2CCU] plat event ignored: parseGunFromTopic failed"
                      << ", topic=" << topic
                      << ", payload=" << payload
                      << std::endl;
        }
        return;
    }
    if (isPlatEventTopic) {
        // BY ZF: 平台在线离线调试入口，先打印原始报文，便于确认 MQTT 消息是否已经进入 SHM2CCU。
        printPlatEventRawTrace(gun, topic, payload);
    }
    if (gun < 0 || gun >= static_cast<int>(m_controllers.size())) {
        if (isPlatEventTopic) {
            std::cout << "[SHM2CCU] plat event ignored: gun out of range"
                      << ", gun=" << gun
                      << ", topic=" << topic
                      << std::endl;
        }
        return;
    }
    SHMPileController* shm = dynamic_cast<SHMPileController*>(m_controllers[gun].get());
    if (shm == nullptr) {
        if (isPlatEventTopic) {
            std::cout << "[SHM2CCU] plat event ignored: shm controller unavailable"
                      << ", gun=" << gun
                      << ", topic=" << topic
                      << std::endl;
        }
        return;
    }

    const bool isPileCmdTopic = topic.find("/pile/") != std::string::npos
                             && topic.find("/cmd") != std::string::npos;
    if (isPileCmdTopic) {
        // BY ZF: 先打印原始 pile cmd 报文，便于排查 stop/start 在 JSON 解析前就被过滤的情况。
        printPileCmdRawTrace(gun, topic, payload);
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        if (isPileCmdTopic) {
            std::cout << "[SHM2CCU] pile cmd json parse failed, gun=" << gun
                      << ", topic=" << topic
                      << std::endl;
        }
        if (isPlatEventTopic) {
            std::cout << "[SHM2CCU] plat event json parse failed, gun=" << gun
                      << ", topic=" << topic
                      << std::endl;
        }
        return;
    }
    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (topic.find("/feeData") != std::string::npos) {
        handleFeeDataMessage(gun, data);
        cJSON_Delete(root);
        return;
    }
    if (isPlatEventTopic) {
        if (!handlePlatEventMessage(gun, root, data)) {
            std::cout << "[SHM2CCU] plat event ignored after parse, gun=" << gun
                      << ", topic=" << topic
                      << std::endl;
        }
        cJSON_Delete(root);
        return;
    }
    if (topic.find("/event") != std::string::npos) {
        handleLogicEventMessage(gun, root, data);
        cJSON_Delete(root);
        return;
    }
    if (topic.find("/cmd") == std::string::npos || !cJSON_IsString(cmd)) {
        if (isPileCmdTopic) {
            std::cout << "[SHM2CCU] pile cmd ignored: missing string field `cmd`, gun=" << gun
                      << ", topic=" << topic
                      << std::endl;
        }
        cJSON_Delete(root);
        return;
    }
    const std::string cmdStr = cmd->valuestring;

    if (cmdStr == "start_charge") {
        // BY ZF: 启动命令收包打印入口，方便联调时确认 pile cmd 已进入 SHM2CCU。
        printPileCmdTrace(gun, "start_charge", topic, payload);
        PlugAndChargeCache& pncCache = m_plugAndChargeCaches[gun];
        TCU2CCU_CmdStartChargeData startData;
        memset(&startData, 0, sizeof(startData));
        startData.loadControlSwitch = 0x02;
        startData.plugAndChargeFlag = 0x01;
        startData.auxPowerVoltage = 0x0C;
        startData.mergeChargeFlag = 0x00;
        startData.v2g = 0x00;
        if (cJSON_IsObject(data)) {
            cJSON* item = cJSON_GetObjectItem(data, "plugAndChargeFlag");
            if (cJSON_IsNumber(item)) startData.plugAndChargeFlag = static_cast<uint8_t>(item->valueint);
            item = cJSON_GetObjectItem(data, "auxPowerVoltage");
            if (cJSON_IsNumber(item)) startData.auxPowerVoltage = static_cast<uint8_t>(item->valueint);
            item = cJSON_GetObjectItem(data, "mergeChargeFlag");
            if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItem(data, "mergedChargeFlag");
            if (!cJSON_IsNumber(item)) item = cJSON_GetObjectItem(data, "combineChargeFlag");
            if (cJSON_IsNumber(item)) startData.mergeChargeFlag = static_cast<uint8_t>(item->valueint);
            item = cJSON_GetObjectItem(data, "v2g");
            if (cJSON_IsNumber(item) && item->valueint != 0) startData.v2g = 0x01;
        }

        if (startData.plugAndChargeFlag == 0x02 && pncCache.localStartIssued) {
            // BY ZF: SHM 本地即插即充在 YX222 触发时已立即写过一次 YX175，这里忽略 logic 的重复启动指令。
            std::cout << "[SHM2CCU] plug-and-charge start_charge ignored, gun=" << gun
                      << ", reason=local_pnc_already_started"
                      << std::endl;
            std::string ack = buildDataPayload(static_cast<uint8_t>(gun), "start_response",
                                               [&startData](cJSON* eventData) {
                cJSON_AddNumberToObject(eventData, "confirmFlag", 0);
                cJSON_AddStringToObject(eventData, "reason", "local_pnc_already_started");
                cJSON_AddNumberToObject(eventData, "plugAndChargeFlag", startData.plugAndChargeFlag);
                cJSON_AddNumberToObject(eventData, "auxPowerVoltage", startData.auxPowerVoltage);
            });
            publishCmdUpset(static_cast<uint8_t>(gun), ack);
            return;
        }

        shm->setStartChargeData(&startData);
        if (shm->startCharge() == 0) {
            std::string ack = buildDataPayload(static_cast<uint8_t>(gun), "start_response",
                                               [&startData](cJSON* eventData) {
                cJSON_AddNumberToObject(eventData, "confirmFlag", 0);
                cJSON_AddStringToObject(eventData, "reason", "shm_write_ok");
                cJSON_AddNumberToObject(eventData, "plugAndChargeFlag", startData.plugAndChargeFlag);
                cJSON_AddNumberToObject(eventData, "auxPowerVoltage", startData.auxPowerVoltage);
            });
            publishCmdUpset(static_cast<uint8_t>(gun), ack);
        }
    } else if (cmdStr == "vehicle_id_confirm") {
        PlugAndChargeCache& pncCache = m_plugAndChargeCaches[gun];
        uint8_t successFlag = 0x01;
        uint8_t failReason = 0x00;
        if (cJSON_IsObject(data)) {
            cJSON* item = cJSON_GetObjectItem(data, "successFlag");
            if (cJSON_IsNumber(item)) successFlag = static_cast<uint8_t>(item->valueint);
            item = cJSON_GetObjectItem(data, "failReason");
            if (cJSON_IsNumber(item)) failReason = static_cast<uint8_t>(item->valueint);
        }
        pncCache.vehicleIdConfirmed = (successFlag == 0x00);
        std::cout << "[SHM2CCU] plug-and-charge vehicle_id_confirm received, gun=" << gun
                  << ", successFlag=" << static_cast<int>(successFlag)
                  << ", failReason=" << static_cast<int>(failReason)
                  << ", vin=" << pncCache.vin
                  << std::endl;
    } else if (cmdStr == "vehicle_auth") {
        PlugAndChargeCache& pncCache = m_plugAndChargeCaches[gun];
        uint8_t successFlag = 0x01;
        uint8_t failReason = 0x00;
        std::string authVin = pncCache.vin;
        if (cJSON_IsObject(data)) {
            cJSON* item = cJSON_GetObjectItem(data, "successFlag");
            if (cJSON_IsNumber(item)) successFlag = static_cast<uint8_t>(item->valueint);
            item = cJSON_GetObjectItem(data, "failReason");
            if (cJSON_IsNumber(item)) failReason = static_cast<uint8_t>(item->valueint);
            item = cJSON_GetObjectItem(data, "vin");
            if (!cJSON_IsString(item)) item = cJSON_GetObjectItem(data, "vinCode");
            if (cJSON_IsString(item)) authVin = sanitizeVinString(item->valuestring);
        }

        // BY ZF: logic 的 successFlag 与 YX195.desname[0] 现统一为 0=成功、1=失败。
        const uint8_t authResult = (successFlag == 0x00) ? 0x00 : 0x01;
        shm->setPlugAndChargeAuthResult(authResult, failReason);
        pncCache.authHandled = true;

        std::cout << "[SHM2CCU] plug-and-charge vehicle_auth received, gun=" << gun
                  << ", successFlag=" << static_cast<int>(successFlag)
                  << ", failReason=" << static_cast<int>(failReason)
                  << ", yx195_result=" << static_cast<int>(authResult)
                  << ", vin=" << authVin
                  << std::endl;

        std::string ack = buildDataPayload(static_cast<uint8_t>(gun), "vehicle_auth_ack",
                                           [successFlag, failReason, &authVin](cJSON* eventData) {
            cJSON_AddNumberToObject(eventData, "successFlag", successFlag);
            cJSON_AddNumberToObject(eventData, "failReason", failReason);
            if (!authVin.empty()) {
                cJSON_AddStringToObject(eventData, "vin", authVin.c_str());
            }
        });
        publishCmdUpset(static_cast<uint8_t>(gun), ack);
    } else if (cmdStr == "stop_charge") {
        // BY ZF: 停机命令收包打印入口，方便联调时确认 pile cmd 已进入 SHM2CCU。
        printPileCmdTrace(gun, "stop_charge", topic, payload);
        PlugAndChargeCache& pncCache = m_plugAndChargeCaches[gun];
        TCU2CCU_CmdStopChargeData stopData;
        memset(&stopData, 0, sizeof(stopData));
        stopData.stopReason = 0x01;
        if (cJSON_IsObject(data)) {
            cJSON* item = cJSON_GetObjectItem(data, "stopReason");
            if (cJSON_IsNumber(item)) stopData.stopReason = static_cast<uint8_t>(item->valueint);
        }
        shm->setStopChargeData(&stopData);
        if (shm->stopCharge() == 0) {
            pncCache.localStartIssued = false;
            std::string ack = buildDataPayload(static_cast<uint8_t>(gun), "stop_response",
                                               [&stopData](cJSON* eventData) {
                cJSON_AddNumberToObject(eventData, "confirmFlag", 0);
                cJSON_AddStringToObject(eventData, "reason", "shm_write_ok");
                cJSON_AddNumberToObject(eventData, "stopReason", stopData.stopReason);
            });
            publishCmdUpset(static_cast<uint8_t>(gun), ack);
        }
    } else if (cmdStr == "power_ctrl") {
        TCU2CCU_CmdPowerAdjustData adjustData;
        memset(&adjustData, 0, sizeof(adjustData));
        bool hasAdjust = false;
        if (cJSON_IsObject(data)) {
            cJSON* item = cJSON_GetObjectItem(data, "adjustType");
            if (cJSON_IsNumber(item)) {
                adjustData.adjustType = static_cast<uint8_t>(item->valueint);
                item = cJSON_GetObjectItem(data, "adjustParam");
                if (cJSON_IsNumber(item)) {
                    adjustData.adjustParam = static_cast<uint16_t>(item->valueint);
                    hasAdjust = true;
                }
            }
            if (!hasAdjust) {
                item = cJSON_GetObjectItem(data, "percentage");
                if (cJSON_IsNumber(item)) {
                    adjustData.adjustType = 0x02;
                    adjustData.adjustParam = roundToUint16(item->valuedouble);
                    hasAdjust = true;
                }
            }
            if (!hasAdjust) {
                item = cJSON_GetObjectItem(data, "maxChargePowerKw");
                if (cJSON_IsNumber(item)) {
                    adjustData.adjustType = 0x01;
                    adjustData.adjustParam = roundToUint16(item->valuedouble);
                    hasAdjust = true;
                }
            }
        } else if (cJSON_IsNumber(data)) {
            adjustData.adjustType = 0x01;
            adjustData.adjustParam = roundToUint16(data->valuedouble);
            hasAdjust = true;
        }
        if (hasAdjust) {
            shm->setPowerAdjustData(&adjustData);
            shm->powerAdjust();
        }
    } else if (isPileCmdTopic) {
        std::cout << "[SHM2CCU] pile cmd ignored: unsupported cmd=" << cmdStr
                  << ", gun=" << gun
                  << ", topic=" << topic
                  << std::endl;
    }

    cJSON_Delete(root);
}

bool PileControllerProcess::handleFeeDataMessage(int gun, cJSON* data)
{
    if (gun < 0 || gun >= static_cast<int>(m_controllers.size()) || !cJSON_IsObject(data)) {
        return false;
    }

    SHMPileController* shm = dynamic_cast<SHMPileController*>(m_controllers[gun].get());
    if (shm == nullptr) {
        return false;
    }

    double totalAmount = 0.0;
    double totalEnergy = 0.0;
    double chargedTime = 0.0;
    bool hasAmount = getJsonNumber(data, "totalAmount", totalAmount);
    bool hasEnergy = getJsonNumber(data, "totalEnergy", totalEnergy);
    bool hasChargedTime = getJsonNumber(data, "chargedTime", chargedTime);
    if (!hasAmount && !hasEnergy && !hasChargedTime) {
        return false;
    }

    shm->updateFeeData(totalAmount, totalEnergy, chargedTime);
    return true;
}

bool PileControllerProcess::handleLogicEventMessage(int gun, cJSON* root, cJSON* data)
{
    if (gun < 0 || gun >= static_cast<int>(m_controllers.size()) || root == nullptr || !cJSON_IsObject(data)) {
        return false;
    }

    cJSON* event = cJSON_GetObjectItem(root, "event");
    if (!cJSON_IsString(event) || std::string(event->valuestring) != "state_change") {
        return false;
    }

    cJSON* to = cJSON_GetObjectItem(data, "to");
    if (!cJSON_IsString(to) || std::string(to->valuestring) != "STOPPED") {
        return false;
    }

    SHMPileController* shm = dynamic_cast<SHMPileController*>(m_controllers[gun].get());
    if (shm == nullptr) {
        return false;
    }

    // BY ZF: logic 进入 STOPPED 时通知 CCU/HMI 进入停机结算界面，YC210/YC466 写 11。
    shm->setPlugAndChargeState(11);
    return true;
}

bool PileControllerProcess::handlePlatEventMessage(int gun, cJSON* root, cJSON* data)
{
    if (gun < 0 || gun >= static_cast<int>(m_controllers.size()) || root == nullptr) {
        return false;
    }

    cJSON* event = cJSON_GetObjectItem(root, "event");
    if (!cJSON_IsString(event)) {
        std::cout << "[SHM2CCU] plat event ignored: missing string field `event`"
                  << ", gun=" << gun
                  << std::endl;
        return false;
    }

    SHMPileController* shm = dynamic_cast<SHMPileController*>(m_controllers[gun].get());
    if (shm == nullptr) {
        return false;
    }

    const char* reason = "";
    if (cJSON_IsObject(data)) {
        cJSON* reasonItem = cJSON_GetObjectItem(data, "reason");
        if (cJSON_IsString(reasonItem) && reasonItem->valuestring != nullptr) {
            reason = reasonItem->valuestring;
        }
    }

    if (std::strcmp(event->valuestring, "platform_online") == 0) {
        shm->setSystemOnlineState(kPlatformOnlineYc32Value);
        std::cout << "[SHM2CCU] plat event processed, gun=" << gun
                  << ", event=platform_online"
                  << ", yc32=" << kPlatformOnlineYc32Value
                  << ", reason=" << reason
                  << std::endl;
        return true;
    }
    if (std::strcmp(event->valuestring, "platform_offline") == 0) {
        shm->setSystemOnlineState(0U);
        std::cout << "[SHM2CCU] plat event processed, gun=" << gun
                  << ", event=platform_offline"
                  << ", yc32=0"
                  << ", reason=" << reason
                  << std::endl;
        return true;
    }
    std::cout << "[SHM2CCU] plat event ignored: unsupported event"
              << ", gun=" << gun
              << ", event=" << event->valuestring
              << std::endl;
    return false;
}

std::string PileControllerProcess::buildDataPayload(uint8_t gunNo,
                                                    const std::string& type,
                                                    const std::function<void(cJSON*)>& fillData)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    const uint64_t seq = ++m_mqttSeq;
    const uint64_t ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(ts));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(seq));
    cJSON_AddStringToObject(root, "source", "pile_controller");
    cJSON_AddNumberToObject(root, "gun", gunNo);
    cJSON_AddStringToObject(root, "type", type.c_str());
    fillData(data);
    cJSON_AddItemToObject(root, "data", data);
    char* text = cJSON_PrintUnformatted(root);
    std::string payload = (text == nullptr) ? "" : text;
    if (text != nullptr) {
        free(text);
    }
    cJSON_Delete(root);
    return payload;
}

std::string PileControllerProcess::buildCmdPayload(uint8_t gunNo,
                                                   const std::string& cmd,
                                                   const std::function<void(cJSON*)>& fillData)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    const uint64_t seq = ++m_mqttSeq;
    const uint64_t ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(ts));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(seq));
    cJSON_AddStringToObject(root, "source", "pile_controller");
    cJSON_AddNumberToObject(root, "gun", gunNo);
    cJSON_AddStringToObject(root, "cmd", cmd.c_str());
    fillData(data);
    cJSON_AddItemToObject(root, "data", data);
    char* text = cJSON_PrintUnformatted(root);
    std::string payload = (text == nullptr) ? "" : text;
    if (text != nullptr) {
        free(text);
    }
    cJSON_Delete(root);
    return payload;
}

bool PileControllerProcess::parseGunFromTopic(const std::string& topic, int& outGun) const
{
    outGun = -1;
    // BY ZF: SHM2CCU 需要同时处理 pile/logic/plat 三类带枪号 topic。
    const char* markers[] = { "/pile/", "/logic/", "/plat/" };
    for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); ++i) {
        const std::string marker = markers[i];
        const size_t pos = topic.find(marker);
        if (pos == std::string::npos) {
            continue;
        }
        const size_t start = pos + marker.size();
        const size_t end = topic.find('/', start);
        if (end == std::string::npos) {
            return false;
        }
        try {
            outGun = std::stoi(topic.substr(start, end - start)) - m_config.biasNo;
            return outGun >= 0;
        } catch (...) {
            return false;
        }
    }
    return false;
}
