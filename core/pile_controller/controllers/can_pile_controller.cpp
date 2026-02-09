/**
 * CAN通信方式的充电桩主控控制器实现
 * BY ZF
 */

#include "can_pile_controller.h"
#include <iostream>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <algorithm>

// BY ZF: 静态成员变量初始化
CANPileController* CANPileController::s_currentInstance = nullptr;

CANPileController::CANPileController()
    : m_gunNo(0)
    , m_baudrate(0)
    , m_bcuAddr(0x8A)      // BY ZF: 默认BCU地址0x8A
    , m_cdzAddr(0)
    , m_packetTimeout(5)
    , m_retryCount(3)
    , m_initialized(false)
    , m_running(false)
    , m_taskRunning(false)
    , m_statusValid(false)
    , m_versionOk(false)
    , m_chargeParamOk(false)
    , m_hasPileId(false)
{
    memset(&m_cachedStatus, 0, sizeof(PileStatus));
    memset(m_pileId, 0, sizeof(m_pileId));
}

CANPileController::~CANPileController()
{
    cleanup();
}

bool CANPileController::initialize(const char* commType, const char* config)
{
    // BY ZF: 解析配置参数（格式：device_path=can0,baudrate=250000,can_id_base=0x180,gun_no=1,packet_timeout=5,retry_count=3）
    // 当前为简单实现，后续可根据需要完善配置解析
    if (commType == nullptr || strcmp(commType, "can") != 0) {
        std::cerr << "[CANPileController] Invalid commType: " 
                  << (commType ? commType : "null") << "\n";
        return false;
    }
    
    if (config == nullptr || strlen(config) == 0) {
        std::cerr << "[CANPileController] Invalid config parameter\n";
        return false;
    }
    
    // BY ZF: 解析配置字符串（格式：device_path=can0,baudrate=250000,can_id_base=0x180,gun_no=1,packet_timeout=5,retry_count=3）
    std::string configStr(config);
    std::istringstream iss(configStr);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        size_t pos = token.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = token.substr(0, pos);
        std::string value = token.substr(pos + 1);
        
        if (key == "device_path") {
            m_devicePath = value;
        } else if (key == "baudrate") {
            m_baudrate = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "bcu_addr") {
            // BY ZF: 支持十六进制格式（0x前缀）
            if (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X") {
                m_bcuAddr = static_cast<uint8_t>(std::stoul(value, nullptr, 16));
            } else {
                m_bcuAddr = static_cast<uint8_t>(std::stoul(value));
            }
        } else if (key == "cdz_addr") {
            // BY ZF: 支持十六进制格式（0x前缀）
            if (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X") {
                m_cdzAddr = static_cast<uint8_t>(std::stoul(value, nullptr, 16));
            } else {
                m_cdzAddr = static_cast<uint8_t>(std::stoul(value));
            }
        } else if (key == "gun_no") {
            m_gunNo = static_cast<uint8_t>(std::stoul(value));
        } else if (key == "packet_timeout") {
            m_packetTimeout = std::stoi(value);
        } else if (key == "retry_count") {
            m_retryCount = std::stoi(value);
        } else if (key == "pile_id") {
            // BY ZF: pile_id 为 14 位十六进制字符串（7字节 BCD）
            std::string s;
            for (char ch : value) {
                if (ch == ' ' || ch == '-' || ch == ':') continue;
                s.push_back(ch);
            }
            if (s.size() == 14) {
                bool ok = true;
                for (size_t i = 0; i < 7; i++) {
                    std::string byteStr = s.substr(i * 2, 2);
                    char* endp = nullptr;
                    long v = std::strtol(byteStr.c_str(), &endp, 16);
                    if (endp == nullptr || *endp != '\0') {
                        ok = false;
                        break;
                    }
                    m_pileId[i] = static_cast<uint8_t>(v & 0xFF);
                }
                m_hasPileId = ok;
            }
        }
    }
    
    // BY ZF: 验证必需参数
    if (m_devicePath.empty() || m_baudrate == 0 || m_cdzAddr == 0) {
        std::cerr << "[CANPileController] Invalid config: device_path=" << m_devicePath
                  << ", baudrate=" << m_baudrate
                  << ", cdz_addr=0x" << std::hex << static_cast<int>(m_cdzAddr) << std::dec
                  << "\n";
        return false;
    }
    
    // BY ZF: 设置默认值
    if (m_bcuAddr == 0) {
        m_bcuAddr = 0x8A;  // 默认BCU地址
    }
    
    // BY ZF: 创建CAN通信对象（C++11兼容，不使用make_unique）
    m_canComm.reset(new CANCommunication());
    
    // BY ZF: 创建CAN2CCU协议对象
    m_protocol.reset(new CAN2CCUProtocol());
    
    // BY ZF: 初始化协议（传入配置字符串）
    std::string protocolConfig = buildProtocolConfig(m_bcuAddr, m_cdzAddr, m_gunNo, 
                                                      m_packetTimeout, m_retryCount);
    if (!m_protocol->initialize(protocolConfig.c_str())) {
        std::cerr << "[CANPileController] Failed to initialize protocol\n";
        return false;
    }
    
    // BY ZF: 设置协议层的发送回调函数（用于协议层直接调用通讯层发送）
    // 使用静态成员函数作为回调包装
    s_currentInstance = this;
    m_protocol->setSendCallback(sendCallbackWrapper);
    
    // BY ZF: 打开CAN设备
    if (!m_canComm->open(m_devicePath, m_baudrate)) {
        std::cerr << "[CANPileController] Failed to open CAN device: " 
                  << m_devicePath << "\n";
        return false;
    }
    
    std::cout << "[CANPileController] Initialized: device=" << m_devicePath 
              << ", baudrate=" << m_baudrate 
              << ", bcu_addr=0x" << std::hex << static_cast<int>(m_bcuAddr)
              << ", cdz_addr=0x" << static_cast<int>(m_cdzAddr)
              << ", gun_no=" << std::dec << static_cast<int>(m_gunNo) << "\n";
    
    // BY ZF: 启动接收线程
    m_running = true;
    m_receiveThread = std::thread(&CANPileController::receiveThread, this);

    // BY ZF: 启动周期任务线程（心跳/版本校验/下发桩参数）
    if (m_taskRunning.load()) {
        m_taskRunning = false;
        if (m_taskThread.joinable()) {
            m_taskThread.join();
        }
    }
    m_taskRunning = true;
    m_lastHeartbeat = std::chrono::steady_clock::now();
    m_lastVersionReq = std::chrono::steady_clock::now();
    m_lastChargeParamReq = std::chrono::steady_clock::now();
    m_taskThread = std::thread(&CANPileController::taskThread, this);
    
    m_initialized = true;
    return true;
}

void CANPileController::cleanup()
{
    if (!m_initialized) {
        return;
    }
    
    // BY ZF: 停止接收线程
    m_running = false;
    if (m_receiveThread.joinable()) {
        m_receiveThread.join();
    }

    // BY ZF: 停止周期任务线程
    m_taskRunning = false;
    if (m_taskThread.joinable()) {
        m_taskThread.join();
    }
    
    // BY ZF: 关闭CAN设备
    if (m_canComm) {
        m_canComm->close();
    }
    
    // BY ZF: 清理协议
    if (m_protocol) {
        m_protocol->cleanup();
    }
    
    m_initialized = false;
    std::cout << "[CANPileController] Cleaned up\n";
}

int CANPileController::startCharge()
{
    if (!m_initialized) {
        return -1;
    }

    if (!m_versionOk.load() || !m_chargeParamOk.load()) {
        std::cerr << "[CANPileController] Start charge blocked: version/charge params not ready\n";
        return -1;
    }
    
    // BY ZF: 编码并发送启动充电命令（协议层内部会直接调用通讯层发送）
    if (m_protocol->encodeStartCharge() != 0) {
        std::cerr << "[CANPileController] Failed to encode and send start charge command\n";
        return -1;
    }
    
    std::cout << "[CANPileController] Start charge command sent\n";
    return 0;
}

int CANPileController::stopCharge()
{
    if (!m_initialized) {
        return -1;
    }
    
    // BY ZF: 编码并发送停止充电命令（协议层内部会直接调用通讯层发送）
    if (m_protocol->encodeStopCharge() != 0) {
        std::cerr << "[CANPileController] Failed to encode and send stop charge command\n";
        return -1;
    }
    
    std::cout << "[CANPileController] Stop charge command sent\n";
    return 0;
}

void CANPileController::taskThread()
{
    using namespace std::chrono;
    const auto heartbeatInterval = milliseconds(1000);
    const auto versionInterval = milliseconds(500);
    const auto chargeParamInterval = milliseconds(500);

    while (m_taskRunning.load()) {
        auto now = steady_clock::now();

        // 心跳
        if (now - m_lastHeartbeat >= heartbeatInterval) {
            heartbeat();
            m_lastHeartbeat = now;
        }

        // 心跳超时检测
        if (getHeartbeatCommStatus() == 0x01) {
            m_versionOk = false;
            m_chargeParamOk = false;
        }

        // 版本校验
        if (!m_versionOk.load()) {
            if (now - m_lastVersionReq >= versionInterval) {
                encodeVersionCheck();
                m_lastVersionReq = now;
            }
            if (isVersionCheckResponseValid()) {
                m_versionOk = true;
            }
        }

        // 下发桩参数
        if (m_versionOk.load() && !m_chargeParamOk.load()) {
            if (now - m_lastChargeParamReq >= chargeParamInterval) {
                if (m_hasPileId) {
                    encodeIssueChargeParams(m_pileId);
                } else {
                    const uint8_t emptyId[7] = {0};
                    encodeIssueChargeParams(emptyId);
                }
                m_lastChargeParamReq = now;
            }
            if (isChargeParamResponseValid()) {
                m_chargeParamOk = true;
            }
        }

        std::this_thread::sleep_for(milliseconds(50));
    }
}

int CANPileController::getStatus(uint8_t gunNo, PileStatus* status)
{
    if (!m_initialized || status == nullptr) {
        return -1;
    }
    
    // BY ZF: 如果缓存的状态有效，直接返回
    if (m_statusValid.load() && m_cachedStatus.gunNo == gunNo) {
        *status = m_cachedStatus;
        return 0;
    }
    
    // BY ZF: 编码并发送读取状态命令（协议层内部会直接发送）
    if (m_protocol->encodeGetStatus(gunNo) != 0) {
        std::cerr << "[CANPileController] Failed to encode and send get status command\n";
        return -1;
    }
    
    // BY ZF: 等待响应（由接收线程处理，这里返回缓存状态）
    // TODO: 后续可改为同步等待响应
    if (m_statusValid.load()) {
        *status = m_cachedStatus;
        return 0;
    }
    
    // BY ZF: 如果无缓存状态，返回空状态
    memset(status, 0, sizeof(PileStatus));
    status->gunNo = gunNo;
    return 0;
}

int CANPileController::setStartChargeData(const TCU2CCU_CmdStartChargeData* cmd)
{
    if (!m_initialized || !m_protocol) {
        return -1;
    }
    return m_protocol->setStartChargeData(cmd);
}

int CANPileController::setStopChargeData(const TCU2CCU_CmdStopChargeData* cmd)
{
    if (!m_initialized || !m_protocol) {
        return -1;
    }
    return m_protocol->setStopChargeData(cmd);
}

int CANPileController::setVersionCheckData(const TCU2CCU_CmdVersionCheckData* cmd)
{
    if (!m_initialized || !m_protocol) {
        return -1;
    }
    return m_protocol->setVersionCheckData(cmd);
}

int CANPileController::setChargeParamData(const TCU2CCU_CmdChargeParamData* cmd)
{
    if (!m_initialized || !m_protocol) {
        return -1;
    }
    return m_protocol->setChargeParamData(cmd);
}

int CANPileController::getStartChargeResponse(TCU2CCU_StartChargeResponseData* out) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getStartChargeResponseData(out);
}

bool CANPileController::isStartChargeResponseValid() const
{
    return m_protocol && m_protocol->isStartChargeResponseValid();
}

int CANPileController::getStopChargeResponse(TCU2CCU_StopChargeResponseData* out) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getStopChargeResponseData(out);
}

bool CANPileController::isStopChargeResponseValid() const
{
    return m_protocol && m_protocol->isStopChargeResponseValid();
}

int CANPileController::getVersionCheckResponse(TCU2CCU_VersionCheckResponseData* out) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getVersionCheckResponseData(out);
}

bool CANPileController::isVersionCheckResponseValid() const
{
    return m_protocol && m_protocol->isVersionCheckResponseValid();
}

int CANPileController::getChargeParamResponse(TCU2CCU_ChargeParamResponseData* out) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getChargeParamResponseData(out);
}

bool CANPileController::isChargeParamResponseValid() const
{
    return m_protocol && m_protocol->isChargeParamResponseValid();
}

int CANPileController::getStartCompleteData(TCU2CCU_StatusStartCompleteData* out) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getStartCompleteData(out);
}

bool CANPileController::isStartCompleteDataValid() const
{
    return m_protocol && m_protocol->isStartCompleteDataValid();
}

int CANPileController::getStopCompleteData(TCU2CCU_StatusStopCompleteData* out) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getStopCompleteData(out);
}

bool CANPileController::isStopCompleteDataValid() const
{
    return m_protocol && m_protocol->isStopCompleteDataValid();
}

int CANPileController::getYC20Data(TCU2CCU_DataYC20* out) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getYC20Data(out);
}

bool CANPileController::isYC20DataValid() const
{
    return m_protocol && m_protocol->isYC20DataValid();
}

int CANPileController::setChargeParams(uint8_t gunNo, uint16_t voltage, uint16_t current)
{
    if (!m_initialized) {
        return -1;
    }
    
    // BY ZF: 编码并发送设置充电参数命令（协议层内部会直接发送）
    if (m_protocol->encodeSetChargeParams(gunNo, voltage, current) != 0) {
        std::cerr << "[CANPileController] Failed to encode and send set charge params command\n";
        return -1;
    }
    
    std::cout << "[CANPileController] Set charge params command sent: gunNo=" 
              << static_cast<int>(gunNo) 
              << ", voltage=" << voltage 
              << ", current=" << current << "\n";
    return 0;
}

int CANPileController::getFaultCode(uint8_t gunNo, uint16_t* faultCode)
{
    if (!m_initialized || faultCode == nullptr) {
        return -1;
    }
    
    // BY ZF: 从缓存状态中获取故障码
    if (m_statusValid.load() && m_cachedStatus.gunNo == gunNo) {
        *faultCode = m_cachedStatus.faultCode;
        return 0;
    }
    
    // BY ZF: 如果无缓存，返回0（无故障）
    *faultCode = 0;
    return 0;
}

int CANPileController::clearFault(uint8_t gunNo)
{
    if (!m_initialized) {
        return -1;
    }
    
    // BY ZF: 编码并发送清除故障命令（协议层内部会直接发送）
    if (m_protocol->encodeClearFault(gunNo) != 0) {
        std::cerr << "[CANPileController] Failed to encode and send clear fault command\n";
        return -1;
    }
    
    std::cout << "[CANPileController] Clear fault command sent: gunNo=" 
              << static_cast<int>(gunNo) << "\n";
    return 0;
}

bool CANPileController::heartbeat()
{
    if (!m_initialized) {
        return false;
    }
    
    // BY ZF: 编码并发送心跳帧（协议层内部会直接发送）
    if (m_protocol->encodeHeartbeat() != 0) {
        return false;
    }
    
    return true;
}

void CANPileController::onStatusChanged(uint8_t gunNo, const PileStatus* status)
{
    // BY ZF: 状态变化回调（由接收线程调用）
    // 当前为占位实现，后续可根据需要完善
    if (status != nullptr) {
        // std::cout << "[CANPileController] Status changed: gunNo=" 
        //           << static_cast<int>(gunNo) << "\n";
    }
}

void CANPileController::receiveThread()
{
    // BY ZF: 接收线程（非阻塞接收，循环处理）
    uint32_t canId;
    uint8_t data[8];
    uint8_t dataLen;
    
    while (m_running.load()) {
        // BY ZF: 非阻塞接收CAN帧
        if (m_canComm->receive(canId, data, dataLen) == 0) {
            // BY ZF: 先按协议解码一帧（更新协议内部状态）
            int decodeRet = m_protocol->decodeFrame(canId, data, dataLen);
            if (decodeRet == 0) {
                // BY ZF: 解码完成，从协议层汇总状态并更新缓存
                PileStatus status;
                if (m_protocol->getAggregatedStatus(&status) == 0) {
                    m_cachedStatus = status;
                    m_statusValid = true;
                    onStatusChanged(status.gunNo, &status);
                }
            }
            // decodeRet < 0：解码失败或非本协议帧，忽略
        } else {
            // BY ZF: 无数据时短休眠，便于快速收齐启动完成多帧并在超时内回复表13 应答
            usleep(1000);  // 1ms
        }
    }
}

int CANPileController::getYX22Data(TCU2CCU_DataYX22* outData) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getYX22Data(outData);
}

bool CANPileController::isYX22DataValid() const
{
    return m_protocol && m_protocol->isYX22DataValid();
}

int CANPileController::getYX23Data(TCU2CCU_DataYX23* outData) const
{
    if (!m_protocol) {
        return -1;
    }
    return m_protocol->getYX23Data(outData);
}

bool CANPileController::isYX23DataValid() const
{
    return m_protocol && m_protocol->isYX23DataValid();
}

uint8_t CANPileController::getHeartbeatCommStatus() const
{
    return m_protocol ? m_protocol->getHeartbeatCommStatus() : 0x01;  // 无协议时视为超时
}

int CANPileController::encodeVersionCheck()
{
    return m_protocol ? m_protocol->encodeVersionCheck() : -1;
}

int CANPileController::encodeIssueChargeParams(const uint8_t pileId[7])
{
    return m_protocol ? m_protocol->encodeIssueChargeParams(pileId) : -1;
}

int CANPileController::sendCallbackWrapper(uint32_t canId, const uint8_t* data, uint8_t dataLen)
{
    // BY ZF: 发送回调包装函数（静态成员函数，用于协议层调用通讯层发送）
    if (s_currentInstance && s_currentInstance->m_canComm) {
        return s_currentInstance->m_canComm->send(canId, data, dataLen);
    }
    return -1;
}

std::string CANPileController::buildProtocolConfig(uint8_t bcuAddr, uint8_t cdzAddr, uint8_t gunNo, 
                                                    int packetTimeout, int retryCount)
{
    // BY ZF: 构建配置字符串（用于协议初始化）
    std::ostringstream oss;
    oss << "bcu_addr=0x" << std::hex << static_cast<int>(bcuAddr) << std::dec
        << ",cdz_addr=0x" << std::hex << static_cast<int>(cdzAddr) << std::dec
        << ",gun_no=" << static_cast<int>(gunNo)
        << ",packet_timeout=" << packetTimeout
        << ",retry_count=" << retryCount;
    return oss.str();
}
