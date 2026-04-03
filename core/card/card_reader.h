/**
 * Card reader serial driver
 * BY ZF
 */

#ifndef TCU_CARD_READER_H
#define TCU_CARD_READER_H

#include "../base/communication/serial_communication.h"
#include "card_protocol.h"
#include <string>

class CardReader {
public:
    CardReader();
    ~CardReader();

    bool open(const CardSerialConfig& config);
    void close();
    bool isOpen() const;
    void setDebug(bool enable);

    // BY ZF: execute returns true when transport/frame exchange completed.
    bool execute(const ReaderCommand& cmd, ReaderResult& out, std::string& err);

private:
    SerialCommunication m_serial;
    CardSerialConfig m_cfg;
    bool m_debug;
};

#endif // TCU_CARD_READER_H
