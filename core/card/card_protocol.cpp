/**
 * Card reader protocol implementation
 * BY ZF
 */

#include "card_protocol.h"
#include <sstream>
#include <iomanip>
#include <cctype>

namespace {
    static const uint8_t kFrameStx = 0x02;
    static const uint8_t kFrameEtx = 0x03;

    static const uint16_t kCodeBuzzControl = 0x3113;
    static const uint16_t kCodeOpenRf = 0x3190;
    static const uint16_t kCodeCloseRf = 0x3191;
    static const uint16_t kCodeActiveCard = 0x3224;
    static const uint16_t kCodeAuthM1 = 0xC202;
    static const uint16_t kCodeReadBlock = 0xC203;
    static const uint16_t kCodeWriteBlock = 0xC204;

    static void appendU16(std::vector<uint8_t>& out, uint16_t value)
    {
        out.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }
}

uint8_t CardReaderProtocol::xorChecksum(const uint8_t* data, size_t len)
{
    uint8_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum ^= data[i];
    }
    return sum;
}

bool CardReaderProtocol::buildRequest(const ReaderCommand& cmd, std::vector<uint8_t>& out, std::string& err)
{
    std::vector<uint8_t> payload;
    uint16_t code = 0;

    switch (cmd.op) {
        case READER_OP_BUZZER:
            code = kCodeBuzzControl;
            appendU16(payload, code);
            appendU16(payload, cmd.buzzerOnMs);
            payload.push_back(cmd.buzzerRepeat);
            break;
        case READER_OP_OPEN_RF:
            code = kCodeOpenRf;
            appendU16(payload, code);
            break;
        case READER_OP_CLOSE_RF:
            code = kCodeCloseRf;
            appendU16(payload, code);
            break;
        case READER_OP_ACTIVE_CARD:
            code = kCodeActiveCard;
            appendU16(payload, code);
            appendU16(payload, cmd.activeTimeoutMs);
            break;
        case READER_OP_AUTH_M1:
            if (cmd.key.size() != 6 || cmd.uid.size() != 4) {
                err = "auth_m1 requires key(6) and uid(4)";
                return false;
            }
            code = kCodeAuthM1;
            appendU16(payload, code);
            payload.push_back(cmd.keyType);
            payload.push_back(cmd.blockNo);
            payload.insert(payload.end(), cmd.key.begin(), cmd.key.end());
            payload.insert(payload.end(), cmd.uid.begin(), cmd.uid.end());
            break;
        case READER_OP_READ_BLOCK:
            code = kCodeReadBlock;
            appendU16(payload, code);
            payload.push_back(cmd.blockNo);
            break;
        case READER_OP_WRITE_BLOCK:
            if (cmd.blockData.size() != 16) {
                err = "write_block requires data(16)";
                return false;
            }
            code = kCodeWriteBlock;
            appendU16(payload, code);
            payload.push_back(cmd.blockNo);
            payload.insert(payload.end(), cmd.blockData.begin(), cmd.blockData.end());
            break;
        case READER_OP_NONE:
        default:
            err = "unsupported reader op";
            return false;
    }

    out.clear();
    out.reserve(payload.size() + 5);
    out.push_back(kFrameStx);
    appendU16(out, static_cast<uint16_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(xorChecksum(payload.data(), payload.size()));
    out.push_back(kFrameEtx);
    return true;
}

bool CardReaderProtocol::decodeResponse(const std::vector<uint8_t>& frame, ReaderFrame& out, std::string& err)
{
    if (frame.size() < 7) {
        err = "frame_too_short";
        return false;
    }
    if (frame[0] != kFrameStx) {
        err = "bad_stx";
        return false;
    }
    const uint16_t payloadLen = static_cast<uint16_t>((frame[1] << 8) | frame[2]);
    if (static_cast<size_t>(payloadLen) + 5 != frame.size()) {
        err = "bad_length";
        return false;
    }
    if (frame.back() != kFrameEtx) {
        err = "bad_etx";
        return false;
    }
    const uint8_t expectXor = xorChecksum(&frame[3], payloadLen);
    if (frame[3 + payloadLen] != expectXor) {
        err = "bad_xor";
        return false;
    }
    if (payloadLen < 2) {
        err = "payload_too_short";
        return false;
    }

    out.statusCode = static_cast<uint16_t>((frame[3] << 8) | frame[4]);
    out.data.assign(frame.begin() + 5, frame.begin() + 3 + payloadLen);
    return true;
}

bool CardReaderProtocol::tryExtractFrame(std::vector<uint8_t>& buffer,
                                         std::vector<uint8_t>& frame,
                                         ReaderParseStatus& parseStatus,
                                         std::string& err)
{
    parseStatus = READER_PARSE_INCOMPLETE;
    err.clear();

    while (!buffer.empty() && buffer.front() != kFrameStx) {
        buffer.erase(buffer.begin());
    }
    if (buffer.empty()) {
        return false;
    }
    if (buffer[0] != kFrameStx) {
        parseStatus = READER_PARSE_BAD_STX;
        err = "bad_stx";
        buffer.erase(buffer.begin());
        return false;
    }
    if (buffer.size() < 3) {
        return false;
    }

    const uint16_t payloadLen = static_cast<uint16_t>((buffer[1] << 8) | buffer[2]);
    if (payloadLen == 0 || payloadLen > 512) {
        parseStatus = READER_PARSE_BAD_LENGTH;
        err = "bad_length";
        buffer.erase(buffer.begin());
        return false;
    }

    const size_t totalLen = static_cast<size_t>(payloadLen) + 5;
    if (buffer.size() < totalLen) {
        return false;
    }

    if (buffer[totalLen - 1] != kFrameEtx) {
        parseStatus = READER_PARSE_BAD_ETX;
        err = "bad_etx";
        buffer.erase(buffer.begin(), buffer.begin() + totalLen);
        return false;
    }

    const uint8_t expectXor = xorChecksum(&buffer[3], payloadLen);
    if (buffer[3 + payloadLen] != expectXor) {
        parseStatus = READER_PARSE_BAD_XOR;
        err = "bad_xor";
        buffer.erase(buffer.begin(), buffer.begin() + totalLen);
        return false;
    }

    frame.assign(buffer.begin(), buffer.begin() + totalLen);
    buffer.erase(buffer.begin(), buffer.begin() + totalLen);
    parseStatus = READER_PARSE_OK;
    return true;
}

uint16_t CardReaderProtocol::mapSuccessResult(const ReaderCommand& cmd)
{
    switch (cmd.op) {
        case READER_OP_BUZZER:
            return READER_RESULT_BUZZER_DRIVED;
        case READER_OP_OPEN_RF:
            return READER_RESULT_RF_OPENED;
        case READER_OP_CLOSE_RF:
            return READER_RESULT_RF_CLOSED;
        case READER_OP_ACTIVE_CARD:
            return READER_RESULT_CARD_ACTIVED;
        case READER_OP_AUTH_M1:
            if (cmd.blockNo < 4) {
                return READER_RESULT_M1_CARD_S0_CERTIFIED;
            }
            if (cmd.blockNo < 8) {
                return cmd.authForWrite ? READER_RESULT_M1_CARD_S4_CERTIFIED_W
                                        : READER_RESULT_M1_CARD_S4_CERTIFIED_R;
            }
            if (cmd.blockNo < 12) {
                return cmd.authForWrite ? READER_RESULT_M1_CARD_S8_CERTIFIED_W
                                        : READER_RESULT_M1_CARD_S8_CERTIFIED_R;
            }
            return READER_RESULT_UNKNOWN;
        case READER_OP_READ_BLOCK:
            if (cmd.blockNo == 1) {
                return READER_RESULT_BLOCK1_DATA_READED;
            }
            if (cmd.blockNo == 16) {
                return READER_RESULT_BLOCK16_DATA_READED;
            }
            if (cmd.blockNo == 32) {
                return READER_RESULT_BLOCK32_DATA_READED;
            }
            return READER_RESULT_UNKNOWN
        case READER_OP_WRITE_BLOCK:
            if (cmd.blockNo == 16) {
                return READER_RESULT_BLOCK16_DATA_WRITED;
            }
            if (cmd.blockNo == 32) {
                return READER_RESULT_BLOCK32_DATA_WRITED;
            }
            return READER_RESULT_UNKNOWN;
        case READER_OP_NONE:
        default:
            return READER_RESULT_UNKNOWN;
    }
}

std::string CardReaderProtocol::opToString(ReaderOpCode op)
{
    switch (op) {
        case READER_OP_BUZZER: return "buzzer";
        case READER_OP_OPEN_RF: return "open_rf";
        case READER_OP_CLOSE_RF: return "close_rf";
        case READER_OP_ACTIVE_CARD: return "active_card";
        case READER_OP_AUTH_M1: return "auth_m1";
        case READER_OP_READ_BLOCK: return "read_block";
        case READER_OP_WRITE_BLOCK: return "write_block";
        case READER_OP_NONE:
        default:
            return "unknown";
    }
}

bool CardReaderProtocol::parseHex(const std::string& text, std::vector<uint8_t>& out)
{
    std::string compact;
    compact.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char ch = static_cast<unsigned char>(text[i]);
        if (std::isxdigit(ch)) {
            compact.push_back(static_cast<char>(ch));
        }
    }
    if (compact.empty() || (compact.size() % 2) != 0) {
        return false;
    }

    out.clear();
    out.reserve(compact.size() / 2);
    for (size_t i = 0; i < compact.size(); i += 2) {
        unsigned int value = 0;
        std::istringstream iss(compact.substr(i, 2));
        iss >> std::hex >> value;
        if (iss.fail()) {
            out.clear();
            return false;
        }
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    }
    return true;
}

std::string CardReaderProtocol::hexDump(const uint8_t* data, size_t len)
{
    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<unsigned int>(data[i]);
    }
    return oss.str();
}

std::string CardReaderProtocol::hexDump(const std::vector<uint8_t>& data)
{
    if (data.empty()) {
        return "";
    }
    return hexDump(&data[0], data.size());
}
