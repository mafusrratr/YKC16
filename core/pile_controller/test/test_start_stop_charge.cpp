/**
 * 启动/停止充电流程测试
 * BY ZF
 *
 * 流程：
 *   [配置阶段] BCU→CDZ 0x07 请求版本(500ms周期) → 收到 CDZ 0x08 后停止 0x07
 *            → BCU→CDZ 0x09 请求桩参数 → 收到 CDZ 0x0A 桩参数应答后进入充电流程
 *   [充电阶段] 遥信插枪 → 5 秒后下发启动命令 → 等待启动完成帧并回复确认 → 充电 2 分钟后下发停止
 *
 * 用法：
 *   test_start_stop_charge [config]
 *   config 默认: device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=0,packet_timeout=5,retry_count=3
 */

#include "../controllers/can_pile_controller.h"
#include "../protocol/tcu2ccu_data.h"
#include "../ipile_controller.h"
#include <iostream>
#include <cstring>
#include <unistd.h>

static const int DELAY_AFTER_PLUG_SEC = 5;    // 插枪后延迟秒数再启动充电
static const int CHARGE_DURATION_SEC  = 300;  // 充电持续 5 分钟
static const unsigned int LOOP_MS       = 100;  // 主循环周期 100ms
static const unsigned int HEARTBEAT_MS  = 1000; // 心跳间隔 1 秒
static const unsigned int VERSION_REQ_MS = 500; // 版本请求 0x07 周期 500ms

// 默认充电桩编号（表24 压缩BCD 7字节），可按实际配置修改
static const uint8_t DEFAULT_PILE_ID[7] = { 0x58, 0x05, 0x00, 0x10, 0x90, 0x40, 0x22 };

int main(int argc, char* argv[])
{
    const char* config = (argc >= 2 && argv[1][0] != '\0')
        ? argv[1]
        : "device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=0,packet_timeout=5,retry_count=3";

    std::cout << "=== 启动/停止充电流程测试 ===" << std::endl;
    std::cout << "流程: [配置] 0x07版本(" << VERSION_REQ_MS << "ms) -> 0x08 -> 0x09桩参数 -> 0x0A -> [充电] 插枪 -> 启动 -> 完成确认 -> " << CHARGE_DURATION_SEC << "秒后停止" << std::endl;
    std::cout << "配置: " << config << std::endl;
    std::cout << "[测试] 配置阶段: BCU 发送 0x07 版本请求(" << VERSION_REQ_MS << "ms 周期)，等待 CDZ 0x08..." << std::endl;

    CANPileController controller;
    if (!controller.initialize("can", config)) {
        std::cerr << "初始化失败，请检查 CAN 设备与配置" << std::endl;
        return 1;
    }
    IPileController* base = &controller;

    enum State { CONFIG_VERSION, CONFIG_PILE_PARAMS, WAIT_PLUG, WAIT_5S, WAIT_START_COMPLETE, CHARGING, STOP_SENT };
    State state = CONFIG_VERSION;
    int countdown = 0;
    unsigned int heartbeatMs = 0;
    unsigned int versionReqMs = 0;  // 距上次发送 0x07 的毫秒数
    bool pileParamSent = false;     // 是否已发送 0x09
    while (true) {
        heartbeatMs += LOOP_MS;
        if (heartbeatMs >= HEARTBEAT_MS) {
            controller.heartbeat();
            heartbeatMs = 0;
        }

        switch (state) {
        case CONFIG_VERSION: {
            // BCU→CDZ 0x07 每 500ms 发一次，收到 0x08 后进入下一步
            versionReqMs += LOOP_MS;
            if (versionReqMs >= VERSION_REQ_MS) {
                controller.encodeVersionCheck();
                versionReqMs = 0;
            }
            if (controller.isVersionCheckResponseValid()) {
                std::cout << "[测试] 已收到版本校验应答(0x08)，停止发0x07，下发桩参数(0x09)..." << std::endl;
                state = CONFIG_PILE_PARAMS;
                pileParamSent = false;
            }
            break;
        }
        case CONFIG_PILE_PARAMS: {
            if (!pileParamSent) {
                if (controller.encodeIssueChargeParams(DEFAULT_PILE_ID) == 0) {
                    pileParamSent = true;
                }
            }
            if (controller.isChargeParamResponseValid()) {
                std::cout << "[测试] 已收到桩参数应答(0x0A)，配置交互完成，等待插枪..." << std::endl;
                state = WAIT_PLUG;
            }
            break;
        }
        case WAIT_PLUG: {
            if (controller.isYX22DataValid()) {
                TCU2CCU_DataYX22 yx22;
                if (controller.getYX22Data(&yx22) == 0 && yx22.vehicleConnectStatus == 1) {
                    std::cout << "[测试] 插枪遥信有效(车辆连接)，" << DELAY_AFTER_PLUG_SEC << " 秒后下发启动充电..." << std::endl;
                    state = WAIT_5S;
                    countdown = DELAY_AFTER_PLUG_SEC * (1000u / LOOP_MS);  // 换算为循环次数
                }
            }
            break;
        }
        case WAIT_5S: {
            if (countdown <= 0) {
                TCU2CCU_CmdStartChargeData startCmd;
                memset(&startCmd, 0, sizeof(startCmd));
                startCmd.loadControlSwitch = 0x02;  // 默认关闭
                startCmd.plugAndChargeFlag = 0x01;  // 默认非即插即充
                startCmd.auxPowerVoltage = 0x0C;    // 默认12V
                controller.setStartChargeData(&startCmd);
                if (base->startCharge() == 0) {
                    std::cout << "[测试] 已下发充电启动命令，等待充电启动完成帧并回复确认..." << std::endl;
                    state = WAIT_START_COMPLETE;
                } else {
                    std::cerr << "[测试] 下发启动充电失败" << std::endl;
                    state = WAIT_PLUG;
                }
            } else {
                countdown--;
            }
            break;
        }
        case WAIT_START_COMPLETE: {
            // 收到启动完成帧后协议层会立即回复表13，此处仅根据有效标志进入充电
            // 收到充电启动完成帧（PGN 0x11）后协议层会自动回复表13 应答帧，此处根据有效标志进入充电状态
            if (controller.isStartCompleteDataValid()) {
                std::cout << "[测试] 已收到启动完成帧并回复确认，进入充电状态，" << CHARGE_DURATION_SEC << " 秒后下发停止..." << std::endl;
                state = CHARGING;
                countdown = CHARGE_DURATION_SEC * (1000u / LOOP_MS);  // 换算为循环次数
            }
            break;
        }
        case CHARGING: {
            if (countdown <= 0) {
                TCU2CCU_CmdStopChargeData stopCmd;
                memset(&stopCmd, 0, sizeof(stopCmd));
                stopCmd.stopReason = 0x01;  // 计费控制单元正常停止
                stopCmd.tcuStopCode = 0x00;
                controller.setStopChargeData(&stopCmd);
                if (base->stopCharge() == 0) {
                    std::cout << "[测试] 已下发充电停止命令，流程结束" << std::endl;
                    state = STOP_SENT;
                } else {
                    std::cerr << "[测试] 下发停止充电失败" << std::endl;
                }
                countdown = 0;
            } else {
                countdown--;
            }
            break;
        }
        case STOP_SENT: {
            std::cout << "[测试] 流程结束，3 秒后退出" << std::endl;
            sleep(3);
            controller.cleanup();
            return 0;
        }
        }

        usleep(LOOP_MS * 1000);  // 主循环 100ms，便于快速响应启动完成帧
    }
}
