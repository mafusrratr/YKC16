/**
 * Card reader serial driver implementation
 * BY ZF
 */

#include "card_reader.h"
#include <iostream>
#include <thread>
#include <chrono>

CardReader::CardReader()
    : m_debug(false)
{
}

CardReader::~CardReader()
{
    close();
}

bool CardReader::open(const CardSerialConfig& config)
{
    SerialParams params;
    params.baudrate = config.baudrate;
    params.dataBits = config.dataBits;
    params.stopBits = config.stopBits;
    params.parity = config.parity;
    params.flowControl = false;

    if (!m_serial.open(config.device, params)) {
        return false;
    }
    m_cfg = config;
    return true;
}

void CardReader::close()
{
    m_serial.close();
}

bool CardReader::isOpen() const
{
    return m_serial.isOpen();
}

void CardReader::setDebug(bool enable)
{
    m_debug = enable;
}

bool CardReader::execute(const ReaderCommand& cmd, ReaderResult& out, std::string& err)
{
    out = ReaderResult();
    out.op = cmd.op;

    if (!m_serial.isOpen()) {
        err = "serial_not_open";
        return false;
    }

    std::vector<uint8_t> request;
    if (!CardReaderProtocol::buildRequest(cmd, request, err)) {
        return false;
    }

    m_serial.flush();
    if (m_debug) {
        std::cout << "[Card][TX] op=" << CardReaderProtocol::opToString(cmd.op)
                  << " data=" << CardReaderProtocol::hexDump(request) << std::endl;
    }

    const int txBytes = m_serial.send(&request[0], request.size());
    if (txBytes != static_cast<int>(request.size())) {
        err = "serial_send_failed";
        return false;
    }

    std::vector<uint8_t> rxBuffer;
    rxBuffer.reserve(512);
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(m_cfg.readTimeoutMs);

    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t chunk[256];
        const int rxBytes = m_serial.receive(chunk, sizeof(chunk));
        if (rxBytes > 0) {
            rxBuffer.insert(rxBuffer.end(), chunk, chunk + rxBytes);
            std::vector<uint8_t> frame;
            ReaderParseStatus parseStatus = READER_PARSE_INCOMPLETE;
            if (CardReaderProtocol::tryExtractFrame(rxBuffer, frame, parseStatus, err)) {
                if (m_debug) {
                    std::cout << "[Card][RX] op=" << CardReaderProtocol::opToString(cmd.op)
                              << " data=" << CardReaderProtocol::hexDump(frame) << std::endl;
                }
                ReaderFrame reply;
                if (!CardReaderProtocol::decodeResponse(frame, reply, err)) {
                    return false;
                }
                out.deviceStatus = reply.statusCode;
                out.data = reply.data;
                out.success = (reply.statusCode == 0);
                out.resultCode = out.success ? CardReaderProtocol::mapSuccessResult(cmd)
                                             : READER_RESULT_ERROR;
                return true;
            }
            if (parseStatus != READER_PARSE_INCOMPLETE) {
                return false;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    err = "serial_recv_timeout";
    return false;
}
