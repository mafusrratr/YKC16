#include "zk/xiaojusdk/include/xiaoju_phm.h"
#include "zk/xiaojusdk/porting/linux/xiaoju_pal.h"

uint8_t *g_phm_data_ = 0;
uint32_t g_phm_size_ = 0;
xj_pal_mutex_t g_phm_lock;

void xj_phm_lock()
{
    XJ_PAL_MUTEX_LOCK(&g_phm_lock);
}
void xj_phm_unlock()
{
    XJ_PAL_MUTEX_UNLOCK(&g_phm_lock);
}
/// 需要初始值的在此添加
void xj_phm_default()
{
    xj_phm_write_word(kXjPhmParamTag, 0x55AA);
    xj_phm_write_byte(kXjPhmParamHangZhouTspSwitch, 0);
    xj_phm_write_byte(kXjPhmParamVirtualPowerShowSwitch, 0);
    xj_phm_write_byte(kXjPhmParamVinSuppostSwitch, 0);
    xj_phm_write_byte(kXjPhmParamCardReaderSuppostSwitch, 0);
    xj_phm_write_byte(kXjPhmParamOutVolOverSwitch, 1);
    xj_phm_write_byte(kXjPhmParamBsmFaultStopSwitch, 1);
    xj_phm_write_byte(kXjPhmParamElectLockCheckSwitch, 1);
    xj_phm_write_byte(kXjPhmParamEntranceGuardCheck, 1);
    xj_phm_write_byte(kXjPhmParamAuxPowerSelect, 0);
    xj_phm_write_byte(kXjPhmParamBhmTimeoutTime, 5);
    xj_phm_write_byte(kXjPhmParamBrmTimeoutTime, 5);
    xj_phm_write_byte(kXjPhmParamBcpTimeoutTime, 5);
    xj_phm_write_byte(kXjPhmParamBroSet0x00Time, 5);
    xj_phm_write_byte(kXjPhmParamBroSet0xAATime, 60);
    xj_phm_write_byte(kXjPhmParamBclTimeoutTime, 1);
    xj_phm_write_byte(kXjPhmParamBcsTimeoutTime, 5);
    xj_phm_write_byte(kXjPhmParamBsmTimeoutTime, 5);
    xj_phm_write_byte(kXjPhmParamOutVolOverValue, 10);
    xj_phm_write_byte(kXjPhmParamStartChargeTimeout, 90);
    xj_phm_write_byte(kXjPhmParamRebootSwitch, 0);
    xj_phm_write_byte(kXjPhmParamKwhCtrlSwitch, 0);
//    xj_phm_write_byte(kXjPhmParamCardSwitch, 0);
    xj_phm_write_byte(kXjPhmParamCoChargeSwitch, 0);
    xj_phm_write_byte(kXjPhmParamOffLineSwitch, 0);
}

xj_bool xj_phm_init(uint8_t *data, uint32_t len)
{
//    xj_bool result = xj_bool_false;
    g_phm_data_ = data;
//    g_phm_size_ = len;
//    if (xj_pal_read_phm(g_phm_data_, len) == 0 && xj_phm_read_word(kXjPhmParamTag) == 0x55AA)
//    {
//        XJ_PAL_MUTEX_INIT(&g_phm_lock);
//        result = xj_bool_true;
//    }
//    /// 如果读取失败则写入
//    else
//    {
        xj_phm_default();
//        if (xj_pal_write_phm(g_phm_data_, len) == 0)
//            result = xj_bool_true;
//    }
    return xj_bool_true;
}


xj_bool xj_phm_write_byte(TXjPhmparam id, uint8_t value)
{
    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        g_phm_data_[id] = value;
        xj_phm_unlock();
        
        return xj_bool_true;
    }
    return xj_bool_false;
}

xj_bool xj_phm_write_word(TXjPhmparam id, uint16_t value)
{
    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        *(uint16_t*)&g_phm_data_[id] = value;
        xj_phm_unlock();
        return xj_bool_true;
    }
    return xj_bool_false;
}

xj_bool xj_phm_write_dword(TXjPhmparam id, uint32_t value)
{
    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        *(uint32_t*)&g_phm_data_[id] = value;
        xj_phm_unlock();
        return xj_bool_true;
    }
    return xj_bool_false;
}

uint32_t xj_phm_write_buffer(TXjPhmparam id, uint8_t *data, uint32_t len)
{
    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        memcpy(&g_phm_data_[id], data, len);
        xj_phm_unlock();
        return len;
    }
    return 0;
}

xj_bool xj_phm_write_file()
{
    return xj_pal_write_phm(g_phm_data_, g_phm_size_) == 0;
}


uint8_t xj_phm_read_byte(TXjPhmparam id)
{
    uint8_t result;
    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        result = g_phm_data_[id];
        xj_phm_unlock();
        return result;
    }
    return 0;
}


uint16_t xj_phm_read_word(TXjPhmparam id)
{
    uint16_t result;

    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        result = *(uint16_t*)&g_phm_data_[id];
        xj_phm_unlock();
        return result;
    }
    return 0;
}

uint32_t xj_phm_read_dword(TXjPhmparam id)
{
    uint32_t result;
    
    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        result = *(uint32_t*)&g_phm_data_[id];
        xj_phm_unlock();
        return result;
    }
    return 0;
}

uint32_t xj_phm_read_buffer(TXjPhmparam id, uint8_t *buffer, uint32_t size)
{
    if (id < kXjPhmParamMax)
    {
        xj_phm_lock();
        memcpy(buffer, &g_phm_data_[id], size);
        xj_phm_unlock();
        return size;
    }
    return 0;
}
