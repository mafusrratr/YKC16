/**
 * 主控通信进程
 * 负责与充电桩主控板通信，获取实时充电状态、控制指令下发等
 * BY ZF
 */

#ifndef PILE_CONTROLLER_PROCESS_H
#define PILE_CONTROLLER_PROCESS_H

#include "../interfaces/iprocess.h"
#include "../base/process/base_process.h"
#include "../base/common/config_manager_lite.h"
#include "../base/common/message_queue.h"
#include "../base/logger/log_sender.h"
#include "ipile_controller.h"
#include "../base/mqtt/mqtt_client.h"
#include <thread>
#include <atomic>
#include <memory>
#include <vector>
#include <string>
#include <cstdint>
#include <chrono>

#include "protocol/tcu2ccu_data.h"

struct cJSON;

// BY ZF: 单个枪的配置结构体
struct GunConfig {
    uint8_t gunNo;                    // 枪号（1-10）
    std::string commType;              // 通信方式（can/485/shm/tcp/udp）
    std::string protocolType;          // 规约类型（cdz/custom/modbus/udp2cdz/direct）
    
    // CAN通信配置
    std::string devicePath;            // 设备路径（can/485用）
    uint32_t baudrate;                 // 波特率（can/485用）
    uint8_t bcuAddr;                   // BCU地址（CAN通信时）
    uint8_t cdzAddr;                   // 充电桩地址（CAN通信时）
    std::string pileId;               // 充电桩编号（14位十六进制字符串）
    
    // 网络通信配置
    std::string networkHost;           // 网络地址（tcp/udp用）
    uint16_t networkPort;              // 网络端口（tcp/udp用）
    
    // 串口通信配置
    uint8_t modbusAddress;             // Modbus地址（串口通信时）
    
    // 共享内存配置
    int shmKey;                        // 共享内存Key（共享内存通信时）
    
    GunConfig() 
        : gunNo(0)
        , baudrate(0)
        , bcuAddr(0)
        , cdzAddr(0)
        , pileId("")
        , networkPort(0)
        , modbusAddress(0)
        , shmKey(0)
    {}
};

// BY ZF: 全局配置结构体
struct PileControllerConfig {
    uint8_t gunCount;                  // 枪数量（1-10）
    std::vector<GunConfig> gunConfigs; // 每个枪的配置（独立配置）
    
    // BY ZF: 全局协议参数（所有枪共用）
    int packetTimeout;                 // 数据包超时（秒）
    int retryCount;                    // 重传次数

    // MQTT 配置
    std::string mqttHost;
    int mqttPort;
    int mqttKeepalive;
    std::string mqttClientId;
    std::string mqttTopicPrefix;
    
    PileControllerConfig() 
        : gunCount(0)
        , packetTimeout(0)
        , retryCount(0)
        , mqttPort(1883)
        , mqttKeepalive(60)
    {}
};

/**
 * 主控通信进程
 * 根据配置选择不同的通信方式实现（CAN/485/共享内存/网络等）
 * 支持多枪独立配置（最多10枪）
 */
class PileControllerProcess : public BaseProcess {
public:
    PileControllerProcess();
    virtual ~PileControllerProcess();
    
    // BaseProcess接口实现
    bool doInitialize() override;
    void doRun() override;
    void doCleanup() override;
    
    // BY ZF: 看门狗喂狗函数（供外部驱动主循环时调用）
    void feedWatchdog();

private:
    /**
     * BY ZF: 加载多枪配置
     * @return true成功, false失败
     */
    bool loadGunConfigs();
    
    /**
     * 根据配置创建IPileController实现（支持多枪）
     * @return true成功, false失败
     */
    bool createPileController();
    
    /**
     * 处理消息队列中的控制命令
     */
    void processCommandMessage();
    
    /**
     * 周期性地从主控读取状态并更新共享内存
     */
    void updateStatusFromController();
    
    /**
     * 处理主控状态变化回调
     */
    void handleStatusChanged(uint8_t gunNo, const PileStatus& status);

    bool initMqtt();
    void publishData(uint8_t gunNo, const std::string& type, const std::string& payload, bool retain = true);
    void publishCmdUpset(uint8_t gunNo, const std::string& payload);
    void onMqttMessage(const std::string& topic, const std::string& payload);
    std::string buildDataPayload(uint8_t gunNo, const std::string& type, const std::function<void(struct cJSON*)>& fillData);
    bool parseGunFromTopic(const std::string& topic, int& outGun) const;

private:
    struct DataCache {
        bool hasYc20;
        bool hasYx22;
        bool hasYx23;
        TCU2CCU_DataYC20 yc20;
        TCU2CCU_DataYX22 yx22;
        TCU2CCU_DataYX23 yx23;
        DataCache()
            : hasYc20(false)
            , hasYx22(false)
            , hasYx23(false)
        {}
    };

    struct EventCache {
        bool hasTotalFault;
        uint8_t totalFault;
        EventCache()
            : hasTotalFault(false)
            , totalFault(0)
        {}
    };

    // BY ZF: 多枪配置
    PileControllerConfig m_config;
    
    // BY ZF: 每个枪的控制器实例（支持多枪）
    std::vector<std::unique_ptr<IPileController>> m_controllers;
    
    // 消息队列
    MessageQueue* m_cmdQueue;  // 命令队列（接收控制命令）
    
    // 日志发送器
    LogSender* m_logger;
    
    // 主循环线程
    std::thread m_mainThread;
    std::atomic<bool> m_running;

    // BY ZF: 共享内存指针（所有枪共享，如果使用共享内存通信）
    void* m_shm;

    // MQTT 客户端
    MqttClient m_mqtt;
    std::atomic<uint64_t> m_mqttSeq;
    std::vector<EventCache> m_eventCaches;
    std::vector<DataCache> m_dataCaches;
    std::chrono::steady_clock::time_point m_lastPublish;
};

#endif // PILE_CONTROLLER_PROCESS_H
