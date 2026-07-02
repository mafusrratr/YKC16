#ifndef   _XIAOJU_H_
#define   _XIAOJU_H_

#include "zk/xiaojusdk/include/redefine.h"
#include "zk/xiaojusdk/include/protocol.h"
#include "zk/xiaojusdk/include/xiaoju_struct.h"
#include "zk/xiaojusdk/include/xiaoju_error.h"
#include "zk_common.h"

#define TEMP_LOG_SIZE               256
#define MAX_BLOCKED_MQTT_RESP_NUM   5    //接收处理队列长度
#define MAX_XJ_INCIDENT_SUM         (CHARGE_GUN_CNT_MAX * 4) //XJ处理事件最大队列长度
#define MAX_XJ_INCIDENT_DATA_LEN    200  //事件数据最大长度
#define RANGE_MIN_TYPE              0
#define RANGE_MAX_TYPE              1
#define BILL_SAVE_PERIOD            30 //账单更新周期时间  30S


#define LOG_LEVEL_NULL              0
#define LOG_LEVEL_MESSAGE           1
#define LOG_LEVEL_DEBUG             2
#define LOG_LEVEL_WARING            3
#define LOG_LEVEL_ERROR             4

#define LOG_STRATEGY_TIME           1
#define LOG_STRATEGY_FILE           2
#define LOG_STRATEGY_NULL           3


/*this struct is stored/restored in binary format when persisting, so do not change the declaration and new members must be appeneded at the back;*/
typedef struct{
	char sdk_version[4];                                // sdk 版本
	xj_fee_config fee_config;                           // 费率，包括电费和服务费，都是用分时电价，共48个时段
	char center_svr_addr[XJ_MAX_SVR_ADDR_LEN];          // 中心服务器地址
	uint16_t center_svr_port;                           // 中心服务器端口号
	char logic_svr_addr[XJ_MAX_SVR_ADDR_LEN];           // 逻辑服务器地址
	uint16_t logic_svr_port;                            // 逻辑服务器端口号
	uint16_t logic_svr_upload_log_port;                 // 逻辑服务器上传日志使用端口
	char username[XJ_MAX_USERNAME_LEN];                 // 登录用户名
	char password[XJ_MAX_PASSWORD_LEN];                 //登录密码
    uint8_t user_version[4];                            // 协议版本，对应小桔充电协议中版本域
	char gun_qr_code[CHARGE_GUN_CNT_MAX][XJ_MAX_BAR_CODE_LEN];//充电枪对应二维码
	char mac_addr[XJ_MAX_MAC_ADDR_LEN];                  //mac 地址，对应106协议中的充电桩Mac地址或者IMEI码
	char equipment_id[XJ_MAX_EQUIPMENT_ID_LEN];          // 充电桩编码
	uint8_t network_cmd_timeout;                         //部分协议超时时间，单位为s,默认为5
	uint16_t sign_in_interval;                           // 签到间隔
	uint16_t gun_cnt;                                    // 充电枪个数
	uint8_t  gun_type;                                   // 枪类型，分为直流和交流
	uint16_t upload_gun_status_interval;                 // 充电枪状态上传间隔，即104报文上传间隔，默认30s
	uint16_t upload_bms_status_interval;                 // BMS协议上传间隔，即302报文上传间隔，默认30s
	uint16_t heartbeat_interval;                         // 心跳上传间隔，默认30s
	uint16_t heartbeat_timeout_check_cnt;                // 心跳超时次数，用于判断桩离线，默认为3，也就是3次心跳无回复就认为已经离线
	uint16_t mqtt_ping_interval;                         //190827 新增mqtt间隔参数
	uint32_t max_power[CHARGE_GUN_CNT_MAX];                  //最大功率
	uint32_t software_restart_cnt;                       //软件启动次数

	uint8_t log_level;                                   //日志等级
	uint8_t log_strategy;                                //日志策略
	uint16_t log_period;                                 //日志上传间隔 单位min
	st_safety_parameters safety_parameters;              //安全围栏参数，用户根据参数判断停机

} xj_params;


typedef struct{
	uint8_t timeout_cnt;
	uint32_t sleeped_sec;
    uint32_t index;
} _heartbeat_param;

typedef struct{
	int sleeped_sec;
} _sign_in_param;

typedef struct{
	uint8_t init_section;
	int index;  	//记录当前时段，用以判断跨时段标志
	uint8_t section_flag; //跨时段标志
}section_ctrl;

typedef struct
{
    uint8_t start_bill_flag;//启动计费标志位
    int int_hour;
    int int_minute;
    int index;
    uint64_t last_kwh; /*上一次电表读数 uint32->uint64*/
    uint32_t accum_kwh;
    uint32_t start_charge_sec;//启动时时间戳
    uint32_t charge_fee; //电费   ，未四舍五入和抹零
    uint32_t service_fee; //服务费  ，未四舍五入和抹零
    uint32_t demurrage;//延误费  ，未四舍五入和抹零
    uint32_t updata_time;
    uint8_t start_soc_flag;
    //zhx 2020-12-10 平台策略与桩端计费策略差异，现分段计费进行银行家算法
    xj_section_fee fee_section;
    section_ctrl s_ctrl;
    xj_fee_config fee_config;// 费率，包括电费和服务费，都是用分时电价，共48个时段
}st_calculate_bill;


typedef struct{
	st_calculate_bill info[CHARGE_GUN_CNT_MAX];
} _calculate_bill_info_param;



typedef struct{
    xj_incident_type incident_name;
    uint8_t incident_data[MAX_XJ_INCIDENT_DATA_LEN];
    uint16_t incident_len;
}st_incident_list;


typedef struct{
    xj_pal_mutex_t incident_lock;
    uint8_t incident_sum;
    uint8_t incident_new;
    st_incident_list incident_list[MAX_XJ_INCIDENT_SUM];
}st_xj_incident_info;

/* 存放error_code 队列 */
#define XJ_ERROR_ITEM_MAX_CNT   6
typedef struct
{
    xj_pal_mutex_t errlist_lock;
    struct
    {
        struct
        {
            uint8_t err_code[4];
            uint8_t flag;/* occurred recovered */
        }error_item[XJ_ERROR_ITEM_MAX_CNT];
        uint8_t get_index,put_index;
    }cache[CHARGE_GUN_CNT_MAX];
}st_errcache_queue_t;

typedef struct{
	int8_t submited;// 响应标志位
	uint8_t qos;
	xj_mqtt_cmd_enum cmd_type;
	char data[320];
} _mqtt_resp_to_send;


/*
*@brief BCD格式的时间转换
*/
void _convert_time(int32_t year,int32_t month,int32_t day,int32_t hour,int32_t minute,int32_t sec,char* bcd_timd);
void _convert_time_back(char* bcd_time,xj_sync_system_time_param* time_param);

/*
*@brief 系统参数保存
*/
void _persist_xj_params(void);
void _restore_persisted_xj_params(xj_params* p);

/*
*@brief 计算字母或数字的长度
*/
int Come_letter_num_len(uint8_t*data);

/*
*@brief 发送事件(code=108)
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
*   type[in] 事件类型
*   event_param[in] 事件参数
*@return 成功=xj_bool_true 失败=xj_bool_false
*/
xj_bool xj_send_event(uint8_t gun_cnt, xj_event_type type,uint32_t	event_param);

/*
*@brief 发送故障(code=118)
*@params
*   error[in] 故障信息
*@return 成功=xj_bool_true 失败=xj_bool_false
*/
xj_bool xj_send_error(xj_error* error);

/*
*@brief 发送电表故障告警(code=120)
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
*@return null
*/
void xj_send_Electric_meter_warning(uint8_t gun);

/*
*@brief 生成二维码
*@params
*   QR[out] 存放二维码的缓存
*   charge[in] 充电桩唯一编码
*   gun_num[in] 枪号[从1开始,即1代表1枪]
*@return 成功=二维码字符串长度 失败=0
*/
uint8_t QR_code_String(char* QR ,const char*charge,const uint8_t gun_num);

/*
*@brief 检查大小端
*@return 0-小端  1-大端
*/
char Check_cpu(void);

/*
*@brief 发送code=1102
*@params
*   update_result[in] 下载结果
*   md5[in] 32位md5值
*   serial_code[in] code=1101中接收到的序列号
*/
void touch_send_1102_code(uint8_t update_result ,uint8_t *md5 ,uint32_t serial_code);

/*
*@brief 充电桩功能参数上传
*@params
*	注意：调用此函数前 需调用xj_phm_write_byte函数对某一功能赋值
*@return 成功=0 失败=-1
*/
int8_t xj_send_charga_param_info();
/*
*@brief 系统启动初始化
*@params
*   gun_status[in] 用户配置的枪信息
*   bms_status[in] 用户配置的bms信息
*   gun_num[in] 用户端充电枪总个数
*@return 成功=0 失败=-1
*/
int8_t xj_APP_start(st_user_gun_info* gun_status, st_user_bms_info* bms_status,uint8_t gun_num);

/*
*@brief 发送插枪事件
*@params
*   gun_index[in] 枪号(枪号从0开始,0代表1枪)
*@return 成功=0 失败=-1
*/
int8_t xj_send_touch_gun_pluged_in(uint8_t gun_index);

/*
*@brief 发送拔枪事件
*@params
*   gun_index[in] 枪号(枪号从0开始,0代表1枪)
*@return 成功=0 失败=-1
*/
int8_t xj_send_touch_gun_pluged_out(uint8_t gun_index);

/*
*@brief 发送充电事件
*@params
*   gun_index[in] 枪号(枪号从0开始,0代表1枪)
*   param[in] 启动参数("0000"成功 其他故障码为中止启动)
*@return 成功=0 失败=-1
*/
int8_t xj_send_touch_charge_start(uint8_t gun_index,uint8_t* param);

/*
*@brief 发送停充事件
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
*   type[in] 事件类型
*   event_param[in] 事件参数
*@return 成功=0 失败=-1
*/
int8_t xj_send_touch_charge_stoped(uint8_t gun_index, uint8_t*stop_reason,uint8_t* param);


/*
*@brief 发送电表加密数据
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
* 	id: 订单号
*	meter_addr:电表地址
*	encry_data:加密数据
*@return 成功=0 失败=-1
*/
int8_t xj_send_meter_encryp_data(uint8_t gun_cnt,char* id,char *meter_addr, char *encry_data);
/*
*@brief 发送告警事件(code=108)
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
*   type[in] 告警类型
*   warning[in] 告警码
*@return 成功=0 失败=-1
*/
int8_t xj_send_touch_warning_occured(uint8_t gun_cnt, uint8_t type,uint8_t* warning);

/*
*@brief 发送带阈值的告警事件(code=108)
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
*   type[in]	告警类型
*   warning[in] 告警码
*   threshold[in] 阈值
*@return 成功=0 失败=-1
 */
int8_t xj_send_warning(uint8_t gun_cnt,uint8_t type,uint8_t *warning,uint32_t threshold);

/*
*@brief 发送故障发生事件(code=118)
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
*   err_no[in] 故障码
*@return 成功=0 失败=-1
*/
int8_t xj_send_touch_error_occured(uint8_t gun_cnt, uint8_t* err_no);

/*
*@brief 发送故障恢复事件(code=118)
*@params
*   gun_cnt[in] 枪号(枪号从0开始,0代表1枪)
*   err_no[in] 故障码
*@return 成功=0 失败=1
*/
int8_t xj_send_touch_error_recovered(uint8_t gun_cnt, uint8_t* err_no);

/*
*@brief 设置桩号
*@return 成功=0 失败=-1
*/
int8_t xj_touch_set_equipment_id(uint8_t* ID_str);

/*
*@brief 设置设备唯一编码
*@return 成功=0 失败=-1
*/
int8_t xj_touch_set_mac_addr(uint8_t* mac_str);

/*
*@brief 刷卡请求启动充电
*@return 成功=0 失败=-1
*/
int8_t xj_send_touch_card_start_charge(xj_card_auth *info);

/*
*@brief 触发发送bms 基础信息  在充电启动后上传一次(code=304,1笔订单发送1次,无论启动成功或失败都需上传)
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*   charge_user_id[in] 订单号
*   data[in] bms信息
*@return 成功=xj_bool_true 失败=xj_bool_false
*/
int8_t touch_send_bms_basic_info (uint8_t gun,char*charge_user_id,st_bms_basic_info *data);

/*
*@brief 触发发送bms 中止CST数据(code=308,1笔订单发送1次)
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*   charge_user_id[in] 订单号
*   data[in] bms信息
*@return 成功=0 失败=-1
*/
int8_t touch_send_bms_cst_data (uint8_t gun,char*charge_user_id,st_off_bms_cst_data* data);

/*
*@brief 触发发送bms 中止BST数据(code=310,1笔订单发送1次)
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*   charge_user_id[in] 订单号
*   data[in] bms信息
*@return 成功=0 失败=-1
*/
int8_t touch_send_bms_bst_data (uint8_t gun,char*charge_user_id,st_off_bms_bst_data* data);

/*
*@brief 触发发送bms 结束统计数据(code=312,1笔订单发送1次,充电结束时发送)
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*   charge_user_id[in] 订单号
*   data[in] bms信息
*@return 成功=0 失败=-1
*/
int8_t touch_send_bms_inish_statistical_data (uint8_t gun ,char*charge_user_id,st_bms_finish_statistical_data* data);

/*
*@brief 获取指定时段的电费
*@params
*   time[in] 要获取的时段。将24小时分成48个时段，每半小时为1段。
                    取值为0-47
*@return 该时段电费,单位分
*/
uint16_t xj_get_charge_fee(uint8_t time);

/*
*@brief 获取指定时段的服务费
*@params
*   time[in] 要获取的时段。将24小时分成48个时段，每半小时为1段。
                    取值为0-47
*@return 该时段服务费,单位分
*/
uint16_t xj_get_service_fee(uint8_t time);

/*
*@brief 获取指定时段的延迟费
*@params
*   time[in] 要获取的时段。将24小时分成48个时段，每半小时为1段。
                    取值为0-47
*@return 该时段服务费,单位分
*/
uint16_t xj_get_demurrage(uint8_t time);

/*
*@brief 获取指定枪的最大功率
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*@return 返回该枪最大功率
*/
uint32_t xj_get_maxpower(uint8_t gun);

/*
*@brief 获取当前充电中的指定枪的订单号
*@params
*   gun[in] 枪号(从0开始)
    id[out] 订单号
*@return 成功=1 失败=0
*/
int8_t get_current_order_number(uint8_t gun,int8_t* id);

/*
*@brief 设置mqtt登录信息
*@params
*   UserName[in] mqtt登录用户名
*   password[in] mqtt登录密码
*@return 成功=0 失败=-1
*/
int8_t xj_touch_set_mqtt_info(uint8_t* UserName ,uint8_t* password);


/*
*@brief 发送vin码启动充电请求
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*   vin[in] vin码
*@return 成功=0 失败=-1
*/
int8_t xj_send_vin_start_charge_request(uint8_t gun_cnt,uint8_t* vin);

/*
*@brief VIN鉴权请求(code=40)
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*   vin[in] VIN码
*@return
*/
void xj_send_touch_vin_charge_reques(uint8_t gun,char *vin);

/*
*@brief 日志输出
*@params
*   type[in] 日志类型
*   format[in] 可变参数
*/
void xj_pal_print_log(xj_log_type type, char* format, ... );

/*
*@brief 打印tcp数据
*@params
*   buffer[in] 数据
*   len[in] 长度
*   type[in] 发送或接收(_TCP_SEND_/_TCP_RECEIVE_)
*@return
*/
void Printf_TCP_Log(char *buff,int len,char type);

/*
*@brief 获取指定枪的当前费用
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*@return 当前费用(分)
*/
uint32_t get_current_fee(uint8_t gun);

/*
*@brief 获取安全围栏参数
*/
const st_safety_parameters* get_xj_safety_parameters(void);

/*
*@brief 获取指定枪对应的二维码
*@params
*   gun[in] 枪号(枪号从0开始,0代表1枪)
*   QR[out] 二维码缓存
*@return 成功=0 失败=-1
*/
int8_t get_gun_qr_code(uint8_t gun, int8_t* QR);

/*
*@brief 初始化故障信息队列
*/
void err_event_info_init(void);

/*
*@brief 将要发送的故障码放入缓存
*@params
*   flag[in] 故障产生/恢复 标志
*   gun_index[in] 枪号
*   err_no[in] 故障码
*@return 成功=0 失败=-1
*/
int8_t err_event_info_push(int8_t flag,uint8_t gun_index,char *err_no);

/*
*@brief 将要发送的故障码从缓存清除
*@params
*   flag[in] 故障产生/恢复 标志
*   gun_index[in] 枪号
*   err_no[in] 故障码
*@return 成功=0 失败=-1
*/
int8_t err_event_info_pop(int8_t flag,uint8_t gun_index,char *err_no);

/*
*@brief 清空缓存中的数据
*/
void err_event_info_clr(void);

// 发送514全部参数消息,主动上报
void send_514_all_notify(void);

#endif

