/**
 * Card reader protocol definition
 * BY ZF
 */

#ifndef TCU_CARD_PROTOCOL_H
#define TCU_CARD_PROTOCOL_H

#include <stdint.h>
#include <string>
#include <vector>

struct CardSerialConfig {
    std::string device;
    uint32_t baudrate;
    uint8_t dataBits;
    uint8_t stopBits;
    char parity;
    int readTimeoutMs;

    CardSerialConfig()
        : device("/dev/ttyS5")
        , baudrate(57600)
        , dataBits(8)
        , stopBits(1)
        , parity('N')
        , readTimeoutMs(2000)
    {}
};

enum ReaderOpCode {
    READER_OP_NONE = 0,
    READER_OP_BUZZER,
    READER_OP_OPEN_RF,
    READER_OP_CLOSE_RF,
    READER_OP_ACTIVE_CARD,
    READER_OP_AUTH_M1,
    READER_OP_READ_BLOCK,
    READER_OP_WRITE_BLOCK
};

enum ReaderParseStatus {
    READER_PARSE_OK = 0,
    READER_PARSE_INCOMPLETE,
    READER_PARSE_BAD_STX,
    READER_PARSE_BAD_LENGTH,
    READER_PARSE_BAD_XOR,
    READER_PARSE_BAD_ETX
};

enum ReaderResultCode {
    READER_RESULT_UNKNOWN = 0x0000,
    READER_RESULT_BUZZER_DRIVED = 0x0009,
    READER_RESULT_RF_OPENED = 0x000A,
    READER_RESULT_RF_CLOSED = 0x000B,
    READER_RESULT_CARD_ACTIVED = 0x000C,
    READER_RESULT_M1_CARD_S0_CERTIFIED = 0x000D,
    READER_RESULT_BLOCK1_DATA_READED = 0x000E,
    READER_RESULT_M1_CARD_S4_CERTIFIED_R = 0x000F,
    READER_RESULT_M1_CARD_S4_CERTIFIED_W = 0x0010,
    READER_RESULT_BLOCK16_DATA_READED = 0x0011,
    READER_RESULT_BLOCK16_DATA_WRITED = 0x0012,
    READER_RESULT_M1_CARD_S8_CERTIFIED_R = 0x0013,
    READER_RESULT_M1_CARD_S8_CERTIFIED_W = 0x0014,
    READER_RESULT_BLOCK32_DATA_READED = 0x0015,
    READER_RESULT_BLOCK32_DATA_WRITED = 0x0016,
    READER_RESULT_ERROR = 0xFFFF
};

struct ReaderCommand {
    ReaderOpCode op;
    uint16_t activeTimeoutMs;
    uint16_t buzzerOnMs;
    uint8_t buzzerRepeat;

    uint8_t keyType;
    uint8_t blockNo;
    bool authForWrite;
    std::vector<uint8_t> key;
    std::vector<uint8_t> uid;
    std::vector<uint8_t> blockData;

    ReaderCommand()
        : op(READER_OP_NONE)
        , activeTimeoutMs(0)
        , buzzerOnMs(500)
        , buzzerRepeat(1)
        , keyType(0x60)
        , blockNo(0)
        , authForWrite(false)
    {}
};

struct ReaderFrame {
    uint16_t statusCode;
    std::vector<uint8_t> data;

    ReaderFrame()
        : statusCode(0)
    {}
};

struct ReaderResult {
    ReaderOpCode op;
    uint16_t resultCode;
    uint16_t deviceStatus;
    bool success;
    std::vector<uint8_t> data;

    ReaderResult()
        : op(READER_OP_NONE)
        , resultCode(READER_RESULT_UNKNOWN)
        , deviceStatus(0)
        , success(false)
    {}
};

class CardReaderProtocol {
public:
    static bool buildRequest(const ReaderCommand& cmd, std::vector<uint8_t>& out, std::string& err);
    static bool decodeResponse(const std::vector<uint8_t>& frame, ReaderFrame& out, std::string& err);
    static bool tryExtractFrame(std::vector<uint8_t>& buffer,
                                std::vector<uint8_t>& frame,
                                ReaderParseStatus& parseStatus,
                                std::string& err);
    static uint16_t mapSuccessResult(const ReaderCommand& cmd);
    static std::string opToString(ReaderOpCode op);
    static bool parseHex(const std::string& text, std::vector<uint8_t>& out);
    static std::string hexDump(const uint8_t* data, size_t len);
    static std::string hexDump(const std::vector<uint8_t>& data);

private:
    static uint8_t xorChecksum(const uint8_t* data, size_t len);
};

#endif // TCU_CARD_PROTOCOL_H
