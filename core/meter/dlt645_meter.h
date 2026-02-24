/**
 * DLT645 meter driver (RS485)
 * BY ZF
 */

#ifndef TCU_METER_DLT645_METER_H
#define TCU_METER_DLT645_METER_H

#include "../base/communication/serial_communication.h"
#include <string>
#include <vector>
#include <stdint.h>

struct MeterSerialConfig {
    std::string device;   // 串口设备路径
    uint32_t baudrate;    // 波特率
    uint8_t dataBits;     // 数据位
    uint8_t stopBits;     // 停止位
    char parity;          // 校验位
    int readTimeoutMs;    // 单次读表超时

    MeterSerialConfig()
        : device("/dev/ttyS1")
        , baudrate(9600)
        , dataBits(8)
        , stopBits(1)
        , parity('E')
        , readTimeoutMs(200)
    {}
};

struct MeterReading {
    double totalEnergy; // kWh
    double voltage;     // V
    double current;     // A

    MeterReading()
        : totalEnergy(0.0)
        , voltage(0.0)
        , current(0.0)
    {}
};

class Dlt645Meter {
public:
    Dlt645Meter();
    ~Dlt645Meter();

    bool open(const MeterSerialConfig& config);
    void close();
    bool isOpen() const;
    void setDebug(bool enable);

    // BY ZF: 顺序读取总电量/电压/电流，任一失败返回 false
    bool readAll(const std::string& meterAddr, MeterReading& out, std::string& err);

private:
    static std::string hexDump(const uint8_t* data, size_t len);
    // BY ZF: 读单个 DI，并按 decimals 处理小数位
    bool readOne(const std::string& meterAddr, uint32_t di, int decimals, double& out, std::string& err);
    bool requestRead(const std::vector<uint8_t>& addrLe, uint32_t di, std::vector<uint8_t>& response, std::string& err);

    // BY ZF: 地址输入形如 "123456789012" 或带分隔符，内部转成 645 所需低位在前格式
    static bool parseAddress(const std::string& addrText, std::vector<uint8_t>& outAddrLe);
    static std::vector<uint8_t> buildReadFrame(const std::vector<uint8_t>& addrLe, uint32_t di);
    static bool tryExtractFrame(std::vector<uint8_t>& buffer, std::vector<uint8_t>& frame);
    static bool decodeReadResponse(const std::vector<uint8_t>& frame, uint32_t expectedDi, int decimals, double& out);
    static uint8_t checksum(const uint8_t* data, size_t len);
    static double decodeBcdLe(const uint8_t* data, size_t len, int decimals);

private:
    SerialCommunication m_serial;
    MeterSerialConfig m_cfg;
    bool m_debug;
};

#endif // TCU_METER_DLT645_METER_H
