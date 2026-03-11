/**
 * CAN2CCU 规约实现
 * BY ZF
 * 
 * 数据管理方式：
 * - 协议层维护基于 tcu2ccu_data.h 的成员变量
 * - 编码时从成员变量构建CAN帧
 * - 解码时将CAN帧数据填充到成员变量
 * - 业务层通过getter/setter访问这些数据
 */

#include "can2ccu_protocol.h"
#include <cstring>
#include <iostream>
#include <sstream>
#include <ctime>
#include <algorithm>

CAN2CCUProtocol::CAN2CCUProtocol()
    : m_bcuAddr(0x8A)          // BY ZF: 默认BCU地址0x8A
    , m_cdzAddr(0)
    , m_gunNo(0)
    , m_packetTimeout(5)
    , m_retryCount(3)
    , m_initialized(false)
    , m_sequence(0)
    , m_heartbeatCount(0)
    , m_ccuHeartbeatRxStatus(0)
    , m_lastCcuHeartbeatTime(0)  // 0=未收到过 CCU 心跳应答
    // BY ZF: 初始化命令数据有效性标志（按声明顺序）
    , m_cmdStartChargeDataValid(false)
    , m_startChargeResponseDataValid(false)
    , m_cmdStopChargeDataValid(false)
    , m_stopChargeResponseDataValid(false)
    , m_cmdVersionCheckDataValid(false)
    , m_versionCheckResponseDataValid(false)
    , m_cmdChargeParamDataValid(false)
    , m_chargeParamResponseDataValid(false)
    // BY ZF: 初始化状态数据有效性标志
    , m_startCompleteDataValid(false)
    , m_stopCompleteDataValid(false)
    , m_pileStateDataValid(false)
    , m_vehicleIdDataValid(false)
    // BY ZF: 初始化遥测数据有效性标志（YC20/YC21）
    , m_yc20DataValid(false)
    , m_yc21DataValid(false)
    // BY ZF: 初始化遥信数据有效性标志（YX22/YX23）
    , m_yx22DataValid(false)
    , m_yx23DataValid(false)
    , m_sendCallback(nullptr)  // BY ZF: 发送回调函数初始化为nullptr
{
    // BY ZF: 初始化所有数据成员变量
    memset(&m_cmdStartChargeData, 0, sizeof(TCU2CCU_CmdStartChargeData));
    memset(&m_startChargeResponseData, 0, sizeof(TCU2CCU_StartChargeResponseData));
    memset(&m_cmdStopChargeData, 0, sizeof(TCU2CCU_CmdStopChargeData));
    memset(&m_stopChargeResponseData, 0, sizeof(TCU2CCU_StopChargeResponseData));
    memset(&m_cmdVersionCheckData, 0, sizeof(TCU2CCU_CmdVersionCheckData));
    memset(&m_versionCheckResponseData, 0, sizeof(TCU2CCU_VersionCheckResponseData));
    memset(&m_cmdChargeParamData, 0, sizeof(TCU2CCU_CmdChargeParamData));
    memset(&m_chargeParamResponseData, 0, sizeof(TCU2CCU_ChargeParamResponseData));
    memset(&m_statusStartCompleteData, 0, sizeof(TCU2CCU_StatusStartCompleteData));
    memset(&m_statusStopCompleteData, 0, sizeof(TCU2CCU_StatusStopCompleteData));
    memset(&m_statusPileStateData, 0, sizeof(TCU2CCU_StatusPileStateData));
    memset(&m_statusVehicleIdData, 0, sizeof(TCU2CCU_StatusVehicleIdData));
    memset(&m_yc20Data, 0, sizeof(TCU2CCU_DataYC20));
    memset(&m_yc21Data, 0, sizeof(TCU2CCU_DataYC21));
    memset(&m_yx22Data, 0, sizeof(TCU2CCU_DataYX22));
    memset(&m_yx23Data, 0, sizeof(TCU2CCU_DataYX23));
    
    // BY ZF: 初始化多帧接收上下文
    for (int i = 0; i < 16; i++) {
        m_longFrameContexts[i].isValid = false;
        m_longFrameContexts[i].isComplete = false;
        m_longFrameContexts[i].totalFrames = 0;
        m_longFrameContexts[i].currentFrame = 0;
        m_longFrameContexts[i].dataLength = 0;
        m_longFrameContexts[i].lastFrameTime = 0;
        memset(m_longFrameContexts[i].buffer, 0, sizeof(m_longFrameContexts[i].buffer));
    }
}

CAN2CCUProtocol::~CAN2CCUProtocol()
{
    cleanup();
}

bool CAN2CCUProtocol::initialize(const char* config)
{
    // BY ZF: 解析配置参数
    // 格式：bcu_addr=0x8A,cdz_addr=0xE0,gun_no=0,packet_timeout=5,retry_count=3
    if (config == nullptr || strlen(config) == 0) {
        std::cerr << "[CAN2CCU] Invalid config parameter\n";
        return false;
    }
    
    // BY ZF: 解析配置字符串
    std::string configStr(config);
    std::istringstream iss(configStr);
    std::string token;
    
    while (std::getline(iss, token, ',')) {
        size_t pos = token.find('=');
        if (pos == std::string::npos) continue;
        
        std::string key = token.substr(0, pos);
        std::string value = token.substr(pos + 1);
        
        if (key == "bcu_addr") {
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
        }
    }
    
    // BY ZF: 验证必需参数
    if (m_cdzAddr == 0) {
        std::cerr << "[CAN2CCU] Invalid config: cdz_addr is required\n";
        return false;
    }
    
    m_initialized = true;
    
    return true;
}

void CAN2CCUProtocol::setSendCallback(SendCallback callback)
{
    // BY ZF: 设置发送回调函数
    m_sendCallback = callback;
}

void CAN2CCUProtocol::cleanup()
{
    // BY ZF: 清理资源
    m_initialized = false;
    m_bcuAddr = 0x8A;
    m_cdzAddr = 0;
    m_gunNo = 0;
    m_sequence = 0;
    m_heartbeatCount = 0;
    m_ccuHeartbeatRxStatus = 0;
    m_lastCcuHeartbeatTime = 0;
    m_sendCallback = nullptr;  // BY ZF: 清除发送回调
    
    // BY ZF: 清除数据有效性标志
    m_cmdStartChargeDataValid = false;
    m_startChargeResponseDataValid = false;
    m_cmdStopChargeDataValid = false;
    m_stopChargeResponseDataValid = false;
    m_cmdVersionCheckDataValid = false;
    m_versionCheckResponseDataValid = false;
    m_cmdChargeParamDataValid = false;
    m_chargeParamResponseDataValid = false;
    m_startCompleteDataValid = false;
    m_stopCompleteDataValid = false;
    m_pileStateDataValid = false;
    m_vehicleIdDataValid = false;
    m_yc20DataValid = false;
    m_yc21DataValid = false;
    m_yx22DataValid = false;
    m_yx23DataValid = false;
}

// ========== CAN ID 计算和解析 ==========

uint32_t CAN2CCUProtocol::calculateCANId(uint8_t priority, uint8_t pgn, uint8_t srcAddr, uint8_t dstAddr)
{
    // BY ZF: CAN ID格式：优先级(8bit) + PGN(8bit) + 目标地址(8bit) + 源地址(8bit)
    // 示例：0x1001E08A = 优先级(0x10) + PGN(0x01) + 目标地址(0xE0) + 源地址(0x8A)
    // 位分配：优先级[31:24] + PGN[23:16] + 目标地址[15:8] + 源地址[7:0]
    uint32_t canId = ((priority & 0xFF) << 24) | ((pgn & 0xFF) << 16) | ((dstAddr & 0xFF) << 8) | (srcAddr & 0xFF);
    return canId;
}

void CAN2CCUProtocol::parseCANId(uint32_t canId, uint8_t* outPriority, uint8_t* outPgn, 
                                 uint8_t* outSrcAddr, uint8_t* outDstAddr)
{
    // BY ZF: CAN ID格式：优先级(8bit) + PGN(8bit) + 目标地址(8bit) + 源地址(8bit)
    // 示例：0x1001E08A = 优先级(0x10) + PGN(0x01) + 目标地址(0xE0) + 源地址(0x8A)
    // 位分配：优先级[31:24] + PGN[23:16] + 目标地址[15:8] + 源地址[7:0]
    if (outPriority) {
        *outPriority = static_cast<uint8_t>((canId >> 24) & 0xFF);
    }
    if (outPgn) {
        *outPgn = static_cast<uint8_t>((canId >> 16) & 0xFF);  // BY ZF: PGN是1字节
    }
    if (outDstAddr) {
        *outDstAddr = static_cast<uint8_t>((canId >> 8) & 0xFF);  // BY ZF: 目标地址在[15:8]位
    }
    if (outSrcAddr) {
        *outSrcAddr = static_cast<uint8_t>(canId & 0xFF);  // BY ZF: 源地址在[7:0]位
    }
}


// ========== 数据访问接口（Getter/Setter）==========

int CAN2CCUProtocol::setStartChargeData(const TCU2CCU_CmdStartChargeData* cmdData)
{
    if (cmdData == nullptr) {
        return -1;
    }
    m_cmdStartChargeData = *cmdData;
    m_cmdStartChargeDataValid = true;
    return 0;
}

int CAN2CCUProtocol::getStartChargeData(TCU2CCU_CmdStartChargeData* outCmdData) const
{
    if (outCmdData == nullptr || !m_cmdStartChargeDataValid) {
        return -1;
    }
    *outCmdData = m_cmdStartChargeData;
    return 0;
}

int CAN2CCUProtocol::getStartChargeResponseData(TCU2CCU_StartChargeResponseData* outData) const
{
    if (outData == nullptr || !m_startChargeResponseDataValid) {
        return -1;
    }
    *outData = m_startChargeResponseData;
    return 0;
}

int CAN2CCUProtocol::setStopChargeData(const TCU2CCU_CmdStopChargeData* cmdData)
{
    if (cmdData == nullptr) {
        return -1;
    }
    m_cmdStopChargeData = *cmdData;
    m_cmdStopChargeDataValid = true;
    return 0;
}

int CAN2CCUProtocol::setVersionCheckData(const TCU2CCU_CmdVersionCheckData* cmdData)
{
    if (cmdData == nullptr) {
        return -1;
    }
    m_cmdVersionCheckData = *cmdData;
    m_cmdVersionCheckDataValid = true;
    return 0;
}

int CAN2CCUProtocol::setChargeParamData(const TCU2CCU_CmdChargeParamData* cmdData)
{
    if (cmdData == nullptr) {
        return -1;
    }
    m_cmdChargeParamData = *cmdData;
    m_cmdChargeParamDataValid = true;
    return 0;
}

int CAN2CCUProtocol::getStopChargeData(TCU2CCU_CmdStopChargeData* outCmdData) const
{
    if (outCmdData == nullptr || !m_cmdStopChargeDataValid) {
        return -1;
    }
    *outCmdData = m_cmdStopChargeData;
    return 0;
}

int CAN2CCUProtocol::getStartCompleteData(TCU2CCU_StatusStartCompleteData* outStatusData) const
{
    if (outStatusData == nullptr || !m_startCompleteDataValid) {
        return -1;
    }
    *outStatusData = m_statusStartCompleteData;
    return 0;
}

int CAN2CCUProtocol::getStopCompleteData(TCU2CCU_StatusStopCompleteData* outStatusData) const
{
    if (outStatusData == nullptr || !m_stopCompleteDataValid) {
        return -1;
    }
    *outStatusData = m_statusStopCompleteData;
    return 0;
}

int CAN2CCUProtocol::getPileStateData(TCU2CCU_StatusPileStateData* outStatusData) const
{
    if (outStatusData == nullptr || !m_pileStateDataValid) {
        return -1;
    }
    *outStatusData = m_statusPileStateData;
    return 0;
}

int CAN2CCUProtocol::getVehicleIdData(TCU2CCU_StatusVehicleIdData* outStatusData) const
{
    if (outStatusData == nullptr || !m_vehicleIdDataValid) {
        return -1;
    }
    *outStatusData = m_statusVehicleIdData;
    return 0;
}

int CAN2CCUProtocol::getYC20Data(TCU2CCU_DataYC20* outData) const
{
    if (outData == nullptr || !m_yc20DataValid) {
        return -1;
    }
    *outData = m_yc20Data;
    return 0;
}

int CAN2CCUProtocol::getYC21Data(TCU2CCU_DataYC21* outData) const
{
    if (outData == nullptr || !m_yc21DataValid) {
        return -1;
    }
    *outData = m_yc21Data;
    return 0;
}

int CAN2CCUProtocol::getYX22Data(TCU2CCU_DataYX22* outData) const
{
    if (outData == nullptr || !m_yx22DataValid) {
        return -1;
    }
    *outData = m_yx22Data;
    return 0;
}

int CAN2CCUProtocol::getYX23Data(TCU2CCU_DataYX23* outData) const
{
    if (outData == nullptr || !m_yx23DataValid) {
        return -1;
    }
    *outData = m_yx23Data;
    return 0;
}

// ========== 编码方法：业务数据 -> CAN帧 ==========

int CAN2CCUProtocol::encodeStartCharge()
{
    // BY ZF: 编码并发送启动充电命令
    // 转换为TCU2CCU数据结构并设置到成员变量，然后直接发送
    if (!m_cmdStartChargeDataValid) {
        // BY ZF: 未设置时使用默认值
        m_cmdStartChargeData.loadControlSwitch = 0x02;  // 默认关闭功率分配
        m_cmdStartChargeData.plugAndChargeFlag = 0x01;  // 默认非即插即充
        m_cmdStartChargeData.auxPowerVoltage   = 0x0C;  // 默认12V
        m_cmdStartChargeData.mergeChargeFlag   = 0x00;  // 默认非并充
        m_cmdStartChargeData.v2g               = 0x00;  // 默认充电模式
        m_cmdStartChargeDataValid = true;
    }
    
    // BY ZF: 调用内部编码方法（编码完成后直接发送）
    return encodeStartChargeFrame();
}

int CAN2CCUProtocol::encodeStartChargeFrame()
{
    // BY ZF: 按表10 充电启动帧编码并发送
    if (!m_cmdStartChargeDataValid) {
        return -1;
    }
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    // 表10：字节1=充电接口标识（由 sendSingleFrame 填 canFrame[0]=m_gunNo）
    //       字节2=负荷控制开关 01H 启用 02H 关闭
    //       字节3=即插即充标识 01H 非即插即充 02H 即插即充 05H/06H 双枪
    //       字节4=辅助电源电压 0CH 12V 18H 24V
    // BY ZF: 业务约定扩展：CAN 数据第8字节（canFrame[7]）01=充电, 02=放电。
    uint8_t businessData[7];
    memset(businessData, 0, sizeof(businessData));
    businessData[0] = m_cmdStartChargeData.loadControlSwitch;
    businessData[1] = m_cmdStartChargeData.plugAndChargeFlag;
    businessData[2] = m_cmdStartChargeData.auxPowerVoltage;
    businessData[3] = (m_cmdStartChargeData.mergeChargeFlag!= 0) ? 0x02 : 0x01;
    businessData[6] = (m_cmdStartChargeData.v2g != 0) ? 0x02 : 0x01;
    return sendSingleFrame(0x10, PGN_START_CHARGE, businessData, 7);
}

int CAN2CCUProtocol::encodeStopCharge()
{
    // BY ZF: 编码并发送停止充电命令
    // 转换为TCU2CCU数据结构并设置到成员变量，然后直接发送
    if (!m_cmdStopChargeDataValid) {
        // BY ZF: 按表14 填充停止充电命令；停止原因 01H=计费控制单元正常停止
        m_cmdStopChargeData.stopReason = 0x01;   // 01H 计费控制单元正常停止
        m_cmdStopChargeData.tcuStopCode = 0x00;  // 0 表示无故障代码
        m_cmdStopChargeDataValid = true;
    }
    
    // BY ZF: 调用内部编码方法（编码完成后直接发送）
    return encodeStopChargeFrame();
}

int CAN2CCUProtocol::encodeStopChargeFrame()
{
    // BY ZF: 按表14 充电停止帧编码并发送
    if (!m_cmdStopChargeDataValid) {
        return -1;
    }
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    // 表14：字节1=充电接口标识（sendSingleFrame 填 canFrame[0]=m_gunNo）
    //       字节2=停止充电原因 01H 计费控制单元正常停止 02H 自身故障 03H 充电控制模块故障等
    //       字节3=停止充电故障代码 01H 通讯故障 02H 读卡器 03H 电表 04H ESAM 05H 计量校验和 FFH 其他
    uint8_t businessData[2];
    businessData[0] = m_cmdStopChargeData.stopReason;
    businessData[1] = m_cmdStopChargeData.tcuStopCode;
    return sendSingleFrame(0x10, PGN_STOP_CHARGE, businessData, 2);
}

int CAN2CCUProtocol::encodeStartCompleteAck(uint8_t loadControlSwitch, uint8_t confirmFlag)
{
    // BY ZF: 表13 充电启动完成应答帧（PGN=0x12）
    // 字节1=充电接口标识（由 sendSingleFrame 填 canFrame[0]=枪号）
    // 字节2=负荷控制开关 01H 启用 02H 关闭；字节3=确认标识 00H 成功 01H 失败
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    uint8_t businessData[2];
    businessData[0] = loadControlSwitch;
    businessData[1] = confirmFlag;
    return sendSingleFrame(0x10, PGN_START_CHARGE_ACK, businessData, 2);
}

int CAN2CCUProtocol::encodeStopCompleteAck(uint8_t stopReason, uint8_t confirmFlag)
{
    // BY ZF: 表17 充电完成应答帧（PGN=0x14）
    // 字节1=充电接口标识（由 sendSingleFrame 填 canFrame[0]=枪号）
    // 字节2=停止原因；字节3=确认标识 00H 成功 01H 失败
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    uint8_t businessData[2];
    businessData[0] = stopReason;
    businessData[1] = confirmFlag;
    return sendSingleFrame(0x10, PGN_STOP_CHARGE_ACK, businessData, 2);
}

int CAN2CCUProtocol::encodeVersionCheck()
{
    // BY ZF: 表22 版本校验帧（PGN=0x07）
    // 字节1=充电接口标识（由 sendSingleFrame 填 canFrame[0]=枪号）
    // 字节2-3=计费控制单元通信协议版本号（压缩BCD码）
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    if (!m_cmdVersionCheckDataValid) {
        // BY ZF: 未设置时使用默认版本 V1.21
        m_cmdVersionCheckData.versionMajor = 0x01;
        m_cmdVersionCheckData.versionMinor = 0x21;
        m_cmdVersionCheckDataValid = true;
    }
    
    uint8_t businessData[2];
    businessData[0] = m_cmdVersionCheckData.versionMajor;
    businessData[1] = m_cmdVersionCheckData.versionMinor;
    return sendSingleFrame(0x18, PGN_VERSION_CHECK, businessData, 2);
}

int CAN2CCUProtocol::encodeIssueChargeParams(const uint8_t pileId[7])
{
    // BY ZF: 表24 下发充电参数帧（PGN=0x09）
    // 字节1=充电接口标识（由 sendSingleFrame 填 canFrame[0]=枪号）
    // 字节2-8=充电桩(机)编号（压缩BCD码，7字节）
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    if (pileId != nullptr) {
        memcpy(m_cmdChargeParamData.pileId, pileId, 7);
        m_cmdChargeParamDataValid = true;
    } else if (!m_cmdChargeParamDataValid) {
        std::cerr << "[CAN2CCU] pileId null and no cached charge param data\n";
        return -1;
    }
    uint8_t businessData[7];
    memcpy(businessData, m_cmdChargeParamData.pileId, 7);
    return sendSingleFrame(0x18, PGN_PILE_CONFIG, businessData, 7);
}

int CAN2CCUProtocol::encodeGetStatus(uint8_t gunNo)
{
    // BY ZF: 编码并发送读取状态命令（待后续完善具体协议）
    // 当前为占位实现
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    
    // TODO: 根据协议文档实现具体的编码逻辑
    // 当前先返回成功，待后续完善
    // std::cout << "[CAN2CCU] encodeGetStatus: gunNo=" 
    //           << static_cast<int>(gunNo) << "\n";
    return 0;
}

int CAN2CCUProtocol::encodeClearFault(uint8_t gunNo)
{
    // BY ZF: 编码并发送清除故障命令（待后续完善具体协议）
    // 当前为占位实现
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    
    // TODO: 根据协议文档实现具体的编码逻辑
    // 当前先返回成功，待后续完善
    (void)gunNo;
    return 0;
}

// BY ZF: 心跳接收超时判定阈值（秒），表18 备注
static const int HEARTBEAT_RX_TIMEOUT_SEC = 3;

int CAN2CCUProtocol::encodeHeartbeat()
{
    // BY ZF: 编码并发送心跳帧（PGN=0x40，表18 计费控制单元心跳帧）
    // 字节3=充电控制模块心跳帧接收状态：在心跳解析中更新 m_lastCcuHeartbeatTime，此处据此计算 00H正常/01H超时
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    
    time_t now = time(nullptr);
    if (m_lastCcuHeartbeatTime != 0 && (now - m_lastCcuHeartbeatTime <= HEARTBEAT_RX_TIMEOUT_SEC)) {
        m_ccuHeartbeatRxStatus = 0x00;  // 正常
    } else {
        m_ccuHeartbeatRxStatus = 0x01;  // 接收超时
    }
    
    uint8_t businessData[2];
    businessData[0] = m_heartbeatCount;       // 心跳计数值
    businessData[1] = m_ccuHeartbeatRxStatus; // 充电控制模块心跳帧接收状态
    m_heartbeatCount++;                       // 0~255 循环
    
    return sendSingleFrame(0x18, PGN_HEARTBEAT, businessData, 2);
}

uint8_t CAN2CCUProtocol::getHeartbeatCommStatus() const
{
    time_t now = time(nullptr);
    if (m_lastCcuHeartbeatTime != 0 && (now - m_lastCcuHeartbeatTime <= HEARTBEAT_RX_TIMEOUT_SEC)) {
        return 0x00;  // 正常
    }
    return 0x01;  // 接收超时
}

// ========== 解码方法：CAN帧 -> 业务数据 ==========

int CAN2CCUProtocol::decodeFrame(uint32_t canId, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 通用解码入口，根据CAN ID中的PGN自动识别帧类型
    if (data == nullptr || dataLen < 1) {
        return -1;
    }
    
    // BY ZF: 解析CAN ID获取PGN
    uint8_t priority = 0;
    uint8_t pgn = 0;
    uint8_t srcAddr = 0;
    uint8_t dstAddr = 0;
    parseCANId(canId, &priority, &pgn, &srcAddr, &dstAddr);
    
    // BY ZF: 验证地址（确保是发给我们的）
    if (dstAddr != m_bcuAddr) {
        // BY ZF: 不是发给我们的，忽略
        return -1;
    }
    // BY ZF: 多枪同总线时必须校验源地址，避免跨枪帧混入导致长帧组包错乱。
    if (srcAddr != m_cdzAddr) {
        return -1;
    }
    
    // BY ZF: 根据PGN分发到对应的解码方法
    switch (pgn) {
        case PGN_START_CHARGE_RESP:
            return decodeStartChargeResponse(data, dataLen);
            
        case PGN_STOP_CHARGE_RESP:
            return decodeStopChargeResponse(data, dataLen);
            
        case PGN_START_COMPLETE:
            return decodeStartCompleteFrame(canId, data, dataLen);
            
        case PGN_STOP_COMPLETE:
            return decodeStopCompleteFrame(canId, data, dataLen);
            
        case PGN_TELEMETRY_20:
            return decodeYC20Frame(canId, data, dataLen);
            
        // BY ZF: YC21 由 BCU 发往 CDZ，本端不解码，PGN_TELEMETRY_21 走 default 忽略
            
        case PGN_TELESIGNAL_22:
            return decodeYX22Frame(canId, data, dataLen);
            
        case PGN_TELESIGNAL_23:
            return decodeYX23Frame(canId, data, dataLen);
            
        case PGN_VERSION_RESP:
            return decodeVersionCheckResponse(data, dataLen);
            
        case PGN_PILE_CONFIG_ACK:
            return decodeChargeParamResponse(data, dataLen);
            
        case PGN_HEARTBEAT_RESP:
            // BY ZF: 心跳解析中更新接收时间，供 getHeartbeatCommStatus 与编码表18 字节3 使用
            m_lastCcuHeartbeatTime = time(nullptr);
            return 0;
            
        default:
            // BY ZF: 未知的PGN，忽略
            return -1;
    }
}

int CAN2CCUProtocol::getAggregatedStatus(PileStatus* outStatus)
{
    // BY ZF: 从当前已解码的成员变量汇总为 PileStatus（不依赖 raw 数据）
    if (outStatus == nullptr) {
        return -1;
    }
    memset(outStatus, 0, sizeof(PileStatus));
    outStatus->gunNo = m_gunNo;
    
    // 优先从遥测 YC20 汇总（长帧，含电压电流 SOC 等）
    if (m_yc20DataValid) {
        outStatus->outputVoltage = m_yc20Data.outputVoltage;
        outStatus->outputCurrent = m_yc20Data.outputCurrent;
        outStatus->outputPower = 0;  // YC20 无功率字段，可由 V*I 后续计算
        outStatus->cumulativeEnergy = 0;  // YC20 无累计电量字段
        outStatus->workStatus = 0;  // YC20 无工作状态字段
        outStatus->faultCode = 0;   // YC20 无故障码字段
        outStatus->soc = m_yc20Data.soc;
        outStatus->temperature = m_yc20Data.batteryMaxTemp;  // 用最高温度代表
        return 0;
    }
    // 其次从遥测 YC21 等单帧补充（后续可扩展）
    if (m_yc21DataValid) {
        // TODO: 从 m_yc21Data 填充当前电量、累计时长等
    }
    if (m_yx22DataValid) {
        // TODO: 从 m_yx22Data 填充桩/枪状态等
    }
    return 0;
}

bool CAN2CCUProtocol::decodeHeartbeat(const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码心跳帧（PGN=0x40或0x41）
    // CAN报文结构：CAN ID（4字节）+ 数据（8字节）
    // CAN ID包含：优先级 + PGN + 目标地址 + 源地址
    // PGN已经在CAN ID中，这里只需要验证数据格式
    if (data == nullptr || dataLen < 8) {
        return false;
    }
    
    // BY ZF: 心跳帧数据格式：[0]=端口号（枪号）, [1-7]=业务数据（通常全0）
    // 这里只做基本验证，PGN验证在decodeFrame中已完成
    return true;
}

// ========== 内部解码方法（将CAN帧数据填充到成员变量）==========

int CAN2CCUProtocol::decodeStartChargeResponse(const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码启动充电应答帧（PGN=0x02，短帧，8字节）表11及表11(续)
    // [0]=充电接口标识 [1]=负荷控制开关 [2]=确认标识 [3]=失败原因 [4]=即插即充标识 [5-7]=预留或其它

    if (data[0] != 0 && data[0] != static_cast<uint8_t>(m_gunNo + 1)) {
        return -1;
    }
    m_startChargeResponseData.loadControlSwitch = data[1];   // 01H 启用 02H 关闭
    m_startChargeResponseData.confirmFlag       = data[2];   // 00H 成功 01H 失败
    m_startChargeResponseData.startFailReason   = data[3];   // 失败原因数值，应用层解析
    m_startChargeResponseData.plugAndChargeFlag = data[4];   // 01H 非即插即充 02H 即插即充
    m_startChargeResponseData.auxPowerVoltage  = (dataLen > 6) ? data[5] : 0;
    m_startChargeResponseDataValid = true;
    return 0;
}

int CAN2CCUProtocol::decodeStopChargeResponse(const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码停止充电应答帧（PGN=0x04，短帧，8字节）表15
    // [0]=充电接口标识 [1]=确认标识 00H 成功 01H 失败 [2-7]=未定义

    if (data[0] != 0 && data[0] != static_cast<uint8_t>(m_gunNo + 1)) {
        return -1;
    }
    m_stopChargeResponseData.confirmFlag = data[1];
    m_stopChargeResponseDataValid = true;
    return 0;
}

int CAN2CCUProtocol::decodeVersionCheckResponse(const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码版本校验应答帧（表23，PGN=0x08，短帧）
    // [0]=充电接口标识 [1]=主版本号（BCD） [2]=次版本号（BCD）
    if (data == nullptr || dataLen < 3) {
        return -1;
    }
    if (data[0] != 0 && data[0] != static_cast<uint8_t>(m_gunNo + 1)) {
        return -1;
    }
    m_versionCheckResponseData.versionMajor = data[1];
    m_versionCheckResponseData.versionMinor = data[2];
    m_versionCheckResponseDataValid = true;
    return 0;
}

int CAN2CCUProtocol::getVersionCheckResponseData(TCU2CCU_VersionCheckResponseData* outData) const
{
    if (outData == nullptr || !m_versionCheckResponseDataValid) {
        return -1;
    }
    *outData = m_versionCheckResponseData;
    return 0;
}

int CAN2CCUProtocol::decodeChargeParamResponse(const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码下发充电参数应答帧（表25，PGN=0x0A，短帧）
    // [0]=充电接口标识 [1]=确认标识 00H成功 01H失败 [2]=失败原因
    if (data == nullptr || dataLen < 3) {
        return -1;
    }
    if (data[0] != 0 && data[0] != static_cast<uint8_t>(m_gunNo + 1)) {
        return -1;
    }
    m_chargeParamResponseData.confirmFlag = data[1];
    m_chargeParamResponseData.failReason  = data[2];
    m_chargeParamResponseDataValid = true;
    return 0;
}

int CAN2CCUProtocol::getChargeParamResponseData(TCU2CCU_ChargeParamResponseData* outData) const
{
    if (outData == nullptr || !m_chargeParamResponseDataValid) {
        return -1;
    }
    *outData = m_chargeParamResponseData;
    return 0;
}

int CAN2CCUProtocol::getStopChargeResponseData(TCU2CCU_StopChargeResponseData* outData) const
{
    if (outData == nullptr || !m_stopChargeResponseDataValid) {
        return -1;
    }
    *outData = m_stopChargeResponseData;
    return 0;
}

int CAN2CCUProtocol::decodeStartCompleteFrame(uint32_t canId, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码充电启动完成状态帧（PGN=0x11，长帧，需要多帧传输）
    // 使用多帧处理机制
    LongFrameContext* context = getLongFrameContext(PGN_START_COMPLETE);
    if (context == nullptr) {
        return -1;
    }
    
    int result = processLongFrame(canId, data, dataLen, context);
    if (result == 0) {
        // BY ZF: 多帧接收完成，按表 C.1 解析启动完成状态帧
        const uint8_t* buf = context->buffer;
        if (buf[0] != 0 && buf[0] != static_cast<uint8_t>(m_gunNo + 1)) {
            resetLongFrameContext(PGN_START_COMPLETE);
            return -1;
        }
        // Data3 成功标识、Data4 失败原因（存十六进制数）
        m_statusStartCompleteData.successFlag    = buf[2];
        m_statusStartCompleteData.chargeFailReason = buf[3];
        // Data5、Data8 协议版本号
        memcpy(m_statusStartCompleteData.pileBmsVersion, &buf[4], 3);
        memcpy(m_statusStartCompleteData.bmsPileVersion, &buf[7], 3);
        m_statusStartCompleteData.handshakeResult = buf[10];
        m_statusStartCompleteData.batteryType     = buf[11];
        m_statusStartCompleteData.maxAllowTemp    = buf[12];  // 1℃/位 -50℃ 偏移，存原始值
        // Data14～Data36 电压电流等（0.1V/0.1A，电流 -400A 偏移用 4000-raw）
        m_statusStartCompleteData.bmsMaxChargeVoltage  = static_cast<uint16_t>(buf[13]) | (static_cast<uint16_t>(buf[14]) << 8);
        m_statusStartCompleteData.cellMaxChargeVoltage = (static_cast<uint16_t>(buf[15]) | (static_cast<uint16_t>(buf[16]) << 8)) / 10;
        m_statusStartCompleteData.maxAllowChargeCurrent = 4000U - (static_cast<uint16_t>(buf[17]) | (static_cast<uint16_t>(buf[18]) << 8));
        m_statusStartCompleteData.ratedTotalVoltage    = static_cast<uint16_t>(buf[19]) | (static_cast<uint16_t>(buf[20]) << 8);
        m_statusStartCompleteData.currentTotalVoltage  = static_cast<uint16_t>(buf[21]) | (static_cast<uint16_t>(buf[22]) << 8);
        m_statusStartCompleteData.ratedCapacity        = static_cast<uint16_t>(buf[23]) | (static_cast<uint16_t>(buf[24]) << 8);
        m_statusStartCompleteData.nominalEnergy        = static_cast<uint16_t>(buf[25]) | (static_cast<uint16_t>(buf[26]) << 8);
        m_statusStartCompleteData.soc                  = static_cast<uint16_t>(buf[27]) | (static_cast<uint16_t>(buf[28]) << 8);
        m_statusStartCompleteData.pileMaxOutputVoltage  = static_cast<uint16_t>(buf[29]) | (static_cast<uint16_t>(buf[30]) << 8);
        m_statusStartCompleteData.pileMinOutputVoltage  = static_cast<uint16_t>(buf[31]) | (static_cast<uint16_t>(buf[32]) << 8);
        m_statusStartCompleteData.pileMaxOutputCurrent = 4000U - (static_cast<uint16_t>(buf[33]) | (static_cast<uint16_t>(buf[34]) << 8));
        m_statusStartCompleteData.pileMinOutputCurrent = 4000U - (static_cast<uint16_t>(buf[35]) | (static_cast<uint16_t>(buf[36]) << 8));
        // Data38 VIN、Data55 厂商、Data59 序列号
        memcpy(m_statusStartCompleteData.vin, &buf[37], 17);
        memcpy(m_statusStartCompleteData.batteryManufacturer, &buf[54], 4);
        memcpy(m_statusStartCompleteData.batterySerial, &buf[58], 4);
        m_statusStartCompleteData.batteryProdYear  = buf[62];
        m_statusStartCompleteData.batteryProdMonth = buf[63];
        m_statusStartCompleteData.batteryProdDay   = buf[64];
        memcpy(m_statusStartCompleteData.batteryChargeCount, &buf[65], 3);
        m_statusStartCompleteData.batteryPropertyFlag = buf[68];
        memcpy(m_statusStartCompleteData.bmsSoftwareVersion, &buf[76], 8);
        m_startCompleteDataValid = true;
        resetLongFrameContext(PGN_START_COMPLETE);
        return 0;
    } else if (result == 1) {
        return 1;
    } else {
        resetLongFrameContext(PGN_START_COMPLETE);
        return -1;
    }
}

int CAN2CCUProtocol::decodeStopCompleteFrame(uint32_t canId, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码充电停止完成状态帧（PGN=0x13，长帧，需要多帧传输）
    // 使用多帧处理机制
    LongFrameContext* context = getLongFrameContext(PGN_STOP_COMPLETE);
    if (context == nullptr) {
        return -1;
    }
    
    int result = processLongFrame(canId, data, dataLen, context);
    if (result == 0) {
        // BY ZF: 多帧接收完成，按表 D.1 解析停止完成状态帧
        const uint8_t* buf = context->buffer;
        if (buf[0] != 0 && buf[0] != static_cast<uint8_t>(m_gunNo + 1)) {
            resetLongFrameContext(PGN_STOP_COMPLETE);
            return -1;
        }
        // Data2 停止原因（存十六进制数）、Data3 成功标识
        m_statusStopCompleteData.stopReason     = buf[1];
        m_statusStopCompleteData.stopSuccessFlag = buf[2];
        m_statusStopCompleteData.bmsStopReason   = buf[3];   // Data4 BMS中止充电原因
        m_statusStopCompleteData.bmsChargeFaultReason = static_cast<uint16_t>(buf[4]) | (static_cast<uint16_t>(buf[5]) << 8);  // Data5
        m_statusStopCompleteData.bmsStopErrorReason  = buf[6];   // Data7
        m_statusStopCompleteData.stopSoc         = buf[7];   // Data8 1%/位
        m_statusStopCompleteData.cellMinVoltage  = (static_cast<uint16_t>(buf[8]) | (static_cast<uint16_t>(buf[9]) << 8)) / 10;   // Data9 0.01V→0.1V
        m_statusStopCompleteData.cellMaxVoltage  = (static_cast<uint16_t>(buf[10]) | (static_cast<uint16_t>(buf[11]) << 8)) / 10;   // Data11
        m_statusStopCompleteData.batteryMinTemp = static_cast<int16_t>(buf[12]) - 50;   // Data13 1℃/位 -50℃ 偏移
        m_statusStopCompleteData.batteryMaxTemp = static_cast<int16_t>(buf[13]) - 50;   // Data14
        // Data15～Data22 超时与其它错误位字段
        const uint8_t d15 = buf[14];
        const uint8_t d16 = buf[15];
        const uint8_t d17 = buf[16];
        const uint8_t d18 = buf[17];
        const uint8_t d19 = buf[18];
        const uint8_t d20 = buf[19];
        const uint8_t d21 = buf[20];
        const uint8_t d22 = buf[21];
        m_statusStopCompleteData.timeoutSpn2560_00   = d15 & 3U;
        m_statusStopCompleteData.timeoutSpn2560_AA   = (d15 >> 2) & 3U;
        m_statusStopCompleteData.timeoutTimeSync     = d16 & 3U;
        m_statusStopCompleteData.timeoutChargeReady  = (d16 >> 2) & 3U;
        m_statusStopCompleteData.timeoutChargeStatus = d17 & 3U;
        m_statusStopCompleteData.timeoutChargeStop  = (d17 >> 2) & 3U;
        m_statusStopCompleteData.timeoutChargeStat  = d18 & 3U;
        m_statusStopCompleteData.bmsOtherError      = (d18 >> 2) & 0x3FU;
        m_statusStopCompleteData.timeoutBmsVehicleId = d19 & 3U;
        m_statusStopCompleteData.timeoutBatteryParams = (d19 >> 2) & 3U;
        m_statusStopCompleteData.timeoutBmsReady    = d20 & 3U;
        m_statusStopCompleteData.timeoutBatteryStatus = (d20 >> 2) & 3U;
        m_statusStopCompleteData.timeoutBatteryReq = d21 & 3U;
        m_statusStopCompleteData.timeoutBmsStop     = (d21 >> 2) & 3U;
        m_statusStopCompleteData.timeoutBmsStat    = d22 & 3U;
        m_statusStopCompleteData.pileOtherError    = (d22 >> 2) & 0x3FU;
        m_stopCompleteDataValid = true;
        resetLongFrameContext(PGN_STOP_COMPLETE);
        return 0;
    } else if (result == 1) {
        return 1;
    } else {
        resetLongFrameContext(PGN_STOP_COMPLETE);
        return -1;
    }
}

int CAN2CCUProtocol::decodeYC20Frame(uint32_t canId, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码遥测帧20（YC20，PGN=0x20，长帧，需要多帧传输）
    // 使用多帧处理机制
    LongFrameContext* context = getLongFrameContext(PGN_TELEMETRY_20);
    if (context == nullptr) {
        return -1;
    }
    
    int result = processLongFrame(canId, data, dataLen, context);
    if (result == 0) {
        // BY ZF: 多帧接收完成，按表 B.1 解析完整数据到 m_yc20Data
        const uint8_t* buf = context->buffer;
        const size_t len = static_cast<size_t>(context->dataLength);
        const size_t kYC20PayloadLen = 36;  // Data1～Data36
        if (len < kYC20PayloadLen) {
            resetLongFrameContext(PGN_TELEMETRY_20);
            return -1;
        }
        // 充电接口标识（Data1）：0=一桩一充，1～255=多接口；与 m_gunNo（0-based）一致时接受
        if (buf[0] != 0 && buf[0] != static_cast<uint8_t>(m_gunNo + 1)) {
            resetLongFrameContext(PGN_TELEMETRY_20);
            return -1;
        }
        // 表 B.1 参数 2～13：Data2～Data20（字节 1～19）
        m_yc20Data.outputVoltage   = static_cast<uint16_t>(buf[1]) | (static_cast<uint16_t>(buf[2]) << 8);   // 0.1 V/位
        m_yc20Data.outputCurrent  = 4000U - (static_cast<uint16_t>(buf[3]) | (static_cast<uint16_t>(buf[4]) << 8));   // 0.1 A/位，偏移 -400A，直接提取为 0.1A 值
        m_yc20Data.soc             = buf[5];   // 1%/位，0～100%
        m_yc20Data.batteryMinTemp  = static_cast<int16_t>(buf[6]) - 50;   // 1℃/位，偏移 -50℃
        m_yc20Data.batteryMaxTemp  = static_cast<int16_t>(buf[7]) - 50;
        m_yc20Data.cellMaxVoltage  = (static_cast<uint16_t>(buf[8]) | (static_cast<uint16_t>(buf[9]) << 8)) / 10;   // 0.01 V/位 → 0.1V 单位
        m_yc20Data.cellMinVoltage  = (static_cast<uint16_t>(buf[10]) | (static_cast<uint16_t>(buf[11]) << 8)) / 10;
        m_yc20Data.pileEnvTemp     = static_cast<int16_t>(buf[12]) - 50;  // 充电机环境温度，1℃/位，偏移 -50℃
        m_yc20Data.guideVoltage    = (static_cast<uint16_t>(buf[13]) | (static_cast<uint16_t>(buf[14]) << 8)) / 10;   // 0.01 V/位 → 0.1V
        m_yc20Data.bmsReqVoltage   = static_cast<uint16_t>(buf[15]) | (static_cast<uint16_t>(buf[16]) << 8);   // 0.1 V/位
        m_yc20Data.bmsReqCurrent   = 4000U - (static_cast<uint16_t>(buf[17]) | (static_cast<uint16_t>(buf[18]) << 8));   // 0.1 A/位，偏移 -400A，直接提取
        m_yc20Data.chargeMode      = buf[19];  // 01H 恒压，02H 恒流
        // 表 B.1（续）参数 14～26：Data21～Data36（字节 20～35）
        m_yc20Data.bmsMeasuredVoltage  = static_cast<uint16_t>(buf[20]) | (static_cast<uint16_t>(buf[21]) << 8);   // 0.1 V/位
        m_yc20Data.bmsMeasuredCurrent  = 4000U - (static_cast<uint16_t>(buf[22]) | (static_cast<uint16_t>(buf[23]) << 8));   // 0.1 A/位，偏移 -400A，直接提取
        m_yc20Data.estimatedRemainTime  = static_cast<uint16_t>(buf[24]) | (static_cast<uint16_t>(buf[25]) << 8);   // 1 min/位
        m_yc20Data.interfaceTemp1  = static_cast<int16_t>(buf[26]) - 50;  // 探头1～4，1℃/位，偏移 -50℃
        m_yc20Data.interfaceTemp2  = static_cast<int16_t>(buf[27]) - 50;
        m_yc20Data.interfaceTemp3  = static_cast<int16_t>(buf[28]) - 50;
        m_yc20Data.interfaceTemp4  = static_cast<int16_t>(buf[29]) - 50;
        m_yc20Data.maxVoltageCellNo = buf[30] + 1;   // 1/位，偏移 1，范围 1～256
        m_yc20Data.maxTempPointNo  = buf[31] + 1;   // 1/位，偏移 1，范围 1～128
        m_yc20Data.minTempPointNo  = buf[32] + 1;
        m_yc20Data.inletTemp       = static_cast<int16_t>(buf[33]) - 50;  // 进/出风口温度，1℃/位，偏移 -50℃
        m_yc20Data.outletTemp      = static_cast<int16_t>(buf[34]) - 50;
        m_yc20Data.envHumidity     = buf[35];  // 1%/位，0～100%
        m_yc20DataValid = true;
        // 测试分析：遥测帧(YC20 PGN 0x20)解析结果打印（温度原始值-50=℃）
        resetLongFrameContext(PGN_TELEMETRY_20);
        return 0;
    } else if (result == 1) {
        // BY ZF: 需要继续接收
        return 1;
    } else {
        resetLongFrameContext(PGN_TELEMETRY_20);
        return -1;
    }
}

int CAN2CCUProtocol::decodeYX22Frame(uint32_t canId, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码遥信数据22（YX22，PGN=0x22，可能需要多帧）
    // CAN报文结构：CAN ID（4字节）+ 数据（8字节）
    // CAN ID包含：优先级 + PGN + 目标地址 + 源地址（已通过decodeFrame验证）
    // CAN帧数据格式（8字节）：[0]=端口号（枪号），[1-7]=业务数据；按表 A.1 解析
    if (data == nullptr || dataLen < 8) {
        return -1;
    }
    
    // BY ZF: 验证枪号（Data1=充电接口标识，规约 1-based：0=一桩一充，1~255=多接口编号；协议层 m_gunNo 为 0-based）
    // 故接受 data[0]==0（单枪）或 data[0]==m_gunNo+1（多枪时接口号与枪号对应）
    if (data[0] != 0 && data[0] != static_cast<uint8_t>(m_gunNo + 1)) {
        return -1;
    }
    
    // BY ZF: 表 A.1 直流充电桩遥信帧（PGN8704/YX22）单帧 8 字节：Data1=枪号，Data2~Data8 为位字段
    // 规约 bit1~bit8 为 1-based（bit1=LSB），即 bitN 对应 (byte >> (N-1)) & 1
    const uint8_t data2 = data[1];
    const uint8_t data3 = data[2];
    const uint8_t data4 = data[3];
    const uint8_t data5 = data[4];
    const uint8_t data6 = data[5];
    
    // Data2：工作状态(bit1~2)、总故障(bit3)、总告警(bit4)、急停(bit5)、烟雾(bit6)、交流断路器(bit7)、直流接触器拒动/误动(bit8)
    m_yx22Data.workStatus              = static_cast<uint8_t>((data2 >> 0) & 3U);
    m_yx22Data.totalFault              = static_cast<uint8_t>((data2 >> 2) & 1U);
    m_yx22Data.totalAlarm              = static_cast<uint8_t>((data2 >> 3) & 1U);
    m_yx22Data.emergencyStopFault      = static_cast<uint8_t>((data2 >> 4) & 1U);
    m_yx22Data.smokeFault              = static_cast<uint8_t>((data2 >> 5) & 1U);
    m_yx22Data.acInputBreakerFault     = static_cast<uint8_t>((data2 >> 6) & 1U);
    m_yx22Data.dcBusContactorFault     = static_cast<uint8_t>((data2 >> 7) & 1U);
    
    // Data3：直流熔断器(bit1)、电子锁故障(bit2)、风扇(bit3)、避雷器(bit4)、绝缘监测(bit5)、极性反接(bit6)、导引故障(bit7)、桩过温(bit8)
    m_yx22Data.dcBusFuseFault         = static_cast<uint8_t>((data3 >> 0) & 1U);
    m_yx22Data.electronicLockFault     = static_cast<uint8_t>((data3 >> 1) & 1U);
    m_yx22Data.fanFault               = static_cast<uint8_t>((data3 >> 2) & 1U);
    m_yx22Data.lightningArresterFault  = static_cast<uint8_t>((data3 >> 3) & 1U);
    m_yx22Data.insulationMonitorFault  = static_cast<uint8_t>((data3 >> 4) & 1U);
    m_yx22Data.batteryReverseFault     = static_cast<uint8_t>((data3 >> 5) & 1U);
    m_yx22Data.guideFault             = static_cast<uint8_t>((data3 >> 6) & 1U);
    m_yx22Data.pileOverTempFault       = static_cast<uint8_t>((data3 >> 7) & 1U);
    
    // Data4：接口过温(bit1)、枪未归位告警(bit2)、BMS通信(bit3)、输入过压(bit4)、输入欠压(bit5)、直流过压(bit6)、直流欠压(bit7)、直流过流(bit8)
    m_yx22Data.interfaceOverTempFault  = static_cast<uint8_t>((data4 >> 0) & 1U);
    m_yx22Data.gunNotReturnAlarm       = static_cast<uint8_t>((data4 >> 1) & 1U);
    m_yx22Data.bmsCommFault            = static_cast<uint8_t>((data4 >> 2) & 1U);
    m_yx22Data.inputOverVoltageFault   = static_cast<uint8_t>((data4 >> 3) & 1U);
    m_yx22Data.inputUnderVoltageFault  = static_cast<uint8_t>((data4 >> 4) & 1U);
    m_yx22Data.dcBusOverVoltageFault   = static_cast<uint8_t>((data4 >> 5) & 1U);
    m_yx22Data.dcBusUnderVoltageFault  = static_cast<uint8_t>((data4 >> 6) & 1U);
    m_yx22Data.dcBusOverCurrentFault   = static_cast<uint8_t>((data4 >> 7) & 1U);
    
    // Data5：充电模块故障(bit1)、交流输入故障(bit2)、交流过压(bit3)、交流欠压(bit4)、缺相(bit5)、直流短路(bit6)、直流过流(bit7)、直流过压(bit8)
    m_yx22Data.moduleFault             = static_cast<uint8_t>((data5 >> 0) & 1U);
    m_yx22Data.moduleAcInputFault      = static_cast<uint8_t>((data5 >> 1) & 1U);
    m_yx22Data.moduleAcOverVoltageFault = static_cast<uint8_t>((data5 >> 2) & 1U);
    m_yx22Data.moduleAcUnderVoltageFault= static_cast<uint8_t>((data5 >> 3) & 1U);
    m_yx22Data.moduleAcPhaseLossFault   = static_cast<uint8_t>((data5 >> 4) & 1U);
    m_yx22Data.moduleDcShortFault       = static_cast<uint8_t>((data5 >> 5) & 1U);
    m_yx22Data.moduleDcOverCurrentFault = static_cast<uint8_t>((data5 >> 6) & 1U);
    m_yx22Data.moduleDcOverVoltageFault = static_cast<uint8_t>((data5 >> 7) & 1U);
    
    // Data6：直流欠压(bit1)、模块过温(bit2)、模块通信(bit3)、模块风扇(bit4)、车辆连接(bit5)、枪座状态(bit6)、电子锁状态(bit7)、直流接触器状态(bit8)
    m_yx22Data.moduleDcUnderVoltageFault= static_cast<uint8_t>((data6 >> 0) & 1U);
    m_yx22Data.moduleOverTempFault     = static_cast<uint8_t>((data6 >> 1) & 1U);
    m_yx22Data.moduleCommFault         = static_cast<uint8_t>((data6 >> 2) & 1U);
    m_yx22Data.moduleFanFault          = static_cast<uint8_t>((data6 >> 3) & 1U);
    // BY ZF: 规约 0=已连接 1=未连接，取反后 1=已连接 0=未连接
    m_yx22Data.vehicleConnectStatus    = static_cast<uint8_t>(1U - ((data6 >> 4) & 1U));
    m_yx22Data.gunSeatStatus           = static_cast<uint8_t>(1U - ((data6 >> 5) & 1U));

    m_yx22Data.electronicLockStatus    = static_cast<uint8_t>((data6 >> 6) & 1U);
    m_yx22Data.dcContactorStatus       = static_cast<uint8_t>((data6 >> 7) & 1U);
    
    // Data7～8：其它类型故障（第1字节=告警信息编号，第2字节=告警信息值，0正常/1异常）
    m_yx22Data.otherFault = static_cast<uint16_t>(data[6]) | (static_cast<uint16_t>(data[7]) << 8);
    
    m_yx22DataValid = true;
    return 0;
}

int CAN2CCUProtocol::decodeYX23Frame(uint32_t canId, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 解码遥信数据23（YX23，PGN=0x23，表 A.2 直流充电桩遥信数据 PGN8960）
    // CAN帧数据格式（8字节）：Data1=充电接口标识，Data2~Data8 为位字段（bit1~bit8 为 1-based）
    if (data == nullptr || dataLen < 8) {
        return -1;
    }
    
    // BY ZF: 验证枪号（Data1 规约 1-based：0=单枪，1~255=接口号；协议层 m_gunNo 为 0-based）
    if (data[0] != 0 && data[0] != static_cast<uint8_t>(m_gunNo + 1)) {
        return -1;
    }
    
    const uint8_t data2 = data[1];
    const uint8_t data3 = data[2];
    const uint8_t data4 = data[3];
    const uint8_t data5 = data[4];
    const uint8_t data6 = data[5];
    const uint8_t data7 = data[6];
    const uint8_t data8 = data[7];
    
    // Data2：门禁故障(bit1)、直流接触器粘连(bit2)、绝缘监测告警(bit3)、泄放回路故障(bit4)、桩过温告警(bit5)、枪过温告警(bit6)、交流接触器拒动/误动(bit7)、交流接触器粘连(bit8)
    m_yx23Data.pileDoorFault              = static_cast<uint8_t>((data2 >> 0) & 1U);
    m_yx23Data.dcContactorStickFault      = static_cast<uint8_t>((data2 >> 1) & 1U);
    m_yx23Data.insulationMonitorAlarm     = static_cast<uint8_t>((data2 >> 2) & 1U);
    m_yx23Data.dischargeFault             = static_cast<uint8_t>((data2 >> 3) & 1U);
    m_yx23Data.pileOverTempAlarm          = static_cast<uint8_t>((data2 >> 4) & 1U);
    m_yx23Data.gunOverTempAlarm           = static_cast<uint8_t>((data2 >> 5) & 1U);
    m_yx23Data.acInputContactorFault      = static_cast<uint8_t>((data2 >> 6) & 1U);
    m_yx23Data.acInputContactorStickFault = static_cast<uint8_t>((data2 >> 7) & 1U);
    
    // Data3：辅助电源故障(bit1)、桥接接触器拒动/误动(bit2)、桥接接触器粘连(bit3)、桥接接触器状态(bit4)、模块紧急停机(bit5)、充电桩水浸(bit6)、机柜门禁(bit7)、机柜水浸(bit8)
    m_yx23Data.auxPowerFault              = static_cast<uint8_t>((data3 >> 0) & 1U);
    m_yx23Data.bridgeContactorFault       = static_cast<uint8_t>((data3 >> 1) & 1U);
    m_yx23Data.bridgeContactorStickFault  = static_cast<uint8_t>((data3 >> 2) & 1U);
    m_yx23Data.bridgeContactorStatus      = static_cast<uint8_t>((data3 >> 3) & 1U);
    m_yx23Data.moduleEmergencyStopStatus  = static_cast<uint8_t>((data3 >> 4) & 1U);
    m_yx23Data.pileWaterFault             = static_cast<uint8_t>((data3 >> 5) & 1U);
    m_yx23Data.cabinetDoorFault           = static_cast<uint8_t>((data3 >> 6) & 1U);
    m_yx23Data.cabinetWaterFault          = static_cast<uint8_t>((data3 >> 7) & 1U);
    
    // Data4：机柜烟雾(bit1)、功率控制通信故障(bit2)、功率控制通信告警(bit3)、功率控制故障(bit4)、功率控制告警(bit5)、开关模块通信故障(bit6)、开关模块通信告警(bit7)、开关模块故障(bit8)
    m_yx23Data.cabinetSmokeFault          = static_cast<uint8_t>((data4 >> 0) & 1U);
    m_yx23Data.powerCtrlCommFault         = static_cast<uint8_t>((data4 >> 1) & 1U);
    m_yx23Data.powerCtrlCommAlarm         = static_cast<uint8_t>((data4 >> 2) & 1U);
    m_yx23Data.powerCtrlFault             = static_cast<uint8_t>((data4 >> 3) & 1U);
    m_yx23Data.powerCtrlAlarm             = static_cast<uint8_t>((data4 >> 4) & 1U);
    m_yx23Data.switchModuleCommFault      = static_cast<uint8_t>((data4 >> 5) & 1U);
    m_yx23Data.switchModuleCommAlarm      = static_cast<uint8_t>((data4 >> 6) & 1U);
    m_yx23Data.switchModuleFault          = static_cast<uint8_t>((data4 >> 7) & 1U);
    
    // Data5：开关模块告警(bit1)、BMS故障(bit2)、BMS发送故障(bit3)、充电模块泄放(bit4)、即插即充请求(bit5)、交流接触器状态(bit6)、交流接触器控制(bit7)、K1控制(bit8)
    m_yx23Data.switchModuleAlarm          = static_cast<uint8_t>((data5 >> 0) & 1U);
    m_yx23Data.bmsFaultByCtrl             = static_cast<uint8_t>((data5 >> 1) & 1U);
    m_yx23Data.bmsSendFaultInfo           = static_cast<uint8_t>((data5 >> 2) & 1U);
    m_yx23Data.moduleDischargeFault       = static_cast<uint8_t>((data5 >> 3) & 1U);
    m_yx23Data.vinReq                     = static_cast<uint8_t>((data5 >> 4) & 1U);
    m_yx23Data.acInputContactorStatus     = static_cast<uint8_t>((data5 >> 5) & 1U);
    m_yx23Data.acContactorCtrlStatus      = static_cast<uint8_t>((data5 >> 6) & 1U);
    m_yx23Data.portK1CtrlStatus           = static_cast<uint8_t>((data5 >> 7) & 1U);
    
    // Data6：K2控制(bit1)、预留(bit2~3)、辅助电源控制(bit4)、辅助电源控制2(bit5)、风机控制(bit6)、电子锁控制(bit7)、[bit8 按规约续]
    m_yx23Data.portK2CtrlStatus           = static_cast<uint8_t>((data6 >> 0) & 1U);
    m_yx23Data.reserved1                  = static_cast<uint8_t>((data6 >> 1) & 3U);
    m_yx23Data.portAuxPowerCtrlStatus     = static_cast<uint8_t>((data6 >> 4) & 1U);
    m_yx23Data.portAuxPowerCtrlStatus2    = static_cast<uint8_t>((data6 >> 5) & 1U);
    m_yx23Data.cabinetFanCtrlStatus       = static_cast<uint8_t>((data6 >> 6) & 1U);
    m_yx23Data.portLockCtrlStatus         = static_cast<uint8_t>((data6 >> 7) & 1U);
    
    // Data7：PE断线(bit1)、导引电压异常(bit2)、模块启动失败(bit3)、模块关机失败(bit4)、模块地址异常(bit5)、散热系统故障(bit6)、机柜过温告警(bit7)、机柜过温故障(bit8)
    m_yx23Data.peBreakFault               = static_cast<uint8_t>((data7 >> 0) & 1U);
    m_yx23Data.guideVoltageAbnormal       = static_cast<uint8_t>((data7 >> 1) & 1U);
    m_yx23Data.moduleStartFail            = static_cast<uint8_t>((data7 >> 2) & 1U);
    m_yx23Data.moduleShutdownFail         = static_cast<uint8_t>((data7 >> 3) & 1U);
    m_yx23Data.moduleAddrAbnormal         = static_cast<uint8_t>((data7 >> 4) & 1U);
    m_yx23Data.coolingSystemFault         = static_cast<uint8_t>((data7 >> 5) & 1U);
    m_yx23Data.cabinetOverTempAlarm       = static_cast<uint8_t>((data7 >> 6) & 1U);
    m_yx23Data.cabinetOverTempFault       = static_cast<uint8_t>((data7 >> 7) & 1U);
    
    // Data8：功率分配失败(bit1)、无空闲模块(bit2)、预充调压失败(bit3)、通信超时(bit4)、湿度告警(bit5)、预留(bit6~8)
    m_yx23Data.powerDistStrategyFail      = static_cast<uint8_t>((data8 >> 0) & 1U);
    m_yx23Data.noIdleModuleFault          = static_cast<uint8_t>((data8 >> 1) & 1U);
    m_yx23Data.prechargeVoltageFail       = static_cast<uint8_t>((data8 >> 2) & 1U);
    m_yx23Data.powerCtrlCommTimeout       = static_cast<uint8_t>((data8 >> 3) & 1U);
    m_yx23Data.envHumidityAlarm           = static_cast<uint8_t>((data8 >> 4) & 1U);
    m_yx23Data.reserved2                  = static_cast<uint8_t>((data8 >> 5) & 7U);
    
    m_yx23DataValid = true;
    return 0;
}

// ========== 多帧处理实现 ==========

CAN2CCUProtocol::LongFrameContext* CAN2CCUProtocol::getLongFrameContext(uint8_t pgn)
{
    // BY ZF: 查找或创建多帧接收上下文
    // 查找已存在的上下文
    for (int i = 0; i < 16; i++) {
        if (m_longFrameContexts[i].isValid && m_longFrameContexts[i].pgn == pgn) {
            return &m_longFrameContexts[i];
        }
    }
    
    // BY ZF: 创建新的上下文
    for (int i = 0; i < 16; i++) {
        if (!m_longFrameContexts[i].isValid) {
            m_longFrameContexts[i].pgn = pgn;
            m_longFrameContexts[i].isValid = true;
            m_longFrameContexts[i].isComplete = false;
            m_longFrameContexts[i].totalFrames = 0;
            m_longFrameContexts[i].currentFrame = 0;
            m_longFrameContexts[i].dataLength = 0;
            m_longFrameContexts[i].lastFrameTime = static_cast<uint32_t>(time(nullptr));
            memset(m_longFrameContexts[i].buffer, 0, sizeof(m_longFrameContexts[i].buffer));
            return &m_longFrameContexts[i];
        }
    }
    
    // BY ZF: 上下文已满，返回nullptr
    return nullptr;
}

void CAN2CCUProtocol::resetLongFrameContext(uint8_t pgn)
{
    // BY ZF: 重置指定PGN的多帧接收上下文
    for (int i = 0; i < 16; i++) {
        if (m_longFrameContexts[i].isValid && m_longFrameContexts[i].pgn == pgn) {
            m_longFrameContexts[i].isValid = false;
            m_longFrameContexts[i].isComplete = false;
            m_longFrameContexts[i].totalFrames = 0;
            m_longFrameContexts[i].currentFrame = 0;
            m_longFrameContexts[i].dataLength = 0;
            memset(m_longFrameContexts[i].buffer, 0, sizeof(m_longFrameContexts[i].buffer));
            break;
        }
    }
}

void CAN2CCUProtocol::resetAllLongFrameContexts()
{
    for (int i = 0; i < 16; i++) {
        m_longFrameContexts[i].isValid = false;
        m_longFrameContexts[i].isComplete = false;
        m_longFrameContexts[i].totalFrames = 0;
        m_longFrameContexts[i].currentFrame = 0;
        m_longFrameContexts[i].dataLength = 0;
        m_longFrameContexts[i].lastFrameTime = 0;
        memset(m_longFrameContexts[i].buffer, 0, sizeof(m_longFrameContexts[i].buffer));
    }
}

int CAN2CCUProtocol::processLongFrame(uint32_t canId, const uint8_t* data, size_t dataLen, LongFrameContext* context)
{
    // BY ZF: 处理多帧数据（长帧组装）
    // CAN报文结构：CAN ID（4字节）+ 数据（8字节）
    // CAN ID包含：优先级 + PGN + 目标地址 + 源地址（已通过decodeFrame验证）
    // CAN帧数据格式（8字节）：
    // 第1帧：[0]=帧序号(1), [1]=总帧数, [2]=有效数据长度低字节, [3]=有效数据长度高字节
    //        [4]=有效数据01（枪号）, [5-7]=有效数据02-04（业务数据前3字节）
    // 后续帧：[0]=帧序号, [1-7]=有效数据(7字节)
    // 最后帧：[0]=帧序号(等于总帧数), [1-7]=有效数据+校验和(最后2字节是校验和，低字节在前)
    // 注意：有效数据长度包括枪号（业务数据长度+1），枪号作为有效数据01放在第1帧的[4]位置
    
    if (data == nullptr || dataLen < 8 || context == nullptr) {
        return -1;
    }

    // BY ZF: 多帧超时处理（使用配置 packet_timeout，未配置时回退 2 秒）
    if (context->isValid && context->lastFrameTime != 0) {
        const uint32_t nowSec = static_cast<uint32_t>(time(nullptr));
        const uint32_t timeoutSec = (m_packetTimeout > 0) ? static_cast<uint32_t>(m_packetTimeout) : 2U;
        if (nowSec - context->lastFrameTime > timeoutSec) {
            context->isValid = false;
            context->isComplete = false;
            context->totalFrames = 0;
            context->currentFrame = 0;
            context->dataLength = 0;
            memset(context->buffer, 0, sizeof(context->buffer));
        }
    }
    
    uint8_t frameSeq = data[0];  // 帧序号（所有帧都在data[0]）
    
    // BY ZF: 处理第1帧（帧序号=1）
    if (frameSeq == 1) {
        // BY ZF: 解析第1帧信息
        context->totalFrames = data[1];  // 总帧数
        context->dataLength = data[2] | ((data[3] << 8) & 0xFF00);  // 有效数据长度（低字节在前，包含枪号）
        context->currentFrame = 1;
        context->lastFrameTime = static_cast<uint32_t>(time(nullptr));
        // BY ZF: 第1帧有效数据（枪号+业务数据前3字节）写入 buffer[0..3]，校验和不包含每帧的“当前报文序号”
        memcpy(context->buffer, &data[4], 4);
        // BY ZF: 如果只有1帧，直接完成（需要验证校验和）
        if (context->totalFrames == 1) {
            size_t validDataLen = static_cast<size_t>(context->dataLength);
            size_t checksumOffset = 4 + validDataLen;  // 校验和起始位置
        
            // BY ZF: 只复制有效数据部分（不包括校验和）到buffer
            if (validDataLen > 0 && validDataLen <= 4) {
                memcpy(context->buffer, &data[4], validDataLen);
            } else {
                // BY ZF: 有效数据长度超出单帧范围，错误
                return -1;
            }
            
            // BY ZF: 提取校验和（根据有效数据长度动态计算位置）
            uint16_t receivedChecksum = data[checksumOffset] | (data[checksumOffset + 1] << 8);  // 低字节在前
            
            // BY ZF: 验证校验和
            // 根据协议文档：校验和计算范围 = 总帧数 + 有效数据长度(2字节) + 有效数据（枪号+业务数据）
            uint16_t calculatedChecksum = 0;
            calculatedChecksum += context->totalFrames;  // 报文总帧数
            calculatedChecksum += static_cast<uint8_t>(context->dataLength & 0xFF);        // 有效数据长度低字节
            calculatedChecksum += static_cast<uint8_t>((context->dataLength >> 8) & 0xFF);  // 有效数据长度高字节
            for (size_t i = 0; i < static_cast<size_t>(context->dataLength); i++) {  // 有效数据（枪号 + 业务数据）
                calculatedChecksum += context->buffer[i];
            }
            
            if (calculatedChecksum != receivedChecksum) {
                return -1;
            }
            
            context->isComplete = true;
            return 0;
        }
        
        return 1;  // 需要继续接收
    }
    
    // BY ZF: 处理后续帧（帧序号>=2）
    if (frameSeq > 1 && frameSeq <= context->totalFrames) {
        if (frameSeq != context->currentFrame + 1) {
            return -1;
        }
        
        // BY ZF: 计算已接收的有效数据长度
        // 第1帧：4字节有效数据（枪号1字节 + 业务数据3字节，即data[4-7]）
        // 后续帧：每帧7字节有效数据（data[1-7]）
        size_t receivedLen = 4 + ((static_cast<size_t>(context->currentFrame) - 1) * 7);  // 第1帧4字节，后续每帧7字节
        size_t remainingLen = static_cast<size_t>(context->dataLength) - receivedLen;
        
        // BY ZF: 检查是否接收完成（最后一帧）
        if (frameSeq == context->totalFrames) {
            // BY ZF: 最后一帧，包含有效数据 + 校验和（最后2字节）
            // 需要分离有效数据和校验和
            // data[1-7]中：前N字节是有效数据，最后2字节是校验和
            // 剩余有效数据长度 = context->dataLength - receivedLen
            size_t remainingValidData = remainingLen;  // 剩余的有效数据长度（不包含校验和）
            
            // BY ZF: 复制剩余的有效数据（不包括校验和）
            // data[1]到data[1+remainingValidData-1]是有效数据
            // data[1+remainingValidData]到data[7]是校验和（2字节）
            if (remainingValidData > 0 && remainingValidData <= 7) {
                memcpy(&context->buffer[receivedLen], &data[1], remainingValidData);
            }
            
            // BY ZF: 提取校验和（最后2字节，在data[1+remainingValidData]和data[1+remainingValidData+1]）
            // 注意：最后一帧的data[1-7]中，最后2字节是校验和
            // 如果remainingValidData <= 5，则校验和在data[6-7]
            // 如果remainingValidData > 5，则校验和位置需要重新计算
            uint16_t receivedChecksum = 0;
            size_t checksumOffset = 1 + remainingValidData;  // 校验和起始位置
            if (checksumOffset + 1 <= 7) {  // 确保校验和位置在data[1-7]范围内
                receivedChecksum = data[checksumOffset] | (data[checksumOffset + 1] << 8);  // 低字节在前
            } else {
                // BY ZF: 校验和位置超出范围，错误
                return -1;
            }
            
            context->currentFrame = frameSeq;
            context->lastFrameTime = static_cast<uint32_t>(time(nullptr));
            
            // BY ZF: 验证校验和
            // 根据协议文档：校验和计算范围 = 总帧数 + 有效数据长度(2字节) + 有效数据（枪号+业务数据）
            uint16_t calculatedChecksum = 0;
            calculatedChecksum += context->totalFrames;  // 报文总帧数
            calculatedChecksum += static_cast<uint8_t>(context->dataLength & 0xFF);        // 有效数据长度低字节
            calculatedChecksum += static_cast<uint8_t>((context->dataLength >> 8) & 0xFF);  // 有效数据长度高字节
            for (size_t i = 0; i < static_cast<size_t>(context->dataLength); i++) {  // 有效数据（枪号 + 业务数据）
                calculatedChecksum += context->buffer[i];
            }
            
            if (calculatedChecksum != receivedChecksum) {
                return -1;
            }
            
            context->isComplete = true;
            return 0;
        }
        
        // BY ZF: 非最后一帧，复制有效数据（从data[1]开始，固定7字节）
        memcpy(&context->buffer[receivedLen], &data[1], 7);
        context->currentFrame = frameSeq;
        context->lastFrameTime = static_cast<uint32_t>(time(nullptr));
        
        return 1;  // 需要继续接收
    }
    
    // BY ZF: 帧序号错误
    return -1;
}

uint16_t CAN2CCUProtocol::calculateChecksum(const uint8_t* data, size_t len)
{
    // BY ZF: 计算校验和（16位累加和）
    // 计算范围：从总帧数字节到信息体最后一个字节（不包括校验和本身）
    // 校验和位置：长帧最后两字节（低字节在前）
    
    if (data == nullptr || len < 2) {
        return 0;
    }
    
    uint16_t checksum = 0;
    // BY ZF: 计算范围不包括最后2字节（校验和本身）
    for (size_t i = 0; i < len - 2; i++) {
        checksum += data[i];
    }
    
    return checksum;
}

bool CAN2CCUProtocol::verifyChecksum(const uint8_t* data, size_t len)
{
    // BY ZF: 验证校验和
    // 校验和位置：最后两字节（低字节在前）
    
    if (data == nullptr || len < 2) {
        return false;
    }
    
    uint16_t calculatedChecksum = calculateChecksum(data, len);
    uint16_t receivedChecksum = data[len - 2] | (data[len - 1] << 8);  // 低字节在前
    
    return (calculatedChecksum == receivedChecksum);
}

// ========== 单帧/多帧发送器实现 ==========

int CAN2CCUProtocol::sendSingleFrame(uint8_t priority, uint8_t pgn, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 发送单帧数据（短帧，业务数据<=7字节）
    // CAN报文结构：CAN ID（4字节）+ 数据（8字节）
    // CAN ID包含：优先级 + PGN + 目标地址 + 源地址
    // CAN帧数据格式（8字节）：
    // [0] = 端口号（枪号）
    // [1-7] = 业务数据（最多7字节）
    
    if (data == nullptr || dataLen == 0 || dataLen > 7) {
        std::cerr << "[CAN2CCU] Invalid data length for single frame: " << dataLen << "\n";
        return -1;
    }
    
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    
    // BY ZF: 构建CAN帧数据（8字节）
    // 注意：PGN和地址已经在CAN ID中，数据中不再重复
    uint8_t canFrame[8];
    memset(canFrame, 0, 8);
    canFrame[0] = m_gunNo+1;  // 端口号（枪号）
    memcpy(&canFrame[1], data, dataLen);  // 业务数据
    
    // BY ZF: 计算CAN ID（包含优先级、PGN、目标地址、源地址）
    uint32_t canId = calculateCANId(priority, pgn, m_bcuAddr, m_cdzAddr);
    
    // BY ZF: 调用发送回调函数发送
    return m_sendCallback(canId, canFrame, 8);
}

int CAN2CCUProtocol::sendMultiFrame(uint8_t priority, uint8_t pgn, const uint8_t* data, size_t dataLen)
{
    // BY ZF: 发送多帧数据（长帧，业务数据>7字节）
    // CAN报文结构：CAN ID（4字节）+ 数据（8字节）
    // CAN ID包含：优先级 + PGN + 目标地址 + 源地址
    // CAN帧数据格式（8字节）：
    // 第1帧：[0]=帧序号(1), [1]=总帧数, [2]=有效数据长度低字节, [3]=有效数据长度高字节
    //        [4]=有效数据01（枪号）, [5-7]=有效数据02-04（业务数据前3字节）
    // 后续帧：[0]=帧序号, [1-7]=有效数据(7字节)
    // 最后帧：[0]=帧序号(等于总帧数), [1-7]=有效数据+校验和(最后2字节是校验和，低字节在前)
    // 注意：有效数据长度包括枪号（业务数据长度+1），枪号作为有效数据01放在第1帧的[4]位置
    
    if (data == nullptr || dataLen == 0) {
        std::cerr << "[CAN2CCU] Invalid data for multi-frame\n";
        return -1;
    }
    
    if (m_sendCallback == nullptr) {
        std::cerr << "[CAN2CCU] Send callback not set\n";
        return -1;
    }
    
    // BY ZF: 校验和计算将在构建完整数据后，基于协议文档要求计算
    // 校验和计算范围：从"报文总帧数"到"有效数据N"（不包括"当前报文序号"和校验和本身）
    
    // BY ZF: 构建完整数据（包含校验和）
    // 注意：有效数据长度包括枪号（+1），所以总数据长度 = 业务数据 + 枪号 + 校验和
    size_t totalDataLen = dataLen + 1 + 2;  // 业务数据 + 枪号(1字节) + 校验和(2字节)
    uint8_t* fullData = new uint8_t[totalDataLen];
    fullData[0] = m_gunNo+1;  // 有效数据01：枪号
    memcpy(&fullData[1], data, dataLen);  // 有效数据02-N：业务数据
    // BY ZF: 计算校验和（从"报文总帧数"到"有效数据N"，不包括"当前报文序号"和校验和本身）
    // 校验和计算范围：总帧数 + 有效数据长度(2字节) + 枪号 + 业务数据
    uint16_t checksum = 0;
    // BY ZF: 先计算总帧数（用于校验和计算）
    // 第1帧：4字节有效数据（枪号1字节 + 业务数据3字节）
    // 后续帧：每帧7字节有效数据
    // 剩余需要传输：剩余有效数据 + 2字节校验和 = (validDataLen - 4) + 2 = validDataLen - 2
    // 后续帧数（包括最后一帧）= ceil((validDataLen - 2) / 7)
    // 总帧数 = 1 + ceil((validDataLen - 2) / 7)
    size_t validDataLen = dataLen + 1;  // 有效数据长度（包含枪号，不包含校验和）
    uint8_t totalFrames = static_cast<uint8_t>(((validDataLen - 2 + 6) / 7) + 1);  // 向上取整：ceil((validDataLen - 2) / 7) + 1
    
    // BY ZF: 计算校验和（从"报文总帧数"到"有效数据N"，不包括"当前报文序号"）
    checksum += totalFrames;  // 报文总帧数
    checksum += static_cast<uint8_t>(validDataLen & 0xFF);        // 有效数据长度低字节
    checksum += static_cast<uint8_t>((validDataLen >> 8) & 0xFF);  // 有效数据长度高字节
    for (size_t i = 0; i < validDataLen; i++) {  // 有效数据（枪号 + 业务数据）
        checksum += fullData[i];
    }
    
    fullData[validDataLen] = static_cast<uint8_t>(checksum & 0xFF);        // 校验和低字节
    fullData[validDataLen + 1] = static_cast<uint8_t>((checksum >> 8) & 0xFF);  // 校验和高字节
    
    // BY ZF: 计算CAN ID（包含优先级、PGN、目标地址、源地址）
    uint32_t canId = calculateCANId(priority, pgn, m_bcuAddr, m_cdzAddr);
    
    // BY ZF: 发送第1帧
    // 根据协议文档：第1帧格式
    // [0]=帧序号(1), [1]=总帧数, [2]=有效数据长度低字节, [3]=有效数据长度高字节
    // [4]=有效数据01（枪号）, [5-7]=有效数据02-04（业务数据前3字节）
    uint8_t frame1[8];
    memset(frame1, 0, 8);
    frame1[0] = 1;  // 帧序号（从1开始）
    frame1[1] = totalFrames;  // 总帧数
    frame1[2] = static_cast<uint8_t>(validDataLen & 0xFF);        // 有效数据长度低字节
    frame1[3] = static_cast<uint8_t>((validDataLen >> 8) & 0xFF); // 有效数据长度高字节
    frame1[4] = fullData[0];  // 有效数据01：枪号
    frame1[5] = (validDataLen > 1) ? fullData[1] : 0;  // 有效数据02：业务数据第1字节
    frame1[6] = (validDataLen > 2) ? fullData[2] : 0;  // 有效数据03：业务数据第2字节
    frame1[7] = (validDataLen > 3) ? fullData[3] : 0;  // 有效数据04：业务数据第3字节
    
    if (m_sendCallback(canId, frame1, 8) != 0) {
        delete[] fullData;
        return -1;
    }
    
    // BY ZF: 发送后续帧
    // 根据协议文档：后续帧格式
    // [0]=帧序号, [1-7]=有效数据（7字节）
    for (uint8_t frameSeq = 2; frameSeq <= totalFrames; frameSeq++) {
        uint8_t frame[8];
        memset(frame, 0, 8);
        frame[0] = frameSeq;  // 帧序号
        
        // BY ZF: 计算数据偏移
        // 第1帧：4字节有效数据（枪号1字节 + 业务数据3字节，即fullData[0-3]）
        // 后续帧：每帧7字节有效数据
        size_t dataOffset = 4 + ((static_cast<size_t>(frameSeq) - 2) * 7);
        size_t copyLen = std::min(static_cast<size_t>(7), validDataLen - dataOffset);
        memcpy(&frame[1], &fullData[dataOffset], copyLen);  // 从frame[1]开始存放有效数据
        
        if (m_sendCallback(canId, frame, 8) != 0) {
            delete[] fullData;
            return -1;
        }
    }
    
    delete[] fullData;
    return 0;
}
