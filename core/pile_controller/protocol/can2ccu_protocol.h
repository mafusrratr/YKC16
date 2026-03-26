/**
 * CAN2CCU 规约实现
 * 规约层：负责将业务数据转换为CAN帧格式
 * BY ZF
 * 
 * CAN帧格式：
 * - CAN扩展帧（29位ID）
 * - 数据长度：8字节
 * - CAN ID格式：优先级(8bit) + PGN(8bit) + 目标地址(8bit) + 源地址(8bit)
 * 
 * 地址配置：
 * - BCU地址（TCU地址）：可配置，默认0x8A
 * - CDZ地址（CCU地址）：从配置文件读取
 * 
 * 数据管理：
 * - 协议层维护基于 tcu2ccu_data.h 的成员变量
 * - 编码时从成员变量构建CAN帧
 * - 解码时将CAN帧数据填充到成员变量
 * - 业务层通过getter/setter访问这些数据
 * - 这些数据可以用于与其他进程通信（共享内存等）
 */

#ifndef CAN2CCU_PROTOCOL_H
#define CAN2CCU_PROTOCOL_H

#include "tcu2ccu_data.h"
#include "../ipile_controller.h"
#include <string>
#include <cstring>  // BY ZF: 提供 memset 函数
#include <ctime>   // BY ZF: time_t 用于心跳接收时间

// BY ZF: 发送回调函数类型定义
// 用于协议层直接调用通讯层发送方法
// 参数：canId - CAN ID, data - CAN帧数据, dataLen - 数据长度
// 返回：0成功，其他失败
typedef int (*SendCallback)(uint32_t canId, const uint8_t* data, uint8_t dataLen);

/**
 * CAN2CCU 规约类
 * 负责CAN2CCU协议的编码和解码
 * 
 * 数据管理方式：
 * - 维护基于 tcu2ccu_data.h 的成员变量（命令数据、状态数据、遥测数据等）
 * - 编码方法：从成员变量构建CAN帧，编码完成后直接调用通讯层发送
 * - 解码方法：将CAN帧数据填充到成员变量
 * - 业务层通过getter/setter方法访问这些数据
 * 
 * 发送机制：
 * - 编码完成后，协议层内部判断单帧/多帧
 * - 单帧：直接调用发送回调发送
 * - 多帧：分帧后依次调用发送回调发送
 */
class CAN2CCUProtocol {
public:
    CAN2CCUProtocol();
    virtual ~CAN2CCUProtocol();
    
    // BY ZF: 协议初始化/反初始化
    bool initialize(const char* config);
    void cleanup();
    
    /**
     * 设置发送回调函数（用于协议层直接调用通讯层发送）
     * @param callback 发送回调函数
     */
    void setSendCallback(SendCallback callback);
    
    // ========== 编码方法：业务数据 -> CAN帧（编码完成后直接发送）==========
    // 注意：这些方法编码完成后会直接调用通讯层发送，不再返回数据
    int encodeStartCharge();
    int encodeStopCharge();
    int encodePowerAdjust();
    int encodeOutputVAControl();
    int encodeGetStatus(uint8_t gunNo);
    int encodeClearFault(uint8_t gunNo);
    
    // ========== 解码入口与状态汇总 ==========
    int decodeFrame(uint32_t canId, const uint8_t* data, size_t dataLen);
    int getAggregatedStatus(PileStatus* outStatus);
    
    // ========== 协议特定方法 ==========
    int encodeHeartbeat();
    bool decodeHeartbeat(const uint8_t* data, size_t dataLen);
    
    // ========== CAN2CCU 协议特定方法 ==========
    
    /**
     * 编码并发送充电启动完成应答帧（表13，PGN=0x12）
     * @param loadControlSwitch 负荷控制开关 01H 启用 02H 关闭
     * @param confirmFlag 确认标识 00H 成功 01H 失败
     * @return 0 成功，否则失败
     */
    int encodeStartCompleteAck(uint8_t loadControlSwitch, uint8_t confirmFlag);
    
    /**
     * 编码并发送充电完成应答帧（表17，PGN=0x14）
     * @param stopReason 停止原因（与停止完成帧一致，见附录D13）
     * @param confirmFlag 确认标识 00H 成功 01H 失败
     * @return 0 成功，否则失败
     */
    int encodeStopCompleteAck(uint8_t stopReason, uint8_t confirmFlag);
    
    /**
     * 编码并发送版本校验帧（表22，PGN=0x07）
     * 协议规定版本号为 V1.21，报文字节 01H 21H，无需参数
     * @return 0 成功，否则失败
     */
    int encodeVersionCheck();
    
    /**
     * 编码并发送下发充电参数帧（表24，PGN=0x09）
     * @param pileId 充电桩(机)编号（压缩BCD码，7字节）
     * @return 0 成功，否则失败
     */
    int encodeIssueChargeParams(const uint8_t pileId[7]);
    
    /**
     * 计算CAN ID
     * @param priority 优先级（根据帧类型指定，如0x10或0x18）
     * @param pgn 协议组号（消息类型，如0x01启动充电、0x03停止充电等）
     * @param srcAddr 源地址（BCU地址，默认0x8A）
     * @param dstAddr 目标地址（CDZ地址）
     * @return CAN ID（29位扩展帧）
     * 
     * CAN ID格式：优先级(8bit) + PGN(8bit) + 目标地址(8bit) + 源地址(8bit)
     * 位分配：优先级[31:24] + PGN[23:16] + 目标地址[15:8] + 源地址[7:0]
     * 示例：0x1001E08A = 优先级(0x10) + PGN(0x01) + 目标地址(0xE0) + 源地址(0x8A)
     */
    uint32_t calculateCANId(uint8_t priority, uint8_t pgn, uint8_t srcAddr, uint8_t dstAddr);
    
    /**
     * 解析CAN ID
     * @param canId CAN ID（29位扩展帧）
     * @param outPriority 输出：优先级
     * @param outPgn 输出：协议组号（1字节）
     * @param outSrcAddr 输出：源地址
     * @param outDstAddr 输出：目标地址
     */
    void parseCANId(uint32_t canId, uint8_t* outPriority, uint8_t* outPgn, 
                    uint8_t* outSrcAddr, uint8_t* outDstAddr);
    
    // ========== 数据访问接口（Getter/Setter）==========
    
    // BY ZF: 命令数据访问（业务层设置，协议层编码时使用）
    
    /**
     * 设置启动充电命令数据
     * @param cmdData 启动充电业务数据
     * @return 0成功，其他失败
     */
    int setStartChargeData(const TCU2CCU_CmdStartChargeData* cmdData);
    int setPowerAdjustData(const TCU2CCU_CmdPowerAdjustData* cmdData);
    int setOutputVAData(const TCU2CCU_CmdOutputVAData* cmdData);
    int setVersionCheckData(const TCU2CCU_CmdVersionCheckData* cmdData);
    int setChargeParamData(const TCU2CCU_CmdChargeParamData* cmdData);
    
    /**
     * 获取启动充电命令数据（下发的命令参数）
     * @param outCmdData 输出：启动充电命令业务数据
     * @return 0成功，其他失败
     */
    int getStartChargeData(TCU2CCU_CmdStartChargeData* outCmdData) const;
    
    /**
     * 获取启动充电应答数据（接收到的应答）
     * @param outData 输出：启动充电应答业务数据
     * @return 0成功，其他失败
     */
    int getStartChargeResponseData(TCU2CCU_StartChargeResponseData* outData) const;
    bool isStartChargeResponseValid() const { return m_startChargeResponseDataValid; }
    void clearStartChargeResponseValid() { m_startChargeResponseDataValid = false; }
    
    /**
     * 设置停止充电命令数据
     * @param cmdData 停止充电业务数据
     * @return 0成功，其他失败
     */
    int setStopChargeData(const TCU2CCU_CmdStopChargeData* cmdData);
    
    /**
     * 获取停止充电命令数据
     * @param outCmdData 输出：停止充电业务数据
     * @return 0成功，其他失败
     */
    int getStopChargeData(TCU2CCU_CmdStopChargeData* outCmdData) const;
    int getPowerAdjustData(TCU2CCU_CmdPowerAdjustData* outCmdData) const;
    int getOutputVAData(TCU2CCU_CmdOutputVAData* outCmdData) const;
    
    /**
     * 获取停止充电应答数据（接收到的应答）
     * @param outData 输出：停止充电应答业务数据
     * @return 0成功，其他失败
     */
    int getStopChargeResponseData(TCU2CCU_StopChargeResponseData* outData) const;
    bool isStopChargeResponseValid() const { return m_stopChargeResponseDataValid; }
    void clearStopChargeResponseValid() { m_stopChargeResponseDataValid = false; }

    bool isPowerAdjustResponseValid() const { return m_powerAdjustResponseDataValid; }
    void clearPowerAdjustResponseValid() { m_powerAdjustResponseDataValid = false; }
    
    /**
     * 获取版本校验应答数据（表23，CCU返回）
     * @param outData 输出：版本校验应答业务数据
     * @return 0成功，其他失败
     */
    int getVersionCheckResponseData(TCU2CCU_VersionCheckResponseData* outData) const;
    bool isVersionCheckResponseValid() const { return m_versionCheckResponseDataValid; }
    
    /**
     * 获取下发充电参数应答数据（表25，CCU返回）
     * @param outData 输出：应答数据
     * @return 0成功，其他失败
     */
    int getChargeParamResponseData(TCU2CCU_ChargeParamResponseData* outData) const;
    bool isChargeParamResponseValid() const { return m_chargeParamResponseDataValid; }
    
    // BY ZF: 状态数据访问（协议层解码后填充，业务层读取）
    
    /**
     * 获取充电启动完成状态数据
     * @param outStatusData 输出：充电启动完成状态业务数据
     * @return 0成功，其他失败
     */
    int getStartCompleteData(TCU2CCU_StatusStartCompleteData* outStatusData) const;
    void clearStartCompleteValid() { m_startCompleteDataValid = false; }
    
    /**
     * 获取充电停止完成状态数据
     * @param outStatusData 输出：充电停止完成状态业务数据
     * @return 0成功，其他失败
     */
    int getStopCompleteData(TCU2CCU_StatusStopCompleteData* outStatusData) const;
    void clearStopCompleteValid() { m_stopCompleteDataValid = false; }
    
    /**
     * 获取充电桩状态信息数据
     * @param outStatusData 输出：充电桩状态信息业务数据
     * @return 0成功，其他失败
     */
    int getPileStateData(TCU2CCU_StatusPileStateData* outStatusData) const;
    
    /**
     * 获取车辆识别信息数据
     * @param outStatusData 输出：车辆识别信息业务数据
     * @return 0成功，其他失败
     */
    int getVehicleIdData(TCU2CCU_StatusVehicleIdData* outStatusData) const;
    
    // BY ZF: 遥测数据访问（协议层解码后填充，业务层读取）
    
    /**
     * 获取遥测帧20数据（YC20）
     * @param outData 输出：遥测帧20业务数据
     * @return 0成功，其他失败
     */
    int getYC20Data(TCU2CCU_DataYC20* outData) const;
    
    /**
     * 获取遥测帧21数据（YC21）
     * @param outData 输出：遥测帧21业务数据
     * @return 0成功，其他失败
     */
    int getYC21Data(TCU2CCU_DataYC21* outData) const;
    
    // BY ZF: 遥信数据访问（协议层解码后填充，业务层读取）
    
    /**
     * 获取遥信数据22（YX22）
     * @param outData 输出：遥信数据22业务数据
     * @return 0成功，其他失败
     */
    int getYX22Data(TCU2CCU_DataYX22* outData) const;
    
    /**
     * 获取遥信数据23（YX23）
     * @param outData 输出：遥信数据23业务数据
     * @return 0成功，其他失败
     */
    int getYX23Data(TCU2CCU_DataYX23* outData) const;
    
    // BY ZF: 数据有效性标志（用于判断数据是否已更新）
    
    /**
     * 检查启动完成状态数据是否有效
     * @return true有效，false无效
     */
    bool isStartCompleteDataValid() const { return m_startCompleteDataValid; }
    
    /**
     * 检查停止完成状态数据是否有效
     * @return true有效，false无效
     */
    bool isStopCompleteDataValid() const { return m_stopCompleteDataValid; }
    
    /**
     * 检查遥测帧20数据是否有效（YC20）
     * @return true有效，false无效
     */
    bool isYC20DataValid() const { return m_yc20DataValid; }
    
    /**
     * 检查遥测帧21数据是否有效（YC21）
     * @return true有效，false无效
     */
    bool isYC21DataValid() const { return m_yc21DataValid; }
    
    /**
     * 检查遥信数据22是否有效（YX22）
     * @return true有效，false无效
     */
    bool isYX22DataValid() const { return m_yx22DataValid; }
    
    /**
     * 检查遥信数据23是否有效（YX23）
     * @return true有效，false无效
     */
    bool isYX23DataValid() const { return m_yx23DataValid; }

    // BY ZF: 重置所有多帧接收上下文（用于通信异常恢复）
    void resetAllLongFrameContexts();
    
    /**
     * 根据心跳接收情况返回通信状态（供外部判断超时、告警等）
     * 内部在心跳应答解析中更新接收时间，据此计算是否超时。
     * @return 0x00 正常（3 秒内收到过充电控制模块心跳应答），0x01 接收超时
     */
    uint8_t getHeartbeatCommStatus() const;

private:
    // BY ZF: 协议配置参数
    uint8_t m_bcuAddr;              // BCU地址（TCU地址），默认0x8A，可配置
    uint8_t m_cdzAddr;              // CDZ地址（CCU地址），从配置文件读取
    uint8_t m_gunNo;                // 枪号（0=左枪，1=右枪）
    int m_packetTimeout;            // 数据包超时（秒）
    int m_retryCount;               // 重传次数
    
    // BY ZF: 协议状态
    bool m_initialized;              // 是否已初始化
    uint8_t m_sequence;             // 序列号（用于应答匹配）
    uint8_t m_heartbeatCount;       // 心跳计数值（表18 字节2），0~255 每次发送+1 循环
    uint8_t m_ccuHeartbeatRxStatus; // 充电控制模块心跳帧接收状态（表18 字节3），编码时由心跳解析结果计算
    time_t m_lastCcuHeartbeatTime;  // 上次收到 CCU 心跳应答时间（0=未收到），用于计算 getHeartbeatCommStatus 与编码字节3
    
    // ========== 命令数据成员变量（业务层设置，编码时使用）==========
    
    // BY ZF: 启动充电命令数据（下发）
    TCU2CCU_CmdStartChargeData m_cmdStartChargeData;
    bool m_cmdStartChargeDataValid;  // 数据是否有效
    
    // BY ZF: 启动充电应答数据（接收）
    TCU2CCU_StartChargeResponseData m_startChargeResponseData;
    bool m_startChargeResponseDataValid;
    
    // BY ZF: 停止充电命令数据（下发）
    TCU2CCU_CmdStopChargeData m_cmdStopChargeData;
    bool m_cmdStopChargeDataValid;

    // BY ZF: 功率调节命令数据（下发，仅处理绝对值模式）
    TCU2CCU_CmdPowerAdjustData m_cmdPowerAdjustData;
    bool m_cmdPowerAdjustDataValid;

    // BY ZF: 输出电压电流调节命令数据（下发，4C帧）
    TCU2CCU_CmdOutputVAData m_cmdOutputVAData;
    bool m_cmdOutputVADataValid;

    // BY ZF: 功率调节应答标志（接收）
    bool m_powerAdjustResponseDataValid;
    
    // BY ZF: 停止充电应答数据（接收）
    TCU2CCU_StopChargeResponseData m_stopChargeResponseData;
    bool m_stopChargeResponseDataValid;
    
    // BY ZF: 版本校验命令数据（表22，下发）
    TCU2CCU_CmdVersionCheckData m_cmdVersionCheckData;
    bool m_cmdVersionCheckDataValid;
    
    // BY ZF: 版本校验应答数据（表23，接收）
    TCU2CCU_VersionCheckResponseData m_versionCheckResponseData;
    bool m_versionCheckResponseDataValid;
    
    // BY ZF: 下发充电参数命令数据（表24，下发）与应答数据（表25，接收）
    TCU2CCU_CmdChargeParamData m_cmdChargeParamData;
    bool m_cmdChargeParamDataValid;
    TCU2CCU_ChargeParamResponseData m_chargeParamResponseData;
    bool m_chargeParamResponseDataValid;
    
    // ========== 状态数据成员变量（解码后填充，业务层读取）==========
    
    // BY ZF: 充电启动完成状态数据
    TCU2CCU_StatusStartCompleteData m_statusStartCompleteData;
    bool m_startCompleteDataValid;  // 数据是否有效
    
    // BY ZF: 充电停止完成状态数据
    TCU2CCU_StatusStopCompleteData m_statusStopCompleteData;
    bool m_stopCompleteDataValid;  // 数据是否有效
    
    // BY ZF: 充电桩状态信息数据
    TCU2CCU_StatusPileStateData m_statusPileStateData;
    bool m_pileStateDataValid;  // 数据是否有效
    
    // BY ZF: 车辆识别信息数据
    TCU2CCU_StatusVehicleIdData m_statusVehicleIdData;
    bool m_vehicleIdDataValid;  // 数据是否有效
    
    // ========== 遥测数据成员变量（解码后填充，业务层读取）==========
    
    // BY ZF: 遥测帧20数据（YC20）
    TCU2CCU_DataYC20 m_yc20Data;
    bool m_yc20DataValid;  // 数据是否有效
    
    // BY ZF: 遥测帧21数据（YC21）
    TCU2CCU_DataYC21 m_yc21Data;
    bool m_yc21DataValid;  // 数据是否有效
    
    // ========== 遥信数据成员变量（解码后填充，业务层读取）==========
    
    // BY ZF: 遥信数据22（YX22）
    TCU2CCU_DataYX22 m_yx22Data;
    bool m_yx22DataValid;  // 数据是否有效
    
    // BY ZF: 遥信数据23（YX23）
    TCU2CCU_DataYX23 m_yx23Data;
    bool m_yx23DataValid;  // 数据是否有效
    
    // ========== 内部编码方法（从成员变量构建CAN帧并直接发送）==========
    
    /**
     * 编码并发送启动充电命令（从成员变量构建CAN帧，然后直接发送）
     * @return 0成功，其他失败
     */
    int encodeStartChargeFrame();
    
    /**
     * 编码并发送停止充电命令（从成员变量构建CAN帧，然后直接发送）
     * @return 0成功，其他失败
     */
    int encodeStopChargeFrame();
    int encodePowerAdjustFrame();
    int encodeOutputVAControlFrame();
    
    /**
     * 发送单帧数据（单帧发送器）
     * @param priority 优先级（与CAN ID格式一致，优先级在前）
     * @param pgn 协议组号（1字节）
     * @param data 协议数据（业务数据，不包含帧头）
     * @param dataLen 数据长度（必须<=4，因为CAN帧只有8字节，需要4字节用于帧头）
     * @return 0成功，其他失败
     */
    int sendSingleFrame(uint8_t priority, uint8_t pgn, const uint8_t* data, size_t dataLen);
    
    /**
     * 发送多帧数据（多帧发送器）
     * @param priority 优先级（与CAN ID格式一致，优先级在前）
     * @param pgn 协议组号（1字节）
     * @param data 协议数据（业务数据，不包含帧头）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int sendMultiFrame(uint8_t priority, uint8_t pgn, const uint8_t* data, size_t dataLen);
    
    // ========== 内部解码方法（将CAN帧数据填充到成员变量）==========
    
    /**
     * 解码启动充电应答帧（填充到成员变量）
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeStartChargeResponse(const uint8_t* data, size_t dataLen);
    
    /**
     * 解码停止充电应答帧（填充到成员变量）
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeStopChargeResponse(const uint8_t* data, size_t dataLen);
    int decodePowerAdjustResponse(const uint8_t* data, size_t dataLen);
    
    /**
     * 解码版本校验应答帧（表23，PGN=0x08，填充到成员变量）
     * @param data CAN帧数据
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeVersionCheckResponse(const uint8_t* data, size_t dataLen);
    
    /**
     * 解码下发充电参数应答帧（表25，PGN=0x0A）
     * @param data CAN帧数据
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeChargeParamResponse(const uint8_t* data, size_t dataLen);
    
    /**
     * 解码充电启动完成状态帧（填充到成员变量，可能需要多帧）
     * @param canId CAN ID
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeStartCompleteFrame(uint32_t canId, const uint8_t* data, size_t dataLen);
    
    /**
     * 解码充电停止完成状态帧（填充到成员变量，可能需要多帧）
     * @param canId CAN ID
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeStopCompleteFrame(uint32_t canId, const uint8_t* data, size_t dataLen);
    
    /**
     * 解码遥测帧20（YC20，填充到成员变量，可能需要多帧）
     * @param canId CAN ID
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeYC20Frame(uint32_t canId, const uint8_t* data, size_t dataLen);
    
    /**
     * 解码遥信数据22（YX22，填充到成员变量，可能需要多帧）
     * @param canId CAN ID
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeYX22Frame(uint32_t canId, const uint8_t* data, size_t dataLen);
    
    /**
     * 解码遥信数据23（YX23，填充到成员变量，可能需要多帧）
     * @param canId CAN ID
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @return 0成功，其他失败
     */
    int decodeYX23Frame(uint32_t canId, const uint8_t* data, size_t dataLen);
    
    // ========== 多帧处理（内部实现）==========
    
    /**
     * 多帧接收上下文结构（用于长帧组装）
     */
    struct LongFrameContext {
        uint8_t pgn;               // 协议组号（1字节）
        uint8_t totalFrames;       // 总帧数
        uint8_t currentFrame;      // 当前已接收帧数
        uint16_t dataLength;       // 有效数据长度
        uint8_t buffer[512];       // 接收缓冲区（根据实际需要调整）
        uint32_t lastFrameTime;    // 最后一帧接收时间（用于超时检测）
        bool isComplete;           // 是否接收完成
        bool isValid;              // 上下文是否有效
        
        LongFrameContext() 
            : pgn(0)
            , totalFrames(0)
            , currentFrame(0)
            , dataLength(0)
            , lastFrameTime(0)
            , isComplete(false)
            , isValid(false)
        {
            memset(buffer, 0, sizeof(buffer));
        }
    };
    
    /**
     * 处理多帧数据（长帧组装）
     * @param canId CAN ID
     * @param data CAN帧数据（8字节）
     * @param dataLen 数据长度
     * @param context 多帧接收上下文
     * @return 0成功，1需要继续接收，-1失败
     */
    int processLongFrame(uint32_t canId, const uint8_t* data, size_t dataLen, LongFrameContext* context);
    
    /**
     * 计算校验和（16位累加和）
     * @param data 数据
     * @param len 数据长度
     * @return 校验和（16位）
     */
    uint16_t calculateChecksum(const uint8_t* data, size_t len);
    
    /**
     * 验证校验和
     * @param data 数据（包含校验和）
     * @param len 数据长度（包含校验和）
     * @return true校验通过，false校验失败
     */
    bool verifyChecksum(const uint8_t* data, size_t len);
    
    // BY ZF: 发送回调函数（用于调用通讯层发送）
    SendCallback m_sendCallback;
    
    // BY ZF: 多帧接收上下文（每个PGN一个上下文）
    LongFrameContext m_longFrameContexts[16];  // 支持最多16个不同的PGN同时接收
    
    /**
     * 获取或创建多帧接收上下文
     * @param pgn 协议组号（1字节）
     * @return 多帧接收上下文指针，失败返回nullptr
     */
    LongFrameContext* getLongFrameContext(uint8_t pgn);
    
    /**
     * 重置多帧接收上下文
     * @param pgn 协议组号（1字节）
     */
    void resetLongFrameContext(uint8_t pgn);
    
    // BY ZF: PGN定义（消息类型）
    static const uint16_t PGN_START_CHARGE = 0x01;      // 启动充电命令
    static const uint16_t PGN_STOP_CHARGE = 0x03;      // 停止充电命令
    static const uint16_t PGN_TIME_SYNC = 0x05;        // 对时命令
    static const uint16_t PGN_VERSION_CHECK = 0x07;     // 版本校验
    static const uint16_t PGN_PILE_CONFIG = 0x09;      // 充电桩参数信息
    static const uint16_t PGN_POWER_ADJUST = 0x0F;     // 功率调节命令
    static const uint16_t PGN_OUTPUT_VA_CTRL = 0x4C;   // 输出电压电流调节命令
    static const uint16_t PGN_START_CHARGE_ACK = 0x12; // 启动充电应答确认
    static const uint16_t PGN_STOP_CHARGE_ACK = 0x14;  // 停止充电应答确认
    static const uint16_t PGN_VIN_CONFIRM = 0x18;      // VIN信息确认
    static const uint16_t PGN_VIN_REQUEST = 0x19;      // VIN信息请求
    static const uint16_t PGN_HEARTBEAT = 0x40;        // 心跳帧
    
    // BY ZF: 接收端PGN定义
    static const uint16_t PGN_START_CHARGE_RESP = 0x02;     // 启动充电应答
    static const uint16_t PGN_STOP_CHARGE_RESP = 0x04;     // 停止充电应答
    static const uint16_t PGN_TIME_SYNC_RESP = 0x06;       // 对时应答
    static const uint16_t PGN_VERSION_RESP = 0x08;         // 版本应答
    static const uint16_t PGN_PILE_CONFIG_ACK = 0x0A;      // 充电桩参数确认
    static const uint16_t PGN_POWER_ADJUST_RESP = 0x10;    // 功率调节应答
    static const uint16_t PGN_START_COMPLETE = 0x11;       // 充电启动完成状态
    static const uint16_t PGN_STOP_COMPLETE = 0x13;         // 停止充电完成状态
    static const uint16_t PGN_VIN_INFO = 0x17;             // VIN信息
    static const uint16_t PGN_VIN_REPLY_ACK = 0x1A;        // VIN信息回复确认
    static const uint16_t PGN_TELEMETRY_20 = 0x20;          // 直流遥测帧20
    static const uint16_t PGN_TELEMETRY_21 = 0x21;          // 直流遥测帧21
    static const uint16_t PGN_TELESIGNAL_22 = 0x22;         // 直流遥信帧1
    static const uint16_t PGN_TELESIGNAL_23 = 0x23;         // 直流遥信帧2
    static const uint16_t PGN_HEARTBEAT_RESP = 0x41;       // 心跳帧应答
    static const uint16_t PGN_VIN_REPLY = 0x43;             // 充电桩回复VIN
};

#endif // CAN2CCU_PROTOCOL_H
