/**
 * DLT645 meter driver implementation
 * BY ZF
 */

#include "dlt645_meter.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <unistd.h>
#include <chrono>
#include <iostream>

namespace {
    // BY ZF: 固定使用 645 读数据命令
    static const uint8_t kFrameHead = 0x68;
    static const uint8_t kFrameTail = 0x16;
    static const uint8_t kCtrlReadData = 0x11;
    static const uint8_t kCtrlReadDataResp = 0x91;

    // BY ZF: 按现场码表固定 DI
    // U: 编码字节 33 34 43 35 -> 解码后 00 01 10 02 (低位在前) -> 0x02100100
    // I: 编码字节 33 34 44 35 -> 解码后 00 01 11 02 (低位在前) -> 0x02110100
    // E: 解码后 00 00 60 00 (低位在前) -> 0x00600000（正向有功总电能）
    // BY ZF: 反向有功电量数据块 DI（低位在前 00 02 FF 00）-> 0x0002FF00
    static const uint32_t kDiVoltage = 0x02100100;
    static const uint32_t kDiCurrent = 0x02110100;
    static const uint32_t kDiTotalEnergy = 0x00600000;
    static const uint32_t kDiReverseEnergy = 0x0002FF00;
}

// BY ZF: 构造函数，资源在 open() 中初始化
Dlt645Meter::Dlt645Meter()
    : m_debug(false)
{
}

// BY ZF: 析构时确保关闭串口
Dlt645Meter::~Dlt645Meter()
{
    close();
}

// BY ZF: 按配置打开串口
bool Dlt645Meter::open(const MeterSerialConfig& config)
{
    // BY ZF: 复用基础串口组件，统一由 meter 进程配置串口参数
    m_cfg = config;
    SerialParams params;
    params.baudrate = config.baudrate;
    params.dataBits = config.dataBits;
    params.stopBits = config.stopBits;
    params.parity = config.parity;
    params.flowControl = false;
    return m_serial.open(config.device, params);
}

// BY ZF: 关闭串口句柄
void Dlt645Meter::close()
{
    m_serial.close();
}

// BY ZF: 查询串口是否已打开
bool Dlt645Meter::isOpen() const
{
    return m_serial.isOpen();
}

void Dlt645Meter::setDebug(bool enable)
{
    m_debug = enable;
}

// BY ZF: 一次读取电能/电压/电流三个测点
bool Dlt645Meter::readAll(const std::string& meterAddr, MeterReading& out, std::string& err)
{
    // BY ZF: 按固定顺序读取，保证上层拿到的是同一轮采样的数据
    double energy = 0.0;
    double reverseEnergy = 0.0;
    double voltage = 0.0;
    double current = 0.0;

    if (!readOne(meterAddr, kDiTotalEnergy, 2, energy, err)) {
        return false;
    }
    if (!readOne(meterAddr, kDiReverseEnergy, 2, reverseEnergy, err)) {
        return false;
    }
    if (!readOne(meterAddr, kDiVoltage, 1, voltage, err)) {
        return false;
    }
    if (!readOne(meterAddr, kDiCurrent, 2, current, err)) {
        return false;
    }

    // BY ZF: 现场该 DI 返回值需再缩小 100 倍后作为 kWh 上送
    out.totalEnergy = energy / 100.0;
    // BY ZF: 反向有功总电能按 0.01kWh 精度解析，readOne() 已完成小数位处理，这里不再重复缩放
    out.reverseEnergy = reverseEnergy;
    // BY ZF: 现场量纲约定：电压再缩小 10 倍，电流再缩小 100 倍
    out.voltage = voltage / 10.0;
    out.current = current / 100.0;
    return true;
}

// BY ZF: 读取单个 DI 并转换为工程量
bool Dlt645Meter::readOne(const std::string& meterAddr, uint32_t di, int decimals, double& out, std::string& err)
{
    std::vector<uint8_t> addrLe;
    if (!parseAddress(meterAddr, addrLe)) {
        err = "invalid_meter_addr";
        return false;
    }

    std::vector<uint8_t> resp;
    if (!requestRead(addrLe, di, resp, err)) {
        return false;
    }

    if (!decodeReadResponse(resp, di, decimals, out)) {
        err = "decode_failed";
        return false;
    }
    if (m_debug) {
        std::cout << "[DLT645][READ_OK] addr=" << meterAddr
                  << " di=0x" << std::hex << di << std::dec
                  << " value=" << out << std::endl;
    }
    return true;
}

// BY ZF: 发送读取请求并在超时窗口内接收响应帧
bool Dlt645Meter::requestRead(const std::vector<uint8_t>& addrLe, uint32_t di, std::vector<uint8_t>& response, std::string& err)
{
    // BY ZF: 单请求单响应，超时前持续收包并尝试抽取完整 645 帧
    std::vector<uint8_t> req = buildReadFrame(addrLe, di);
    if (m_debug) {
        std::cout << "[DLT645][TX] di=0x" << std::hex << di << std::dec
                  << " frame=" << hexDump(req.data(), req.size()) << std::endl;
    }
    if (m_serial.send(req.data(), req.size()) <= 0) {
        err = "serial_send_failed";
        return false;
    }

    std::vector<uint8_t> rxBuf;
    rxBuf.reserve(256);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(m_cfg.readTimeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t tmp[128];
        int n = m_serial.receive(tmp, sizeof(tmp));
        if (n > 0) {
            if (m_debug) {
                std::cout << "[DLT645][RX_RAW] bytes=" << n
                          << " data=" << hexDump(tmp, static_cast<size_t>(n)) << std::endl;
            }
            rxBuf.insert(rxBuf.end(), tmp, tmp + n);
            if (tryExtractFrame(rxBuf, response)) {
                if (m_debug) {
                    std::cout << "[DLT645][RX_FRAME] di=0x" << std::hex << di << std::dec
                              << " frame=" << hexDump(response.data(), response.size()) << std::endl;
                }
                return true;
            }
        } else {
            usleep(2000);
        }
    }

    err = "serial_recv_timeout";
    if (m_debug) {
        std::cout << "[DLT645][TIMEOUT] di=0x" << std::hex << di << std::dec
                  << " cached=" << hexDump(rxBuf.data(), rxBuf.size()) << std::endl;
    }
    return false;
}

// BY ZF: 文本地址转 645 地址域（6字节，低位在前）
bool Dlt645Meter::parseAddress(const std::string& addrText, std::vector<uint8_t>& outAddrLe)
{
    // BY ZF: 只保留十六进制字符，兼容 "11-22-33-44-55-66" 这类输入
    std::string hex;
    hex.reserve(addrText.size());
    for (size_t i = 0; i < addrText.size(); i++) {
        const char c = addrText[i];
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            hex.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }

    if (hex.size() != 12) {
        return false;
    }

    std::vector<uint8_t> be(6, 0);
    for (size_t i = 0; i < 6; i++) {
        const std::string b = hex.substr(i * 2, 2);
        unsigned int v = 0;
        std::stringstream ss;
        ss << std::hex << b;
        ss >> v;
        be[i] = static_cast<uint8_t>(v & 0xFF);
    }

    outAddrLe.assign(be.rbegin(), be.rend());
    return true;
}

// BY ZF: 组装 645 读数据请求帧
std::vector<uint8_t> Dlt645Meter::buildReadFrame(const std::vector<uint8_t>& addrLe, uint32_t di)
{
    // BY ZF: DI 采用低字节在前并做 +0x33 编码
    std::vector<uint8_t> frame;
    frame.reserve(16);

    frame.push_back(kFrameHead);
    for (size_t i = 0; i < 6; i++) {
        frame.push_back(addrLe[i]);
    }
    frame.push_back(kFrameHead);
    frame.push_back(kCtrlReadData);
    frame.push_back(0x04);

    uint8_t di0 = static_cast<uint8_t>((di >> 0) & 0xFF);
    uint8_t di1 = static_cast<uint8_t>((di >> 8) & 0xFF);
    uint8_t di2 = static_cast<uint8_t>((di >> 16) & 0xFF);
    uint8_t di3 = static_cast<uint8_t>((di >> 24) & 0xFF);

    frame.push_back(static_cast<uint8_t>(di0 + 0x33));
    frame.push_back(static_cast<uint8_t>(di1 + 0x33));
    frame.push_back(static_cast<uint8_t>(di2 + 0x33));
    frame.push_back(static_cast<uint8_t>(di3 + 0x33));

    frame.push_back(checksum(frame.data(), frame.size()));
    frame.push_back(kFrameTail);
    return frame;
}

// BY ZF: 从接收缓存中抽取一帧完整 645 报文
bool Dlt645Meter::tryExtractFrame(std::vector<uint8_t>& buffer, std::vector<uint8_t>& frame)
{
    // BY ZF: 兼容 645 前导唤醒字节 FE FE FE FE，先剥离
    while (!buffer.empty() && buffer.front() == 0xFE) {
        buffer.erase(buffer.begin());
    }

    // BY ZF: 按 68 AAAAAA 68 ... CS 16 格式抽帧
    while (buffer.size() >= 12) {
        if (buffer[0] != kFrameHead) {
            buffer.erase(buffer.begin());
            continue;
        }

        if (buffer.size() < 10) {
            return false;
        }
        if (buffer[7] != kFrameHead) {
            buffer.erase(buffer.begin());
            continue;
        }

        const uint8_t len = buffer[9];
        const size_t frameLen = static_cast<size_t>(12 + len);
        if (buffer.size() < frameLen) {
            return false;
        }

        if (buffer[frameLen - 1] != kFrameTail) {
            buffer.erase(buffer.begin());
            continue;
        }

        const uint8_t cs = checksum(buffer.data(), frameLen - 2);
        if (cs != buffer[frameLen - 2]) {
            buffer.erase(buffer.begin());
            continue;
        }

        frame.assign(buffer.begin(), buffer.begin() + frameLen);
        buffer.erase(buffer.begin(), buffer.begin() + frameLen);
        return true;
    }
    return false;
}

// BY ZF: 解析读数据响应，校验 DI 并输出测量值
bool Dlt645Meter::decodeReadResponse(const std::vector<uint8_t>& frame, uint32_t expectedDi, int decimals, double& out)
{
    if (frame.size() < 16) {
        return false;
    }
    if (frame[0] != kFrameHead || frame[7] != kFrameHead) {
        return false;
    }
    if (frame[8] != kCtrlReadDataResp) {
        return false;
    }

    const uint8_t len = frame[9];
    if (len < 4 || frame.size() < static_cast<size_t>(12 + len)) {
        return false;
    }

    uint8_t di0 = static_cast<uint8_t>(frame[10] - 0x33);
    uint8_t di1 = static_cast<uint8_t>(frame[11] - 0x33);
    uint8_t di2 = static_cast<uint8_t>(frame[12] - 0x33);
    uint8_t di3 = static_cast<uint8_t>(frame[13] - 0x33);
    uint32_t di = static_cast<uint32_t>(di0)
                | (static_cast<uint32_t>(di1) << 8)
                | (static_cast<uint32_t>(di2) << 16)
                | (static_cast<uint32_t>(di3) << 24);
    if (di != expectedDi) {
        return false;
    }

    const size_t dataBytes = static_cast<size_t>(len - 4);
    if (dataBytes == 0) {
        return false;
    }

    // BY ZF: 数据区先执行 -0x33 解码，再按 BCD 转浮点
    std::vector<uint8_t> value(dataBytes, 0);
    for (size_t i = 0; i < dataBytes; i++) {
        value[i] = static_cast<uint8_t>(frame[14 + i] - 0x33);
    }

    // BY ZF: 反向有功总电能 DI=0x0002FF00 返回 5 组 * 4 字节数据块：
    // BY ZF: 总电能 + 各费率电能。当前现场仅取首组“总反向有功总电能”，按 0.01kWh 解析。
    if (expectedDi == kDiReverseEnergy) {
        if (dataBytes < 4) {
            return false;
        }
        out = decodeBcdLe(value.data(), 4, decimals);
        return true;
    }

    out = decodeBcdLe(value.data(), value.size(), decimals);
    return true;
}

// BY ZF: 计算 645 累加校验
uint8_t Dlt645Meter::checksum(const uint8_t* data, size_t len)
{
    uint32_t s = 0;
    for (size_t i = 0; i < len; i++) {
        s += data[i];
    }
    return static_cast<uint8_t>(s & 0xFF);
}

// BY ZF: BCD（低位在前）转十进制浮点
double Dlt645Meter::decodeBcdLe(const uint8_t* data, size_t len, int decimals)
{
    // BY ZF: data 为低位在前，先转成高位在前十进制串
    std::string digits;
    digits.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        const uint8_t b = data[len - 1 - i];
        digits.push_back(static_cast<char>('0' + ((b >> 4) & 0x0F)));
        digits.push_back(static_cast<char>('0' + (b & 0x0F)));
    }

    unsigned long long value = 0;
    for (size_t i = 0; i < digits.size(); i++) {
        const char c = digits[i];
        if (c < '0' || c > '9') {
            return 0.0;
        }
        value = value * 10ULL + static_cast<unsigned long long>(c - '0');
    }

    double f = static_cast<double>(value);
    while (decimals-- > 0) {
        f /= 10.0;
    }
    return f;
}

std::string Dlt645Meter::hexDump(const uint8_t* data, size_t len)
{
    if (!data || len == 0) {
        return "";
    }
    std::ostringstream oss;
    oss << std::hex;
    for (size_t i = 0; i < len; i++) {
        const int v = static_cast<int>(data[i]);
        if (v < 16) {
            oss << '0';
        }
        oss << v;
        if (i + 1 < len) {
            oss << ' ';
        }
    }
    return oss.str();
}
