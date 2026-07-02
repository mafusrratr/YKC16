#include <ctype.h>

#include "zk/xiaojusdk/include/redefine.h"
#include "zk/xiaojusdk/include/protocol.h"
#include "zk/xiaojusdk/include/xiaoju.h"
#include "zk/xiaojusdk/include/xiaoju_error.h"
#include "zk/xiaojusdk/include/xiaoju_phm.h"
#include "zk_common.h"
#define SIMULATOR_GUN_NUM   CHARGE_GUN_CNT

typedef     st_user_gun_info    simulated_gun_info  ;
typedef     st_user_bms_info    simulated_bms_info  ;
xj_bill_info recv_bill_data[2];
#define CONSOLE_BUFFER_LEN  512
#define ARGVS_MAX   16
#define log_file "order_info"
st_off_bms_bst_data data_310;
unsigned short m[49],n[49],o[49],u,y,z;
uint8_t update_bill_flag;
unsigned int bill_money[2];
uint8_t update_bill_number;
unsigned char update_fee_flag;
unsigned char* start_stop_reason;
unsigned char recv_bill_flag[2];
struct cmd_table
{
    const char *name;
    int (*cmd)(struct cmd_table *,int argc,char *const argv[]);
};
typedef struct cmd_table cmd_tbl_t;
/*
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
    uint8_t error_huifu;
    uint8_t* error_code_huifu;
    uint8_t heart_over_flag;
    uint8_t heart_over;
    uint8_t set_time_flag;
    uint8_t link_ok;
} st_usert_charge_info; //模拟充电桩信息 ，用户实际不需要此
*/
xj_sync_system_time_param time_app;

simulated_gun_info _simulator_gun_status[SIMULATOR_GUN_NUM];
simulated_bms_info _simulator_bms_status[SIMULATOR_GUN_NUM];

st_bms_basic_info _simulator_bms_basic_info[SIMULATOR_GUN_NUM];
st_bms_finish_statistical_data _simulator_bms_finsih_info[SIMULATOR_GUN_NUM];

st_usert_charge_info _simulated_info[SIMULATOR_GUN_NUM];//模拟桩端信息

st_xj_histroy_bill histroy_bill_info[MAX_HISTORY_ORDER_CNT];

int get_fee()
{
  //  if(argc >= 2)
    {
  //      int flag = atoi(argv[1]);
  //      printf("flag = %d\n",flag);
   //     if(flag == 1)
        {
            extern void _interval_work_for_sign_in(uint8_t clean_flag);
            printf("send 106 cmd...\n");
            _interval_work_for_sign_in(0);
        }
    }

    unsigned short w,x;
    m[48] = 65535;
    n[48] = 65535;
    o[48] = 65535;
    int pos = 0;
    printf("charge_fee: \n");
    for(pos = 0;pos < 48;pos++)
    {
        m[pos] = xj_get_charge_fee(pos);
        printf("[%d]: %d  ",pos,m[pos]);
    }
    printf("\n");
    for(pos=0;pos<49;pos++)
    {
        if(m[pos] != w)
        {
            if(pos == 0)
            {
                w = m[pos];
                x = 0;
                continue;
            }
            printf("%02d:%02d-%02d:%02d   %d\n",(x*30/60),x*30%60,(pos)*30/60,(pos)*30%60,w);
            w = m[pos];
            x = pos;
        }
    }

    printf("service_fee: \n");
    for(pos = 0;pos < 48;pos++)
    {
        n[pos] = xj_get_service_fee(pos);
        printf("[%d]: %hu  ",pos,n[pos]);
    }
    printf("\n");

    for(pos=0;pos<49;pos++)
    {
        if(n[pos] != w)
        {
            if(pos == 0)
            {
                w = n[pos];
                x = 0;
                continue;
            }
            printf("%02d:%02d-%02d:%02d   %d\n",(x*30/60),x*30%60,(pos)*30/60,(pos)*30%60,w);
            w = n[pos];
            x = pos;
        }
    }

    printf("demurrage: \n");
    for(pos = 0;pos < 48;pos++)
    {
        o[pos] = xj_get_demurrage(pos);
        printf("[%d]: %hu  ",pos,o[pos]);
    }
    printf("\n");
    for(pos=0;pos<49;pos++)
    {
        if(o[pos] != w)
        {
            if(pos == 0)
            {
                w = o[pos];
                x = 0;
                continue;
            }
            printf("%02d:%02d-%02d:%02d   %d\n",(x*30/60),x*30%60,(pos)*30/60,(pos)*30%60,w);
            w = o[pos];
            x = pos;
        }
    }
    return 0;
}

/* 模拟车端bms信息 */
void *producer_car_fn(void *args)
{
    args = args;
    int soc = 0;
    int meter = 0;
    int pos = 0;
    if(! _simulator_bms_status ||
       ! _simulator_gun_status)
    {
        printf("Invalid bms or gun status info\n");
        return NULL;
    }

    srand(time(NULL)); /* for meter count*/

    while(1)
    {
        for(pos = 0;pos < SIMULATOR_GUN_NUM;pos++)
        {
            if(_simulated_info[pos].internal_status == xj_work_stat_type_idle)
            {
                _simulator_bms_status[pos].bcl_voltage_need = 0; // 电压需求 分辨率：0.1V
	            _simulator_bms_status[pos].bcl_current_need = 0;// 电流需求      分辨率：0.1A，-400A偏移量
	            _simulator_bms_status[pos].bcl_charge_mode = 0x01;// 充电模式       0x01表示恒压充电，0x02表示恒流充电
	            _simulator_bms_status[pos].bcs_test_voltage = 0;// 充电电压测量值          分辨率：0.1V
	            _simulator_bms_status[pos].bcs_test_current = 0;// 充电电流测量值          分辨率：0.1A，-400A偏移量
                _simulator_bms_status[pos].bcs_max_single_voltage = 0; // 最高单体动力蓄电池电压              分辨率：0.01V，数据范围: 0~24 V;
                _simulator_bms_status[pos].bcs_min_single_voltage = 0; // 最低单体动力蓄电池电压              分辨率：0.01V，数据范围: 0~24 V;
	            _simulator_bms_status[pos].bcs_max_single_no = 0;// 最高单体动力蓄电池组号              分辨率1/位，范围0-15

                _simulator_bms_status[pos].bcs_current_soc = 0;// 当前荷电状态soc%       分辨率：1%/位，0-100%
	            _simulator_bms_status[pos].charge_time_left = 0;// 估算剩余充电时间
	            _simulator_bms_status[pos].bsm_single_no = 0;// 最高单体动力蓄电池电压所在编号
                _simulator_bms_status[pos].bsm_max_temperature = 0; // 最高动力蓄电池温度
	            _simulator_bms_status[pos].bsm_max_temperature_check_no = 0;// 最高温度检测点编号
	            _simulator_bms_status[pos].bsm_min_temperature = 0;// 最低动力蓄电池温度
	            _simulator_bms_status[pos].bsm_min_temperature_check_no = 0;// 最低动力蓄电池温度检测点编号
	            _simulator_bms_status[pos].bsm_voltage_too_high_or_too_low = 0;// 单体动力蓄电池电压过高或过低
	            _simulator_bms_status[pos].bsm_car_battery_soc_too_high_or_too_low = 0;// 整车动力蓄电池荷电状态soc过高或过低
	            _simulator_bms_status[pos].bsm_car_battery_charge_over_current = 0;// 动力蓄电池充电过电流
	            _simulator_bms_status[pos].bsm_battery_temperature_too_high =10; // 动力蓄电池温度过高
	            _simulator_bms_status[pos].bsm_battery_insulation_state =0;// 动力蓄电池绝缘状态
	            _simulator_bms_status[pos].bsm_battery_connect_state = 0;// 动力蓄电池组输出连接器连接状态
	            _simulator_bms_status[pos].bsm_allow_charge = 0;// 允许充电

	            _simulator_bms_status[pos].dc_charge_voltage = 0;//直流充电电压
	            _simulator_bms_status[pos].dc_charge_current = 0;//直流充电电流

                _simulator_gun_status[pos].soc_percent = 6;//当前电量SOC 单位1%
	            _simulator_gun_status[pos].dc_charge_voltage = 2;//直流充电电压
	            _simulator_gun_status[pos].dc_charge_current = 3;//直流充电电流
	            _simulator_gun_status[pos].bms_need_voltage = 4;//BMS需求电压
	            _simulator_gun_status[pos].bms_need_current = 5;//BMS需求电流
	            _simulator_gun_status[pos].ac_a_vol = 6;//交流A相充电电压
	            _simulator_gun_status[pos].ac_b_vol = 7;//交流B相充电电压
	            _simulator_gun_status[pos].ac_c_vol = 8;//交流C相充电电压
	            _simulator_gun_status[pos].ac_a_cur = 9;//交流A相充电电流
	            _simulator_gun_status[pos].ac_b_cur = 10;//交流B相充电电流
	            _simulator_gun_status[pos].ac_c_cur = 11;//交流C相充电电流

	            _simulator_gun_status[pos].meter_kwh_num = _simulated_info[pos].current_meter_kwh;/* 当前电表读数 int32->int64*/
	            //printf("idle_meter = %lx\n",_simulator_gun_status[pos].meter_kwh_num);
                _simulator_gun_status[pos].charge_power_kw = 0;//充电功率

                continue;
            }
            /* 自检 */
            if(_simulated_info[pos].internal_status == xj_work_stat_type_bist)
            {
                _simulator_bms_basic_info[pos].brm_bms_connect_version[0] = 0x01; //BRM-BMS通讯协议版本号
                _simulator_bms_basic_info[pos].brm_bms_connect_version[1] = 0x02; //BRM-BMS通讯协议版本号
                _simulator_bms_basic_info[pos].brm_bms_connect_version[2] = 0x03; //BRM-BMS通讯协议版本号
                _simulator_bms_basic_info[pos].brm_bms_connect_version[3] = 0x00; //BRM-BMS通讯协议版本号
                _simulator_bms_basic_info[pos].brm_battery_type = xj_battery_type_sanyuan;//电池类型
                _simulator_bms_basic_info[pos].brm_battery_power = 123;//整车动力蓄电池系统额定容量/Ah
                _simulator_bms_basic_info[pos].brm_battery_volt = 500;// 整车动力蓄电池系统额定总电压
                _simulator_bms_basic_info[pos].brm_battery_supplier = 66; // 电池生产厂商
                _simulator_bms_basic_info[pos].brm_battery_seq = 345;// 电池组序号
                _simulator_bms_basic_info[pos].brm_battery_produce_year = 2020;// 电池组生厂日期：年
                _simulator_bms_basic_info[pos].brm_battery_produce_month = 6;// 电池组生厂日期：月
                _simulator_bms_basic_info[pos].brm_battery_produce_day = 17;// 电池组生厂日期：日
                _simulator_bms_basic_info[pos].brm_battery_charge_count = 10;// 电池组充电次数
                _simulator_bms_basic_info[pos].brm_battery_property_identification = 678; // 电池组产权标识
                if(_simulator_bms_basic_info[pos].brm_vin[0] == '\0')
                    memcpy(&_simulator_bms_basic_info[pos].brm_vin[0],"UUJJXXVVKKRRW0001",17);// 车辆识别码vin
                uint64_t bms_ver = 0xffffffE407010101;
                memcpy(&_simulator_bms_basic_info[pos].brm_bms_software_version[0],(char *)&bms_ver,8);// BMS软件版本号
                _simulator_bms_basic_info[pos].bcp_max_voltage = 500;// 单体动力蓄电池最高允许充电电压
                _simulator_bms_basic_info[pos].bcp_max_current = 100;// 最高允许充电电流
                _simulator_bms_basic_info[pos].bcp_max_power = 100;// 动力蓄电池标称总能量
                _simulator_bms_basic_info[pos].bcp_total_voltage = 500;// 最高允许充电总电压
                _simulator_bms_basic_info[pos].bcp_max_temperature = 100;// 最高允许温度
                _simulator_bms_basic_info[pos].bcp_battery_soc = 100;// 整车动力蓄电池荷电状态
                _simulator_bms_basic_info[pos].bcp_battery_soc_current_voltage = 200; // 整车动力蓄电池当前电池电压
                _simulator_bms_basic_info[pos].bro_bms_is_ready = 2; // 是否充电准备好
                _simulator_bms_basic_info[pos].CRO_isReady = 1;// CRO-充电机是否充电准备好

                continue;
            }

            if(_simulated_info[pos].internal_status == xj_work_stat_type_charging)
            {

                _simulator_bms_status[pos].bcl_voltage_need = 1000; // 电压需求 分辨率：0.1V
	            _simulator_bms_status[pos].bcl_current_need = 9000;// 电流需求      分辨率：0.1A，-400A偏移量
	            _simulator_bms_status[pos].bcl_charge_mode = 0x01;// 充电模式       0x01表示恒压充电，0x02表示恒流充电
	            _simulator_bms_status[pos].bcs_test_voltage = 100;// 充电电压测量值          分辨率：0.1V
	            _simulator_bms_status[pos].bcs_test_current = 9000;// 充电电流测量值          分辨率：0.1A，-400A偏移量
                _simulator_bms_status[pos].bcs_max_single_voltage = 100; // 最高单体动力蓄电池电压              分辨率：0.01V，数据范围: 0~24 V;
                _simulator_bms_status[pos].bcs_min_single_voltage = 100; // 最低单体动力蓄电池电压              分辨率：0.01V，数据范围: 0~24 V;
	            _simulator_bms_status[pos].bcs_max_single_no = 10;// 最高单体动力蓄电池组号              分辨率1/位，范围0-15
	            soc++;
                if(soc % 10 == 0)
                    _simulator_bms_status[pos].bcs_current_soc += 1;// 当前荷电状态soc%       分辨率：1%/位，0-100%
	            _simulator_bms_status[pos].charge_time_left = 110;// 估算剩余充电时间
	            _simulator_bms_status[pos].bsm_single_no = 120;// 最高单体动力蓄电池电压所在编号
                _simulator_bms_status[pos].bsm_max_temperature = 340; // 最高动力蓄电池温度
	            _simulator_bms_status[pos].bsm_max_temperature_check_no = 550;// 最高温度检测点编号
	            _simulator_bms_status[pos].bsm_min_temperature = 330;// 最低动力蓄电池温度
	            _simulator_bms_status[pos].bsm_min_temperature_check_no = 330;// 最低动力蓄电池温度检测点编号
	            _simulator_bms_status[pos].bsm_voltage_too_high_or_too_low = 440;// 单体动力蓄电池电压过高或过低
	            _simulator_bms_status[pos].bsm_car_battery_soc_too_high_or_too_low = 340;// 整车动力蓄电池荷电状态soc过高或过低
	            _simulator_bms_status[pos].bsm_car_battery_charge_over_current = 340;// 动力蓄电池充电过电流
	            _simulator_bms_status[pos].bsm_battery_temperature_too_high = 100; // 动力蓄电池温度过高
	            _simulator_bms_status[pos].bsm_battery_insulation_state = 250;// 动力蓄电池绝缘状态
	            _simulator_bms_status[pos].bsm_battery_connect_state = 220;// 动力蓄电池组输出连接器连接状态
	            _simulator_bms_status[pos].bsm_allow_charge = 0;// 允许充电

	            _simulator_bms_status[pos].dc_charge_voltage = 1230;//直流充电电压
	            _simulator_bms_status[pos].dc_charge_current = 220;//直流充电电流
	            if(_simulated_info[pos].soc_flag == 0)
                {
                    if(soc % 10 == 0)
                        _simulator_gun_status[pos].soc_percent += 2;//当前电量SOC 单位1%
                }

                //printf("charge-soc = %d \n",_simulator_gun_status[pos].soc_percent);
                if(_simulator_gun_status[pos].soc_percent >= 92)
                {
                    _simulated_info[pos].internal_status = xj_work_stat_type_stoping;
                    _simulated_info[pos].charge_flag = 0xf0; /* stop charge */
                }

	            _simulator_gun_status[pos].dc_charge_voltage = 330;//直流充电电压
	            _simulator_gun_status[pos].dc_charge_current = 440;//直流充电电流
	            _simulator_gun_status[pos].bms_need_voltage = 550;//BMS需求电压
	            _simulator_gun_status[pos].bms_need_current = 660;//BMS需求电流
	            _simulator_gun_status[pos].ac_a_vol = 770;//交流A相充电电压
	            _simulator_gun_status[pos].ac_b_vol = 880;//交流B相充电电压
	            _simulator_gun_status[pos].ac_c_vol = 330;//交流C相充电电压
	            _simulator_gun_status[pos].ac_a_cur = 440;//交流A相充电电流
	            _simulator_gun_status[pos].ac_b_cur = 550;//交流B相充电电流
	            _simulator_gun_status[pos].ac_c_cur = 660;//交流C相充电电流

                meter++;
                if(meter % 10 == 0)
                {
                    /* +1 使其落入 1-222区间 */
                    _simulated_info[pos].current_meter_kwh += rand() % 222 + 1;
                }

                _simulator_gun_status[pos].meter_kwh_num = _simulated_info[pos].current_meter_kwh;
                //printf("charge-meter =  %lx \n",_simulator_gun_status[pos].meter_kwh_num);

                _simulator_gun_status[pos].charge_power_kw = 3600;//充电功率

                continue;
            }
            /* 正常停充时进入 */
            if(_simulated_info[pos].internal_status == xj_work_stat_type_stoping)
            {
                _simulator_bms_finsih_info[pos].bsd_stop_soc = _simulator_gun_status[pos].soc_percent;
                _simulator_bms_finsih_info[pos].bsd_battery_low_voltage=2;
                _simulator_bms_finsih_info[pos].bsd_battery_high_voltage=2;
                _simulator_bms_finsih_info[pos].bsd_battery_low_temperature=2;
                _simulator_bms_finsih_info[pos].bsd_battery_high_temperature=2;
                _simulator_bms_finsih_info[pos].bem_error_spn2560_00=2;
                _simulator_bms_finsih_info[pos].bem_error_spn2560_aa=2;
                _simulator_bms_finsih_info[pos].bem_error_time_sync=2;
                _simulator_bms_finsih_info[pos].bem_error_ready_to_charge=2;
                _simulator_bms_finsih_info[pos].bem_error_receive_status=2;
                _simulator_bms_finsih_info[pos].bem_error_receive_stop_charge=2;
                _simulator_bms_finsih_info[pos].bem_error_receive_report=2;
                _simulator_bms_finsih_info[pos].bem_error_other=2;

                continue;
            }
        }
        xj_pal_msleep(500);
    }
    return NULL;
}

void *ctrl_pile_fn(void *args)
{
    args = args;
    uint8_t index = 0;
    int msglen = 0;
    uint8_t gun_cnt = (sizeof(_simulated_info) / sizeof(_simulated_info[0]));

    for(;;)
    {
        for(index = 0; index < gun_cnt; index++)
        {
            if(_simulated_info[index].connect_charge == 1)
            {
                _simulated_info[index].connect_charge = 0;
                if(_simulated_info[index].connection == 1)
                    xj_send_touch_gun_pluged_in(index);
                else
                    xj_send_touch_gun_pluged_out(index);
            }
            if(_simulated_info[index].heart_over_flag == 1)
            {
                _simulated_info[index].heart_over_flag = 0;
                if(_simulated_info[index].heart_over)
                {
                    xj_send_touch_error_occured(index,"3001");
                }else
                    xj_send_touch_error_recovered(index,"3001");
            }
            if(_simulated_info[index].error_chufa == 1)
            {
                xj_send_touch_error_occured(index,_simulated_info[index].error_code);
                _simulated_info[index].error_chufa = 0;
            }
            if(_simulated_info[index].error_huifu == 1)
            {
                xj_send_touch_error_recovered(index,_simulated_info[index].error_code_huifu);
                _simulated_info[index].error_huifu = 0;
            }
            if(_simulated_info[index].charge_flag == 0x0f) /* start charge success*/
            {
                _simulated_info[index].charge_flag = -1; /* clear */
                /* send charge cmd */
                printf("Now,Start Charge.......\n");

                //xj_pal_msleep(1 * 1000);/*Wait for the data to be ready */

                printf("########### vin_code = %s \n",_simulator_bms_basic_info[index].brm_vin);
                touch_send_bms_basic_info(index,_simulated_info[index].charge_id,&_simulator_bms_basic_info[index]);
                xj_send_touch_charge_start(index,XJ_SUCCEED_RESULT);

                _simulated_info[index].internal_status = xj_work_stat_type_charging;
            }else if(_simulated_info[index].charge_flag == 0x1f) /*start charge unsuccess*/
            {
                _simulated_info[index].charge_flag = -1; /* clear */
                /* send charge cmd */
                printf("Now,Start Charge Error....\n");

                //xj_pal_msleep(1 * 1000);/*Wait for the data to be ready */

                printf("########### vin_code = %s \n",_simulator_bms_basic_info[index].brm_vin);
                touch_send_bms_basic_info(index,_simulated_info[index].charge_id,&_simulator_bms_basic_info[index]);
                xj_send_touch_charge_start(index,start_stop_reason);//"0001");

                _simulated_info[index].internal_status = xj_work_stat_type_gun_not_pluged_out;
            }
            else if(_simulated_info[index].charge_flag == 0xf0) /* stop charge */
            {
                _simulated_info[index].charge_flag = -1; /* clear */

                printf("Now,Stop Charge.......\n");

                //xj_pal_msleep(1 * 1000);/*Wait for the data to be ready */

                /* 308 310 */
                st_off_bms_cst_data data_308;
                data_308.CST_error_reason = 0;
                data_308.CST_fault_reason = 0;
                data_308.CST_stop_reason = 0;
                touch_send_bms_cst_data (_simulated_info[index].gun,_simulated_info[index].charge_id,&data_308);

            //    st_off_bms_bst_data data_310;
            //    data_310.BST_error_reason = 0;
             //   data_310.BST_fault_reason = 0;
            //    data_310.BST_stop_reason = 0;
                touch_send_bms_bst_data (_simulated_info[index].gun,_simulated_info[index].charge_id,&data_310);

                touch_send_bms_inish_statistical_data(index,_simulated_info[index].charge_id,&_simulator_bms_finsih_info[index]);
                xj_send_touch_charge_stoped(index,start_stop_reason,XJ_SUCCEED_RESULT);

                _simulated_info[index].internal_status = xj_work_stat_type_gun_not_pluged_out;
            }
            if(VIN_INFO.cdz_port_info[index].send2host == YES)
            {
                printf("[%d]send vin %s\n",index,&VIN_INFO.cdz_port_info[index].cdz_info[VIN_INFO.cdz_port_info[index].out_pos].vin_buf[0]);
                printf("[%d]send vin %s\n",index+1,&VIN_INFO.cdz_port_info[index+1].cdz_info[VIN_INFO.cdz_port_info[index+1].out_pos].vin_buf[0]);
                xj_send_touch_vin_charge_reques(index,&VIN_INFO.cdz_port_info[index].cdz_info[VIN_INFO.cdz_port_info[index].out_pos].vin_buf[0]);
                VIN_INFO.cdz_port_info[index].send2host = NO;
                VIN_INFO.cdz_port_info[index].out_pos++;
                if(VIN_INFO.cdz_port_info[index].out_pos >= MAX_CDZ_NUM)
                {
                        VIN_INFO.cdz_port_info[index].out_pos=0;
                }
            }
        }

        //xj_pal_msleep(500);/* 500ms */
    }
}

char Save_info_file(char *log,int size)
{
    FILE *fp = NULL;
    fp = fopen(log_file,"w");
    if(NULL == fp)
    {
            return 0;
    }
    unsigned short size1;
    for(size1 = 0;size1<size;size1++)
        fputc(log[size1],fp);
/*    if(-1 == fputs(log,fp) )
    {
            fclose(fp);
            return 0;
    }
*/
    fclose(fp);
    return 1;
}
unsigned int Read_info_file(char* log,unsigned int size)
{
    FILE *fp = NULL;
    char c;
    fp = fopen(log_file,"r");
    if(NULL == fp)
    {
            return 0;
    }
    unsigned int m=0;
    for(m;m<size;m++)
    {
        c = fgetc(fp);
        log[m] = c;
        printf("0x%02x ",c);
    }
    printf("\n*****m=%d\n",m);
 /*   while((c = fgetc(fp))!=EOF)
    {
        log[m] = c;
        printf("0x%02x ",c);
        m++;
    }*/
/*    if(-1 == fputs(log,fp) )
    {
            fclose(fp);
            return 0;
    }
*/
    fclose(fp);
    return m;
}

int8_t callback_software_download(xj_software_upgrade_param *param )
{
    uint8_t update_result = 0;//模拟成功
    char check_md5[XJ_MAX_CHECKSUM_LEN];
    uint32_t serial_code = param ->serial_code;//回复相同即可

    //模拟下载文件并校验
    memcpy(check_md5,param->md5,XJ_MAX_CHECKSUM_LEN);//模拟成功下载计算md5

    touch_send_1102_code(update_result,check_md5,serial_code);//此为模拟下载成功后，回复1102
}

void callback_stop_charge(uint8_t gun_cnt,char* err)
{
    strcpy(err,XJ_SUCCEED_RESULT);//模拟正常情况 仅代表收到停止命令

    /* flags */
    _simulated_info[gun_cnt].internal_status = xj_work_stat_type_stoping;
    UNI_CMD_INTERFACE* cmd_to_BCU = (UNI_CMD_INTERFACE* )&cmd_info.cmd_HOST_to_BCU.uni_cmd_interface[0];
    cmd_to_BCU[cmd_info.cmd_HOST_to_BCU.cmd_in_pos].cmd_port_no = gun_cnt;
    UNI_CMD_QUEUE* q = &(cmd_info.cmd_HOST_to_BCU);
    q->uni_cmd_interface[q->cmd_in_pos].cmd_type = 0x41;
    q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf[9] = 0;
  //  memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-12-1);
    q->uni_cmd_interface[q->cmd_in_pos].cmd_len = 10;//datalen-12-1;
  //  if(dstPole == 1)
    {
            q->cmd_in_pos++;
            if (q->cmd_in_pos >= MAX_CMD_NUM)
                    q->cmd_in_pos = 0;
    }
    printf("recv stop\n");
    //_simulated_info[gun_cnt].charge_flag = 0xf0; /* stop charge */
}

void callback_start_charge(uint8_t gun_cnt,char* err,char* id,uint8_t id_len,uint16_t user_tel,uint8_t charge_policy,uint32_t charge_policy_param)
{
    recv_bill_flag[gun_cnt] = 0;
    memset((char*)&_simulated_info[gun_cnt].charge_id,00,XJ_MAX_CHAEGE_USER_ID_LEN);
    memcpy(_simulated_info[gun_cnt].charge_id,id,id_len);
    _simulated_info[gun_cnt].phone = user_tel;
    printf("user tel = %d\n",user_tel);

    //_simulator_gun_status[gun_cnt].soc_percent = 6;

    strcpy(err,XJ_SUCCEED_RESULT);//模拟正常情况

	xj_pal_print_log(xj_log_message,"gun %d start charge type %d ;param %d\n",gun_cnt,charge_policy,charge_policy_param);
    /* flags */
    _simulated_info[gun_cnt].internal_status = xj_work_stat_type_bist;
    UNI_CMD_INTERFACE* cmd_to_BCU = (UNI_CMD_INTERFACE* )&cmd_info.cmd_HOST_to_BCU.uni_cmd_interface[0];
    cmd_to_BCU[cmd_info.cmd_HOST_to_BCU.cmd_in_pos].cmd_port_no = gun_cnt;
    UNI_CMD_QUEUE* q = &(cmd_info.cmd_HOST_to_BCU);
    q->uni_cmd_interface[q->cmd_in_pos].cmd_type = 0x41;
    q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf[9] = 1;
   // memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-12-1);
    q->uni_cmd_interface[q->cmd_in_pos].cmd_len = 10;//datalen-12-1;
//    if(dstPole == 1)
    {
            q->cmd_in_pos++;
            if (q->cmd_in_pos >= MAX_CMD_NUM)
                    q->cmd_in_pos = 0;
    }
    printf("recv start\n");
    //_simulated_info[gun_cnt].charge_flag = 0x0f; /* start charge */
}

int8_t callback_power_control(uint8_t gun_cnt,uint32_t kwh)
{
	xj_pal_print_log(xj_log_message,"kwh control  gun = %d ;kwh %d\n",gun_cnt,kwh);
	return 0;
}

//举例
void callback_xj_param_set()
{
	uint8_t i = 0;
	uint8_t param[25];
	for(i = 0;i < 25;i++){
		param[i] = xj_phm_read_byte(i);
	}
	//write flash
}

void callback_display_qr(uint8_t gun_cnt,uint8_t*qr_str,uint16_t len,char* err)
{
    xj_pal_print_log(xj_log_message,"simulator Qr code setting  gun = %d ;qr=%s ;len = %d\n",gun_cnt,qr_str,len);
    memcpy(hmi_info.bcu_info_to_hmi[gun_cnt].pole_ini.poleId,qr_str,len);
    printf("[%d] %s\n",gun_cnt,hmi_info.bcu_info_to_hmi[gun_cnt].pole_ini.poleId);
    _simulated_info[0].link_ok = 1;
    strcpy(err,XJ_SUCCEED_RESULT);//模拟正常情况
}

void callback_set_sys_time(xj_sync_system_time_param time,char* err)
{
    xj_pal_print_log(xj_log_message,"simulator set time %04d-%02d-%02d  %02d:%02d %02d\n",time.year,time.month,time.day,time.hour,time.minute,time.sec);
    memcpy(&time_app.year,&time.year,sizeof (xj_sync_system_time_param));
    strcpy(err,XJ_SUCCEED_RESULT);//模拟正常情况
    _simulated_info[0].set_time_flag = 1;
}

//err 0表示成功，1长度错误2订单号错误3超过最大范围4超过最小范围5其它
void callback_safety_protect_param(xj_l2_safety_prot_param *param,uint8_t *err)
{
	xj_pal_print_log(xj_log_message,"%d\tparam4-1%d\tparam4-2%d\n"
			"param5-1%d\tparam5-2%d\tparam5-3%d\nparam6-1%d\tparam6-2%d\tparam6-3%d\n"
			"param7-1%d\tparam7-2%d\tparam7-3%d\nparam8-1%d\tparam8-2%d\tparam8-3%d\tparam8-4%d\n"
			"param9 %d\nparam10 %d\nparam11 %d\nparam12 %d\n",
			param->gun_index,param->safety4.param_1,param->safety4.param_2,param->safety5.param_1,
			param->safety5.param_2,param->safety5.param_3,param->safety6.param_1,
			param->safety6.param_2,param->safety6.param_3,param->safety7.param_1,
			param->safety7.param_2,param->safety7.param_3,param->safety8.param_1,
			param->safety8.param_2,param->safety8.param_3,param->safety8.param_4,
			param->safety9,param->safety10,param->safety11,param->safety12);
}

int8_t callback_save_bill(char* bill,uint32_t size)
{
    //用户实现保存账单
    //update_bill_flag = 1;   //fqh linshi quxiao
    printf("**************size=%d\n",size);
    st_xj_histroy_bill *save_bile;
    printf("Write bill to file\n");
    if(Save_info_file(bill,size))
        {
        printf("SAVE success\n");
    }
    else
        printf("save fail\n");
    save_bile = bill;
    printf("statue = %d [%d] start_year=%d\n",save_bile->status,save_bile->bill.gun_index,save_bile->bill.charge_start_time_year);
    bill_money[save_bile->bill.gun_index] = save_bile->bill.total_charge_fee + save_bile->bill.total_service_fee;
    unsigned int    i;
    for(i=0; i<size; i++)
    {
        printf("0x%02x ",(unsigned char)bill[i]);
        //if( 0 == (i+1)%16 )
         //   printf("\n");
    }
    printf("\n");
    printf("**************\n");
    xj_pal_print_log(xj_log_message,"bill_sizeof=%d sizeof(st_xj_history_bill)= %d\n",sizeof(bill),sizeof(st_xj_histroy_bill));
    xj_pal_print_log(xj_log_message,"printf bill ****%s****\n",bill);
    update_bill_number = sizeof (bill)/sizeof (st_xj_histroy_bill);
    xj_pal_print_log(xj_log_message,"save_bill bill_size=%d bill_number = %d\n",size,update_bill_number);
    memcpy(&histroy_bill_info[0].status,bill,size);
    xj_pal_print_log(xj_log_message,"bill_status = %d gun=%d\n",histroy_bill_info[0].status,histroy_bill_info[0].bill.gun_index);
    xj_pal_print_log(xj_log_message,"bill is %s\n",bill);
    return 0;
}

int8_t callback_read_bill(char* bill,uint32_t size)
{
    //用户实现读取账单
    unsigned int i;
    printf("callback_read_bill\n");
    if(Read_info_file(bill,size))
        printf("read ok\n");
    else
        printf("read fail\n");
    for(i=0; i<size; i++)
    {
        printf("0x%02x ",(unsigned char)bill[i]);
        //if( 0 == (i+1)%16 )
         //   printf("\n");
    }
    printf("\n");
    printf("**************\n");
    return 0;
}

int8_t callback_upload_log(const xj_upload_log_param* upload_url,char* log_name,uint16_t max_len)
{
    //用户实现上传日志
    return 0;
}


int8_t callback_show_bill(xj_bill_info bill_data,uint8_t gun_index)
{
    //以下为测试段
    memcpy(&recv_bill_data[gun_index].gun_index,&bill_data.gun_index,sizeof(xj_bill_info));
   // &recv_bill_data[gun_index] = bill_data;
    xj_pal_print_log(xj_log_message,"Test: gun = %d ,bill id:%s ,kwh:%d ,charge_fee:%d ,service_fee:%d \n",gun_index,bill_data.charge_user_id,
                     bill_data.charge_kwh_amount,bill_data.total_charge_fee,bill_data.total_service_fee);
    recv_bill_flag[gun_index] = 1;
    return 0;
}

int8_t callback_lock_control(uint8_t gun_cnt,uint8_t type)
{
    //模拟桩端电子锁控制
    return 0;//模拟成功
}


//vin码启动充电，鉴权反馈回调
int8_t callback_vin_start_charge_result(st_user_vin_send_result result)
{
    //此为模拟vin码启动充电。
    printf("####### vin_result = %d \n",result.Request_result);
    if(0 == result.Request_result)
    {
        recv_bill_flag[result.gun_index] = 0;
        uint8_t err[5];
        VIN_INFO.cdz_port_info[result.gun_index].result_flag = YES;
        _simulated_info[result.gun_index].phone = result.phone;
        printf("[%d]phone = %d\n",result.gun_index,_simulated_info[result.gun_index].phone);
        //注意！：实际桩端一定是先启动充电并自检获取了vin码，回调应该实现获取鉴权成功，开始功率输出。demo仅做接口测试使用
        //callback_start_charge(result.gun_index,err,result.charge_user_id,strnlen(result.charge_user_id,32),result.phone,0,0);
        return 0;
    }
    else
    {
        bcu_info.module_comm_state.error_flag[result.gun_index] = 1;
        VIN_INFO.cdz_port_info[result.gun_index].result_flag = NO;
        //注意：实际桩在鉴权失败后了立即停止充电
        return -1;
    }
}
/*******************************
 *参数说明 vin_data :
 *		 vin_data_result:
 *
 *
 */
void callback_set_vin_white_list(T_VIN_SET *vin_data,T_VIN_SET *vin_data_result)
{
	//VIN码判断清除 写入用例
	/*
	int i = 0,j = 0,vin_i = 0,clean_flag = 0,add_flag = 0;
	char vin_null[17] = {0};
	for(j = 0; j < vin_data->white_list_num; j++){
		clean_flag = 0;
		//判别是否清除VIN
		for(i = 0; i < sizeof(vin_code.vin_list)/sizeof(vin_code.vin_list[0]);i++){
			if(0 == strcmp(vin_code.vin_list[i],vin_data->vin_white_list[j].vin)){
				if(2 == vin_data->vin_white_list[i].state){
					memset(vin_code.vin_list[i],0x0,sizeof(vin_code.vin_list[i]));
					//清除VIN成功
					vin_data_result->vin_white_list[j].state = 0;
				}else if(1 == vin_data->vin_white_list[i].state){
					//写入VIN失败 VIN码已存在
					vin_data_result->vin_white_list[j].state = 1;
					printf("%d vin is exist \n",j);
				}else{
					printf("vin set param error \n");
				}
				clean_flag = 1;
			}
		}
		//判别是否写入VIN
		if((0 == clean_flag) && (1 == vin_data->vin_white_list[j].state)){
			for(i = 0; i < sizeof(vin_code.vin_list)/sizeof(vin_code.vin_list[0]);i++){
				if(0 == strcmp(vin_null,vin_code.vin_list[i])){
					memcpy(vin_code.vin_list[i],vin_data->vin_white_list[j].vin,sizeof(vin_code.vin_list[i]));
					//写入VIN成功
					vin_data_result->vin_white_list[j].state = 0;
				}
			}
		}else if((0 == clean_flag) && (2 == vin_data->vin_white_list[j].state)){
			//清除失败，内存中无此VIN码
			vin_data_result->vin_white_list[j].state = 1;
			printf("%d vin clean error \n",j);
		}
		memcpy(vin_data_result->vin_white_list[j].vin,vin_data->vin_white_list[j].vin,sizeof(vin_data->vin_white_list[j].vin));
	}
	WriteLocalVinList(vin_code.vin_list,sizeof(vin_code.vin_list));
	 */
}

void callback_set_card_white_list(T_CARD_SET *card_data,T_CARD_SET *card_data_result)
{
	//参照VIN码
}

//0-success 1-fail
void callback_clean_vin_white_list(int8_t *result)
{
	*result = 0;
}

//0-success 1-fail
void callback_clean_card_white_list(int8_t *result)
{
	//
	*result = 0;
}


int8_t callback_dev_reboot_control(uint8_t dev_num, uint8_t type)
{
	exit(0);
    return -1;
}
/*************** command *********************/

int do_help(struct cmd_table *table,int argc,char *const argv[])
{
    printf("Cmd List\n");
    printf("       pluged_in    [gun_index (index from 0) ]\n");
    printf("       pluged_out   [gun_index (index from 0) ]\n");
    printf("       error_occured  [gun_index (index from 0) ] [error_code]\n");
    printf("       error_recovered [gun_index (index from 0) ] [error_code]\n");
    printf("       set_vin [gun_index] [vin_code]\n");
    printf("       set_card\n");
    printf("       set_warning [gun_index] [warning_code]\n");
    printf("       ctrl_soc [gun_index]\n");
    printf("       get_fee [1(send 106 cmd)]\n");
    printf("       show_maxpower [gun_index]\n");
    return 0;
}

int do_pluged_in(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 2)
    {
        printf("[pluged_in] invalid params\n");
        return 1;
    }

    int gun_index = atoi(argv[1]);
    if(_simulated_info[gun_index].internal_status != xj_work_stat_type_idle)
    {
        printf("work state error ,current work_state = %d \n",_simulated_info[gun_index].internal_status);
        return 1;
    }

    xj_send_touch_gun_pluged_in(gun_index);
    printf("pluged in.....\n");
    return 0;
}

int do_pluged_out(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 2)
    {
        printf("[pluged_out] invalid params\n");
        return 1;
    }
    int gun_index = atoi(argv[1]);
    if(_simulated_info[gun_index].internal_status == xj_work_stat_type_gun_not_pluged_out)
    {
        xj_send_touch_gun_pluged_out(gun_index);
        printf("pluged out.....\n");
    }
    else if(_simulated_info[gun_index].internal_status == xj_work_stat_type_bist ||
            _simulated_info[gun_index].internal_status == xj_work_stat_type_charging)
    {
        xj_send_touch_error_occured(gun_index,"100d");
        xj_send_touch_charge_stoped(gun_index,"100d",XJ_SUCCEED_RESULT);
        xj_send_touch_gun_pluged_out(gun_index);
    }
    _simulated_info[gun_index].internal_status = xj_work_stat_type_idle;
    return 0;
}

int do_error_occured(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 3)
    {
        printf("[error_occured] invalid params\n");
        return 1;
    }
    int gun_index = atoi(argv[1]);
    char *err_code = argv[2];
    if(_simulated_info[gun_index].internal_status == xj_work_stat_type_bist ||
            _simulated_info[gun_index].internal_status == xj_work_stat_type_charging)
    {
        xj_send_touch_error_occured(gun_index,err_code);
        xj_send_touch_charge_stoped(gun_index,err_code,XJ_SUCCEED_RESULT);
        _simulated_info[gun_index].internal_status = xj_work_stat_type_gun_not_pluged_out;
    }
    else
    {
        xj_send_touch_error_occured(gun_index,err_code);
    }
    return 0;
}

int do_error_recovered(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 3)
    {
        printf("[error_recovered] invalid params\n");
        return 1;
    }
    int gun_index = atoi(argv[1]);
    char *err_code = argv[2];

    xj_send_touch_error_recovered(gun_index,err_code);

    return 0;
}

int do_set_vin(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 3)
    {
        printf("invalid params\n");
        return 1;
    }

    int gun_index = atoi(argv[1]);
    char *vin_code = argv[2];
    if(vin_code)
        memcpy(&_simulator_bms_basic_info[gun_index].brm_vin[0],vin_code,17);
    xj_send_touch_vin_charge_reques(gun_index,vin_code);

    return 0;
}

int do_set_card(struct cmd_table *table,int arg,char *const argv[])
{


    return 0;
}

int do_set_warning(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 2)
    {
        printf("##### \n");
        return 1;
    }

    int gun_index = atoi(argv[1]);
    char *warning_code = argv[2];
    unsigned int th = 0;
    unsigned char type = 1;
    if(argc == 4)
    {
        th = atoi(argv[3]);
    }

     if(_simulated_info[gun_index].internal_status == xj_work_stat_type_bist ||
            _simulated_info[gun_index].internal_status == xj_work_stat_type_charging)
    {
        if(!th)
            xj_send_touch_warning_occured(gun_index,type,warning_code);
        else
            xj_send_warning(gun_index,type,warning_code,th);

        xj_send_touch_charge_stoped(gun_index,warning_code,XJ_SUCCEED_RESULT);
        _simulated_info[gun_index].internal_status = xj_work_stat_type_gun_not_pluged_out;
    }
    else
    {
         if(th)
            xj_send_warning(gun_index,type,warning_code,th);
         else
            xj_send_touch_warning_occured(gun_index,type,warning_code);
    }

    return 0;
}

int do_ctrl_soc(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 2)
    {
        printf("invalid params \n");
        return 1;
    }

    int gun_index = atoi(argv[1]);
    _simulated_info[gun_index].soc_flag = ~(_simulated_info[gun_index].soc_flag) & 0x01;
    if(_simulated_info[gun_index].soc_flag == 0)
    {
        printf("###### Soc Normal.....\n");
    }
    else if(_simulated_info[gun_index].soc_flag == 1)
    {
        printf("###### Soc Keeping....\n");
    }
    return 0;
}

int do_get_fee(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc >= 2)
    {
        int flag = atoi(argv[1]);
        printf("flag = %d\n",flag);
        if(flag == 1)
        {
            extern void _interval_work_for_sign_in(uint8_t clean_flag);
            printf("send 106 cmd...\n");
            _interval_work_for_sign_in(0);
        }
    }

    int pos = 0;
    printf("charge_fee: \n");
    for(pos = 0;pos < 48;pos++)
    {
        printf("[%d]: %d  ",pos,xj_get_charge_fee(pos));
    }
    printf("\n");

    printf("service_fee: \n");
    for(pos = 0;pos < 48;pos++)
    {
        printf("[%d]: %hu  ",pos,xj_get_service_fee(pos));
    }
    printf("\n");

    printf("demurrage: \n");
    for(pos = 0;pos < 48;pos++)
    {
        printf("[%d]: %hu  ",pos,xj_get_demurrage(pos));
    }
    printf("\n");

    return 0;
}

int do_show_maxpower(struct cmd_table *table,int argc,char *const argv[])
{
    if(argc < 2)
    {
        printf("invalid params\n");
        return 1;
    }
    int gun_index = atoi(argv[1]);

    printf("------------------------\n");
    printf("gun_num: [%d]\n",gun_index);
    printf("max_power: [%d]\n",xj_get_maxpower(gun_index));
    printf("------------------------\n");
    return 0;
}

static cmd_tbl_t g_cmds[] =
{
    {
        "help",do_help
    },
    {
        "pluged_in",do_pluged_in
    },
    {
        "pluged_out",do_pluged_out
    },
    {
        "error_occured",do_error_occured
    },
    {
        "error_recovered",do_error_recovered
    },
    {
        "set_vin",do_set_vin
    },
    {
        "set_card",do_set_card
    },
    {
        "set_warning",do_set_warning
    },
    {
        "ctrl_soc",do_ctrl_soc
    },
    {
        "get_fee",do_get_fee
    },
    {
        "show_maxpower",do_show_maxpower
    }
};

int readline(char *buffer)
{
    char *p = buffer;
    char *p_buf = p;
    int n = 0;
    int plen = 0;
    int col;
    char c;

    /* print prompt */
    const char *prompt = "Cmd# ";
    if(prompt)
    {
        plen = strlen(prompt);
        printf("%s",prompt);
    }
    col = plen;

    for(;;)
    {
        c = getchar();
        switch(c)
        {
        case '\r':
        case '\n':
            *p = '\0';
            printf("\r\n");
            return (p - p_buf);
        case '\0':
            continue;
        case 0x03:      /* ^C break*/
            p_buf[0] = '\0';
            return (-1);
        default:
            /* must be a normal character then */
            if(n < 512 - 2)
            {
                if(c == '\t')
                {

                }
                else
                {
                    ++col; /* echo input */
                    putchar(c);
                }
                *p++ = c;
                ++n;
            }
            else   /* buffer is full */
            {
                putchar('\a');
            }
        }
    }
    return n;
}

int cmd_process(int argc,char *argv[])
{
    int pos = 0;
    for(pos = 0; pos < sizeof(g_cmds) / sizeof(g_cmds[0]); pos++)
    {
        if(strcmp(argv[0],g_cmds[pos].name) == 0)
        {
            return g_cmds[pos].cmd(&g_cmds[pos],argc,argv);
        }
    }
    return 1; /* error */
}

int parse_line(char *line,char *argv[])
{
    int nargs = 0;

    printf ("parse_line: \"%s\"\n", line);

    while (nargs < 512)
    {

        /* skip any white space */
        while (isblank(*line))
            ++line;

        if (*line == '\0')  	/* end of line, no more args	*/
        {
            argv[nargs] = NULL;

            printf ("parse_line: nargs=%d\n", nargs);

            return (nargs);
        }

        argv[nargs++] = line;	/* begin of argument string	*/

        /* find end of string */
        while (*line && !isblank(*line))
            ++line;

        if (*line == '\0')  	/* end of line, no more args	*/
        {
            argv[nargs] = NULL;

            printf ("parse_line: nargs=%d\n", nargs);

            return (nargs);
        }

        *line++ = '\0';		/* terminate current arg	 */
    }

    printf ("** Too many args (max. %d) **\n", 512);

    printf ("parse_line: nargs=%d\n", nargs);

    return (nargs);
}

void run_command(char *cmd)
{
    char *argv[ARGVS_MAX + 1];
    int argc;
    argc = parse_line(cmd,argv);

    cmd_process(argc,argv);
}
void main_loop()
{
    int len;
    char buffer[CONSOLE_BUFFER_LEN] = "";

    for(;;)
    {
        len = readline(buffer);
        if(len > 0)
        {
            run_command(buffer);
            memset(buffer,0x00,sizeof(buffer));
        }
    }
}


int *xiaoju_main()
{
    update_fee_flag = 0;
    xj_pal_print_log(xj_log_message,"This is a demo！Current version %d.%d.%d.%d!\n",(XJ_VERSION_CODE&0x000000FF),(XJ_VERSION_CODE&0x0000FF00)>>8,(XJ_VERSION_CODE&0x00FF0000)>>16,(XJ_VERSION_CODE&0xFF000000)>>24);
    if(xj_APP_start(&_simulator_gun_status[0],&_simulator_bms_status[0],SIMULATOR_GUN_NUM)==0)
    {
        get_fee();
        update_fee_flag = 1;
    }
    uint8_t pos = 0;
    for(pos = 0; pos < SIMULATOR_GUN_NUM; pos++)
    {
        _simulated_info[pos].internal_status = xj_work_stat_type_idle;
        _simulated_info[pos].charge_flag = -1;
        _simulated_info[pos].soc_flag = 0;
        _simulated_info[pos].current_meter_kwh = 1234567890;
    }

    //xj_pal_thread_t producer_car;
    //xj_pal_thread_create("producer_car",&producer_car,producer_car_fn,NULL,xj_osPriorityHigh,XJ_OS_STACK_SIZE_MEDIUM);

    xj_pal_thread_t ctrl_pile;
    xj_pal_thread_create("ctrl_pile",&ctrl_pile,ctrl_pile_fn,NULL,xj_osPriorityHigh,XJ_OS_STACK_SIZE_MEDIUM);

   // main_loop();
}

