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
#include <cjson/cJSON.h>

namespace {

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

}

PileControllerProcess::PileControllerProcess()
    : BaseProcess(PROC_PILE_CONTROLLER, "SHM2CCUProcess")
    , m_cmdQueue(nullptr)
    , m_logger(nullptr)
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
    m_logger = new LogSender("SHM2CCU");

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
    m_dataCaches.resize(m_config.gunCount);
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
    if (m_logger != nullptr) {
        delete m_logger;
        m_logger = nullptr;
    }

    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

void PileControllerProcess::feedWatchdog()
{
    static MessageQueue watchdogQueue(MSG_KEY_WATCHDOG);
    static int queueReady = -1;
    static std::chrono::steady_clock::time_point lastFeedTime =
        std::chrono::steady_clock::now() - std::chrono::seconds(5);
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if ((now - lastFeedTime) < std::chrono::seconds(5)) {
        return;
    }
    if (queueReady == -1) {
        queueReady = watchdogQueue.open() ? 1 : (watchdogQueue.create() ? 1 : 0);
    }
    if (queueReady == 1) {
        const char* processName = "tcu_shmctrl";
        watchdogQueue.send(MSG_WATCHDOG_FEED, processName, strlen(processName));
        lastFeedTime = now;
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

        if (doPublish && dataCache.hasYc20) {
            const TCU2CCU_DataYC20& current = dataCache.yc20;
            std::string payload = buildDataPayload(gunNo, "yc", [&current](cJSON* data) {
                cJSON_AddNumberToObject(data, "outputVoltage", static_cast<double>(current.outputVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "outputCurrent", static_cast<double>(current.outputCurrent) / 10.0);
                cJSON_AddNumberToObject(data, "soc", current.soc);
                cJSON_AddNumberToObject(data, "batteryMinTemp", current.batteryMinTemp);
                cJSON_AddNumberToObject(data, "batteryMaxTemp", current.batteryMaxTemp);
                cJSON_AddNumberToObject(data, "cellMaxVoltage", static_cast<double>(current.cellMaxVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "cellMinVoltage", static_cast<double>(current.cellMinVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "pileEnvTemp", current.pileEnvTemp);
                cJSON_AddNumberToObject(data, "guideVoltage", static_cast<double>(current.guideVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "bmsReqVoltage", static_cast<double>(current.bmsReqVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "bmsReqCurrent", static_cast<double>(current.bmsReqCurrent) / 10.0);
                cJSON_AddNumberToObject(data, "chargeMode", current.chargeMode);
                cJSON_AddNumberToObject(data, "bmsMeasuredVoltage", static_cast<double>(current.bmsMeasuredVoltage) / 10.0);
                cJSON_AddNumberToObject(data, "bmsMeasuredCurrent", static_cast<double>(current.bmsMeasuredCurrent) / 10.0);
                cJSON_AddNumberToObject(data, "estimatedRemainTime", current.estimatedRemainTime);
                cJSON_AddNumberToObject(data, "interfaceTemp1", current.interfaceTemp1);
                cJSON_AddNumberToObject(data, "interfaceTemp2", current.interfaceTemp2);
                cJSON_AddNumberToObject(data, "interfaceTemp3", current.interfaceTemp3);
                cJSON_AddNumberToObject(data, "interfaceTemp4", current.interfaceTemp4);
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
                std::string payload = buildDataPayload(gunNo, "start_complete", [&eventData](cJSON* data) {
                    auto addByteArray = [](cJSON* obj, const char* key, const uint8_t* bytes, size_t len) {
                        cJSON* arr = cJSON_CreateArray();
                        for (size_t i = 0; i < len; ++i) {
                            cJSON_AddItemToArray(arr, cJSON_CreateNumber(bytes[i]));
                        }
                        cJSON_AddItemToObject(obj, key, arr);
                    };
                    cJSON_AddNumberToObject(data, "successFlag", eventData.successFlag);
                    cJSON_AddNumberToObject(data, "chargeFailReason", eventData.chargeFailReason);
                    cJSON_AddNumberToObject(data, "batteryType", eventData.batteryType);
                    cJSON_AddNumberToObject(data, "maxAllowTemp", eventData.maxAllowTemp);
                    cJSON_AddNumberToObject(data, "bmsMaxChargeVoltage", eventData.bmsMaxChargeVoltage);
                    cJSON_AddNumberToObject(data, "cellMaxChargeVoltage", eventData.cellMaxChargeVoltage);
                    cJSON_AddNumberToObject(data, "maxAllowChargeCurrent", eventData.maxAllowChargeCurrent);
                    cJSON_AddNumberToObject(data, "ratedTotalVoltage", eventData.ratedTotalVoltage);
                    cJSON_AddNumberToObject(data, "currentTotalVoltage", eventData.currentTotalVoltage);
                    cJSON_AddNumberToObject(data, "ratedCapacity", eventData.ratedCapacity);
                    cJSON_AddNumberToObject(data, "nominalEnergy", eventData.nominalEnergy);
                    cJSON_AddNumberToObject(data, "soc", eventData.soc);
                    cJSON_AddNumberToObject(data, "pileMaxOutputVoltage", eventData.pileMaxOutputVoltage);
                    cJSON_AddNumberToObject(data, "pileMinOutputVoltage", eventData.pileMinOutputVoltage);
                    cJSON_AddNumberToObject(data, "pileMaxOutputCurrent", eventData.pileMaxOutputCurrent);
                    cJSON_AddNumberToObject(data, "pileMinOutputCurrent", eventData.pileMinOutputCurrent);
                    cJSON_AddStringToObject(data, "vin", eventData.vin);
                    cJSON_AddStringToObject(data, "batteryManufacturer", eventData.batteryManufacturer);
                    addByteArray(data, "batterySerial", eventData.batterySerial, sizeof(eventData.batterySerial));
                    cJSON_AddNumberToObject(data, "batteryProdYear", eventData.batteryProdYear);
                    cJSON_AddNumberToObject(data, "batteryProdMonth", eventData.batteryProdMonth);
                    cJSON_AddNumberToObject(data, "batteryProdDay", eventData.batteryProdDay);
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
                    cJSON_AddNumberToObject(data, "cellMinVoltage", eventData.cellMinVoltage);
                    cJSON_AddNumberToObject(data, "cellMaxVoltage", eventData.cellMaxVoltage);
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
            t << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gunNo - 1) << "/cmd";
            m_mqtt.subscribe(t.str(), 1);
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
    t << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gunNo) << "/data";
    m_mqtt.publish(t.str(), payload, 0, retain);
}

void PileControllerProcess::publishCmdUpset(uint8_t gunNo, const std::string& payload, bool retain)
{
    std::ostringstream t;
    t << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gunNo) << "/event";
    m_mqtt.publish(t.str(), payload, 2, retain);
}

void PileControllerProcess::onMqttMessage(const std::string& topic, const std::string& payload)
{
    int gun = -1;
    if (!parseGunFromTopic(topic, gun) || topic.find("/cmd") == std::string::npos) {
        return;
    }
    if (gun < 0 || gun >= static_cast<int>(m_controllers.size())) {
        return;
    }
    SHMPileController* shm = dynamic_cast<SHMPileController*>(m_controllers[gun].get());
    if (shm == nullptr) {
        return;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (root == nullptr) {
        return;
    }
    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    cJSON* data = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return;
    }
    const std::string cmdStr = cmd->valuestring;

    if (cmdStr == "start_charge") {
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
    } else if (cmdStr == "stop_charge") {
        TCU2CCU_CmdStopChargeData stopData;
        memset(&stopData, 0, sizeof(stopData));
        stopData.stopReason = 0x01;
        if (cJSON_IsObject(data)) {
            cJSON* item = cJSON_GetObjectItem(data, "stopReason");
            if (cJSON_IsNumber(item)) stopData.stopReason = static_cast<uint8_t>(item->valueint);
        }
        shm->setStopChargeData(&stopData);
        if (shm->stopCharge() == 0) {
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
    }

    cJSON_Delete(root);
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

bool PileControllerProcess::parseGunFromTopic(const std::string& topic, int& outGun) const
{
    outGun = -1;
    const std::string marker = "/pile/";
    const size_t pos = topic.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    const size_t start = pos + marker.size();
    const size_t end = topic.find('/', start);
    if (end == std::string::npos) {
        return false;
    }
    try {
        outGun = std::stoi(topic.substr(start, end - start));
    } catch (...) {
        return false;
    }
    return true;
}
