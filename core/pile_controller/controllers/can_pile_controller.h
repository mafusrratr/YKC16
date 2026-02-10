/**
 * CAN通信方式的充电桩主控控制器
 * 实现 IPileController 接口，使用 CANCommunication 和 CAN2CCUProtocol
 * BY ZF
 */

#ifndef CAN_PILE_CONTROLLER_H
#define CAN_PILE_CONTROLLER_H

#include "../ipile_controller.h"
#include "../../base/communication/can_communication.h"
#include "../protocol/can2ccu_protocol.h"
#include <memory>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

/**
 * CAN通信方式的充电桩主控控制器
 * 
 * 架构：
 * - 应用层：CANPileController（实现 IPileController）
 * - 规约层：CAN2CCUProtocol（具体协议实现）
 * - 通信层：CANCommunication（CAN通信基类）
 * 
 * 数据流：
 * 业务调用 -> CANPileController -> CAN2CCUProtocol（编码） -> CANCommunication（发送）
 * CANCommunication（接收） -> CAN2CCUProtocol（解码） -> CANPileController -> 业务回调
 */
class CANPileController : public IPileController {
public:
    CANPileController();
    virtual ~CANPileController();
    
    // IPileController 接口实现
    bool initialize(const char* commType, const char* config) override;
    void cleanup() override;
    
    int startCharge() override;
    int stopCharge() override;
    int getStatus(uint8_t gunNo, PileStatus* status) override;

    int setStartChargeData(const TCU2CCU_CmdStartChargeData* cmd);
    int setStopChargeData(const TCU2CCU_CmdStopChargeData* cmd);
    int setVersionCheckData(const TCU2CCU_CmdVersionCheckData* cmd);
    int setChargeParamData(const TCU2CCU_CmdChargeParamData* cmd);

    int getStartChargeResponse(TCU2CCU_StartChargeResponseData* out) const;
    bool isStartChargeResponseValid() const;
    int getStopChargeResponse(TCU2CCU_StopChargeResponseData* out) const;
    bool isStopChargeResponseValid() const;

    int getVersionCheckResponse(TCU2CCU_VersionCheckResponseData* out) const;
    bool isVersionCheckResponseValid() const;
    int getChargeParamResponse(TCU2CCU_ChargeParamResponseData* out) const;
    bool isChargeParamResponseValid() const;

    int getStartCompleteData(TCU2CCU_StatusStartCompleteData* out) const;
    bool isStartCompleteDataValid() const;
    int getStopCompleteData(TCU2CCU_StatusStopCompleteData* out) const;
    bool isStopCompleteDataValid() const;
    void clearStartChargeResponseValid();
    void clearStopChargeResponseValid();
    void clearStartCompleteValid();
    void clearStopCompleteValid();

    int getYC20Data(TCU2CCU_DataYC20* out) const;
    bool isYC20DataValid() const;
    int getYX22Data(TCU2CCU_DataYX22* out) const;
    bool isYX22DataValid() const;
    int getYX23Data(TCU2CCU_DataYX23* out) const;
    bool isYX23DataValid() const;

    /** 根据心跳接收情况返回通信状态，供外部处理超时/告警。0x00 正常，0x01 接收超时 */
    uint8_t getHeartbeatCommStatus() const;
    
    // CAN 特有功能（不在基类）
    int getFaultCode(uint8_t gunNo, uint16_t* faultCode);
    int clearFault(uint8_t gunNo);
    bool heartbeat();
    int encodeVersionCheck();
    int encodeIssueChargeParams(const uint8_t pileId[7]);

private:
    struct RetryState {
        bool active;
        std::chrono::steady_clock::time_point start;
        std::chrono::steady_clock::time_point lastSend;
        RetryState() : active(false) {}
    };

    // BY ZF: 接收线程（非阻塞接收，循环处理）
    void receiveThread();
    // BY ZF: 周期性任务线程（心跳/版本校验/下发桩参数）
    void taskThread();

    // BY ZF: 状态变化回调（由接收线程触发）
    void onStatusChanged(uint8_t gunNo, const PileStatus* status);
    
    // BY ZF: 构建配置字符串（用于协议初始化）
    std::string buildProtocolConfig(uint8_t bcuAddr, uint8_t cdzAddr, uint8_t gunNo, 
                                     int packetTimeout, int retryCount);
    
    // BY ZF: 发送回调包装函数（用于协议层调用通讯层发送）
    static int sendCallbackWrapper(uint32_t canId, const uint8_t* data, uint8_t dataLen);
    
    // BY ZF: 当前实例指针（用于静态回调函数访问实例）
    static CANPileController* s_currentInstance;

private:
    // BY ZF: 通信层（CAN通信基类）
    std::unique_ptr<CANCommunication> m_canComm;
    
    // BY ZF: 规约层（CAN2CCU协议）
    std::unique_ptr<CAN2CCUProtocol> m_protocol;
    
    // BY ZF: 配置参数
    uint8_t m_gunNo;                  // 枪号（0=左枪，1=右枪）
    std::string m_devicePath;         // CAN设备路径（如 "can0"）
    uint32_t m_baudrate;              // 波特率
    uint8_t m_bcuAddr;                // BCU地址（TCU地址），默认0x8A，可配置
    uint8_t m_cdzAddr;                // CDZ地址（CCU地址），从配置文件读取
    int m_packetTimeout;              // 数据包超时（秒）
    int m_retryCount;                 // 重传次数
    
    // BY ZF: 状态管理
    bool m_initialized;               // 是否已初始化
    std::atomic<bool> m_running;      // 接收线程运行标志
    std::thread m_receiveThread;      // 接收线程

    std::atomic<bool> m_taskRunning;  // 周期任务线程运行标志
    std::thread m_taskThread;         // 周期任务线程
    
    // BY ZF: 状态缓存（用于getStatus）
    PileStatus m_cachedStatus;        // 缓存的状态
    std::atomic<bool> m_statusValid;  // 状态是否有效

    // BY ZF: 版本校验与桩参数状态
    std::atomic<bool> m_versionOk;
    std::atomic<bool> m_chargeParamOk;
    std::chrono::steady_clock::time_point m_lastHeartbeat;
    std::chrono::steady_clock::time_point m_lastVersionReq;
    std::chrono::steady_clock::time_point m_lastChargeParamReq;

    // BY ZF: 充电桩编号（BCD 7字节）
    uint8_t m_pileId[7];
    bool m_hasPileId;

    // BY ZF: 重发控制（250ms 间隔，5s 超时）
    std::mutex m_retryMutex;
    RetryState m_startReqRetry;
    RetryState m_stopReqRetry;
    RetryState m_startCompleteAckRetry;
    RetryState m_stopCompleteAckRetry;


    // BY ZF: 启动完成和停止完成状态
    uint8_t m_startCompleteLoadSwitch;
    uint8_t m_stopCompleteReason;
};

#endif // CAN_PILE_CONTROLLER_H
