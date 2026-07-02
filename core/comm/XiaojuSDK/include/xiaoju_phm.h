/********************************************************
  使用说明:PHM定义为掉电存储参数管理,实现为分配一个内存空间和等大小文件空间,通过地址枚举可以
          方便设置和读取每个地址位区域，用于少字节线程间通讯
 *******************************************************/
#ifndef   _XIAOJU_PHM_H_
#define   _XIAOJU_PHM_H_

#include "zk/xiaojusdk/include/xiaoju_struct.h"
#include "zk/xiaojusdk/include/xiaoju_error.h"
#include "stdint.h"

/********************************************************
  * @Description：掉电存储参数地址枚举值
  * @Note:枚举定义根据地址递增,下一个参数地址为上一个参数地址+上一个参数长度
  * @Note:kXjPhmParamMax为文件和内存大小,需要4字节对齐,小桔参数占用100字节以内，用户如用可以自行增加后使用100字节以后区域
  * @Note:数据有变更需要更新到文件需要调用xj_phm_write_file()同步，不然数据只更新到内存中
 *******************************************************/
typedef enum
{
    kXjPhmParamTag                      = 0,        // 起始标记0x55AA，   **字节长度:2
    kXjPhmParamHangZhouTspSwitch        = 2,        // 杭州平台开关 默认(0) 0:关闭 1:打开 0xff无效     **字节长度:1
    kXjPhmParamVirtualPowerShowSwitch   = 3,        // 虚拟电量开关 默认(0) 0:关闭 1:打开 0xff无效      **字节长度:1
    kXjPhmParamVinSuppostSwitch         = 4,        // 即插即充(vin码)支持充电开关 默认(0) 0:关闭 1:打开 0xff无效 **字节长度:1
    kXjPhmParamCardReaderSuppostSwitch  = 5,        // 刷卡充电支持开关 默认(0) 0:关闭 1:打开 0xff无效    **字节长度:1
    kXjPhmParamOutVolOverSwitch         = 6,        // 外侧电压大于10v功能开关 默认(1) 0:关闭 1:打开 0xff无效    **字节长度:1
    kXjPhmParamBsmFaultStopSwitch       = 7,        // bms错误停止充电开关 默认(1) 0:关闭 1:打开 0xff无效    **字节长度:1
    kXjPhmParamElectLockCheckSwitch     = 8,        // 电子锁故障检查开关 默认(1) 0:关闭 1:打开 0xff无效    **字节长度:1
    kXjPhmParamEntranceGuardCheck       = 9,        // 门禁检查开关 默认(1) 0:关闭 1:打开 0xff无效    **字节长度:1
    kXjPhmParamAuxPowerSelect           = 10,       // 辅助电源选择 默认(0) 0:关闭 1:打开 0xff无效    **字节长度:1
    kXjPhmParamBhmTimeoutTime           = 11,       // bhm报文超时时间 单位秒(默认5,范围5-15)   **字节长度:1
    kXjPhmParamBrmTimeoutTime           = 12,       // brm报文超时时间 单位秒(默认5,范围5-15)   **字节长度:1
    kXjPhmParamBcpTimeoutTime           = 13,       // bcp报文超时时间 单位秒(默认5,范围5-15)    **字节长度:1
    kXjPhmParamBroSet0x00Time           = 14,       // bro=0x00报文超时时间 单位秒(默认5,范围5-15)    **字节长度:1
    kXjPhmParamBroSet0xAATime           = 15,       // bro=0xaa报文超时时间 单位秒(默认60,范围60-90)    **字节长度:1
    kXjPhmParamBclTimeoutTime           = 16,       // bcl报文超时时间 单位秒(默认1,范围1-5)    **字节长度:1
    kXjPhmParamBcsTimeoutTime           = 17,       // bcs报文超时时间 单位秒(默认5,范围5-15)     **字节长度:1
    kXjPhmParamBsmTimeoutTime           = 18,       // bsm报文超时时间 单位秒(默认5,范围5-15)     **字节长度:1
    kXjPhmParamOutVolOverValue          = 19,       // 外侧电压大于阈值 单位V(默认10范围10-60)    **字节长度:1
    kXjPhmParamStartChargeTimeout       = 20,       // 启动充电超时时间 单位秒(默认90,范围90-120) **字节长度:1
	kXjPhmParamRebootSwitch				= 21,		// 重启功能开关 默认(0) 0:关闭 1:打开 0xff无效     **字节长度:1
	kXjPhmParamKwhCtrlSwitch			= 22,		// 功率控制功能开关 默认(0) 0:关闭 1:打开 0xff无效     **字节长度:1
//	kXjPhmParamCardSwitch				= 23,		// 刷卡充电功能开关 默认(0) 0:关闭 1:打开 0xff无效     **字节长度:1
	kXjPhmParamCoChargeSwitch			= 23,		// 双枪并充功能 默认(0) 0:关闭 1:打开 0xff无效     **字节长度:1
	kXjPhmParamOffLineSwitch			= 24,		// 离线充电功能开关 默认(0) 0:关闭 1:打开 0xff无效     **字节长度:1
	// 小桔参数占用100字节以内，用户如用可以自行增加后使用100字节以后区域
    kXjPhmParamMax                      = 100,      // 小桔参数占用100字节以内，用户如用可以自行增加后使用100字节以后区域

}TXjPhmparam;

/********************************************************
  * @Description：初始化
  * @Arguments	：
			    data:内存区分配
                len:内存区大小(同保存文件大小)
  * @Returns	：
                xj_bool_true:成功 xj_bool_false:失败
 *******************************************************/
xj_bool xj_phm_init(uint8_t *data, uint32_t len);

/********************************************************
  * @Description：phm单字节写入
  * @Arguments	：
			    id:地址枚举值
                value:写入值
  * @Returns	：
                xj_bool_true:成功 xj_bool_false:失败
 *******************************************************/
xj_bool xj_phm_write_byte(TXjPhmparam id, uint8_t value);
/********************************************************
  * @Description：phm双字节写入
  * @Arguments	：
			    id:地址枚举值
                value:写入值
  * @Returns	：
                xj_bool_true:成功 xj_bool_false:失败
 *******************************************************/
xj_bool xj_phm_write_word(TXjPhmparam id, uint16_t value);
/********************************************************
  * @Description：phm4字节写入
  * @Arguments	：
			    id:地址枚举值
                value:写入值
  * @Returns	：
                xj_bool_true:成功 xj_bool_false:失败
 *******************************************************/
xj_bool xj_phm_write_dword(TXjPhmparam id, uint32_t value);
/********************************************************
  * @Description：phm多字节写入
  * @Arguments	：
			    id:地址枚举值
                data:写入数据地址
                len:写入长度
  * @Returns	：
                返回写入成功的字节数
 *******************************************************/
uint32_t xj_phm_write_buffer(TXjPhmparam id, uint8_t *data, uint32_t len);
/********************************************************
  * @Description：保存到文件(需要调用才会同步到文件)
  * @Arguments	：
			    无
  * @Returns	：
                xj_bool_true:成功 xj_bool_false:失败
 *******************************************************/
xj_bool xj_phm_write_file();
/********************************************************
  * @Description：phm单字节读取
  * @Arguments	：
			    id:地址枚举值
  * @Returns	：
                读取到的值
 *******************************************************/
uint8_t xj_phm_read_byte(TXjPhmparam id);
/********************************************************
  * @Description：phm双字节读取
  * @Arguments	：
			    id:地址枚举值
  * @Returns	：
                读取到的值
 *******************************************************/
uint16_t xj_phm_read_word(TXjPhmparam id);
/********************************************************
  * @Description：phm 4字节读取
  * @Arguments	：
			    id:地址枚举值
  * @Returns	：
                读取到的值
 *******************************************************/
uint32_t xj_phm_read_dword(TXjPhmparam id);
/********************************************************
  * @Description：phm多字节读取
  * @Arguments	：
			    id:地址枚举值
                buffer:缓冲区
                size:读取大小
  * @Returns	：
                返回读取成功的字节数
 *******************************************************/
uint32_t xj_phm_read_buffer(TXjPhmparam id, uint8_t *buffer, uint32_t size);

#endif
