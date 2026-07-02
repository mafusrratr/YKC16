#ifndef __XIAOJU_PAL_H__
#define __XIAOJU_PAL_H__

#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <stdarg.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "zk/xiaojusdk/include/redefine.h"
#include "zk/xiaojusdk/include/xiaoju_struct.h"



/*******************************************
任务优先级:
用户根据实际使用的操作系统和实际情况修改对应优先级
如 FreeRTOS中，数字越大，优先级越高，从最低优先级0 到最高优先级(configMAX_PRIORITIES – 1)
   UCOS中。数字越小，优先级越高
********************************************/
typedef enum
{
    xj_osPriorityIdle          = 0,          ///< priority: idle (lowest)
    xj_osPriorityLow           = 1,          ///< priority: low
    xj_osPriorityBelowNormal   = 2,          ///< priority: below normal
    xj_osPriorityNormal        = 3,          ///< priority: normal (default)
    xj_osPriorityAboveNormal   = 4,          ///< priority: above normal
    xj_osPriorityHigh          = 5,          ///< priority: high
    xj_osPriorityRealtime      = 6,          ///< priority: realtime (highest)
    xj_osPriorityError         =  0x84        ///< system cannot determine priority or thread has illegal priority
} Task_Priority;

/*程序堆栈设置参考值*/
#define XJ_OS_STACK_SIZE_LITTLE     128
#define XJ_OS_STACK_SIZE_MEDIUM     256
#define XJ_OS_STACK_SIZE_LARGE      512




/*控制日志是否输出*/
#define XJ_LOG_RECORD_SWITCH                    _LOG_RECORD_ON_// _LOG_RECORD_OFF_

/*协议起始域标识厂家*/
#define CHARGER_IDENTIFIER_HEADER               0xD07D

/* mqtt 发送缓存大小*/
#define MQTT_SNED_BUF_LEN                       1024

/* mqtt 接收缓存大小*/
#define MQTT_RECV_BUF_LEN                       1536

/* unused */
#define CHARGE_GUN_CNT                          2    /******/

/* 充电桩枪最大数量*/
#define CHARGE_GUN_CNT_MAX                      4

/* 默认枪个数*/
#define CHARGE_GUN_CNT_DEFAULT                  2

/* 最大保存账单数量,必须大于枪个数*/
#define MAX_HISTORY_ORDER_CNT	        (CHARGE_GUN_CNT_MAX * 2)

/* 枪类型 (默认直流)*/
#define XJ_GUN_TYPE                     XJ_GUN_TYPE_DC

/* 充电桩编码(默认可调接口设置) */
//#define XJ_EQUIPMENT_ID                 "TEST00001"
//#define XJ_EQUIPMENT_ID                 "7013030012108002"

#define XJ_EQUIPMENT_ID                 "0159300103000024"
/* 中心服务器地址 */
#define XJ_CENTER_SVR_ADDR              "unicron.didichuxing.com"

/* 中心服务器端口号*/
#define XJ_CENTER_SVR_PORT              1883

/* 逻辑服务器地址 (epower-equipment-server.xiaojukeji.com正式桩 116.85.2.54-1884 yufa)*/
//#define XJ_LOGIC_SVR_ADDR               "epower-equipment-server-test.xiaojukeji.com"
#define XJ_LOGIC_SVR_ADDR               "epower-equipment-server.xiaojukeji.com"
//#define XJ_LOGIC_SVR_ADDR               "10.190.4.172"
//#define XJ_LOGIC_SVR_ADDR               "10.96.88.3"

/* 逻辑服务器端口号*/
#define XJ_LOGIC_SVR_PORT               1884
//#define XJ_LOGIC_SVR_PORT               1663
//#define XJ_LOGIC_SVR_PORT               1663

/* mqtt用户名 (正式桩需要修改)*/
#define XJ_MQTT_USERNAME              "91320191608935509F" // "91110113MA01CF8F83"

//#define XJ_MQTT_USERNAME 					"91110112551433697H"
/* mqtt 密码 */
#define XJ_MQTT_PASSWORD            "MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCt380vsOSLs6RVtq8bHj+LSQZJfbF6YjUJwRU0P8R8X/pwjuXaM9rcouURS2B5/YFT7e1VmP7oMt3EPSDLdEqoiIqpUvrcz981lvd4oj8WhQpbv7E2rWN0TdwDN7rkC9gjnAuFWlqtkyLy10UFO5HXS8qsolexPSeLAG0C2VgFewIDAQAB"
//     "JvL8so96zyM6ppaTPfEe2JRt9lsnJ07EhT/oQhcCAyuE7Eyo5RoQ0MXBIXyyD13cNN2LqK3ViHLKCFbE/IkKXpeDfIMpCWt8niVn29Vpaf38gtVf0ne7RWPpHC4PlP+gIWLPRVUV1ei1RSeCWfJ4GtDJ0fuOuq7ij0gq/4BIiKU="//正式需修改

/* mac addr */
#define XJ_MAC_ADDR                     "000000"

/* 用户桩软件版本号 (自增迭代)*/
#define USER_VERSION                    0x00010000

/* 大小端 需配置(默认小端)*/
#define XJ_LITTLE_ENDIAN

/* 使用故障缓存 */
#define XJ_ERR_CACHE_USED

//MQTT 协议需要高字节在前
#if defined(XJ_LITTLE_ENDIAN)
#define XJ_PAL_HTONS(s)     BigLittleSwap16(s)
#define XJ_PAL_NTOHS(s)     BigLittleSwap16(s)
#define XJ_PAL_HTONL(s)     BigLittleSwap32(s)
#else
#define XJ_PAL_HTONS(s)     (s)
#define XJ_PAL_NTOHS(s)     (s)
#define XJ_PAL_HTONL(s)     (s)
#endif

/* type redefined */
typedef pthread_mutex_t xj_pal_mutex_t;//互斥信号结构
typedef int xj_pal_socket_handle;//网络连接控制块
typedef pthread_t xj_pal_thread_t;//任务控制块
typedef time_t xj_pal_time_t;//时间戳类型


/*
*@brief 创建任务
*/
int xj_pal_thread_create(char* threadName,xj_pal_thread_t *thread,void *(*start_routine)(void *arg),void *par,Task_Priority prio,uint32_t stack_size );

/*
*@brief 销毁任务
*/
void xj_pal_thread_cancel(xj_pal_thread_t* thread);

/*
*@brief 毫秒级sleep
*/
void xj_pal_msleep(uint32_t millsec);

/*
*@brief 创建互斥锁
*/
void Redefine_OSMutexCreate(xj_pal_mutex_t  * p_mutex );

/*
*@brief 获取互斥锁
*/
void Redefine_OSMutexPend(xj_pal_mutex_t*         p_mutex,uint32_t timeout);

/*
*@brief 释放互斥锁
*/
void Redefine_OSMutexPost(xj_pal_mutex_t*         p_mutex);

/*
*@brief 销毁互斥锁
*/
void Redefine_OSMutexDel(xj_pal_mutex_t*         p_mutex);

/*
*@brief 发送网络数据
*/
ssize_t xj_pal_sendall(xj_pal_socket_handle fd, const void* buf, size_t len, int flags);

/*
*@brief 接收网络数据
*/
ssize_t xj_pal_recvall(xj_pal_socket_handle fd, void* buf, size_t bufsz, int flags);

/*
*@brief 创建网络句柄
*/
xj_pal_socket_handle* xj_pal_open_nb_socket(const char* addr, const char* port);

/*
*@brief 关闭网络句柄
*/
void xj_pal_close_socket(xj_pal_socket_handle* socket);

/*
*@brief 获取当前时间-秒
*/
int xj_pal_get_int_sec(void);

/*
*@brief 获取当前时间-分
*/
int xj_pal_get_int_minute(void);

/*
*@brief 获取当前时间-小时(24制)
*/
int xj_pal_get_int_hour(void);

/*
*@brief 获取当前时间-日
*/
int xj_pal_get_int_day(void);

/*
*@brief 获取当前时间-月
*/
int xj_pal_get_int_month(void);

/*
*@brief 获取当前时间-年
*/
int xj_pal_get_int_year(void);

/*
*@brief 获取时间戳(例如从1970-1-1到现在的秒数)
*/
xj_pal_time_t xj_pal_time(void);

/*
*@brief 数据写入
*/
int8_t xj_pal_write_persist_params(char* input,int size);

/*
*@brief 数据读取
*/
int8_t xj_pal_read_persist_params(char* output,int limit);
/*
*@brief 通用保存数据写入
*/
int8_t xj_pal_write_phm(char* input,int size);
/*
*@brief 通用保存数据读取
*/
int8_t xj_pal_read_phm(char* output,int limit);

/*
*@brief 日志输出
*/
void User_log_export(const char*str,uint32_t len);

/* for mqtt */
#define XJ_PAL_MUTEX_INIT(mtx_ptr)      (Redefine_OSMutexCreate((mtx_ptr)))
#define XJ_PAL_MUTEX_LOCK(mtx_ptr)      (Redefine_OSMutexPend((mtx_ptr),100))
#define XJ_PAL_MUTEX_UNLOCK(mtx_ptr)    (Redefine_OSMutexPost((mtx_ptr)))
#define XJ_PAL_MUTEX_DESTORY(mtx_ptr)   (Redefine_OSMutexDel((mtx_ptr)))



#define MQTT_PAL_MUTEX_INIT(mtx_ptr) XJ_PAL_MUTEX_INIT(mtx_ptr)
#define MQTT_PAL_MUTEX_LOCK(mtx_ptr) XJ_PAL_MUTEX_LOCK(mtx_ptr)
#define MQTT_PAL_MUTEX_UNLOCK(mtx_ptr) XJ_PAL_MUTEX_UNLOCK(mtx_ptr)
#define MQTT_PAL_MUTEX_DESTORY(mtx_ptr) XJ_PAL_MUTEX_DESTORY(mtx_ptr)

#define MQTT_PAL_TIME() xj_pal_time()
#define MQTT_PAL_HTONS(s) XJ_PAL_HTONS(s)
#define MQTT_PAL_NTOHS(s) XJ_PAL_NTOHS(s)
#define mqtt_pal_recvall xj_pal_recvall
#define mqtt_pal_sendall xj_pal_sendall

typedef xj_pal_time_t mqtt_pal_time_t;
typedef xj_pal_mutex_t mqtt_pal_mutex_t;
typedef xj_pal_socket_handle mqtt_pal_socket_handle;


/********************************************************
  * @Description：用户实现 账单展示
  * @Arguments	：
			     bill_data:账单信息
			     gun_cnt：账单对应的枪号
			     0表示枪1 ,1表示枪2,以此类推
  * @Returns	：
                  成功返回0
                  失败返回其它
 *******************************************************/
extern int8_t callback_show_bill(xj_bill_info bill_data,uint8_t gun_cnt);

/********************************************************
  * @Description：用户实现 显示二维码
  * @Arguments	：
			    gun_cnt:要显示的枪号 ，必须小于枪总数
			    0表示枪1 ,1表示枪2,以此类推
			    qr_str: 二维码字符串
			    len：字符长度
			    err: 执行结果,用4字节ASCII表示，"0000"表示成功 "FFFF"表示失败
  * @Returns	：
                无
 *******************************************************/
extern void callback_display_qr(uint8_t gun_cnt,uint8_t*qr_str,uint16_t len,char* err);

/********************************************************
  * @Description：用户实现 vin码启动充电结果回调
  * @Arguments	：
			     result:参考注释
  * @Returns	：
                  成功返回0
                  失败返回其它
    @remark     ：SDK会在鉴权成功情况下对该回调的结果判断，返回0成功则进入自检状态，
                    用户需在完全启动充电后调用    xj_send_touch_charge_start   。
                  若鉴权失败或该回调返回其它，则保留原本状态
 *******************************************************/
extern int8_t callback_vin_start_charge_result(st_user_vin_send_result result);


/********************************************************
  * @Description：用户实现 启动充电 回调函数
  * @Arguments	：
			    gun_cnt:启动的枪号 ，必须小于枪总数
			    0表示枪1 ,1表示枪2,以此类推
			    err: 执行结果,用4字节ASCII表示，"0000"表示成功
			    id：订单号
			    id_len 订单号长度
			    user_tel 用户手机尾号
			    charge_policy:充电策略
			    	0:充满为止（默认）1:时间控制充电2:金额控制充电3:电量控制充电4:按SOC控制充电
			    charge_policy_param :充电策略参数
			    	时间单位为1秒 金额单位为0.01元 电量时单位为0.01kw SOC单位为%
  * @Returns	：
                无
 *******************************************************/
extern void callback_start_charge(uint8_t gun_cnt,char* err,char* id,uint8_t id_len,uint16_t user_tel,uint8_t charge_policy,uint32_t charge_policy_param);


/********************************************************
  * @Description：用户实现 停止充电 回调函数
  * @Arguments	：
			    gun_cnt:启动的枪号 ，必须小于枪总数
			    0表示枪1 ,1表示枪2,以此类推
			    err: 执行结果,用4字节ASCII表示，"0000"表示成功
  * @Returns	：
                无
 *******************************************************/
extern void callback_stop_charge(uint8_t gun_cnt, char* err);




/********************************************************
  * @Description：用户实现 设置系统时间
  * @Arguments	：
			    time:时间结构体
			    err: 执行结果,用4字节ASCII表示，"0000"表示成功 "FFFF"表示失败
  * @Returns	：
                无
 *******************************************************/
extern void callback_set_sys_time(xj_sync_system_time_param time,char* err);


/********************************************************
  * @Description：用户实现 软件下载  ，异步执行
  * @Arguments	：
			    param->upgrade_type 程序类型
                param->target_type 目标类型
                param->url：供下载的http url 字符串         最大长度128
                param->checksum:文件的MD5校验码   最大长度32
                param->serial_code:code 序列号域 回复1102时使用
  * @Returns	：
                  成功返回0
                  失败返回其它
 *******************************************************/
extern int8_t callback_software_download(xj_software_upgrade_param *param);



/********************************************************
  * @Description：用户实现 账单存储
  * @Arguments	：
			    bill:写入账单信息指针
			    size:数据大小
  * @Returns	：
                  成功返回0
                  失败返回其它
 *******************************************************/
extern int8_t callback_save_bill(char* bill,uint32_t size);


/********************************************************
  * @Description：用户实现 读取本地账单
  * @Arguments	：
			    bill:读取账单信息指针
			    len:账单信息大小
  * @Returns	：
                  成功返回0
                  失败返回其它
 *******************************************************/
extern int8_t callback_read_bill(char* bill,uint32_t len);


/********************************************************
  * @Description：用户实现 上传日志
  * @Arguments	：
			     upload_url:http post的地址
			     log_name:用户上传的日志名
			     max_len：日志名的最大写入长度
  * @Returns	：
                  成功返回0
                  失败返回其它
 *******************************************************/
extern int8_t callback_upload_log(const xj_upload_log_param* upload_url,char* log_name,uint16_t max_len);


/********************************************************
  * @Description：用户实现 电子锁控制
  * @Arguments	：
			     gun_cnt:要显示的枪号 ，必须小于枪总数
			     0表示枪1 ,1表示枪2,以此类推
			     type:控制类型 0解锁 1上锁
  * @Returns	：
                  成功返回0
                  失败返回其它
 *******************************************************/
extern int8_t callback_lock_control(uint8_t gun_cnt,uint8_t type);

/********************************************************
  * @Description：用户实现 安全防护工作参数
  * @Arguments	：
			     xj_l2_safety_prot_param:  参数

			     err:	执行结果 0表示成功，
							    1长度错误
								2订单号错误
								3超过最大范围
								4超过最小范围
								5其它
 *******************************************************/
extern void callback_safety_protect_param(xj_l2_safety_prot_param *param,uint8_t *err);


/********************************************************
  * @Description：用户实现 功率控制 回调函数
  * @Arguments	：
			    gun_cnt:启动的枪号 ，必须小于枪总数
			    0表示枪1 ,1表示枪2,以此类推
			    kwh :最大输出功率
			    	单位kw
  * @Returns	：
                无
 *******************************************************/
extern int8_t callback_power_control(uint8_t gun_cnt,uint32_t kwh);

extern void callback_set_vin_white_list(T_VIN_SET *vin_data,T_VIN_SET *vin_data_result);

extern void callback_set_card_white_list(T_CARD_SET *card_data,T_CARD_SET *card_data_result);

extern void callback_clean_vin_white_list(int8_t *result);

extern void callback_clean_card_white_list(int8_t *result);


/********************************************************
  * @Description：用户实现 重启控制
  * @Arguments	：
			     dev_num:设备号
			     0表示tcu 1:ccu
			     type:控制类型 0:立刻重启
  * @Returns	：
                  成功返回0
                  失败返回其它
 *******************************************************/
extern int8_t callback_dev_reboot_control(uint8_t dev_num, uint8_t type);

/********************************************************
  * @Description：用户实现  平台下发参数后，桩端需将参数保存并开启或关闭对应功能，通过调用"xj_phm_read_byte"
  * @Arguments	：
  * @Returns	：
 *******************************************************/
extern void callback_xj_param_set();
#endif
