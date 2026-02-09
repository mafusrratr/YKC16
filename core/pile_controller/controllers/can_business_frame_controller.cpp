/**
 * CAN 业务帧解析控制器实现
 * 仅从 CAN 链路接收并解析业务帧，数据来源为 CAN 总线
 * BY ZF
 */

#include "can_business_frame_controller.h"
#include "../protocol/tcu2ccu_data.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <unistd.h>

CANBusinessFrameController::CANBusinessFrameController()
    : m_baudrate(0)
    , m_bcuAddr(0x8A)
    , m_cdzAddr(0)
    , m_gunNo(0)
    , m_initialized(false)
    , m_running(false)
{
}

CANBusinessFrameController::~CANBusinessFrameController()
{
    cleanup();
}

void CANBusinessFrameController::parseConfig(const char* config)
{
    m_devicePath.clear();
    m_baudrate = 0;
    m_bcuAddr = 0x8A;
    m_cdzAddr = 0;
    m_gunNo = 0;

    if (config == nullptr || strlen(config) == 0) {
        return;
    }

    std::string configStr(config);
    std::istringstream iss(configStr);
    std::string token;

    while (std::getline(iss, token, ',')) {
        size_t pos = token.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        std::string key = token.substr(0, pos);
        std::string value = token.substr(pos + 1);

        if (key == "device_path") {
            m_devicePath = value;
        } else if (key == "baudrate") {
            m_baudrate = static_cast<uint32_t>(std::stoul(value));
        } else if (key == "bcu_addr") {
            if (value.size() >= 2 && (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X")) {
                m_bcuAddr = static_cast<uint8_t>(std::stoul(value, nullptr, 16));
            } else {
                m_bcuAddr = static_cast<uint8_t>(std::stoul(value));
            }
        } else if (key == "cdz_addr") {
            if (value.size() >= 2 && (value.substr(0, 2) == "0x" || value.substr(0, 2) == "0X")) {
                m_cdzAddr = static_cast<uint8_t>(std::stoul(value, nullptr, 16));
            } else {
                m_cdzAddr = static_cast<uint8_t>(std::stoul(value));
            }
        } else if (key == "gun_no") {
            m_gunNo = static_cast<uint8_t>(std::stoul(value));
        }
    }

    if (m_bcuAddr == 0) {
        m_bcuAddr = 0x8A;
    }
}

bool CANBusinessFrameController::initialize(const char* config)
{
    if (config == nullptr || strlen(config) == 0) {
        std::cerr << "[CANBusinessFrameController] Invalid config\n";
        return false;
    }

    parseConfig(config);

    if (m_devicePath.empty() || m_baudrate == 0 || m_cdzAddr == 0) {
        std::cerr << "[CANBusinessFrameController] Invalid config: device_path=" << m_devicePath
                  << ", baudrate=" << m_baudrate
                  << ", cdz_addr=0x" << std::hex << static_cast<int>(m_cdzAddr) << std::dec << "\n";
        return false;
    }

    m_canComm.reset(new CANCommunication());
    m_protocol.reset(new CAN2CCUProtocol());

    std::ostringstream oss;
    oss << "bcu_addr=0x" << std::hex << static_cast<int>(m_bcuAddr) << std::dec
        << ",cdz_addr=0x" << std::hex << static_cast<int>(m_cdzAddr) << std::dec
        << ",gun_no=" << static_cast<int>(m_gunNo)
        << ",packet_timeout=5,retry_count=3";
    if (!m_protocol->initialize(oss.str().c_str())) {
        std::cerr << "[CANBusinessFrameController] Failed to initialize protocol\n";
        return false;
    }

    if (!m_canComm->open(m_devicePath, m_baudrate)) {
        std::cerr << "[CANBusinessFrameController] Failed to open CAN device: " << m_devicePath << "\n";
        return false;
    }

    std::cout << "[CANBusinessFrameController] Initialized: device=" << m_devicePath
              << ", baudrate=" << m_baudrate
              << ", bcu_addr=0x" << std::hex << static_cast<int>(m_bcuAddr)
              << ", cdz_addr=0x" << static_cast<int>(m_cdzAddr)
              << ", gun_no=" << std::dec << static_cast<int>(m_gunNo) << "\n";

    m_initialized = true;
    m_running = false;
    return true;
}

void CANBusinessFrameController::cleanup()
{
    if (!m_initialized) {
        return;
    }
    stop();
    if (m_canComm && m_canComm->isOpen()) {
        m_canComm->close();
    }
    m_canComm.reset();
    m_protocol.reset();
    m_initialized = false;
}

void CANBusinessFrameController::run()
{
    if (!m_initialized || !m_canComm || !m_protocol) {
        std::cerr << "[CANBusinessFrameController] Not initialized\n";
        return;
    }

    m_running = true;
    std::cout << "[CANBusinessFrameController] 开始从 CAN 链路接收并解析业务帧（按 Ctrl+C 退出）\n";

    uint32_t canId;
    uint8_t data[8];
    uint8_t dataLen;

    while (m_running.load()) {
        if (m_canComm->receive(canId, data, dataLen) == 0) {
            int ret = m_protocol->decodeFrame(canId, data, static_cast<size_t>(dataLen));
            if (ret == 0) {
                if (m_protocol->isYX22DataValid()) {
                    onYX22Decoded();
                }
            }
        } else {
            usleep(10000);
        }
    }

    std::cout << "[CANBusinessFrameController] 接收循环已停止\n";
}

void CANBusinessFrameController::stop()
{
    m_running = false;
}

void CANBusinessFrameController::onYX22Decoded()
{
    TCU2CCU_DataYX22 yx22;
    if (m_protocol->getYX22Data(&yx22) != 0) {
        return;
    }
    std::cout << "[YX22] workStatus=" << static_cast<int>(yx22.workStatus)
              << " totalFault=" << static_cast<int>(yx22.totalFault)
              << " totalAlarm=" << static_cast<int>(yx22.totalAlarm)
              << " vehicleConnect=" << static_cast<int>(yx22.vehicleConnectStatus)
              << " otherFault=0x" << std::hex << yx22.otherFault << std::dec << "\n";
}
