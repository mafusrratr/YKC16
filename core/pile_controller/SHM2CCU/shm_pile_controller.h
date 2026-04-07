/**
 * Shared-memory pile controller adapter.
 * BY ZF
 */

#ifndef SHM2CCU_SHM_PILE_CONTROLLER_H
#define SHM2CCU_SHM_PILE_CONTROLLER_H

#include "ipile_controller.h"
#include "tcu2ccu_data.h"
#include "shm_point_map.h"
#include "libv2gshm/cshm/v2gshm.h"
#include <string>
#include <stdint.h>

class SHMPileController : public IPileController {
public:
    SHMPileController();
    virtual ~SHMPileController();

    bool initialize(const char* commType, const char* config) override;
    void cleanup() override;
    int startCharge() override;
    int stopCharge() override;
    int getStatus(uint8_t gunNo, PileStatus* status) override;

    void setStartChargeData(const TCU2CCU_CmdStartChargeData* data);
    void setStopChargeData(const TCU2CCU_CmdStopChargeData* data);
    void setPowerAdjustData(const TCU2CCU_CmdPowerAdjustData* data);
    int powerAdjust();

    bool getYC20Data(TCU2CCU_DataYC20* data) const;
    bool getYX22Data(TCU2CCU_DataYX22* data) const;
    bool getYX23Data(TCU2CCU_DataYX23* data) const;
    bool getStartCompleteData(TCU2CCU_StatusStartCompleteData* data) const;
    bool getStopCompleteData(TCU2CCU_StatusStopCompleteData* data) const;
    unsigned int getYcRawValueByBase(int baseIndex) const;

    bool hasStartCompleteEvent() const;
    bool hasStopCompleteEvent() const;
    bool hasPowerCtrlResponseEvent() const;

    void clearStartCompleteEvent();
    void clearStopCompleteEvent();
    void clearPowerCtrlResponseEvent();

    uint8_t getWorkStatus() const;
    uint8_t getTotalFault() const;
    uint8_t getTotalAlarm() const;
    uint8_t getGunNo() const { return m_gunNo; }
    int getGunIndex() const { return m_gunIndex; }

private:
    SHMPileController(const SHMPileController&);
    SHMPileController& operator=(const SHMPileController&);

    bool parseConfig(const char* config);
    int yxIndex(int baseIndex) const;
    int ycIndex(int baseIndex) const;
    YX* getYxPoint(int baseIndex) const;
    YC* getYcPoint(int baseIndex) const;
    const char* getYxDesname(int baseIndex) const;
    const char* getYcDesname(int baseIndex) const;
    bool getYxValue(int baseIndex) const;
    unsigned int getYcValue(int baseIndex) const;
    int16_t getYcSignedValue(int baseIndex) const;
    uint8_t getChargePortWorkStatus() const;
    void clearYxPoint(int baseIndex);
    void zeroMemory(void* ptr, size_t size) const;
    void copyAsciiBytes(uint8_t* dest, size_t len, const char* src) const;
    void copyAsciiChars(char* dest, size_t len, const char* src) const;
    void fillBatteryProdDate(uint8_t& year, uint8_t& month, uint8_t& day) const;

private:
    CShm* m_shm;
    uint8_t m_gunNo;
    int m_gunIndex;
    int m_shmKey;
    TCU2CCU_CmdStartChargeData m_startChargeData;
    TCU2CCU_CmdStopChargeData m_stopChargeData;
    TCU2CCU_CmdPowerAdjustData m_powerAdjustData;
};

#endif  // SHM2CCU_SHM_PILE_CONTROLLER_H
