/**
 * 主控通信进程实现
 * BY ZF
 */

#include "pile_controller_process.h"
#include "../../libv2gshm/libcshm/v2gshm.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <sstream>
#include <cstring>
#include <cjson/cJSON.h>

// BY ZF: 包含具体的IPileController实现类
#include "controllers/can_pile_controller.h"

PileControllerProcess::PileControllerProcess()
    : BaseProcess(PROC_PILE_CONTROLLER, "PileControllerProcess")
    , m_cmdQueue(nullptr)
    , m_logger(nullptr)
    , m_running(false)
    , m_shm(nullptr)
    , m_mqttSeq(0)
{
}

PileControllerProcess::~PileControllerProcess()
{
    stop();
}

bool PileControllerProcess::doInitialize()
{
    // BY ZF: 初始化日志发送器
    m_logger = new LogSender("PileController");
    
    // BY ZF: 加载多枪配置
    if (!loadGunConfigs()) {
        std::cerr << "[PileController] Failed to load gun configs" << std::endl;
        return false;
    }
    
    // BY ZF: 创建命令队列（可选：无守护进程时打开失败，可单独运行做 CAN 测试）
    m_cmdQueue = new MessageQueue(MSG_KEY_CMD);
    if (!m_cmdQueue->open()) {
        std::cerr << "[PileController] Command queue not available (errno=ENOENT: run without daemon?), continue without command queue" << std::endl;
        delete m_cmdQueue;
        m_cmdQueue = nullptr;
    }
    
    // BY ZF: 初始化共享内存（如果任何枪使用共享内存通信）
    bool needShm = false;
    for (const auto& gunCfg : m_config.gunConfigs) {
        if (gunCfg.commType == "shm") {
            needShm = true;
            break;
        }
    }
    
    if (needShm) {
        CShm* cshm = new CShm();
        if (!cshm->init()) {
            std::cerr << "[PileController] Failed to initialize shared memory" << std::endl;
            return false;
        }
        m_shm = cshm;
    }
    
    // BY ZF: 创建所有枪的控制器实例
    if (!createPileController()) {
        std::cerr << "[PileController] Failed to create pile controllers" << std::endl;
        return false;
    }

    m_eventCaches.resize(m_config.gunCount);
    m_dataCaches.resize(m_config.gunCount);
    m_lastPublish = std::chrono::steady_clock::now();

    // BY ZF: 初始化 MQTT（失败不影响主流程）
    if (!initMqtt()) {
        std::cerr << "[PileController] MQTT init failed, continue without MQTT" << std::endl;
    }
    
    std::cout << "[PileController] Initialized successfully (gun_count: " 
              << static_cast<int>(m_config.gunCount) << ")" << std::endl;
    return true;
}

// BY ZF: 加载多枪配置
bool PileControllerProcess::loadGunConfigs()
{
    ConfigManagerLite& config = getConfig();
    
    // 读取枪数量
    m_config.gunCount = static_cast<uint8_t>(config.getInt("PileController", "gun_count", 2));
    if (m_config.gunCount < 1 || m_config.gunCount > 10) {
        std::cerr << "[PileController] Invalid gun_count: " << static_cast<int>(m_config.gunCount) 
                  << " (must be 1-10)" << std::endl;
        return false;
    }
    
    // BY ZF: 为每个枪加载独立配置
    m_config.gunConfigs.resize(m_config.gunCount);
    for (uint8_t i = 0; i < m_config.gunCount; i++) {
        uint8_t gunNo = i + 1;  // 枪号从1开始
        GunConfig& gunCfg = m_config.gunConfigs[i];
        gunCfg.gunNo = gunNo;
        
        std::string gunPrefix = "gun" + std::to_string(gunNo) + "_";
        
        // BY ZF: 读取必需配置项（不存在则报错）
        gunCfg.commType = config.getString("PileController", gunPrefix + "comm_type", "");
        if (gunCfg.commType.empty()) {
            std::cerr << "[PileController] Missing gun" << static_cast<int>(gunNo) << "_comm_type" << std::endl;
            return false;
        }
        
        gunCfg.protocolType = config.getString("PileController", gunPrefix + "protocol_type", "");
        if (gunCfg.protocolType.empty()) {
            std::cerr << "[PileController] Missing gun" << static_cast<int>(gunNo) << "_protocol_type" << std::endl;
            return false;
        }
        
        // BY ZF: 根据通信方式读取对应配置项
        if (gunCfg.commType == "can") {
            gunCfg.devicePath = config.getString("PileController", gunPrefix + "device_path", "");
            gunCfg.baudrate = config.getInt("PileController", gunPrefix + "baudrate", 0);
            gunCfg.bcuAddr = static_cast<uint8_t>(config.getInt("PileController", gunPrefix + "bcu_addr", 0));
            gunCfg.cdzAddr = static_cast<uint8_t>(config.getInt("PileController", gunPrefix + "cdz_addr", 0));
            gunCfg.pileId = config.getString("PileController", gunPrefix + "pile_id", "");
            
            if (gunCfg.devicePath.empty() || gunCfg.baudrate == 0 || gunCfg.bcuAddr == 0 || gunCfg.cdzAddr == 0) {
                std::cerr << "[PileController] Invalid CAN config for gun" << static_cast<int>(gunNo) 
                          << ": device_path=" << gunCfg.devicePath 
                          << ", baudrate=" << gunCfg.baudrate 
                          << ", bcu_addr=0x" << std::hex << static_cast<int>(gunCfg.bcuAddr)
                          << ", cdz_addr=0x" << static_cast<int>(gunCfg.cdzAddr) << std::dec << std::endl;
                return false;
            }
        } else if (gunCfg.commType == "485") {
            gunCfg.devicePath = config.getString("PileController", gunPrefix + "device_path", "");
            gunCfg.baudrate = config.getInt("PileController", gunPrefix + "baudrate", 0);
            gunCfg.modbusAddress = static_cast<uint8_t>(config.getInt("PileController", gunPrefix + "modbus_address", 0));
            
            if (gunCfg.devicePath.empty() || gunCfg.baudrate == 0) {
                std::cerr << "[PileController] Invalid serial config for gun" << static_cast<int>(gunNo) << std::endl;
                return false;
            }
        } else if (gunCfg.commType == "tcp" || gunCfg.commType == "udp") {
            gunCfg.networkHost = config.getString("PileController", gunPrefix + "ip", "");
            gunCfg.networkPort = static_cast<uint16_t>(config.getInt("PileController", gunPrefix + "port", 0));
            
            if (gunCfg.networkHost.empty() || gunCfg.networkPort == 0) {
                std::cerr << "[PileController] Invalid network config for gun" << static_cast<int>(gunNo) << std::endl;
                return false;
            }
        } else if (gunCfg.commType == "shm") {
            // BY ZF: 共享内存配置可选，使用默认值
            gunCfg.shmKey = config.getInt("PileController", gunPrefix + "shm_key", 0);
        } else {
            std::cerr << "[PileController] Unknown comm_type for gun" << static_cast<int>(gunNo) 
                      << ": " << gunCfg.commType << std::endl;
            return false;
        }
    }
    
    // BY ZF: 读取全局协议参数（所有枪共用）
    m_config.packetTimeout = config.getInt("PileController", "packet_timeout", 0);
    m_config.retryCount = config.getInt("PileController", "retry_count", 0);
    
    if (m_config.packetTimeout == 0 || m_config.retryCount == 0) {
        std::cerr << "[PileController] Missing global protocol parameters: "
                  << "packet_timeout=" << m_config.packetTimeout 
                  << ", retry_count=" << m_config.retryCount << std::endl;
        return false;
    }

    // MQTT 配置
    m_config.mqttHost = config.getString("PileController", "mqtt_host", "localhost");
    m_config.mqttPort = config.getInt("PileController", "mqtt_port", 1883);
    m_config.mqttKeepalive = config.getInt("PileController", "mqtt_keepalive", 60);
    m_config.mqttClientId = config.getString("PileController", "mqtt_client_id", "pile_controller");
    m_config.mqttTopicPrefix = config.getString("PileController", "mqtt_topic_prefix", "tcu");
    
    std::cout << "[PileController] Loaded config for " << static_cast<int>(m_config.gunCount) << " gun(s)" << std::endl;
    return true;
}

bool PileControllerProcess::createPileController()
{
    // BY ZF: 为每个枪创建对应的控制器实例
    m_controllers.resize(m_config.gunCount);
    
    for (uint8_t i = 0; i < m_config.gunCount; i++) {
        const GunConfig& gunCfg = m_config.gunConfigs[i];
        
        // BY ZF: 根据配置创建对应的控制器
        if (gunCfg.commType == "can") {
            // BY ZF: 创建CAN控制器
            if (gunCfg.protocolType != "can2ccu") {
                std::cerr << "[PileController] CAN protocol type must be 'can2ccu' for gun" 
                          << static_cast<int>(gunCfg.gunNo) << ", got: " << gunCfg.protocolType << std::endl;
                return false;
            }
            
            // BY ZF: 创建CAN控制器实例（C++11兼容，不使用make_unique）
            std::unique_ptr<CANPileController> controller(new CANPileController());
            
            // BY ZF: 构建配置字符串（用于控制器初始化）
            std::ostringstream configStream;
            configStream << "device_path=" << gunCfg.devicePath
                         << ",baudrate=" << gunCfg.baudrate
                         << ",bcu_addr=0x" << std::hex << static_cast<int>(gunCfg.bcuAddr)
                         << ",cdz_addr=0x" << static_cast<int>(gunCfg.cdzAddr) << std::dec
                         << ",gun_no=" << static_cast<int>(gunCfg.gunNo - 1)  // BY ZF: 枪号转换为0-based（0=左枪，1=右枪）
                         << ",packet_timeout=" << m_config.packetTimeout
                         << ",retry_count=" << m_config.retryCount;
            if (!gunCfg.pileId.empty()) {
                configStream << ",pile_id=" << gunCfg.pileId;
            }
            
            // BY ZF: 初始化控制器
            if (!controller->initialize("can", configStream.str().c_str())) {
                std::cerr << "[PileController] Failed to initialize CAN controller for gun" 
                          << static_cast<int>(gunCfg.gunNo) << std::endl;
                return false;
            }
            
            m_controllers[i] = std::move(controller);
            std::cout << "[PileController] Created CAN controller for gun" 
                      << static_cast<int>(gunCfg.gunNo) << std::endl;
        } else if (gunCfg.commType == "tcp" || gunCfg.commType == "udp") {
            // TODO: 实现 NetworkPileController
            std::cerr << "[PileController] Network implementation not yet implemented for gun" 
                      << static_cast<int>(gunCfg.gunNo) << std::endl;
            return false;
        } else if (gunCfg.commType == "485") {
            // TODO: 实现 SerialPileController
            std::cerr << "[PileController] Serial implementation not yet implemented for gun" 
                      << static_cast<int>(gunCfg.gunNo) << std::endl;
            return false;
        } else if (gunCfg.commType == "shm") {
            // TODO: 实现 SharedMemoryPileController
            std::cerr << "[PileController] Shared memory implementation not yet implemented for gun" 
                      << static_cast<int>(gunCfg.gunNo) << std::endl;
            return false;
        } else {
            std::cerr << "[PileController] Unknown comm_type for gun" << static_cast<int>(gunCfg.gunNo) 
                      << ": " << gunCfg.commType << std::endl;
            return false;
        }
    }
    
    return true;
}

void PileControllerProcess::doRun()
{
    // BY ZF: 只创建一次工作线程并阻塞直到其退出，避免 BaseProcess::run() 每 100ms 调用 doRun() 时重复创建线程导致多线程竞争和退出时崩溃
    m_running = true;
    m_mainThread = std::thread([this]() {
        while (m_running.load()) {
            // BY ZF: 更新状态（遍历所有枪）
            updateStatusFromController();
            
            // 心跳由 CANPileController 内部周期线程负责
            
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
    
    // BY ZF: 清理所有枪的控制器
    for (auto& controller : m_controllers) {
        if (controller) {
            controller->cleanup();
            controller.reset();
        }
    }
    m_controllers.clear();
    
    if (m_shm) {
        delete static_cast<CShm*>(m_shm);
        m_shm = nullptr;
    }
    
    if (m_cmdQueue) {
        delete m_cmdQueue;
        m_cmdQueue = nullptr;
    }
    
    if (m_logger) {
        delete m_logger;
        m_logger = nullptr;
    }

    m_mqtt.loopStop(true);
    m_mqtt.disconnect();
}

void PileControllerProcess::feedWatchdog()
{
    // 通过消息队列发送心跳到守护进程
    // TODO: 实现看门狗心跳
}

void PileControllerProcess::processCommandMessage()
{
    if (!m_cmdQueue) {
        return;
    }
    
    // 当前已弃用消息队列命令，改为 MQTT 控制
}

void PileControllerProcess::updateStatusFromController()
{
    auto now = std::chrono::steady_clock::now();
    const bool doPublish = (now - m_lastPublish) >= std::chrono::milliseconds(1000);
    if (doPublish) {
        m_lastPublish = now;
    }
    // BY ZF: 遍历所有枪，从主控读取状态并更新共享内存
    for (size_t i = 0; i < m_controllers.size(); i++) {
        if (m_controllers[i]) {
            PileStatus status;
            if (m_controllers[i]->getStatus(m_config.gunConfigs[i].gunNo, &status) == 0) {
                // TODO: 更新共享内存中对应枪的状态
                // updateSharedMemoryStatus(status);
            }
        }
    }

    // MQTT 发布（聚合 500ms）
    for (size_t i = 0; i < m_controllers.size(); i++) {
        CANPileController* can = dynamic_cast<CANPileController*>(m_controllers[i].get());
        if (!can) {
            continue;
        }
        uint8_t gunNo = static_cast<uint8_t>(m_config.gunConfigs[i].gunNo - 1);

        DataCache& dataCache = m_dataCaches[i];
        if (can->isYC20DataValid()) {
            TCU2CCU_DataYC20 yc20;
            if (can->getYC20Data(&yc20) == 0) {
                dataCache.yc20 = yc20;
                dataCache.hasYc20 = true;
            }
        }
        if (can->isYX22DataValid()) {
            TCU2CCU_DataYX22 yx22;
            if (can->getYX22Data(&yx22) == 0) {
                dataCache.yx22 = yx22;
                dataCache.hasYx22 = true;
            }
        }
        if (can->isYX23DataValid()) {
            TCU2CCU_DataYX23 yx23;
            if (can->getYX23Data(&yx23) == 0) {
                dataCache.yx23 = yx23;
                dataCache.hasYx23 = true;
            }
        }
        if (doPublish && dataCache.hasYc20) {
            const TCU2CCU_DataYC20& yc20 = dataCache.yc20;
            std::string payload = buildDataPayload(gunNo, "yc", [&yc20](cJSON* data) {
                cJSON_AddNumberToObject(data, "outputVoltage", yc20.outputVoltage);
                cJSON_AddNumberToObject(data, "outputCurrent", yc20.outputCurrent);
                cJSON_AddNumberToObject(data, "soc", yc20.soc);
                cJSON_AddNumberToObject(data, "batteryMinTemp", yc20.batteryMinTemp);
                cJSON_AddNumberToObject(data, "batteryMaxTemp", yc20.batteryMaxTemp);
                cJSON_AddNumberToObject(data, "pileEnvTemp", yc20.pileEnvTemp);
            });
            publishData(gunNo, "yc", payload, false);
        }
        EventCache& cache = m_eventCaches[i];
        if (cache.pendingClear) {
            if (now - cache.workStatusZeroAt >= std::chrono::milliseconds(500)) {
                cache.hasStartResp = false;
                cache.hasStopResp = false;
                cache.hasStartComplete = false;
                cache.hasStopComplete = false;
                cache.pendingClear = false;
            }
        }

        if (doPublish && dataCache.hasYx22) {
            const TCU2CCU_DataYX22& yx22 = dataCache.yx22;
            const uint8_t vinReq = dataCache.hasYx23 ? dataCache.yx23.vinReq : 0;
            std::string payload = buildDataPayload(gunNo, "yx", [&yx22, vinReq](cJSON* data) {
                cJSON_AddNumberToObject(data, "workStatus", yx22.workStatus);
                cJSON_AddNumberToObject(data, "totalFault", yx22.totalFault);
                cJSON_AddNumberToObject(data, "totalAlarm", yx22.totalAlarm);
                cJSON_AddNumberToObject(data, "emergencyStopFault", yx22.emergencyStopFault);
                cJSON_AddNumberToObject(data, "vehicleConnectStatus", yx22.vehicleConnectStatus);
                cJSON_AddNumberToObject(data, "vinReq", vinReq);
                cJSON_AddNumberToObject(data, "gunSeatStatus", yx22.gunSeatStatus);
                cJSON_AddNumberToObject(data, "electronicLockStatus", yx22.electronicLockStatus);
                cJSON_AddNumberToObject(data, "dcContactorStatus", yx22.dcContactorStatus);
                cJSON_AddNumberToObject(data, "otherFault", yx22.otherFault);
            });
            publishData(gunNo, "yx", payload, false);
            if (cache.hasWorkStatus) {
                if (cache.lastWorkStatus != 0 && yx22.workStatus == 0) {
                    cache.pendingClear = true;
                    cache.workStatusZeroAt = now;
                }
            }
            cache.hasWorkStatus = true;
            cache.lastWorkStatus = yx22.workStatus;
        }

        // 事件即时发布
        if (dataCache.hasYx22) {
            const TCU2CCU_DataYX22& yx22 = dataCache.yx22;
            if (!cache.hasTotalFault || cache.totalFault != yx22.totalFault) {
                cache.totalFault = yx22.totalFault;
                cache.hasTotalFault = true;
                if (yx22.totalFault) {
                    std::string payload = buildDataPayload(gunNo, "deviceErr", [&yx22, &dataCache](cJSON* data) {
                        cJSON_AddNumberToObject(data, "totalFault", yx22.totalFault);
                        std::vector<const char*> faults;
                        auto addFault = [&faults](bool on, const char* name) {
                            if (on) {
                                faults.push_back(name);
                            }
                        };
                        addFault(yx22.emergencyStopFault, "emergencyStopFault");
                        addFault(yx22.smokeFault, "smokeFault");
                        addFault(yx22.acInputBreakerFault, "acInputBreakerFault");
                        addFault(yx22.dcBusContactorFault, "dcBusContactorFault");
                        addFault(yx22.dcBusFuseFault, "dcBusFuseFault");
                        addFault(yx22.electronicLockFault, "electronicLockFault");
                        addFault(yx22.fanFault, "fanFault");
                        addFault(yx22.lightningArresterFault, "lightningArresterFault");
                        addFault(yx22.insulationMonitorFault, "insulationMonitorFault");
                        addFault(yx22.batteryReverseFault, "batteryReverseFault");
                        addFault(yx22.guideFault, "guideFault");
                        addFault(yx22.pileOverTempFault, "pileOverTempFault");
                        addFault(yx22.interfaceOverTempFault, "interfaceOverTempFault");
                        addFault(yx22.gunNotReturnAlarm, "gunNotReturnAlarm");
                        addFault(yx22.bmsCommFault, "bmsCommFault");
                        addFault(yx22.inputOverVoltageFault, "inputOverVoltageFault");
                        addFault(yx22.inputUnderVoltageFault, "inputUnderVoltageFault");
                        addFault(yx22.dcBusOverVoltageFault, "dcBusOverVoltageFault");
                        addFault(yx22.dcBusUnderVoltageFault, "dcBusUnderVoltageFault");
                        addFault(yx22.dcBusOverCurrentFault, "dcBusOverCurrentFault");
                        addFault(yx22.moduleFault, "moduleFault");
                        addFault(yx22.moduleAcInputFault, "moduleAcInputFault");
                        addFault(yx22.moduleAcOverVoltageFault, "moduleAcOverVoltageFault");
                        addFault(yx22.moduleAcUnderVoltageFault, "moduleAcUnderVoltageFault");
                        addFault(yx22.moduleAcPhaseLossFault, "moduleAcPhaseLossFault");
                        addFault(yx22.moduleDcShortFault, "moduleDcShortFault");
                        addFault(yx22.moduleDcOverCurrentFault, "moduleDcOverCurrentFault");
                        addFault(yx22.moduleDcOverVoltageFault, "moduleDcOverVoltageFault");
                        addFault(yx22.moduleDcUnderVoltageFault, "moduleDcUnderVoltageFault");
                        addFault(yx22.moduleOverTempFault, "moduleOverTempFault");
                        addFault(yx22.moduleCommFault, "moduleCommFault");
                        addFault(yx22.moduleFanFault, "moduleFanFault");
                        if (dataCache.hasYx23) {
                            const TCU2CCU_DataYX23& yx23 = dataCache.yx23;
                            addFault(yx23.pileDoorFault, "pileDoorFault");
                            addFault(yx23.dcContactorStickFault, "dcContactorStickFault");
                            addFault(yx23.insulationMonitorAlarm, "insulationMonitorAlarm");
                            addFault(yx23.dischargeFault, "dischargeFault");
                            addFault(yx23.pileOverTempAlarm, "pileOverTempAlarm");
                            addFault(yx23.gunOverTempAlarm, "gunOverTempAlarm");
                            addFault(yx23.acInputContactorFault, "acInputContactorFault");
                            addFault(yx23.acInputContactorStickFault, "acInputContactorStickFault");
                            addFault(yx23.auxPowerFault, "auxPowerFault");
                            addFault(yx23.bridgeContactorFault, "bridgeContactorFault");
                            addFault(yx23.bridgeContactorStickFault, "bridgeContactorStickFault");
                            addFault(yx23.pileWaterFault, "pileWaterFault");
                            addFault(yx23.cabinetDoorFault, "cabinetDoorFault");
                            addFault(yx23.cabinetWaterFault, "cabinetWaterFault");
                            addFault(yx23.cabinetSmokeFault, "cabinetSmokeFault");
                            addFault(yx23.powerCtrlCommFault, "powerCtrlCommFault");
                            addFault(yx23.powerCtrlCommAlarm, "powerCtrlCommAlarm");
                            addFault(yx23.powerCtrlFault, "powerCtrlFault");
                            addFault(yx23.powerCtrlAlarm, "powerCtrlAlarm");
                            addFault(yx23.switchModuleCommFault, "switchModuleCommFault");
                            addFault(yx23.switchModuleCommAlarm, "switchModuleCommAlarm");
                            addFault(yx23.switchModuleFault, "switchModuleFault");
                            addFault(yx23.switchModuleAlarm, "switchModuleAlarm");
                            addFault(yx23.bmsFaultByCtrl, "bmsFaultByCtrl");
                            addFault(yx23.bmsSendFaultInfo, "bmsSendFaultInfo");
                            addFault(yx23.moduleDischargeFault, "moduleDischargeFault");
                            addFault(yx23.peBreakFault, "peBreakFault");
                            addFault(yx23.guideVoltageAbnormal, "guideVoltageAbnormal");
                            addFault(yx23.moduleStartFail, "moduleStartFail");
                            addFault(yx23.moduleShutdownFail, "moduleShutdownFail");
                            addFault(yx23.moduleAddrAbnormal, "moduleAddrAbnormal");
                            addFault(yx23.coolingSystemFault, "coolingSystemFault");
                            addFault(yx23.cabinetOverTempAlarm, "cabinetOverTempAlarm");
                            addFault(yx23.cabinetOverTempFault, "cabinetOverTempFault");
                            addFault(yx23.powerDistStrategyFail, "powerDistStrategyFail");
                            addFault(yx23.noIdleModuleFault, "noIdleModuleFault");
                            addFault(yx23.prechargeVoltageFail, "prechargeVoltageFail");
                            addFault(yx23.powerCtrlCommTimeout, "powerCtrlCommTimeout");
                            addFault(yx23.envHumidityAlarm, "envHumidityAlarm");
                        }
                        cJSON* arr = cJSON_CreateArray();
                        for (const char* name : faults) {
                            cJSON_AddItemToArray(arr, cJSON_CreateString(name));
                        }
                        cJSON_AddItemToObject(data, "faults", arr);
                        cJSON_AddNumberToObject(data, "otherFault", yx22.otherFault);
                    });
                    publishData(gunNo, "deviceErr", payload, false);
                }
            }
        }
        if (can->isStartChargeResponseValid()) {
            TCU2CCU_StartChargeResponseData r;
            if (can->getStartChargeResponse(&r) == 0) {
                if (!cache.hasStartResp) {
                    std::string payload = buildDataPayload(gunNo, "start_response", [&r](cJSON* data) {
                        cJSON_AddNumberToObject(data, "confirmFlag", r.confirmFlag);
                        cJSON_AddNumberToObject(data, "startFailReason", r.startFailReason);
                        cJSON_AddNumberToObject(data, "loadControlSwitch", r.loadControlSwitch);
                        cJSON_AddNumberToObject(data, "plugAndChargeFlag", r.plugAndChargeFlag);
                        cJSON_AddNumberToObject(data, "auxPowerVoltage", r.auxPowerVoltage);
                    });
                    publishCmdUpset(gunNo, payload);
                    cache.startResp = r;
                    cache.hasStartResp = true;
                }
            }
            can->clearStartChargeResponseValid();
        }

        if (can->isStopChargeResponseValid()) {
            TCU2CCU_StopChargeResponseData r;
            if (can->getStopChargeResponse(&r) == 0) {
                if (!cache.hasStopResp) {
                    std::string payload = buildDataPayload(gunNo, "stop_response", [&r](cJSON* data) {
                        cJSON_AddNumberToObject(data, "confirmFlag", r.confirmFlag);
                    });
                    publishCmdUpset(gunNo, payload);
                    cache.stopResp = r;
                    cache.hasStopResp = true;
                }
            }
            can->clearStopChargeResponseValid();
        }

        if (can->isStartCompleteDataValid()) {
            TCU2CCU_StatusStartCompleteData r;
            if (can->getStartCompleteData(&r) == 0) {
                if (!cache.hasStartComplete) {
                    std::string payload = buildDataPayload(gunNo, "start_complete", [&r](cJSON* data) {
                        auto addByteArray = [](cJSON* obj, const char* key, const uint8_t* bytes, size_t len) {
                            cJSON* arr = cJSON_CreateArray();
                            for (size_t i = 0; i < len; i++) {
                                cJSON_AddItemToArray(arr, cJSON_CreateNumber(bytes[i]));
                            }
                            cJSON_AddItemToObject(obj, key, arr);
                        };

                        cJSON_AddNumberToObject(data, "successFlag", r.successFlag);
                        cJSON_AddNumberToObject(data, "chargeFailReason", r.chargeFailReason);
                        addByteArray(data, "pileBmsVersion", r.pileBmsVersion, 3);
                        addByteArray(data, "bmsPileVersion", r.bmsPileVersion, 3);
                        cJSON_AddNumberToObject(data, "handshakeResult", r.handshakeResult);
                        cJSON_AddNumberToObject(data, "batteryType", r.batteryType);
                        cJSON_AddNumberToObject(data, "maxAllowTemp", r.maxAllowTemp);
                        cJSON_AddNumberToObject(data, "bmsMaxChargeVoltage", r.bmsMaxChargeVoltage);
                        cJSON_AddNumberToObject(data, "cellMaxChargeVoltage", r.cellMaxChargeVoltage);
                        cJSON_AddNumberToObject(data, "maxAllowChargeCurrent", r.maxAllowChargeCurrent);
                        cJSON_AddNumberToObject(data, "ratedTotalVoltage", r.ratedTotalVoltage);
                        cJSON_AddNumberToObject(data, "currentTotalVoltage", r.currentTotalVoltage);
                        cJSON_AddNumberToObject(data, "ratedCapacity", r.ratedCapacity);
                        cJSON_AddNumberToObject(data, "nominalEnergy", r.nominalEnergy);
                        cJSON_AddNumberToObject(data, "soc", r.soc);
                        cJSON_AddNumberToObject(data, "pileMaxOutputVoltage", r.pileMaxOutputVoltage);
                        cJSON_AddNumberToObject(data, "pileMinOutputVoltage", r.pileMinOutputVoltage);
                        cJSON_AddNumberToObject(data, "pileMaxOutputCurrent", r.pileMaxOutputCurrent);
                        cJSON_AddNumberToObject(data, "pileMinOutputCurrent", r.pileMinOutputCurrent);
                        auto sanitizeAscii = [](const char* src, size_t len) {
                            std::string out;
                            out.reserve(len);
                            for (size_t i = 0; i < len; i++) {
                                unsigned char ch = static_cast<unsigned char>(src[i]);
                                if (ch == 0) {
                                    break;
                                }
                                if (ch >= 0x20 && ch <= 0x7e) {
                                    out.push_back(static_cast<char>(ch));
                                } else {
                                    out.push_back('.');
                                }
                            }
                            return out;
                        };
                        std::string vinStr = sanitizeAscii(r.vin, 17);
                        std::string manuStr = sanitizeAscii(r.batteryManufacturer, 4);
                        cJSON_AddStringToObject(data, "vin", vinStr.c_str());
                        cJSON_AddStringToObject(data, "batteryManufacturer", manuStr.c_str());
                        addByteArray(data, "batterySerial", r.batterySerial, 4);
                        cJSON_AddNumberToObject(data, "batteryProdYear", r.batteryProdYear);
                        cJSON_AddNumberToObject(data, "batteryProdMonth", r.batteryProdMonth);
                        cJSON_AddNumberToObject(data, "batteryProdDay", r.batteryProdDay);
                        addByteArray(data, "batteryChargeCount", r.batteryChargeCount, 3);
                        cJSON_AddNumberToObject(data, "batteryPropertyFlag", r.batteryPropertyFlag);
                        addByteArray(data, "bmsSoftwareVersion", r.bmsSoftwareVersion, 8);
                    });
                    publishCmdUpset(gunNo, payload);
                    cache.startComplete = r;
                    cache.hasStartComplete = true;
                }
            }
            can->clearStartCompleteValid();
        }

        if (can->isStopCompleteDataValid()) {
            TCU2CCU_StatusStopCompleteData r;
            if (can->getStopCompleteData(&r) == 0) {
                if (!cache.hasStopComplete) {
                    std::string payload = buildDataPayload(gunNo, "stop_complete", [&r](cJSON* data) {
                        cJSON_AddNumberToObject(data, "stopReason", r.stopReason);
                        cJSON_AddNumberToObject(data, "stopSuccessFlag", r.stopSuccessFlag);
                        cJSON_AddNumberToObject(data, "bmsStopReason", r.bmsStopReason);
                        cJSON_AddNumberToObject(data, "bmsChargeFaultReason", r.bmsChargeFaultReason);
                        cJSON_AddNumberToObject(data, "bmsStopErrorReason", r.bmsStopErrorReason);
                        cJSON_AddNumberToObject(data, "stopSoc", r.stopSoc);
                        cJSON_AddNumberToObject(data, "cellMinVoltage", r.cellMinVoltage);
                        cJSON_AddNumberToObject(data, "cellMaxVoltage", r.cellMaxVoltage);
                        cJSON_AddNumberToObject(data, "batteryMinTemp", r.batteryMinTemp);
                        cJSON_AddNumberToObject(data, "batteryMaxTemp", r.batteryMaxTemp);

                        std::vector<std::string> faults;
                        auto addTimeoutFault = [&faults](const char* name, uint8_t code) {
                            if (code == 1) {
                                faults.emplace_back(name);
                            }
                        };

                        addTimeoutFault("timeoutSpn2560_00", r.timeoutSpn2560_00);
                        addTimeoutFault("timeoutSpn2560_AA", r.timeoutSpn2560_AA);
                        addTimeoutFault("timeoutTimeSync", r.timeoutTimeSync);
                        addTimeoutFault("timeoutChargeReady", r.timeoutChargeReady);
                        addTimeoutFault("timeoutChargeStatus", r.timeoutChargeStatus);
                        addTimeoutFault("timeoutChargeStop", r.timeoutChargeStop);
                        addTimeoutFault("timeoutChargeStat", r.timeoutChargeStat);
                        addTimeoutFault("timeoutBmsVehicleId", r.timeoutBmsVehicleId);
                        addTimeoutFault("timeoutBatteryParams", r.timeoutBatteryParams);
                        addTimeoutFault("timeoutBmsReady", r.timeoutBmsReady);
                        addTimeoutFault("timeoutBatteryStatus", r.timeoutBatteryStatus);
                        addTimeoutFault("timeoutBatteryReq", r.timeoutBatteryReq);
                        addTimeoutFault("timeoutBmsStop", r.timeoutBmsStop);
                        addTimeoutFault("timeoutBmsStat", r.timeoutBmsStat);
                        if (r.bmsOtherError) {
                            char buf[32];
                            std::snprintf(buf, sizeof(buf), "bmsOtherError=0x%02X", r.bmsOtherError);
                            faults.emplace_back(buf);
                        }
                        if (r.pileOtherError) {
                            char buf[32];
                            std::snprintf(buf, sizeof(buf), "pileOtherError=0x%02X", r.pileOtherError);
                            faults.emplace_back(buf);
                        }
                        cJSON* arr = cJSON_CreateArray();
                        for (const auto& s : faults) {
                            cJSON_AddItemToArray(arr, cJSON_CreateString(s.c_str()));
                        }
                        cJSON_AddItemToObject(data, "faults", arr);
                    });
                    publishCmdUpset(gunNo, payload);
                    cache.stopComplete = r;
                    cache.hasStopComplete = true;
                }
            }
            can->clearStopCompleteValid();
        }
    }
}

void PileControllerProcess::handleStatusChanged(uint8_t gunNo, const PileStatus& status)
{
    // TODO: 处理主控主动上报的状态变化
    // 更新共享内存
    // 发送事件到消息队列
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
        for (uint8_t i = 0; i < m_config.gunCount; i++) {
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

    for (uint8_t i = 0; i < m_config.gunCount; i++) {
        uint8_t gunNo = m_config.gunConfigs[i].gunNo;
        std::ostringstream t;
        t << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gunNo - 1) << "/cmd";
        m_mqtt.subscribe(t.str(), 1);
    }
    return true;
}

void PileControllerProcess::publishData(uint8_t gunNo, const std::string& type, const std::string& payload, bool retain)
{
    std::ostringstream t;
    t << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gunNo) << "/data";
    m_mqtt.publish(t.str(), payload, 0, retain);
}

void PileControllerProcess::publishCmdUpset(uint8_t gunNo, const std::string& payload)
{
    std::ostringstream t;
    t << m_config.mqttTopicPrefix << "/pile/" << static_cast<int>(gunNo) << "/cmdupset";
    m_mqtt.publish(t.str(), payload, 2, false);
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
    CANPileController* can = dynamic_cast<CANPileController*>(m_controllers[gun].get());
    if (!can) {
        return;
    }

    cJSON* root = cJSON_Parse(payload.c_str());
    if (!root) {
        return;
    }
    cJSON* cmd = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd)) {
        cJSON_Delete(root);
        return;
    }
    std::string cmdStr = cmd->valuestring;
    cJSON* data = cJSON_GetObjectItem(root, "data");

    if (cmdStr == "start_charge") {
        TCU2CCU_CmdStartChargeData startCmd;
        memset(&startCmd, 0, sizeof(startCmd));
        startCmd.loadControlSwitch = 0x02;
        startCmd.plugAndChargeFlag = 0x01;
        startCmd.auxPowerVoltage = 0x0C;
        if (cJSON_IsObject(data)) {
            cJSON* v = cJSON_GetObjectItem(data, "loadControlSwitch");
            if (cJSON_IsNumber(v)) startCmd.loadControlSwitch = static_cast<uint8_t>(v->valueint);
            v = cJSON_GetObjectItem(data, "plugAndChargeFlag");
            if (cJSON_IsNumber(v)) startCmd.plugAndChargeFlag = static_cast<uint8_t>(v->valueint);
            v = cJSON_GetObjectItem(data, "auxPowerVoltage");
            if (cJSON_IsNumber(v)) startCmd.auxPowerVoltage = static_cast<uint8_t>(v->valueint);
        }
        can->setStartChargeData(&startCmd);
        can->startCharge();
    } else if (cmdStr == "stop_charge") {
        TCU2CCU_CmdStopChargeData stopCmd;
        memset(&stopCmd, 0, sizeof(stopCmd));
        stopCmd.stopReason = 0x01;
        stopCmd.tcuStopCode = 0x00;
        if (cJSON_IsObject(data)) {
            cJSON* v = cJSON_GetObjectItem(data, "stopReason");
            if (cJSON_IsNumber(v)) stopCmd.stopReason = static_cast<uint8_t>(v->valueint);
            v = cJSON_GetObjectItem(data, "tcuStopCode");
            if (cJSON_IsNumber(v)) stopCmd.tcuStopCode = static_cast<uint8_t>(v->valueint);
        }
        can->setStopChargeData(&stopCmd);
        can->stopCharge();
    } else if (cmdStr == "clear_fault") {
        can->clearFault(static_cast<uint8_t>(gun));
    }

    cJSON_Delete(root);
}

std::string PileControllerProcess::buildDataPayload(uint8_t gunNo, const std::string& type, const std::function<void(cJSON*)>& fillData)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* data = cJSON_CreateObject();
    uint64_t seq = ++m_mqttSeq;
    uint64_t ts = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::system_clock::now().time_since_epoch()).count());
    cJSON_AddNumberToObject(root, "ts", static_cast<double>(ts));
    cJSON_AddNumberToObject(root, "seq", static_cast<double>(seq));
    cJSON_AddStringToObject(root, "source", "pile_controller");
    cJSON_AddNumberToObject(root, "gun", gunNo);
    cJSON_AddStringToObject(root, "type", type.c_str());
    fillData(data);
    cJSON_AddItemToObject(root, "data", data);
    char* text = cJSON_PrintUnformatted(root);
    std::string payload = text ? text : "";
    if (text) {
        free(text);
    }
    cJSON_Delete(root);
    return payload;
}

bool PileControllerProcess::parseGunFromTopic(const std::string& topic, int& outGun) const
{
    outGun = -1;
    std::string marker = "/pile/";
    size_t pos = topic.find(marker);
    if (pos == std::string::npos) {
        return false;
    }
    size_t start = pos + marker.size();
    size_t end = topic.find('/', start);
    if (end == std::string::npos) {
        return false;
    }
    std::string gunStr = topic.substr(start, end - start);
    try {
        outGun = std::stoi(gunStr);
    } catch (...) {
        return false;
    }
    return true;
}
