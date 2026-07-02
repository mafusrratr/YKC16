#ifndef _XIAOJU_STRUCT_
#define _XIAOJU_STRUCT_

#include "zk/xiaojusdk/include/redefine.h"

/* for xj_pal_print_log */
#define _TCP_SEND_              1
#define _TCP_RECEIVE_           0

/* for log enable or disable */
#define _LOG_RECORD_ON_         1
#define _LOG_RECORD_OFF_        0

/* for gun type*/
#define XJ_GUN_TYPE_DC          1
#define	XJ_GUN_TYPE_AC          2

#define _LITTLE_MODE_           0
#define _BIG_MODE_              1


#define XJ_MAX_EQUIPMENT_ID_LEN 				32+1
#define XJ_MAX_BAR_CODE_DESC_LEN			    128+1
#define XJ_MAX_BAR_CODE_LEN					    128+1
#define XJ_MAX_USERNAME_LEN 					64
#define XJ_MAX_PASSWORD_LEN 				    256
#define XJ_MAX_SVR_ADDR_LEN  				    128+1
#define XJ_UPLOAD_LOG_URL_LEN				    128+1
#define XJ_MAX_LOG_FILE_NAME_LEN			    128+1
#define XJ_MAX_CHAEGE_USER_ID_LEN  			    32+1
#define XJ_MAX_URL_LEN  					    128+1
#define XJ_MAX_CHECKSUM_LEN  				    32+1
#define XJ_MAX_CAR_PLATE_NO_LEN  			    8+1
#define XJ_MAX_CAR_VIN_LEN  				    17+1
#define XJ_MAX_BRM_BMS_CONNECT_VERSION_LEN		3+1
#define XJ_MAX_BRM_BMS_SOFTWARE_VERSION_LEN		8+1
#define XJ_MAX_ERR_NO_LEN				        4+1
#define XJ_MAX_BRM_VIN_LEN				        17+1
#define XJ_MAX_MAC_ADDR_LEN				        32+1

typedef enum{
	xj_bool_false = 0,
	xj_bool_true = 1
} xj_bool;

/* for mqtt connect */
typedef enum {
	xj_login_err_no_succeed = 0,
	xj_login_err_no_failed_to_connect_center_svr = 1,
	xj_login_err_no_failed_to_get_logic_svr_addr = 2,
	xj_login_err_no_failed_to_connect_logic_svr = 3,
	xj_login_err_no_wrong_username_or_password = 4,
} xj_login_err_no;


typedef struct{
	uint32_t charge_fee[48]; //电费
	uint32_t service_fee[48]; //服务费
	uint32_t demurrage[48];//延误费
} xj_fee_config;//整形 单位为分

typedef struct{
	uint32_t charge_fee[48]; //电费
	uint32_t service_fee[48]; //服务费
	uint32_t demurrage[48];//延误费
} xj_section_fee;//整形 单位为分

typedef enum {
	xj_event_gun_pluged_in = 1,                          // 插枪事件
	xj_event_gun_pluged_out = 2,                         // 拔枪事件
	xj_event_charge_started = 3,                         // 启动充电事件
	xj_event_charge_stoped = 4,                          // 停止充电事件
} xj_event_type;

typedef struct{
	uint8_t gun_index;
	xj_event_type event_type;
} xj_event;


typedef struct{
	uint16_t gun_index;
	uint8_t card_id[16];//充电卡卡号
	uint8_t random_id[48];//随机数
	uint8_t phy_id[4];//物理卡号
} xj_card_auth;


typedef struct{
	uint16_t gun_index;
	uint8_t card_id[16];
} xj_card_charge;


typedef struct{
	uint8_t gun_index;
	char err_no[XJ_MAX_ERR_NO_LEN];
	uint32_t err_flag;
} xj_error;

typedef enum {
	xj_work_stat_type_idle = 0,	         	            // 空闲状态
	xj_work_stat_type_gun_pluged_in = 1,	            // 已插枪
	xj_work_stat_type_charging = 2,					    // 充电中
	xj_work_stat_type_gun_not_pluged_out = 3,	        // 未拔枪
	xj_work_stat_type_reserve = 4,	                    // 预约
	xj_work_stat_type_bist = 5,	                        // 自检
	//xj_work_stat_type_in_error = 6,                   // 故障  //故障不关联状态2020-5-7 17:39:24
    xj_work_stat_type_stoping = 7					    // 停止中
} xj_work_stat_type;


typedef enum {
	xj_car_connection_stat_type_disconnected = 0,
	xj_car_connection_stat_type_connected = 2
} xj_car_connection_stat_type;

/*
typedef enum {
	xj_charge_policy_type_auto = 0,
	xj_charge_policy_type_by_time = 1,
	xj_charge_policy_type_by_fee = 2,
	xj_charge_policy_type_by_kw = 3,
	xj_charge_policy_type_by_soc = 4
} xj_charge_policy_type;
*/

/* internal */
typedef struct{
	uint8_t gun_index;                                    //充电枪口号，从1开始
	xj_work_stat_type work_stat;                          //充电枪工作状态，对应104报文中的工作状态
    xj_car_connection_stat_type car_connection_stat;      //充电枪连接状态，对应104报文中的车连接状态

	uint8_t soc_percent;                                  //当前SOC
    uint8_t  start_flags;                                 // 该枪订单是否，启动失败
	char stop_reason[XJ_MAX_ERR_NO_LEN];                  //202账单报文中的停止充电原因
	uint32_t cumulative_charge_fee;                       //已充金额，默认上传为0	整形  要除100
	uint16_t dc_charge_voltage;                           //104报文中直流充电电压
	uint16_t dc_charge_current;                           //104报文中的直流充电电流
	uint16_t bms_need_voltage;                            //104报文中的BMS需求电压
	uint16_t bms_need_current;                            //104报文中的BMS需求电流
	uint16_t ac_a_vol;                                    //104报文中的交流A相充电电压
	uint16_t ac_b_vol;                                    //104报文中的交流B相充电电压
	uint16_t ac_c_vol;                                    //104报文中的交流C相充电电压
	uint16_t ac_a_cur;                                    //104报文中的交流A相充电电流
	uint16_t ac_b_cur;                                    //104报文中的交流B相充电电流
	uint16_t ac_c_cur;                                    //104报文中的交流C相充电电流
	uint16_t charged_sec;                                 //104报文中的已充电时间
	uint64_t before_charge_meter_kwh_num; /*104报文中的充电前电表读数 uint32->uint64*/
	char charge_user_id[XJ_MAX_CHAEGE_USER_ID_LEN];       //104报文中的充电/预约用户ID。这个就是充电订单的订单号
	uint16_t start_charge_time_year;                      // 启动充电时的年
	uint8_t start_charge_time_month;                      // 启动充电时的月
	uint8_t start_charge_time_day;                        // 启动充电时的日
	uint8_t start_charge_time_hour;                       // 启动充电时的时
	uint8_t start_charge_time_minute;                     // 启动充电时的分
	uint8_t start_charge_time_sec;                        // 启动充电时的秒
	uint8_t parallel_charging;							  //充电模式 0-正常模式 1-双枪并充
	uint16_t charge_power_kw;                             // 充电功率
	uint16_t user_tel;                                    //用户手机号后4位

	uint64_t current_meter_kwh_num; /* 104报文中的当前电表读数 uint32->uint64*/
	uint32_t charging_temp;								  //充电桩温度
} xj_gun_status;

typedef enum {
	xj_battery_type_qiansuan = 0,
	xj_battery_type_nieqing = 1,
	xj_battery_type_linsuantieli = 2,
	xj_battery_type_mengsuanli = 3,
	xj_battery_type_gusuanli = 4,
	xj_battery_type_sanyuan = 5,
	xj_battery_type_juhewuli = 6,
    xj_battery_type_taisuanli = 7,
	xj_battery_type_other = 8,
} xj_battery_type;

typedef struct{
	uint8_t gun_index;                                                   // 枪口号，从1开始
	xj_work_stat_type work_stat;                                         // 工作状态，对应302报文中的工作状态
	xj_car_connection_stat_type car_connection_stat;                     // 车连接状态,对应302报文中的车连接状态

	char brm_bms_connect_version[XJ_MAX_BRM_BMS_CONNECT_VERSION_LEN];    // BRM-BMS通讯协议版本号
	xj_battery_type brm_battery_type;	                                 // BR-电池类型
	uint16_t brm_battery_power;											 //	整车动力蓄电池系统额定容量
	uint16_t brm_battery_volt;                                           // 整车动力蓄电池系统额定总电压
	uint16_t brm_battery_supplier;                                       // 电池生产厂商
	uint16_t brm_battery_seq;                                            // 电池组序号
	uint16_t brm_battery_produce_year;                                   // 电池组生厂日期：年
	uint8_t brm_battery_produce_month;                                   // 电池组生厂日期：月
	uint8_t brm_battery_produce_day;                                     // 电池组生厂日期：日
	uint32_t brm_battery_charge_count;                                   // 电池组充电次数
	uint32_t brm_battery_property_identification;                        // 电池组产权标识
	char brm_vin[XJ_MAX_BRM_VIN_LEN];                                    // 车辆识别码vin
	char brm_bms_software_version[XJ_MAX_BRM_BMS_SOFTWARE_VERSION_LEN];  // BMS软件版本号
	uint32_t bcp_max_voltage;                                            // 单体动力蓄电池最高允许充电电压
	uint32_t bcp_max_current;                                            // 最高允许充电电流
	uint32_t bcp_max_power;                                              // 动力蓄电池标称总能量
	uint32_t bcp_total_voltage;                                          // 最高允许充电总电压
	uint16_t bcp_max_temperature;                                        // 最高允许温度
	uint16_t bcp_battery_soc;                                             // 整车动力蓄电池荷电状态
	uint32_t bcp_battery_soc_current_voltage;                             // 整车动力蓄电池当前电池电压
	uint8_t bro_BMS_isReady;                                             // BRO-BMS是否充电准备好
    uint8_t CRO_isReady;                                                 // CRO-充电机是否充电准备好


	uint16_t bcl_voltage_need;                                           // 电压需求
	uint16_t bcl_current_need;                                           // 电流需求
	uint8_t bcl_charge_mode;                                             // 充电模式
	uint16_t bcs_test_voltage;                                           // 充电电压测量值
	uint16_t bcs_test_current;                                           // 充电电流测量值
	uint16_t bcs_max_single_voltage;                                     // 最高单体动力蓄电池电压
	uint16_t bcs_max_single_no;                                          // 最高单体动力蓄电池组号
	uint8_t bcs_current_soc;                                             // 当前荷电状态soc%
	uint16_t charge_time_left;                                           // 估算剩余充电时间
	uint16_t bsm_single_no;                                              // 最高单体动力蓄电池电压所在编号
	uint16_t bsm_max_temperature;                                        // 最高动力蓄电池温度
	uint16_t bsm_max_temperature_check_no;                               // 最高温度检测点编号
	uint16_t bsm_min_temperature;                                        // 最低动力蓄电池温度
	uint16_t bsm_min_temperature_check_no;                               // 最低动力蓄电池温度检测点编号
	uint16_t bsm_voltage_too_high_or_too_low;                            // 单体动力蓄电池电压过高或过低
	uint16_t bsm_car_battery_soc_too_high_or_too_low;                    // 整车动力蓄电池荷电状态soc过高或过低
	uint16_t bsm_car_battery_charge_over_current;                        // 动力蓄电池充电过电流
	uint16_t bsm_battery_temperature_too_high;                           // 动力蓄电池温度过高
	uint16_t bsm_battery_insulation_state;                               // 动力蓄电池绝缘状态
	uint16_t bsm_battery_connect_state;                                  // 动力蓄电池组输出连接器连接状态
	uint16_t bsm_allow_charge;                                           // 允许充电

	uint16_t bsd_battery_low_voltage;                                    // BSD-动力蓄电池单体最低电压
	uint16_t bsd_battery_high_voltage;                                   // BSD-动力蓄电池单体最高电压
	uint16_t bsd_battery_low_temperature;                                // BSD-动力蓄电池最低温度
	uint16_t bsd_battery_high_temperature;                               // BSD-动力蓄电池最高温度
	uint16_t bem_error_spn2560_00;                                       // BEM-接收SPN2560=0x00的充电桩辨识报文超时
	uint16_t bem_error_spn2560_aa;                                       // BEM-接收SPN2560=0xaa的充电桩辨识报文超时
	uint16_t bem_error_time_sync;                                        // BEM-接收充电桩的时间同步和最大输出能力报文超时
	uint16_t bem_error_ready_to_charge;                                  // BEM-接收充电桩完成充电准备报文超时
	uint16_t bem_error_receive_status;                                   // BEM-接收充电桩充电状态报文超时
	uint16_t bem_error_receive_stop_charge;                              // BEM-接收充电桩终止充电报文超时
	uint16_t bem_error_receive_report;                                   // BEM-接收充电桩充电统计报文超时
	uint16_t bem_error_other;                                            // BEM-其他

	uint8_t CST_stop_reason;                                             //CST - 充电机中止原因
    uint16_t CST_fault_reason;                                           //CST - 中止充电故障原因
    uint8_t CST_error_reason;                                            //CST - 中止充电错误原因
    uint8_t BST_stop_reason;                                             //BST - 充电机中止原因
    uint16_t BST_fault_reason;                                           //BST - 中止充电故障原因
    uint8_t BST_error_reason;                                            //BST - 中止充电错误原因
	uint8_t bsd_stop_soc;                                               // 终止荷电状态soc

//  #if 0以下是sdk1.0版本所需要的参数  用于302传输  2.0版本后不需要以下参数
	uint16_t bst_bms_soc_target;                                         // BMS达到所需求的SOC目标值
	uint16_t bst_bms_voltage_target;                                     // BMS达到总电压的设定值
	uint16_t bst_single_voltage_target;                                  // 达到单体电压的设定值
	uint16_t bst_finish;                                                 // 充电桩主动终止
	uint16_t bst_isolation_error;                                        // 绝缘故障
	uint16_t bst_connect_over_temperature;                               // 输出连接器过温故障
	uint32_t bst_over_temperature;                                       // BST-BMS元件，输出连接器过温
	uint16_t bst_connect_error;                                          // BST-充电连接器故障
	uint16_t bst_battery_over_temperature;                               // BST-电池组温度过高故障
	uint16_t bst_high_voltage_relay_error;                               // 高压继电器故障
	uint16_t bst_point2_test_error;                                      // 检测点2电压检测故障
	uint16_t bst_other_error;                                            // 其他故障
	uint16_t bst_current_too_high;                                       // 电流过大
	uint16_t bst_voltage_too_high;                                       // 电压异常
	uint16_t bst_stop_soc;                                               // 终止荷电状态soc
//endif
} xj_bms_status;



typedef struct
{
    char brm_bms_connect_version[XJ_MAX_BRM_BMS_CONNECT_VERSION_LEN]; //BRM-BMS通讯协议版本号
    int32_t brm_battery_type;//电池类型
    int32_t brm_battery_power;//整车动力蓄电池系统额定容量/Ah
    int32_t brm_battery_volt;// 整车动力蓄电池系统额定总电压
    int32_t brm_battery_supplier; // 电池生产厂商
    int32_t brm_battery_seq;// 电池组序号
    int32_t brm_battery_produce_year;// 电池组生厂日期：年
    int32_t brm_battery_produce_month;// 电池组生厂日期：月
    int32_t brm_battery_produce_day;// 电池组生厂日期：日
    int32_t brm_battery_charge_count;// 电池组充电次数
    int32_t brm_battery_property_identification; // 电池组产权标识
    char brm_vin[XJ_MAX_BRM_VIN_LEN];// 车辆识别码vin
    char brm_bms_software_version[XJ_MAX_BRM_BMS_SOFTWARE_VERSION_LEN];// BMS软件版本号
    int32_t bcp_max_voltage;// 单体动力蓄电池最高允许充电电压
    int32_t bcp_max_current;// 最高允许充电电流
    int32_t bcp_max_power;// 动力蓄电池标称总能量
    int32_t bcp_total_voltage;// 最高允许充电总电压
    int32_t bcp_max_temperature;// 最高允许温度
    int32_t bcp_battery_soc;// 整车动力蓄电池荷电状态
    int32_t bcp_battery_soc_current_voltage; // 整车动力蓄电池当前电池电压
    int32_t bro_bms_is_ready; // 是否充电准备好
    uint8_t CRO_isReady;// CRO-充电机是否充电准备好
}st_bms_basic_info;//bms 基础信息


typedef struct
{
    int32_t bcl_voltage_need; // 电压需求
    int32_t bcl_current_need;// 电流需求
    int32_t bcl_charge_mode;// 充电模式
    int32_t bcs_test_voltage;// 充电电压测量值
    int32_t bcs_test_current;// 充电电流测量值
    int32_t bcs_max_single_voltage; // 最高单体动力蓄电池电压
    int32_t bcs_max_single_no;// 最高单体动力蓄电池组号
    int32_t bcs_current_soc;// 当前荷电状态soc%
    int32_t charge_time_left;// 估算剩余充电时间
    int32_t bsm_single_no;// 最高单体动力蓄电池电压所在编号
    int32_t bsm_max_temperature; // 最高动力蓄电池温度
    int32_t bsm_max_temperature_check_no;// 最高温度检测点编号
    int32_t bsm_min_temperature;// 最低动力蓄电池温度
    int32_t bsm_min_temperature_check_no;// 最低动力蓄电池温度检测点编号
    int32_t bsm_voltage_too_high_or_too_low;// 单体动力蓄电池电压过高或过低
    int32_t bsm_car_battery_soc_too_high_or_too_low;// 整车动力蓄电池荷电状态soc过高或过低
    int32_t bsm_car_battery_charge_over_current;// 动力蓄电池充电过电流
    int32_t bsm_battery_temperature_too_high; // 动力蓄电池温度过高
    int32_t bsm_battery_insulation_state;// 动力蓄电池绝缘状态
    int32_t bsm_battery_connect_state;// 动力蓄电池组输出连接器连接状态
    int32_t bsm_allow_charge;// 允许充电


	int32_t dc_charge_voltage;//直流充电电压
	int32_t dc_charge_current;//直流充电电流
	int32_t bcs_min_single_voltage; // 最低单体动力蓄电池电压
}st_bms_process_data;//bms 过程数据

typedef struct
{
	uint8_t CST_stop_reason;//CST - 充电机中止原因
    uint16_t CST_fault_reason;//CST - 中止充电故障原因
    uint8_t CST_error_reason;//CST - 中止充电错误原因
}st_off_bms_cst_data;//中止BMS-CST数据，充电机中止充电


typedef struct
{
    uint8_t BST_stop_reason;//BST - 充电机中止原因
    uint16_t BST_fault_reason;//BST - 中止充电故障原因
    uint8_t BST_error_reason; //BST - 中止充电错误原因
}st_off_bms_bst_data;//中止BMS-BST数据,BMS中止充电



typedef struct
{
    uint8_t bsd_stop_soc;// 终止荷电状态soc
    int32_t bsd_battery_low_voltage;// BSD-动力蓄电池单体最低电压
	int32_t bsd_battery_high_voltage;// BSD-动力蓄电池单体最高电压
	int32_t bsd_battery_low_temperature;// BSD-动力蓄电池最低温度
	int32_t bsd_battery_high_temperature;// BSD-动力蓄电池最高温度
	int32_t bem_error_spn2560_00; // BEM-接收SPN2560=0x00的充电桩辨识报文超时
	int32_t bem_error_spn2560_aa;// BEM-接收SPN2560=0xaa的充电桩辨识报文超时
	int32_t bem_error_time_sync;  // BEM-接收充电桩的时间同步和最大输出能力报文超时
	int32_t bem_error_ready_to_charge;// BEM-接收充电桩完成充电准备报文超时
	int32_t bem_error_receive_status;// BEM-接收充电桩充电状态报文超时
	int32_t bem_error_receive_stop_charge;// BEM-接收充电桩终止充电报文超时
	int32_t bem_error_receive_report; // BEM-接收充电桩充电统计报文超时
	int32_t bem_error_other;// BEM-其他
}st_bms_finish_statistical_data;//结束BMS统计数据


typedef struct{
    uint32_t    serial_code;
	uint8_t gun_index;
	uint32_t charge_policy;
	uint32_t charge_policy_param;
	uint16_t user_tel;
	char charge_user_id[XJ_MAX_CHAEGE_USER_ID_LEN];
} xj_start_charge_param;

typedef struct{
	char err_no[XJ_MAX_ERR_NO_LEN];
} xj_start_charge_result;

typedef struct{
	char err_no[XJ_MAX_ERR_NO_LEN];
} xj_control_cmd_result;

typedef struct{
	uint32_t	serial_code;
	uint8_t	gun_index;
	uint32_t 	power_kwh;
}xj_power_ctrl_param;

typedef enum{
	xj_control_cmd_type_stop_charge = 2,
	xj_control_cmd_type_reboot      = 11,
	xj_control_cmd_type_reset_factory_settings = 12,
	xj_control_cmd_type_gun_unlock = 14,
	xj_control_cmd_type_reconnect = 20,
} xj_control_cmd_type;

typedef struct{
    uint32_t    serial_code;
	uint8_t gun_index;	//枪号
	uint8_t cmd_count;	//命令数量
	uint16_t cmd_len;   //参数长度
	uint32_t cmd_type;  //控制类型
	uint8_t cmd_data[128]; //参数数据
} xj_control_cmd_param;

typedef struct{
	uint8_t gun_index;
	char charge_user_id[XJ_MAX_CHAEGE_USER_ID_LEN];
} xj_stop_order_param;

typedef struct{
	char err_no[XJ_MAX_ERR_NO_LEN];
} xj_stop_order_result;

typedef enum{
	xj_software_upgrade_type_charger_software = 0,
	xj_software_upgrade_type_middle_ware = 1,
	xj_software_upgrade_type_ad = 2,
} xj_software_upgrade_type;

typedef enum{
	xj_software_target_type_tcu = 10,
	xj_software_target_type_ccu = 11,
} xj_software_target_type;


typedef struct{
	xj_software_upgrade_type upgrade_type;
	xj_software_target_type target_type;
	char url[XJ_MAX_URL_LEN];
	char md5[XJ_MAX_CHECKSUM_LEN];
    uint32_t    serial_code;
} xj_software_upgrade_param;

typedef struct{
	uint8_t file_downloaded;
	char checksum[XJ_MAX_CHECKSUM_LEN];
} xj_software_upgrade_result;

typedef struct{
	char upload_url[XJ_UPLOAD_LOG_URL_LEN];
} xj_upload_log_param;

typedef struct{
	char log_name[XJ_MAX_LOG_FILE_NAME_LEN];
} xj_upload_log_result;

typedef struct{
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t sec;
} xj_sync_system_time_param;


/* event type */
typedef enum{
    xj_incident_type_null           = 0,
	xj_incident_type_reconnection   = 1,    //  网络重连
    xj_incident_type_set_left_qr    = 2,    //  设置左枪二维码
    xj_incident_type_set_righ_qr    = 3,    //  设置右枪二维码
    xj_incident_type_start_charge   = 4,    //  启动充电
    xj_incident_type_stop_charge    = 5,    //  停止充电
    xj_incident_type_set_time       = 6,    //  设置时间
    xj_incident_type_software_upgrade=7,    //  软件升级
    xj_incident_type_params_updated = 8,    //  参数更新
    xj_incident_type_upload_log     = 9,    //  上传日志
    xj_incident_type_control_cmd    = 10,   //   控制指令
    xj_incident_type_lock_control   = 11,   //   锁控制
    xj_incident_type_vin_start_charg= 12,   //   vin启动充电
    xj_incident_type_send_108       = 13,   //   108事件上报
    xj_incident_type_send_304       = 14,   //   304上报
    xj_incident_type_send_312       = 15,   //   312上报
    xj_incident_type_send_120       = 16,   //   120上报
	xj_incident_type_set_vin_write_list=17, //  43
	xj_incident_type_clean_vin_write_list=18,// 45
	xj_incident_type_set_card_write_list = 19,
	xj_incident_type_clean_card_write_list =20,
	xj_incident_type_send_80		= 21,	//80
	xj_incident_type_power_control  = 22,
}xj_incident_type;

#if 0
typedef struct {
	char* addr;
	int len;
} _string_data_addr;
#endif

typedef struct{
	int8_t gun_index;                                       // 枪号
	char charge_user_id[XJ_MAX_CHAEGE_USER_ID_LEN];         // 订单号
	int16_t charge_start_time_year;                         // 开始充电时间年
	int8_t charge_start_time_month;
	int8_t charge_start_time_day;
	int8_t charge_start_time_hour;
	int8_t charge_start_time_minute;
	int8_t charge_start_time_sec;
	int16_t charge_end_time_year;                            // 结束充电时间年
	int8_t charge_end_time_month;
	int8_t charge_end_time_day;
	int8_t charge_end_time_hour;
	int8_t charge_end_time_minute;
	int8_t charge_end_time_sec;
	int16_t charge_time;                                    // 累计充电时间
	int8_t start_soc;                                       // 开始充电的SOC
	int8_t end_soc;                                         //  停止充电的SOC
	char err_no[XJ_MAX_ERR_NO_LEN];                         // 	结束充电原因
	uint32_t charge_kwh_amount;                              // 	充电电量      单位  0.001kwh
	uint64_t start_charge_kwh_meter; /*开始充电时电表读数 uint32->uint64*/
	uint64_t end_charge_kwh_meter; /*结束充电时的电表读数 uin32->uint64*/
	uint32_t total_charge_fee;                               //	电费      单位: 分
	uint32_t total_service_fee;                              //	服务费     单位: 分
	char car_vin[XJ_MAX_CAR_VIN_LEN];	                      //	VIN码
	uint32_t kwh_amount[48]; /*分时电量 uint16->uint32*/
	int8_t start_type;                                      //	起始充电类型
} xj_bill_info;




typedef enum{
    xj_bill_not_upload      = 1,//未上传
    xj_bill_ready_upload    = 2,//待上传
    xj_bill_completed_Upload= 3,//已上传
}xj_bill_status;



typedef struct{

    xj_bill_status status;
    xj_bill_info bill;
}st_xj_histroy_bill;


typedef enum{
    xj_log_Null     = 0,
    xj_log_message  = 1,
    xj_log_remind   = 2,
    xj_log_warning  = 3,
    xj_log_Error    = 4,

}xj_log_type;

typedef enum{
    type_int8       = 1,
    type_uint8      = 2,
    type_int16      = 3,
    type_uint16     = 4,
    type_int32      = 5,
    type_uint32     = 6,
    type_int64      = 7,
    type_uint64     = 8,
    type_float      = 9,
    type_double     = 10,
    type_string8    = 11,
    type_string16   = 12,
    type_string32   = 13,
    type_string64   = 14,
    type_string128  = 15,
    type_string130  = 16,
    type_string66   = 17,
	type_uint16x3,
}xj_param_type;


typedef struct
{
    void* p_param;//参数指针
    xj_param_type param_type;//参数类型
    void* p_param_max;//参数允许设置最大值
    void* p_param_min;//参数允许设置最小值
}st_set_param_info;

#if 0
typedef struct
{
    uint16_t param_1;
    uint16_t param_2;
}__attribute__ ((aligned (1))) st_assembly_param;//组合参数
#endif

typedef struct
{
    uint16_t param_1;
    uint16_t param_2;
}XJ_ALIGN(1) st_assembly_param2;/*组合参数*/

typedef struct
{
    uint16_t param_1;
    uint16_t param_2;
    uint16_t param_3;
}XJ_ALIGN(1) st_assembly_param3;/*组合参数*/

typedef struct
{
    uint16_t param_1;
    uint16_t param_2;
    uint16_t param_3;
    uint16_t param_4;
}XJ_ALIGN(1) st_assembly_param4;/*组合参数*/



typedef struct
{
    uint16_t safety_12;//默认5 单位0.1V 充电桩预充阶段测量车辆电池电压值与BCP报文中“整车动力蓄电池当前电池电压”差异绝对值大指定电压数值时则停止充电启动流程
    uint16_t safety_13;//默认420 单位0.01V 充电阶段，如果电池是三元锂电池，持续监测BCS/BMV报文中“最高单体动力蓄电池电压”，若超过指定电压数值则立即停止充电；
    uint16_t safety_14;//默认300 单位0.01mv 充电阶段，若电池类型为三元锂电池，持续监测充电过程中三元锂电池中单体最高电压与最低电池差，若大于指定电压数值且持续10s则认为电芯不均衡严重需告警。
    uint16_t safety_15;//默认10 单位秒 充电阶段，持续监测BSM报文中的“最高动力蓄电池温度”是否超过BCP报文中的“最高允许温度”，若是且持续N秒则立即停止充电；
    uint16_t safety_16;//默认10 单位秒 充电阶段，持续监测BMT报文中温度最高值是否超过BCP报文中“最高允许温度”，若是且持续N秒则立即停止充电；
    uint16_t safety_17;//默认5 单位0.1℃/min 充电阶段，充电过程中持续监测并计算动力蓄电池温升速率，若温速率大于N℃/1min则立即停止充电；
    uint16_t safety_18;//默认50 单位℃ 充电阶段，若BSM报文中电池组温度超过N℃则停止充电。
    uint16_t safety_19;//默认50 单位℃ 充电阶段，若BMT报文中电池组温度超过N℃则停止充电。

    st_assembly_param2 safety_20;//参数1默认10 单位0.1V，参数2默认1 单位min 充电阶段，BCS报文中“充电电压测量值”和充电机直流电表电压测量值差值绝对值超过nV并持续n min则停止充电。
    st_assembly_param2 safety_21;//参数1默认10 单位0.1V，参数2默认1 单位min 充电阶段，BCS报文中“充电电压测量值”和充电机高压板测量值差异绝对值超过nV并持续1min则停止充电。
    st_assembly_param2 safety_22;//参数1默认10 单位0.1V，参数2默认1 单位min 充电阶段，充电机直流电表电压测量值和充电机高压板测量值差异绝对值超过nV并持续1min则停止充电。

    uint16_t safety_23;//默认1 单位秒 充电阶段，BCS报文中“充电电流测量值”与充电桩输出电流测量值(>=30A时)之差绝对值超过1%并持续N秒则停止充电。
    uint16_t safety_24;//默认1 单位秒 充电阶段，BCS报文中“充电电流测量值”与充电桩输出电流测量值（<30A时）之差绝对值超过0.3A并持续Ns则停止充电。
    uint16_t safety_25;//默认105 单位0.01倍 充电阶段，当充电电量（从直流电表获取）超过“总能量*(1-初始SOC)*N”时停止充电。
    uint16_t safety_26;//默认5 单位分钟min 充电阶段，起充SOC<50%，充电过程中BCS数据"当前荷电状态SOC"保持不变超过设定的时间中止充电
    uint16_t safety_27;//默认5 单位分钟min 充电阶段，起充SOC<50%，充电过程中BCS数据"充电电压测量值"保持不变超过设定的时间中止充电。
    uint16_t safety_28;//默认5 单位分钟min 充电阶段，起充SOC<50%，充电过程中BCS数据"充电电流测量值"保持不变超过设定的时间中止充电
    uint16_t safety_29;//默认5 单位分钟min 充电阶段，起充SOC<50%，充电过程中BCS数据"最高单体动力蓄电池电压"保持不变超过设定的时间中止充电。
    uint16_t safety_30;//默认5 单位分钟min 充电阶段，起充SOC<50%，充电过程中BSM数据"最高动力蓄电池温度"保持不变超过设定的时间中止充电
    uint16_t safety_31;//默认85 单位℃ 充电阶段，充电连接装置温度超过N℃则开始降功率输出，当温度下降到安全阈值则开始增加输出功率 (充电枪过温)

    st_assembly_param2 safety_32;//参数1默认100 单位℃ ，参数2默认30 单位秒，充电阶段，当充电连接装置温度超过N℃且持续N s则停止充电；
    uint16_t safety_33;//默认10 单位1V      充电阶段，在绝缘检测阶段通过检测熔断器前后两端的电压，压差超过nV认为是熔断器断路异常或者直接采用带反馈信号的熔断器，一旦检测到熔断器断路则告警且中止充电流程。
    uint16_t safety_34;//默认36 单位1V     充电阶段，一把枪启动充电时，检验别的未充电枪电压若高于nV则中止充电流程。
    uint16_t safety_35;//默认50% 单位1% 参数地址26-30五项截至SOC百分比

/*
    st_assembly_param2 safety_36; //参数1默认1 数据范围0.5-1 精度0.1 参数2默认1 数据范围0-1 精度0.1
    st_assembly_param3 safety_37; //参数1默认65 数据范围0-100 精度1  参数2默认65 数据范围0-100 精度1 参数3默认1 数据范围0-1 精度0.1
    st_assembly_param3 safety_38; //参数1默认3 数据范围0-10 精度0.1  参数2默认3 数据范围0-10 精度0.1 参数3默认1 数据范围0-1 精度0.1
    st_assembly_param3 safety_39; //参数1默认20 数据范围0-50 精度1   参数2默认25 数据范围0-50 精度1  参数3默认1 数据范围0-1 精度0.1
    st_assembly_param4 safety_40; //参数1默认1.1 数据范围0-2 精度0.1 参数2默认1.1 数据范围0-2 精度0.1参数3默认1 数据范围0-2 精度0.1 参数4默认1 数据范围0-1 精度0.1
    uint16_t safety_41; 	//默认0.1 数据范围0-0.5 精度0.1
    uint16_t safety_42;		//默认15 数据范围10-60 精度1
    uint16_t safety_43;		//默认300 数据范围10-1000 精度1
    uint16_t safety_44;		//默认36 数据范围10-100 精度1
    */
}st_safety_parameters;//安全围栏相关参数

#pragma pack(1)	//按char对齐
typedef struct{
	uint8_t gun_index;
	uint8_t charge_user_id[32];
	st_assembly_param2  safety4;		//参数1默认1 数据范围0.5-1 精度0.1 参数2默认1 数据范围0-1 精度0.1
	st_assembly_param3  safety5;		//参数1默认65 数据范围0-100 精度1  参数2默认65 数据范围0-100 精度1 参数3默认1 数据范围0-1 精度0.1
	st_assembly_param3 	safety6;		//参数1默认3 数据范围0-10 精度0.1  参数2默认3 数据范围0-10 精度0.1 参数3默认1 数据范围0-1 精度0.1
	st_assembly_param3	safety7;		//参数1默认20 数据范围0-50 精度1   参数2默认25 数据范围0-50 精度1  参数3默认1 数据范围0-1 精度0.1
	st_assembly_param4	safety8;		//参数1默认1.1 数据范围0-2 精度0.1 参数2默认1.1 数据范围0-2 精度0.1参数3默认1 数据范围0-2 精度0.1 参数4默认1 数据范围0-1 精度0.1
	uint16_t safety9; 					//默认0.1 数据范围0-0.5 精度0.1
	uint16_t safety10;					//默认15 数据范围10-60 精度1
	uint16_t safety11;					//默认300 数据范围10-1000 精度1
	uint16_t safety12;					//默认36 数据范围10-100 精度1
}xj_l2_safety_prot_param;

#pragma pack()	//取消

//平台每次下发VIN码白名单最大数量
#define MAX_VIN_NUM 	20

typedef struct{
	uint8_t vin[17];//vin码
	uint8_t state; //
}user_vin_white_list;

typedef struct{
	uint16_t white_list_num;//白名单数量
	user_vin_white_list vin_white_list[MAX_VIN_NUM];
}T_VIN_SET;

typedef struct{
	uint8_t card[16];//卡号
	uint8_t state; //
}user_card_white_list;

typedef struct{
	uint16_t white_list_num;//白名单数量
	user_card_white_list card_white_list[MAX_VIN_NUM];
}T_CARD_SET;

typedef struct{
	uint16_t gun_index;//充电口号 0表示枪1; 1表示枪2
	uint8_t	charge_user_id[32];//订单号
	int32_t balance;//账户余额
	uint8_t Request_result;//鉴权结果     0成功 1失败
	uint8_t failure_reasons;//失败原因
	uint32_t remainkon;//剩余里程
	uint32_t dump_energy;//可充电量
	uint32_t residue_degree;//剩余次数
	uint16_t phone;//手机尾号
}st_user_vin_send_result;//用户vin码启动充电回调结果//参考<小桔协议> code41

typedef struct{
	uint8_t major_gun;	 	//major_gun:并充主枪 ，必须小于枪总数 0表示枪1 ,1表示枪2,以此类推
	uint8_t minor_gun;	 	//minor_gun:并充副枪 ，必须小于枪总数 0表示枪1 ,1表示枪2,以此类推
	uint8_t charge_type;	// charge_type: 充电模式  0表示无并充 ,1表示并充
//	uint8_t co_charge_sum;	//当前并充数量
}st_user_co_chargeing;  //并充数据结构
/************* user data ******************/
typedef struct {
	int32_t soc_percent;//当前电量SOC 单位1%
	int32_t dc_charge_voltage;//直流充电电压
	int32_t dc_charge_current;//直流充电电流
	int32_t bms_need_voltage;//BMS需求电压
	int32_t bms_need_current;//BMS需求电流
	int32_t ac_a_vol;//交流A相充电电压
	int32_t ac_b_vol;//交流B相充电电压
	int32_t ac_c_vol;//交流C相充电电压
	int32_t ac_a_cur;//交流A相充电电流
	int32_t ac_b_cur;//交流B相充电电流
	int32_t ac_c_cur;//交流C相充电电流
	uint64_t meter_kwh_num; /* 当前电表读数 int32->int64*/
	int32_t parallel_charging; //充电模式 0-正常模式 1-双枪并充
	int32_t charge_power_kw;//充电功率
	int32_t chargeing_temp; //充电桩温度
} st_user_gun_info;//充电枪实时信息

typedef struct {
	int32_t bcl_voltage_need; // 电压需求 分辨率：0.1V
	int32_t bcl_current_need;// 电流需求      分辨率：0.1A，-400A偏移量
	int32_t bcl_charge_mode;// 充电模式       0x01表示恒压充电，0x02表示恒流充电
	int32_t bcs_test_voltage;// 充电电压测量值          分辨率：0.1V
	int32_t bcs_test_current;// 充电电流测量值          分辨率：0.1A，-400A偏移量
    int32_t bcs_max_single_voltage; // 最高单体动力蓄电池电压              分辨率：0.01V，数据范围: 0~24 V;
    int32_t bcs_min_single_voltage; // 最低单体动力蓄电池电压              分辨率：0.01V，数据范围: 0~24 V;
	int32_t bcs_max_single_no;// 最高单体动力蓄电池组号              分辨率1/位，范围0-15
	int32_t bcs_current_soc;// 当前荷电状态soc%       分辨率：1%/位，0-100%
	int32_t charge_time_left;// 估算剩余充电时间
	int32_t bsm_single_no;// 最高单体动力蓄电池电压所在编号
    int32_t bsm_max_temperature; // 最高动力蓄电池温度
	int32_t bsm_max_temperature_check_no;// 最高温度检测点编号
	int32_t bsm_min_temperature;// 最低动力蓄电池温度
	int32_t bsm_min_temperature_check_no;// 最低动力蓄电池温度检测点编号
	int32_t bsm_voltage_too_high_or_too_low;// 单体动力蓄电池电压过高或过低
	int32_t bsm_car_battery_soc_too_high_or_too_low;// 整车动力蓄电池荷电状态soc过高或过低
	int32_t bsm_car_battery_charge_over_current;// 动力蓄电池充电过电流
	int32_t bsm_battery_temperature_too_high; // 动力蓄电池温度过高
	int32_t bsm_battery_insulation_state;// 动力蓄电池绝缘状态
	int32_t bsm_battery_connect_state;// 动力蓄电池组输出连接器连接状态
	int32_t bsm_allow_charge;// 允许充电

	int32_t dc_charge_voltage;//直流充电电压
	int32_t dc_charge_current;//直流充电电流

}st_user_bms_info;//BMS实时信息


#endif


