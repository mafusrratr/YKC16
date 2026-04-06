/**
 * Pile controller interface for SHM2CCU.
 * BY ZF
 */

#ifndef SHM2CCU_IPILE_CONTROLLER_H
#define SHM2CCU_IPILE_CONTROLLER_H

#include <stdint.h>

struct PileStatus {
    uint8_t gunNo;
    uint16_t outputVoltage;
    int16_t outputCurrent;
    uint32_t outputPower;
    uint32_t cumulativeEnergy;
    uint8_t workStatus;
    uint16_t faultCode;
    uint8_t soc;
    uint16_t temperature;
};

class IPileController {
public:
    virtual ~IPileController() {}
    virtual bool initialize(const char* commType, const char* config) = 0;
    virtual void cleanup() = 0;
    virtual int startCharge() = 0;
    virtual int stopCharge() = 0;
    virtual int getStatus(uint8_t gunNo, PileStatus* status) = 0;
};

#endif  // SHM2CCU_IPILE_CONTROLLER_H
