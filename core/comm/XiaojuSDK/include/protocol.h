#ifndef _PROTOCOL_H_
#define _PROTOCOL_H_

#include "zk/xiaojusdk/include/redefine.h"
#include "zk/xiaojusdk/include/mqtt.h"
#include "zk/xiaojusdk/porting/linux/xiaoju_pal.h"

#define  ECO_FALSE   0
#define  ECO_TRUE    1



#define  _CONNECT_          1
#define  _DISCONNECT_       0

#define	 DATA_FIELD_COUNT   14
#define  SIGNIN_106_LEGTH   141
#define  SIGNIN_106_CMD     106
#define	 BASE_LENGTH        15

#define	REMOTE_HEADER       0xd07d

/*!!!!!!!!!!!!!!!!!!此版本号禁止更改!!!!!!!!!!!!!!!!!!!!!!!!!*/
#define XJ_VERSION_CODE     0x100A0003  // 0x00000102//xj协议版本号

#define TIME_OUT_3_SEC      3

#define CMD1_MAX_PARAM_LEN  192

#define CMD501_MAX_DATA_LEN  256
#define CMD503_MAX_DATA_LEN  128
#define CMD1309_MAX_DATA_LEN 1024
#define CMD505_MAX_DATA_LEN  128
#define CMD507_MAX_DATA_LEN  512
#define CMD508_MAX_DATA_LEN  100
#define CMD511_MAX_DATA_LEN  100
#define CMD513_MAX_DATA_LEN  100
#define CMD514_MAX_DATA_LEN  200

#define CMD_43_MAX_VIN_NUM 	20

extern  int _g_is_login;


#define CODE23_NO_ERROR        0   //23应答错误码，无错误
#define CODE23_GUN_ERROR       1   //23应答错误码，枪号错误
#define CODE23_STATUS_ERROR    2   //23应答错误码，枪状态错误
#define CODE23_ID_ERROR        3   //23应答错误码，设备编号错误
#define CODE23_OTHER_ERROR     4   //23应答错误码，其它错误






#define CODE502_NO_ERROR        0   //502应答错误码，无错误
#define CODE502_LEN_ERROR       1   //502应答错误码，长度错误
#define CODE502_ADDR_ERROR      2   //502应答错误码，参数地址错误
#define CODE502_MAX_RANGE_ERROR 3   //502应答错误码，超出设置最大范围
#define CODE502_MIN_RANGE_ERROR 4   //502应答错误码，超出设置最小范围
#define CODE502_OTHER_ERROR     5   //502应答错误码，其它错误



#define CODE1301_NO_ERROR           0   //1301应答错误码，无错误
#define CODE1301_LEN_ERROR          1   //1301应答错误码，长度错误
#define CODE1301_CLASS_NUM_ERROR    2   //1301应答错误码，分组数量错误
#define CODE1301_CLASS_ERROR        3   //1301应答错误码，分组错误


#define CODE503_NO_ERROR           0   //503应答错误码，无错误
#define CODE503_LEN_ERROR          1   //503应答错误码，长度错误
#define CODE503_GUN_ERROR          2   //503应答错误码，枪号错误
#define CODE503_OTHER_ERROR        3   //503应答错误码，其它错误




typedef enum{
	xj_cmd_type_1 	= 1,//废弃
	xj_cmd_type_2	= 2,//废弃
	xj_cmd_type_3	= 3,//废弃
	xj_cmd_type_4	= 4,//废弃
	xj_cmd_type_5	= 5,
	xj_cmd_type_6	= 6,
	xj_cmd_type_7 	= 7,
	xj_cmd_type_8 	= 8,
	xj_cmd_type_11	= 11,
	xj_cmd_type_12 	= 12,
	xj_cmd_type_19  = 19,
	xj_cmd_type_20  = 20,
	xj_cmd_type_23	= 23,
	xj_cmd_type_24	= 24,

	xj_cmd_type_33	= 33,
	xj_cmd_type_34	= 34,
	xj_cmd_type_35	= 35,
	xj_cmd_type_36	= 36,
	xj_cmd_type_37	= 37,
	xj_cmd_type_38	= 38,
	xj_cmd_type_40	= 40,
	xj_cmd_type_41	= 41,
	xj_cmd_type_42	= 42,
	xj_cmd_type_43 	= 43,
	xj_cmd_type_44	= 44,
	xj_cmd_type_45	= 45,
	xj_cmd_type_80  = 80,
	xj_cmd_type_81 	= 81,
	xj_cmd_type_101	= 101,
	xj_cmd_type_102 = 102,
	xj_cmd_type_103	= 103,
	xj_cmd_type_104	= 104,
	xj_cmd_type_105 = 105,
	xj_cmd_type_106	= 106,
	xj_cmd_type_107	= 107,
	xj_cmd_type_108 = 108,
	xj_cmd_type_113	= 113,
	xj_cmd_type_114	= 114,
	xj_cmd_type_117	= 117,
	xj_cmd_type_118	= 118,
	xj_cmd_type_119	= 119,
	xj_cmd_type_120	= 120,
	xj_cmd_type_201	= 201,
	xj_cmd_type_202	= 202,
	xj_cmd_type_205	= 205,//废弃
	xj_cmd_type_206	= 206,//废弃
	xj_cmd_type_301	= 301,//废弃
	xj_cmd_type_302	= 302,//废弃
	xj_cmd_type_303	= 303,
	xj_cmd_type_304	= 304,
	xj_cmd_type_305	= 305,
	xj_cmd_type_306	= 306,
	xj_cmd_type_307	= 307,
	xj_cmd_type_308	= 308,
	xj_cmd_type_309	= 309,
	xj_cmd_type_310	= 310,
	xj_cmd_type_311	= 311,
	xj_cmd_type_312	= 312,

	xj_cmd_type_331	= 331,
	xj_cmd_type_332	= 332,

	xj_cmd_type_409	= 409,
	xj_cmd_type_410	= 410,
	xj_cmd_type_501	= 501,
	xj_cmd_type_502	= 502,
	xj_cmd_type_503	= 503,
	xj_cmd_type_504	= 504,
	xj_cmd_type_505	= 505,
	xj_cmd_type_506	= 506,
	xj_cmd_type_507	= 507,
	xj_cmd_type_508	= 508,
	xj_cmd_type_509	= 509,
	xj_cmd_type_510	= 510,
	xj_cmd_type_511	= 511,
	xj_cmd_type_512	= 512,
	xj_cmd_type_513	= 513,
	xj_cmd_type_514	= 514,
	xj_cmd_type_801	= 801,
	xj_cmd_type_802	= 802,
	xj_cmd_type_1101  = 1101,
	xj_cmd_type_1102	= 1102,
	xj_cmd_type_1303	= 1303,//废弃
	xj_cmd_type_1304	= 1304,//废弃
	xj_cmd_type_1305	= 1305,//废弃
	xj_cmd_type_1306	= 1306,//废弃
	xj_cmd_type_1309	= 1309,
	xj_cmd_type_1310	= 1310,
}xj_mqtt_cmd_enum;

// 1
typedef struct{
	uint8_t	    cmd_type;
	uint8_t	    cmd_num;
	uint16_t	cmd_len;
	uint32_t	start_addr;
	uint8_t 	data[CMD1_MAX_PARAM_LEN];
	uint32_t	serial_code;
} xj_cmd_1;

// 2
typedef struct{
	uint8_t	    equipment_id[32];
	uint8_t	    cmd_type;
	uint8_t	    cmd_num;
	uint8_t	    result;
	uint32_t	start_addr;
	uint8_t 	data[CMD1_MAX_PARAM_LEN];
	uint32_t    serial_code;
	uint16_t	data_len;//用于记录要发送查询的数据长度  ，但实际协议中并没有该字段
} xj_cmd_2;

// 3
typedef struct{
	uint8_t	    cmd_type;
	uint32_t	start_addr;
	uint8_t	    cmd_num;
	uint16_t	cmd_len;
	uint8_t	    data[128];
	uint32_t	serial_code;
} xj_cmd_3;

// 4
typedef struct{
	uint8_t	equipment_id[32];
	uint8_t	cmd_type;
	uint32_t	start_addr;
	uint8_t	result;
	uint8_t	data[128];
	uint16_t	dataLen;
	uint32_t	serial_code;
} xj_cmd_4;

//5
typedef struct {
	uint8_t 	gun_index;
	uint32_t	addr;
	uint8_t 	cmd_num;
	uint16_t	cmd_len;
	uint32_t	cmd_param[48];
	uint32_t	serial_code;
}xj_cmd_5;

// 6
typedef struct {
	uint8_t  	equipment_id[32];
	uint8_t  	gun_index;
	uint32_t	addr;
	uint8_t	cmd_num;
	uint8_t	result[4];
	uint32_t	serial_code;
}xj_cmd_6;

//7
typedef struct{
    uint16_t user_tel;
	uint8_t	gun_index;
	int32_t		charge_type;
	uint32_t	charge_policy;
	uint32_t	charge_policy_param;
	uint8_t	book_time[8];
	uint8_t	book_delay_time;
	uint8_t	charge_user_id[32];
	uint8_t	allow_offline_charge;
	int32_t		allow_offline_charge_kw_amout;
	uint8_t	charge_delay_fee;
	int32_t		charge_delay_wait_time;
	uint32_t	serial_code;
} xj_cmd_7;

typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t result[4];
	uint8_t	charge_user_id[32];
	uint32_t serial_code;
}xj_cmd_8;
// 11
typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t charge_seq[32];
	uint32_t serial_code;
}xj_cmd_11;
// 12
typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t charge_seq[32];
	uint8_t result[4];
	uint32_t  serial_code;
} xj_cmd_12;

//19
typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint32_t power_kwh;
	uint32_t	serial_code;
}xj_cmd_19;
//20
typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t result;
	uint32_t serial_code;
}xj_cmd_20;


// 23
typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t lock_type;
	uint32_t  serial_code;
}xj_cmd_23;

// 24
typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t result;
	uint32_t  serial_code;
} xj_cmd_24;

//332
typedef struct{
	uint16_t reserve;		 //预留
	uint8_t equipment_id[32];//充电桩编码
	uint8_t success_sign;  //成功标识
	uint32_t  serial_code;
}xj_cmd_332;

//331
typedef struct{
	uint8_t equipment_id[32];//充电桩编码
	uint16_t reserve;		 //预留
	uint32_t  serial_code;
}xj_cmd_331;

// 33
typedef struct{
	uint8_t equipment_id[32];
	uint16_t gun_index;
	uint16_t auth_result;
	uint32_t  card_money;
}xj_cmd_33;

// 35
typedef struct{
	uint16_t gun_index;
	uint8_t equipment_id[32];
	uint8_t card_id[16];
	uint16_t result;
}xj_cmd_35;

//37
typedef struct{
	uint8_t card[16];//卡号
	uint8_t state; //
}white_list_card;
// 37
typedef struct{
	uint8_t equipment_id[32];//充电桩编码
	uint16_t white_list_num;//白名单数量
	white_list_card card_white_list[CMD_43_MAX_VIN_NUM];
	uint32_t  serial_code;
}xj_cmd_37;

// 38
typedef struct{
	uint8_t equipment_id[32];//充电桩编码
	uint16_t white_list_num;//白名单数量
	white_list_card card_white_list[CMD_43_MAX_VIN_NUM];
	uint32_t  serial_code;
}xj_cmd_38;

// 41
typedef struct{

	uint8_t equipment_id[32];//充电桩编码
	uint8_t gun_index;//充电口号
	uint8_t	charge_user_id[32];//订单号
	uint8_t vin[17];//vin码
	int32_t balance;//账户余额
	uint8_t Request_result;//鉴权结果
	uint8_t failure_reasons;//失败原因
	uint32_t remainkon;//剩余里程
	uint32_t dump_energy;//可充电量
	uint32_t residue_degree;//剩余次数
	uint16_t phone;//手机尾号
	uint32_t  serial_code;
}xj_cmd_41;

//  42 43
typedef struct{
	uint8_t vin[17];//vin码
	uint8_t state; //
}white_list;

typedef struct{
	uint8_t equipment_id[32];//充电桩编码
	uint16_t white_list_num;//白名单数量
	white_list vin_white_list[CMD_43_MAX_VIN_NUM];
	uint32_t  serial_code;
}xj_cmd_42;

typedef struct{
	uint8_t equipment_id[32];//充电桩编码
	uint16_t white_list_num;//白名单数量
	white_list vin_white_list[CMD_43_MAX_VIN_NUM];
	uint32_t  serial_code;
}xj_cmd_43;


//44
typedef struct{
	uint16_t reserve;		 //预留
	uint8_t equipment_id[32];//充电桩编码
	uint8_t success_sign;  //成功标识
	uint32_t  serial_code;
}xj_cmd_44;

//45
typedef struct{
	uint8_t equipment_id[32];//充电桩编码
	uint16_t reserve;		 //预留
	uint32_t  serial_code;
}xj_cmd_45;


// 34
typedef struct{
	uint8_t equipment_id[32];
	uint16_t gun_index;
	uint8_t card_id[16];
	uint8_t random_id[48];
	uint8_t phy_card_id[4];
	uint32_t  serial_code;
} xj_cmd_34;

// 36
typedef struct{
	uint16_t gun_index;
	uint8_t equipment_id[32];
	uint8_t card_id[16];
	uint32_t  serial_code;
} xj_cmd_36;


typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t vin[17];
	uint32_t  serial_code;
} xj_cmd_40;

typedef struct
{
	uint8_t equipment_id[32];
	uint8_t gun_index;
	uint8_t	charge_user_id[32];//订单号
	uint8_t	encry_data[34];
	uint8_t	meter_addr[6];
}xj_cmd_80;

typedef struct
{
	uint8_t gun_index;
	uint8_t equipment_id[32];
	uint8_t	charge_user_id[32];//订单号
}xj_cmd_81;

//101
typedef struct{
	uint16_t heart_index;
} xj_cmd_101;
//102
typedef struct{
	uint8_t	equipment_id[32];
	uint16_t heart_index;
} xj_cmd_102;

//103
typedef struct{
	uint8_t	gun_index;
	uint8_t	charge_user_id[32];
	int32_t 	charge_card_account;
	uint8_t	accountEnoughFlag;
} xj_cmd_103;
//104
typedef struct{
	uint8_t	equipment_id[32];
	uint8_t	gun_cnt;
	uint8_t	gun_index;
	uint8_t 	gun_type;
	uint8_t 	work_stat;
	uint8_t  soc_percent;
	uint8_t  alarm_stat[4];
	uint8_t car_connection_stat;
	uint32_t cumulative_charge_fee;
	uint16_t	dc_charge_voltage;
	uint16_t dc_charge_current;
	uint16_t bms_need_voltage;
	uint16_t bms_need_current;
	uint8_t bms_charge_mode;
	uint16_t ac_a_vol;
	uint16_t ac_b_vol;
	uint16_t ac_c_vol;
	uint16_t ac_a_cur;
	uint16_t ac_b_cur;
	uint16_t ac_c_cur;
	uint16_t charge_full_time_left;
	int32_t charged_sec;
	int32_t cum_charge_kwh_amount; /*累计充电电量 */

	uint64_t before_charge_meter_kwh_num; /*充电前电表读数 int32->int64*/
	uint64_t now_meter_kwh_num; /* 当前电表读数 int32->int64*/

	uint8_t start_charge_type;
	uint8_t charge_policy;
	int32_t charge_policy_param;
	uint8_t book_flag;
	uint8_t charge_user_id[32];
	uint8_t book_timeout_min;
	uint8_t book_start_charge_time[8];
	int32_t before_charge_card_account;
	int32_t parallel_charging; //充电模式 0-正常模式 1-双枪并充
	int32_t charge_power_kw; /* 充电功率 */
	int32_t charging_temp; /*充电桩温度*/
} xj_cmd_104;
//105
typedef struct{
 uint16_t    reserve1;
 uint16_t    reserve2;
 uint8_t     time[8];
}xj_cmd_105;
//106
typedef struct {
  uint16_t	charge_mode_num;
  uint16_t  charge_mode_rate;
  uint8_t   equipment_id[32];
  uint8_t   offline_charge_flag;
  uint32_t	stake_version;
  uint16_t  stake_type;
  uint32_t  stake_start_times;
  uint8_t   data_up_mode;
  uint16_t  sign_interval;
  uint8_t   reserve;
  uint8_t   gun_index;
  uint8_t   heartInterval;
  uint8_t   heart_out_times;
  uint32_t	stake_charge_record_num;
  uint8_t   stake_systime[8];
  uint8_t   stake_last_charge_time[8];
  uint8_t   stake_last_start_time[8];
  uint8_t   signCode[8];
  uint8_t   mac[32];
  uint32_t  ccu_version;
}xj_cmd_106;
//107
typedef struct{
 uint8_t    equipment_id[32];
 uint8_t    gun_index;
 uint32_t	   event_name;
}xj_cmd_107;
//108
typedef struct {
  uint8_t    gun_index;
  uint32_t	 event_addr;
  uint32_t	 event_param;
  uint8_t  charge_user_id[32];
}xj_cmd_108;


typedef struct
{
    uint8_t gun_cnt;
    xj_event_type type;
    uint32_t event_param;
}st_108_cmd;
//113
typedef struct {
  uint8_t    url[128];
  uint32_t		port;
}xj_cmd_113;

//114
typedef struct {
  uint8_t    equipment_id[32];
}xj_cmd_114;

//117
typedef struct {
  uint8_t    equipment_id[32];
  uint8_t	gun_index;
  uint8_t	errCode[4];
}xj_cmd_117;


//118
typedef struct {
  uint8_t    equipment_id[32];
  uint8_t	gun_index;
  uint8_t	err_code[4];
  uint8_t	err_status;
}xj_cmd_118;


typedef struct {
  uint8_t    equipment_id[32];
  uint8_t	gun_index;
  uint8_t	warning_code[4];
}xj_cmd_119;


typedef struct {
  uint8_t   equipment_id[32];
  uint8_t	gun_index;
  uint8_t	warning_code[4];
  uint8_t   charge_user_id[32];
  uint8_t   type;
  uint8_t   threshold[4];
  uint8_t   retain[4];
}xj_cmd_120;


//301
typedef struct {
  uint16_t 	gun_index;
  uint8_t    equipment_id[32];
}xj_cmd_301;

//302
typedef struct {
    uint16_t	gun_index;
    uint8_t	equipment_id[32];
    uint8_t	work_stat;
    uint8_t car_connect_stat;
    uint8_t brm_bms_connect_version[3];
    uint8_t brm_battery_type;
    uint32_t brm_battery_power;
    uint32_t brm_battery_voltage;
    uint32_t brm_battery_supplier;
    uint32_t brm_battery_seq;
    uint16_t brm_battery_produce_year;
    uint8_t brm_battery_produce_month;
    uint8_t brm_battery_produce_day;
    uint32_t brm_battery_charge_count;
    uint8_t brm_battery_property_identification;
    uint8_t brm_vin[17];
    uint8_t brm_BMS_version[8];
    uint32_t bcp_max_voltage;
    uint32_t bcp_max_current;
    uint32_t bcp_max_power;
    uint32_t bcp_total_voltage;
    uint8_t bcp_max_temperature;
    uint16_t bcp_battery_soc;
    uint32_t bcp_battery_soc_current_voltage;
    uint8_t bro_BMS_isReady;
    uint32_t bcl_voltage_need;
    uint32_t bcl_current_need;
    uint8_t bcl_charge_mode;
    uint32_t bcs_test_voltage;
    uint32_t bcs_test_current;
    uint32_t bcs_max_single_voltage;
    uint8_t bcs_max_single_no;
    uint16_t bcs_current_soc;
    uint32_t last_charge_time;
    uint8_t bsm_single_no;
    uint8_t bsm_max_temperature;
    uint8_t bsm_max_temperature_check_no;
    uint8_t bsm_min_temperature;
    uint8_t bsm_min_temperature_check_no;
    uint8_t bsm_voltage_too_high_or_too_low;
    uint8_t bsm_car_battery_soc_too_high_or_too_low;
    uint8_t bsm_car_battery_charge_over_current;
    uint8_t bsm_battery_temperature_too_high;
    uint8_t bsm_battery_insulation_state;
    uint8_t bsm_battery_connect_state;
    uint8_t bsm_allow_charge;
    uint8_t bst_BMS_soc_target;
    uint8_t bst_BMS_voltage_target;
    uint8_t bst_single_voltage_target;
    uint8_t bst_finish;
    uint8_t bst_isolation_error;
    uint8_t bst_connect_over_temperature;
    uint8_t bst_BMS_over_temperature;
    uint8_t bst_connect_error;
    uint8_t bst_battery_over_temperature;
    uint8_t bst_high_voltage_relay_error;
    uint8_t bst_point2_test_error;
    uint8_t bst_other_error;
    uint8_t bst_current_too_high;
    uint8_t bst_voltage_too_high;
    uint16_t bst_stop_soc;
    uint32_t bsd_battery_low_voltage;
    uint32_t bsd_battery_high_voltage;
    uint8_t bsd_battery_low_temperature;
    uint8_t bsd_battery_high_temperature;
    uint8_t error_68;
    uint8_t error_69;
    uint8_t error_70;
    uint8_t error_71;
    uint8_t error_72;
    uint8_t error_73;
    uint8_t error_74;
    uint8_t error_75;
}xj_cmd_302;


//303
typedef struct {
  uint32_t    serial_code;
  uint16_t 	gun_index;
  uint8_t    equipment_id[32];
  uint8_t charge_user_id[32];//订单号
}xj_cmd_303;


//304
typedef struct {
    uint32_t    serial_code;
    uint16_t gun_index;//枪号
    uint8_t	equipment_id[32];//充电桩编码
    uint8_t charge_user_id[32];//订单号
    uint8_t	work_stat;//工作状态
    uint8_t brm_bms_connect_version[3];//bms版本
    uint8_t brm_battery_type;//电池类型
    uint32_t brm_battery_power;
    uint32_t brm_battery_voltage;
    uint32_t brm_battery_supplier;
    uint32_t brm_battery_seq;
    uint16_t brm_battery_produce_year;
    uint8_t brm_battery_produce_month;
    uint8_t brm_battery_produce_day;
    uint32_t brm_battery_charge_count;
    uint8_t brm_battery_property_identification;
    uint8_t brm_vin[17];
    uint8_t brm_BMS_version[8];
    uint32_t bcp_max_voltage;
    uint32_t bcp_max_current;
    uint32_t bcp_max_power;
    uint32_t bcp_total_voltage;
    uint8_t bcp_max_temperature;
    uint16_t bcp_battery_soc;
    uint32_t bcp_battery_soc_current_voltage;
    uint8_t bro_BMS_isReady;
    uint8_t CRO_isReady;
}xj_cmd_304;


//305
typedef struct {
  uint32_t    serial_code;
  uint16_t 	gun_index;
  uint8_t    equipment_id[32];
  uint8_t charge_user_id[32];//订单号
}xj_cmd_305;



//306
typedef struct {
    uint32_t    serial_code;
    uint16_t gun_index;//枪号
    uint8_t	equipment_id[32];//充电桩编码
    uint8_t charge_user_id[32];//订单号
    uint8_t	work_stat;//工作状态
    uint32_t bcl_voltage_need;
    uint32_t bcl_current_need;
    uint8_t bcl_charge_mode;
    uint32_t bcs_test_voltage;
    uint32_t bcs_test_current;
    uint32_t bcs_max_single_voltage;
    uint8_t bcs_max_single_no;
    uint16_t bcs_current_soc;
    uint32_t last_charge_time;
    uint8_t bsm_single_no;
    uint8_t bsm_max_temperature;
    uint8_t bsm_max_temperature_check_no;
    uint8_t bsm_min_temperature;
    uint8_t bsm_min_temperature_check_no;
    uint8_t bsm_voltage_too_high_or_too_low;
    uint8_t bsm_car_battery_soc_too_high_or_too_low;
    uint8_t bsm_car_battery_charge_over_current;
    uint8_t bsm_battery_temperature_too_high;
    uint8_t bsm_battery_insulation_state;
    uint8_t bsm_battery_connect_state;
    uint8_t bsm_allow_charge;
}xj_cmd_306;

typedef struct {
  uint32_t    serial_code;
  uint16_t 	gun_index;
  uint8_t    equipment_id[32];
  uint8_t charge_user_id[32];//订单号
}xj_cmd_307;


//308
typedef struct {
    uint32_t    serial_code;
    uint16_t gun_index;//枪号
    uint8_t	equipment_id[32];//充电桩编码
    uint8_t charge_user_id[32];//订单号
    uint8_t	work_stat;//工作状态
    uint8_t CST_stop_reason;
    uint16_t CST_fault_reason;
    uint8_t CST_error_reason;
}xj_cmd_308;

typedef struct {
  uint32_t    serial_code;
  uint16_t 	gun_index;
  uint8_t    equipment_id[32];
  uint8_t charge_user_id[32];//订单号
}xj_cmd_309;

//310
typedef struct {
    uint32_t    serial_code;
    uint16_t gun_index;//枪号
    uint8_t	equipment_id[32];//充电桩编码
    uint8_t charge_user_id[32];//订单号
    uint8_t	work_stat;//工作状态

    uint8_t BST_stop_reason;
    uint16_t BST_fault_reason;
    uint8_t BST_error_reason;
}xj_cmd_310;

typedef struct {
  uint32_t    serial_code;
  uint16_t 	gun_index;
  uint8_t    equipment_id[32];
  uint8_t charge_user_id[32];//订单号
}xj_cmd_311;


//312
typedef struct {
    uint32_t serial_code;
    uint16_t gun_index;//枪号
    uint8_t	equipment_id[32];//充电桩编码
    uint8_t charge_user_id[32];//订单号
    uint8_t	work_stat;//工作状态
    uint8_t bsd_stop_soc;
    uint16_t bsd_battery_low_voltage;
    uint16_t bsd_battery_high_voltage;
    uint8_t bsd_battery_low_temperature;
    uint8_t bsd_battery_high_temperature;
    uint8_t error_68;
    uint8_t error_69;
    uint8_t error_70;
    uint8_t error_71;
    uint8_t error_72;
    uint8_t error_73;
    uint8_t error_74;
    uint8_t error_75;
}xj_cmd_312;


// 201
typedef struct{
	uint8_t	gun_index;
	uint8_t 	user_id[32];
    uint32_t    serial_code;
}xj_cmd_201;
//202
typedef struct{
	uint8_t equipment_id[32];
	uint8_t gun_type;
	uint8_t gun_index;
	uint8_t charge_user_id[32];
	uint8_t charge_start_time[8];
	uint8_t charge_end_time[8];
	uint32_t charge_time;
	uint8_t start_soc;
	uint8_t end_soc;
	uint8_t err_no[4];
	uint32_t charge_kwh_amount; /*充电电量 */

	uint64_t start_charge_kwh_meter; /*充电前电表读数 uint32->uint64*/
	uint64_t end_charge_kwh_meter; /* 充电后电表读数 uint32->uint64*/

	uint32_t total_charge_fee;
	uint32_t is_not_stoped_by_card;
	uint32_t start_card_money;
	uint32_t end_card_money;
	uint32_t total_service_fee;
	uint8_t is_paid_by_offline;
	uint8_t charge_policy;
	uint32_t charge_policy_param;
	uint8_t car_vin[17];
	uint8_t car_plate_no[8];
	/* 分时电量 uint16->uint32 */
	uint32_t kwh_amount_1;
	uint32_t kwh_amount_2;
	uint32_t kwh_amount_3;
	uint32_t kwh_amount_4;
	uint32_t kwh_amount_5;
	uint32_t kwh_amount_6;
	uint32_t kwh_amount_7;
	uint32_t kwh_amount_8;
	uint32_t kwh_amount_9;
	uint32_t kwh_amount_10;
	uint32_t kwh_amount_11;
	uint32_t kwh_amount_12;
	uint32_t kwh_amount_13;
	uint32_t kwh_amount_14;
	uint32_t kwh_amount_15;
	uint32_t kwh_amount_16;
	uint32_t kwh_amount_17;
	uint32_t kwh_amount_18;
	uint32_t kwh_amount_19;
	uint32_t kwh_amount_20;
	uint32_t kwh_amount_21;
	uint32_t kwh_amount_22;
	uint32_t kwh_amount_23;
	uint32_t kwh_amount_24;
	uint32_t kwh_amount_25;
	uint32_t kwh_amount_26;
	uint32_t kwh_amount_27;
	uint32_t kwh_amount_28;
	uint32_t kwh_amount_29;
	uint32_t kwh_amount_30;
	uint32_t kwh_amount_31;
	uint32_t kwh_amount_32;
	uint32_t kwh_amount_33;
	uint32_t kwh_amount_34;
	uint32_t kwh_amount_35;
	uint32_t kwh_amount_36;
	uint32_t kwh_amount_37;
	uint32_t kwh_amount_38;
	uint32_t kwh_amount_39;
	uint32_t kwh_amount_40;
	uint32_t kwh_amount_41;
	uint32_t kwh_amount_42;
	uint32_t kwh_amount_43;
	uint32_t kwh_amount_44;
	uint32_t kwh_amount_45;
	uint32_t kwh_amount_46;
	uint32_t kwh_amount_47;
	uint32_t kwh_amount_48;

	uint8_t start_charge_type;
	uint32_t	serial_code;
} xj_cmd_202;

typedef xj_cmd_201  xj_cmd_205;
typedef xj_cmd_202  xj_cmd_206;

// 409
typedef struct{
	uint8_t	equipment_id[32];
	uint8_t	log_name[128];
	uint32_t   serial_code;
}xj_cmd_409;

//410
typedef struct{
	uint8_t	equipment_id[32];
	uint8_t	log_name[128];
	uint32_t   serial_code;
} xj_cmd_410;


typedef struct{
    uint32_t    serial_code;
    uint16_t    cmd_len;
	uint8_t 	data[CMD501_MAX_DATA_LEN];
} xj_cmd_501;


typedef struct{
	uint32_t serial_code;
	uint32_t success_number;
    uint8_t equipment_id[32];
    uint8_t set_result;
} xj_cmd_502;


typedef struct{
    uint32_t    serial_code;
    uint8_t     equipment_id[32];
	uint16_t    data_len;
	uint8_t 	data[CMD503_MAX_DATA_LEN];
} xj_cmd_503;


typedef struct{
    uint32_t    serial_code;
    uint8_t     equipment_id[32];
	uint8_t     set_result;
} xj_cmd_504;

/*
typedef struct{
	uint8_t	gun_index;
	uint8_t	charge_user_id[32];
	uint16_t single_batt

	uint32_t	serial_code;
} xj_cmd_505;
*/

typedef struct{
	uint32_t	serial_code;
	uint16_t data_len;
	uint8_t 	data[CMD505_MAX_DATA_LEN];
} xj_cmd_505;


typedef struct{
	uint32_t	serial_code;
	uint8_t	gun_index;
	uint8_t	charge_user_id[32];
	uint8_t set_result;
} xj_cmd_506;

#pragma pack(1)	//按char对齐
// 507消息头
typedef struct{
    uint8_t     equipment_id[32];
	uint32_t	serial_code;
	uint8_t     param_cnt;
	uint8_t     res;
} xj_cmd_507_head;
// 507消息体
typedef struct{
    uint8_t     param_id;
	uint8_t     data_len;
} xj_cmd_507_body;
// 507
typedef struct{
 //   uint8_t     equipment_id[32];
	uint32_t	serial_code;
	uint16_t    data_len;
	uint8_t 	data[CMD507_MAX_DATA_LEN];
} xj_cmd_507;

// 508
typedef struct{
	uint8_t    id;
	uint8_t     reason;
} xj_cmd_508_fail_item;

// 508
typedef struct{
    uint8_t     equipment_id[32];
	uint32_t	serial_code;
	uint16_t    result;
	uint16_t    success_cnt;
	uint16_t    fail_cnt;

	xj_cmd_508_fail_item 	fail_item[CMD508_MAX_DATA_LEN];
} xj_cmd_508;

// 511消息头
typedef struct{
    uint8_t     equipment_id[32];
	uint32_t	serial_code;
	uint8_t     param_id;
	uint8_t     res[3];
	uint16_t    param_value1;
	uint16_t    param_value2;
} xj_cmd_511_head;

// 511
typedef struct{
	uint32_t	serial_code;
	uint16_t    data_len;
	uint8_t 	data[CMD511_MAX_DATA_LEN];
} xj_cmd_511;
// 512
typedef struct{
	uint8_t     equipment_id[32];
	uint32_t	serial_code;
	uint8_t     result;
	uint8_t     fail_reson;
	uint8_t     res[2];
} xj_cmd_512;

// 513消息头
typedef struct{
    uint8_t     equipment_id[32];
	uint32_t	serial_code;
	uint8_t     req_cnt;
	uint8_t     res;
} xj_cmd_513_head;
// 513消息体
typedef struct{
	uint8_t     id;
} xj_cmd_513_body;
// 513
typedef struct{
	 uint32_t	serial_code;
	uint16_t    data_len;
	uint8_t 	data[CMD513_MAX_DATA_LEN];
} xj_cmd_513;

// 514
typedef struct{
	uint8_t     equipment_id[32];
	uint32_t	serial_code;
	uint8_t     resp_cnt;
	uint8_t     res;
	uint8_t 	data[CMD514_MAX_DATA_LEN];
} xj_cmd_514;

#pragma pack()	//取消

// 509
typedef struct{
	uint8_t	equipment_id[32];
	uint8_t	log_name[128];
	uint32_t   serial_code;
}xj_cmd_509;

//510
typedef struct{
	uint8_t	equipment_id[32];
	uint8_t	log_name[128];
	uint32_t   serial_code;
} xj_cmd_510;

// 801
typedef struct{
	uint32_t	key_len;
	uint8_t key_datas[128];
	uint8_t	equipment_id[32];
	uint16_t encrypted_type;
	uint8_t	encrypted_version[5];
	uint32_t   serial_code;
}xj_cmd_801;

//802
typedef struct{
	uint32_t	key_len;
	uint8_t key_datas[128];
	uint8_t	equipment_id[32];
	uint16_t encrypted_type;
	uint8_t	encrypted_version;
	uint32_t   	serial_code;
} xj_cmd_802;

// 1101
typedef struct {
  uint8_t    soft_type;
  uint8_t	soft_param;
  uint8_t	download_url[128];
  uint8_t	md5[32];
  uint32_t 	serial_code;
}xj_cmd_1101;

//1102
typedef struct {
  uint8_t    update_result;
  uint8_t	md5[32];
  uint32_t 	serial_code;
}xj_cmd_1102;

// 1303
typedef struct {
  uint8_t    cmd_type;
  uint32_t	fee_data[48];
  uint32_t 	serial_code;
}xj_cmd_1303;

//1304
typedef struct {
  uint32_t	fee_data[48];
  uint32_t 	serial_code;
}xj_cmd_1304;


// 1305
typedef struct {
  uint8_t    cmd_type;
  uint8_t	gun_index;
  uint32_t	fee_data[48];
  uint32_t 	serial_code;
}xj_cmd_1305;

//1306
typedef struct {
  uint8_t    cmd_type;
  uint8_t	gun_index;
  uint32_t	fee_data[48];
  uint32_t 	serial_code;
}xj_cmd_1306;

typedef struct {
  uint32_t 	serial_code;
  uint16_t data_len;
  uint8_t class_num;
  uint8_t data[CMD1309_MAX_DATA_LEN];
}xj_cmd_1309;

typedef struct {
  uint32_t 	serial_code;
  uint8_t set_result;
}xj_cmd_1310;



typedef void* callback_cmd_received(void* param,xj_mqtt_cmd_enum cmd_type);

typedef struct{
	uint32_t 	masterSerialNum;
	uint16_t	heart_index;
	char* 		address;
	char* 		port;
	char* 		username;
	char* 		password;
	char* 		topic;
	struct	mqtt_client client;
	xj_pal_socket_handle 		*sockfd;
	mqtt_pal_mutex_t sendlock;
	xj_pal_thread_t client_daemon;
	callback_cmd_received* callback;
	uint8_t	sendbuf[MQTT_SNED_BUF_LEN];
	uint8_t  recvbuf[MQTT_RECV_BUF_LEN];
}mqttBase;


typedef struct{
	uint16_t 	header_code;
	uint16_t 	length_code;
	uint32_t   	version_code;
	uint32_t   	serial_code;
	uint16_t  	cmd_code;
	int8_t  	check_code;
	uint8_t*  	datas;
}protocol_base;

typedef struct{
	xj_cmd_33	cmd_33;
	uint8_t	recv_cmd_33_flag;
	xj_cmd_35	cmd_35;
	uint8_t	recv_cmd_35_flag;
	xj_cmd_41	cmd_41;
	uint8_t	recv_cmd_41_flag;
	xj_cmd_81 	cmd_81;
	uint8_t	recv_cmd_81_flag;
	xj_cmd_101	cmd_101;
	uint8_t	recv_cmd_101_flag;
	xj_cmd_103 	cmd_103;
	uint8_t	recv_cmd_103_flag;
	xj_cmd_105	cmd_105;
	uint8_t	recv_cmd_105_flag;
	xj_cmd_107	cmd_107;
	uint8_t	recv_cmd_107_flag;
	xj_cmd_113	cmd_113;
	uint8_t	recv_cmd_113_flag;
	xj_cmd_117	cmd_117;
	uint8_t	recv_cmd_117_flag;
	xj_cmd_119	cmd_119;
	uint8_t	recv_cmd_119_flag;
	xj_cmd_201	cmd_201;
	uint8_t	recv_cmd_201_flag;
	xj_cmd_201	cmd_205;
	uint8_t	recv_cmd_205_flag;
	xj_cmd_301	cmd_301;
	uint8_t	recv_cmd_301_flag;

    xj_cmd_303	cmd_303;
    uint8_t	recv_cmd_303_flag;
    xj_cmd_305	cmd_305;
    uint8_t	recv_cmd_305_flag;
    xj_cmd_307	cmd_307;
    uint8_t	recv_cmd_307_flag;
    xj_cmd_309	cmd_309;
    uint8_t	recv_cmd_309_flag;
    xj_cmd_311	cmd_311;
    uint8_t	recv_cmd_311_flag;
}recv_protocol_base;


extern mqttBase mqttMessageBase;

void* client_refresher(void* client);
void* xj_mqtt_send_cmd(xj_mqtt_cmd_enum cmd_type, void * cmd, uint16_t time_out, uint8_t Qos);
void xj_mqtt_disconnect(void);
char xj_mqtt_connect(char* addr,int port,char* username,char* password,char* client_identifier, void*(*callback_cmd_received)(void* param,xj_mqtt_cmd_enum cmd_type));
void xj_mqtt_send_resp(xj_mqtt_cmd_enum cmd_type, void * cmd, uint8_t Qos);
extern void xj_pal_print_log(xj_log_type type, char* format, ... );
extern int8_t err_event_info_pop(int8_t flag,uint8_t gun_index,char *err_no);
#endif

