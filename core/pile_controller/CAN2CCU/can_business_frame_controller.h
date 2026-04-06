/**
 * CAN 业务帧解析控制器
 * 仅从 CAN 链路接收报文并解析为业务帧（YX22、YC20 等），不发送命令
 * BY ZF
 *
 * 用途：联调时用主控/真实 CAN 数据验证规约层解码逻辑，数据来源为 CAN 总线
 */

#ifndef CAN_BUSINESS_FRAME_CONTROLLER_H
#define CAN_BUSINESS_FRAME_CONTROLLER_H

#include "../../base/communication/can_communication.h"
#include "can2ccu_protocol.h"
#include <memory>
#include <string>
#include <atomic>

class CANBusinessFrameController {
public:
    CANBusinessFrameController();
    ~CANBusinessFrameController();

    /**
     * 初始化：打开 CAN 设备并初始化规约（仅接收、不解码以外的逻辑）
     * @param config 配置字符串，格式：device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=1
     * @return true 成功，false 失败
     */
    bool initialize(const char* config);

    /**
     * 释放资源
     */
    void cleanup();

    /**
     * 运行接收循环：从 CAN 链路收帧 -> decodeFrame -> 解析出业务数据（如 YX22）时打印
     * 阻塞直到 stop() 被调用或出错
     */
    void run();

    /**
     * 停止接收循环
     */
    void stop();

    /**
     * 是否已初始化
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * 获取规约层指针（供测试或外部读取 YX22 等）
     */
    CAN2CCUProtocol* getProtocol() { return m_protocol.get(); }
    const CAN2CCUProtocol* getProtocol() const { return m_protocol.get(); }

private:
    void parseConfig(const char* config);
    void onYX22Decoded();

private:
    std::unique_ptr<CANCommunication> m_canComm;
    std::unique_ptr<CAN2CCUProtocol> m_protocol;

    std::string m_devicePath;
    uint32_t m_baudrate;
    uint8_t m_bcuAddr;
    uint8_t m_cdzAddr;
    uint8_t m_gunNo;

    bool m_initialized;
    std::atomic<bool> m_running;
};

#endif // CAN_BUSINESS_FRAME_CONTROLLER_H
