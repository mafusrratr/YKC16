#ifndef __APP_DEMO_H__
#define __APP_DEMO_H__

#include "zk/xiaojusdk/include/xiaoju_struct.h"
#include "zk/xiaojusdk/porting/linux/xiaoju_pal.h"

#define SIMULATOR_USE_FLAG

#define ERROR_TIME              100000*1
#define SIMULATOR_GUN_NUM       CHARGE_GUN_CNT

typedef struct
{
    uint8_t gun;//枪号
    uint8_t charge_id[XJ_MAX_CHAEGE_USER_ID_LEN];//订单号
    uint16_t phone;//用户手机尾号
    int connection;//枪连接状态
    uint8_t connect_charge;//
    xj_work_stat_type internal_status;
    uint64_t current_meter_kwh;
    int charge_flag;// 0x0f 启动充电 0xf0 停止充电
    int soc_flag;
    uint8_t error_chufa;  //
    uint8_t* error_code;
    uint8_t* started_code;
    uint8_t* stopped_code;
    uint8_t error_huifu;
    uint8_t* error_code_huifu;
    uint8_t heart_over_flag;
    uint8_t heart_over;
    uint8_t ddb_over_flag;
    uint8_t ddb_over;
    uint8_t set_time_flag;
    uint8_t link_ok;
} st_usert_charge_info; //模拟充电桩信息 ，用户实际不需要此


extern st_xj_histroy_bill histroy_bill_info[MAX_HISTORY_ORDER_CNT];
extern st_usert_charge_info _simulated_info[SIMULATOR_GUN_NUM];//模拟桩端信息

extern st_user_gun_info _simulator_gun_status[SIMULATOR_GUN_NUM];
extern st_user_bms_info _simulator_bms_status[SIMULATOR_GUN_NUM];

extern st_bms_basic_info _simulator_bms_basic_info[SIMULATOR_GUN_NUM];
extern st_bms_finish_statistical_data _simulator_bms_finsih_info[SIMULATOR_GUN_NUM];

void* _simulator_charge_thread(void* arg);
void* _simulator_thread(void* arg);
extern void xj_pal_print_log(xj_log_type type, char* format, ... );

extern xj_pal_thread_t app_charger_thread;
extern st_off_bms_bst_data data_310;
extern xj_sync_system_time_param time_app;

extern uint8_t update_bill_flag;
extern uint8_t update_bill_number;
extern unsigned short m[49],n[49],o[49];
extern unsigned char update_fee_flag;
extern void xj_pal_print_log(xj_log_type type, char* format, ... );
extern unsigned char recv_bill_flag[2];
extern xj_bill_info recv_bill_data[2];
extern unsigned int bill_money[2];
extern unsigned char *start_stop_reason;
#endif

