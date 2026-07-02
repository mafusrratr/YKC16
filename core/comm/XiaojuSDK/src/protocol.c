#include "zk/xiaojusdk/include/protocol.h"


mqttBase mqttMessageBase;
static recv_protocol_base recv_protocol;

static uint8_t _g_recv_data_bytes_buf[MQTT_RECV_BUF_LEN];
static uint8_t _g_send_data_bytes_buf[512];
static uint8_t _g_cmd_bytes_buf[MQTT_RECV_BUF_LEN];

static char xj_Socket_state = _DISCONNECT_;

int8_t  get_check_sum(int8_t* datas,uint16_t len);
int utf8_to_iso(uint8_t*in,int iLen,uint8_t *out);

void parse_protocol_header(protocol_base* protocol,uint16_t cmd,uint32_t serial_code,uint16_t len)
{
    protocol->cmd_code = cmd;
    protocol->header_code = CHARGER_IDENTIFIER_HEADER;
    protocol->version_code = XJ_VERSION_CODE;
    protocol->serial_code = serial_code;
    protocol->length_code = len;

    memset(protocol->datas,0x00,len);
    int count=0;
    memcpy(&protocol->datas[count],&protocol->header_code,sizeof(protocol->header_code));
    count += sizeof(protocol->header_code);
    memcpy(&protocol->datas[count],&protocol->length_code,sizeof(protocol->length_code));
    count += sizeof(protocol->length_code);
    memcpy(&protocol->datas[count],&protocol->version_code,sizeof(protocol->version_code));
    count += sizeof(protocol->version_code);
    memcpy(&protocol->datas[count],&protocol->serial_code,sizeof(protocol->serial_code));
    count += sizeof(protocol->serial_code);
    memcpy(&protocol->datas[count],&protocol->cmd_code,sizeof(protocol->cmd_code));
    count += sizeof(protocol->cmd_code);
}


uint8_t  mqtt_send_message(protocol_base* package,uint16_t len,uint32_t qos,uint8_t* logTitle)
{

    if(qos == 0)
    {
        qos = MQTT_PUBLISH_QOS_0;
    }
    else if(qos == 1)
    {
        qos = MQTT_PUBLISH_QOS_1;
    }
    else if(qos == 2)
    {
        qos = MQTT_PUBLISH_QOS_2;
    }
    else
    {
        qos = MQTT_PUBLISH_QOS_0;
    }

    mqtt_publish(&mqttMessageBase.client, mqttMessageBase.topic, (void*)package->datas, len, MQTT_PUBLISH_QOS_0);
    if (mqttMessageBase.client.error != MQTT_OK)
    {
        xj_pal_print_log(xj_log_message,"send error,errorcode=%x\n",mqttMessageBase.client.error);

    }
    if(NULL != logTitle)
        xj_pal_print_log(xj_log_message,logTitle);

    return ECO_TRUE;
}


// 1
int32_t recv_set_int_1_protocol(xj_cmd_1* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_1));

    int count=DATA_FIELD_COUNT;
    count += 4;
    param->cmd_type = datas[count++];
    memcpy(&(param->start_addr),&datas[count],sizeof(param->start_addr));
    count += sizeof(param->start_addr);
    param->cmd_num = datas[count++];
    memcpy(&param->cmd_len,&datas[count],sizeof(param->cmd_len));
    count += sizeof(param->cmd_len);
    if(1 == param->cmd_type)
    {
        if(1 > param->cmd_len || param->cmd_len > len  || CMD1_MAX_PARAM_LEN < param->cmd_len)
        {
            xj_pal_print_log(xj_log_remind,"recv_set_int_1_protocol.cmd_num wrong.\n");
            return 1;
        }
        else
        {
            memcpy((uint8_t*)param->data,&datas[count],param->cmd_len);
        }
    }

    return 0;
}

// 3
int32_t recv_set_string_3_protocol(xj_cmd_3* param,uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;

    if(param == NULL)
    {
        return 1;
    }
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_3));
    count += 4;
    param->cmd_type = datas[count++];
    memcpy(&(param->start_addr),&datas[count],sizeof(param->start_addr));
    count += sizeof(param->start_addr);
    memcpy(&param->cmd_len,&datas[count],sizeof(param->cmd_len));
    count += sizeof(param->cmd_len);

    if(param->cmd_type == 1)
    {
        if(param->cmd_len <1 || param->cmd_len>len)
        {
            xj_pal_print_log(xj_log_remind,"recv_set_string_3_protocol.cmd_num wrong\n");
            return 1;
        }
        if(param->cmd_len<256)
        {
            strcpy(param->data,&datas[count]);
        }
    }

    return 0;
}
// 5
int32_t recv_control_5_protocol(xj_cmd_5* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    memset(param,0x00,sizeof(xj_cmd_5));
    int count=DATA_FIELD_COUNT;
    count += 4;

    param->gun_index = datas[count++];
    memcpy(&(param->addr),&datas[count],sizeof(param->addr));
    count += sizeof(param->addr);
    param->cmd_num = datas[count++];
    memcpy(&param->cmd_len,&datas[count],sizeof(param->cmd_len));
    count += sizeof(param->cmd_len);

    if(param->cmd_len>len || param->cmd_len<1 || param->cmd_num<1)
    {
        xj_pal_print_log(xj_log_remind,"recv_control_5_protocol.param illegal\n");
        return 1;
    }
    memcpy((uint8_t*)param->cmd_param,&datas[count],param->cmd_len);
    return 0;
}
// 7
int32_t recv_start_charge_7_protocol(xj_cmd_7* param,uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;

    if(param == NULL)
    {
        return 1;
    }

    len = len;//warning
    memset(param,0x00,sizeof(xj_cmd_7));
    count += 2;
    memcpy((uint8_t*)&(param->user_tel),&datas[count],sizeof(param->user_tel));
    count += sizeof(param->user_tel);
    param->gun_index = datas[count++];
    memcpy((uint8_t*)&(param->charge_type),&datas[count],sizeof(param->charge_type));
    count += sizeof(param->charge_type);
    count += 4;
    memcpy((uint8_t*)&(param->charge_policy),&datas[count],sizeof(param->charge_policy));
    count += sizeof(param->charge_policy);

    memcpy((uint8_t*)&(param->charge_policy_param),&datas[count],sizeof(param->charge_policy_param));
    count += sizeof(param->charge_policy_param);
    memcpy(param->book_time,&datas[count],sizeof(param->book_time));
    count += sizeof(param->book_time);
    memcpy((uint8_t*)&(param->book_delay_time),&datas[count],sizeof(param->book_delay_time));
    count += sizeof(param->book_delay_time);
    strcpy((param->charge_user_id),&datas[count]);
    count += sizeof(param->charge_user_id);
    memcpy((uint8_t*)&(param->allow_offline_charge),&datas[count],sizeof(param->allow_offline_charge));
    count += sizeof(param->allow_offline_charge);
    memcpy((uint8_t*)&(param->allow_offline_charge_kw_amout),&datas[count],sizeof(param->allow_offline_charge_kw_amout));
    count += sizeof(param->allow_offline_charge_kw_amout);
    memcpy((uint8_t*)&(param->allow_offline_charge_kw_amout),&datas[count],sizeof(param->allow_offline_charge_kw_amout));
    count += sizeof(param->allow_offline_charge_kw_amout);
    memcpy((uint8_t*)&(param->charge_delay_fee),&datas[count],sizeof(param->charge_delay_fee));
    count += sizeof(param->charge_delay_fee);
    memcpy((uint8_t*)&(param->charge_delay_wait_time),&datas[count],sizeof(param->charge_delay_wait_time));
    count += sizeof(param->charge_delay_wait_time);

    return 0;
}
// 11
int32_t recv_stop_charge_11_protocol(xj_cmd_11* param,uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;

    if(param == NULL)
    {
        return 1;
    }
    len = len;//warning
    memset(param,0x00,sizeof(xj_cmd_11));

    strncpy((param->equipment_id),&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    param->gun_index = datas[count++];
    strncpy(param->charge_seq,&datas[count],sizeof(param->charge_seq));
    count += sizeof(param->charge_seq);

    return 0;
}

// 19
int32_t recv_power_control_19_protocol(xj_cmd_19* param,uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;
    if(param == NULL)
    {
        return 1;
    }
    len = len;//warning
    memset(param,0x00,sizeof(xj_cmd_19));
    strncpy((param->equipment_id),&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    param->gun_index = datas[count++];
    memcpy((uint8_t*)&(param->power_kwh),&datas[count],sizeof(param->power_kwh));
    count += sizeof(param->power_kwh);

    return 0;
}

// 23
int32_t recv_control_lock_23_protocol(xj_cmd_23* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    len = len;//warning
    memset(param,0x00,sizeof(xj_cmd_23));
    int count=DATA_FIELD_COUNT;
    count += 4;

    strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    param->gun_index = datas[count++];
    param->lock_type = datas[count++];
    return 0;
}

// 331
int32_t recv_clean_card_charge_331_protocol(xj_cmd_331* param,uint8_t* datas,uint16_t len)
{
	if(param == NULL)
	{
		return 1;
	}
	len = len;//warning
	memset(param,0x00,sizeof(xj_cmd_331));

	int count = DATA_FIELD_COUNT;

	strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
	count += sizeof(param->equipment_id);
	memcpy((uint8_t*)&param->reserve,&datas[count],sizeof(param->reserve));
	count += sizeof(param->reserve);

	return 0;
}


// 33
xj_cmd_33* recv_card_auth_33_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_33* param = &recv_protocol.cmd_33;
    if(param == NULL)
    {
        return param;
    }
    len = len;//warning
    memset(param,0x00,sizeof(xj_cmd_33));
    recv_protocol.recv_cmd_33_flag = ECO_TRUE;

    int count=DATA_FIELD_COUNT;

    strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->auth_result,&datas[count],sizeof(param->auth_result));
    count += sizeof(param->auth_result);
    memcpy((uint8_t*)&param->card_money,&datas[count],sizeof(param->card_money));
    count += sizeof(param->card_money);


    return param;
}



// 35
xj_cmd_35* recv_card_charge_35_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_35*  param = &recv_protocol.cmd_35;
    if(param == NULL)
    {
        return param;
    }
    len = len;//warning
    memset(param,0x00,sizeof(xj_cmd_35));
    recv_protocol.recv_cmd_35_flag = ECO_TRUE;
    int count=DATA_FIELD_COUNT;

    strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->card_id,&datas[count],sizeof(param->card_id));
    count += sizeof(param->card_id);
    memcpy((uint8_t*)&param->result,&datas[count],sizeof(param->result));
    count += sizeof(param->result);

    return param;
}

// 37
int32_t recv_clean_card_charge_37_protocol(xj_cmd_37* param,uint8_t* datas,uint16_t len)
{
	if(param == NULL)
	{
		return 1;
	}
	len = len;//warning
	memset(param,0x00,sizeof(xj_cmd_43));

	int count = DATA_FIELD_COUNT;

	strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
	count += sizeof(param->equipment_id);
	memcpy((uint8_t*)&param->white_list_num,&datas[count],sizeof(param->white_list_num));
	count += sizeof(param->white_list_num);
	memcpy((uint8_t*)&param->card_white_list,&datas[count],param->white_list_num*sizeof(white_list_card));
	return 0;
}

xj_cmd_41* recv_card_charge_41_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_41*  param = &recv_protocol.cmd_41;
    if(param == NULL)
    {
        return param;
    }
    len = len;//warning
    memset(param,0x00,sizeof(xj_cmd_41));

    int count = DATA_FIELD_COUNT;


    memcpy((uint8_t*)&param->serial_code,&datas[8],sizeof(param->serial_code));

    strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->charge_user_id,&datas[count],sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    memcpy((uint8_t*)&param->vin,&datas[count],sizeof(param->vin));
    count += sizeof(param->vin);

    memcpy((uint8_t*)&param->balance,&datas[count],sizeof(param->balance));
    count += sizeof(param->balance);
    memcpy((uint8_t*)&param->Request_result,&datas[count],sizeof(param->Request_result));
    count += sizeof(param->Request_result);
    memcpy((uint8_t*)&param->failure_reasons,&datas[count],sizeof(param->failure_reasons));
    count += sizeof(param->failure_reasons);
    memcpy((uint8_t*)&param->remainkon,&datas[count],sizeof(param->remainkon));
    count += sizeof(param->remainkon);
    memcpy((uint8_t*)&param->dump_energy,&datas[count],sizeof(param->dump_energy));
    count += sizeof(param->dump_energy);
    memcpy((uint8_t*)&param->residue_degree,&datas[count],sizeof(param->residue_degree));
    count += sizeof(param->residue_degree);
    memcpy((uint8_t*)&param->phone,&datas[count],sizeof(param->phone));
    count += sizeof(param->phone);

    recv_protocol.recv_cmd_41_flag = ECO_TRUE;

    return param;
}

// 43
int32_t recv_vin_offlin_charge_43_protocol(xj_cmd_43* param,uint8_t* datas,uint16_t len)
{
	if(param == NULL)
	{
		return 1;
	}
    len = len;//warning
	memset(param,0x00,sizeof(xj_cmd_43));

	if(param->white_list_num > 20){
		xj_pal_print_log(xj_log_remind,"recv 43 wrong list num is too long %d\n",param->white_list_num);
		return 1;
	}
	int count = DATA_FIELD_COUNT;

	strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
	count += sizeof(param->equipment_id);
	memcpy((uint8_t*)&param->white_list_num,&datas[count],sizeof(param->white_list_num));
	count += sizeof(param->white_list_num);
	memcpy((uint8_t*)&param->vin_white_list,&datas[count],param->white_list_num*sizeof(white_list));

	return 0;
}

// 45
int32_t recv_clean_vin_charge_45_protocol(xj_cmd_45* param,uint8_t* datas,uint16_t len)
{
	if(param == NULL)
	{
		return 1;
	}
    len = len;//warning
	memset(param,0x00,sizeof(xj_cmd_45));

	int count = DATA_FIELD_COUNT;

	strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
	count += sizeof(param->equipment_id);
	memcpy((uint8_t*)&param->reserve,&datas[count],sizeof(param->reserve));
	count += sizeof(param->reserve);
	return 0;
}


// 101
xj_cmd_101* recv_heart_101_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_101* param = &recv_protocol.cmd_101;

    if(param == NULL)
    {
        return param;
    }
    len = len;//warning
    int i = 0;

    int count=DATA_FIELD_COUNT;
    count += 4;

    memcpy(&param->heart_index,&datas[count],sizeof(param->heart_index));
    count += sizeof(param->heart_index);

    recv_protocol.recv_cmd_101_flag = ECO_TRUE;
    return param;
}
// 103
xj_cmd_103* recv_charge_status_103_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_103* param = &recv_protocol.cmd_103;
    int count=DATA_FIELD_COUNT;

    if(param == NULL)
    {
        return param;
    }
    int i = 0;
//    printf("101=============================== len %d\n",len);
//    for(i = 0;i < len;i++){
//    	printf("%x\t",datas[i]);
//    	if(i == 33){
//    		printf("\n");
//    	}
//    }
//    printf("\n");
    memset(param,0x00,sizeof(xj_cmd_103));
    recv_protocol.recv_cmd_103_flag = ECO_TRUE;

    count +=4;
    len = len;//warning
    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strcpy((uint8_t*)&param->charge_user_id,&datas[count]);
    count += sizeof(param->charge_user_id);
    memcpy(&param->charge_card_account,&datas[count],sizeof(param->charge_card_account));
    count += sizeof(param->charge_card_account);
    memcpy(&param->accountEnoughFlag,&datas[count],sizeof(param->accountEnoughFlag));
    count += sizeof(param->accountEnoughFlag);
    return param;
}

// 105
int32_t recv_fault_105_protocol(xj_cmd_105* param, uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;
    if(param == NULL)
    {
        return 1;
    }
    len = len;//warning

    memcpy((uint8_t*)&(param->reserve1),&datas[count],sizeof(param->reserve1));
    count += sizeof(param->reserve1);
    memcpy((uint8_t*)&(param->reserve2),&datas[count],sizeof(param->reserve2));
    count += sizeof(param->reserve2);
    memcpy((uint8_t*)&(param->time),&datas[count],sizeof(param->time));
    count += sizeof(param->time);


    recv_protocol.cmd_105 = *param;
    recv_protocol.recv_cmd_105_flag = ECO_TRUE;

    return 0;
}
// 107
xj_cmd_107* recv_fault_107_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_107* param = &recv_protocol.cmd_107;
    int count=DATA_FIELD_COUNT;

    if(param == NULL)
    {
        return param;
    }
    len = len;//warning

    recv_protocol.recv_cmd_107_flag = ECO_TRUE;
    count +=4;
    strncpy(param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    param->gun_index = datas[count++];
    memcpy((uint8_t*)&(param->event_name),&datas[count],sizeof(param->event_name));
    count += sizeof(param->event_name);

    return param;
}
// 113
xj_cmd_113* recv_fault_113_protocol(uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;
    xj_cmd_113* param = &recv_protocol.cmd_113;

    if(param == NULL)
    {
        return param;
    }
    recv_protocol.recv_cmd_113_flag = ECO_TRUE;
    len = len;//warning

    count +=4;

    strcpy((uint8_t*)&param->url,&datas[count]);
    count += sizeof(param->url);
    memcpy(&param->port,&datas[count],sizeof(param->port));
    count += sizeof(param->port);

    return param;
}

// 117
xj_cmd_117* recv_fault_117_protocol(uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;
    xj_cmd_117* param = &recv_protocol.cmd_117;
    if(param == NULL)
    {
        return param;
    }

    len = len;//warning
    strncpy((uint8_t*)&param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    param->gun_index = datas[count++];
    strncpy((uint8_t*)&param->errCode,&datas[count],sizeof(param->errCode));
    count += sizeof(param->errCode);

    recv_protocol.recv_cmd_117_flag = ECO_TRUE;

      /* test */
    #if defined(XJ_ERR_CACHE_USED)
    err_event_info_pop(-1,param->gun_index - 1,param->errCode);
    #endif // defined

    return param;
}

xj_cmd_119* recv_fault_119_protocol(uint8_t* datas,uint16_t len)
{
    int count=DATA_FIELD_COUNT;
    xj_cmd_119* param = &recv_protocol.cmd_119;
    if(param == NULL)
    {
        return param;
    }
    len = len;//warning
    strncpy((uint8_t*)&param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    param->gun_index = datas[count++];
    strncpy((uint8_t*)&param->warning_code,&datas[count],sizeof(param->warning_code));
    count += sizeof(param->warning_code);

    recv_protocol.recv_cmd_119_flag = ECO_TRUE;
    return param;
}


// 201
xj_cmd_201* recv_bill_201_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_201* param = &recv_protocol.cmd_201;
    int count=DATA_FIELD_COUNT;

    if(param == NULL)
    {
        return param;
    }
    memset(param,0x00,sizeof(xj_cmd_201));
    recv_protocol.recv_cmd_201_flag = ECO_TRUE;
    len = len;//warning


    memcpy((char*)&param->serial_code,&datas[8],sizeof(param->serial_code));


    count +=4;


    param->gun_index = datas[count++];
    strcpy((uint8_t*)&param->user_id,&datas[count]);
    count += sizeof(param->user_id);

    return param;
}

// 205
int32_t recv_history_bill_205_protocol(xj_cmd_205* param, uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }

    int count=DATA_FIELD_COUNT;
    count +=4;
    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strcpy((uint8_t*)&param->user_id,&datas[count]);
    count += sizeof(param->user_id);
    return 0;
}

xj_cmd_301* recv_bms_status_301_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_301* param = &recv_protocol.cmd_301;
    if(param == NULL)
    {
        return param;
    }

    memset(param,0x00,sizeof(xj_cmd_301));
    recv_protocol.recv_cmd_301_flag = ECO_TRUE;

    int count=DATA_FIELD_COUNT;
    count +=2;

    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->equipment_id,&datas[count],32);
    count += 32;
    return param;
}

xj_cmd_303* recv_bms_303_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_303* param = &recv_protocol.cmd_303;
    if(param == NULL)
    {
        return param;
    }

    memset(param,0x00,sizeof(xj_cmd_303));
    recv_protocol.recv_cmd_303_flag = ECO_TRUE;

    int count=DATA_FIELD_COUNT;
    count +=2;

    memcpy(&param->serial_code,&datas[8],sizeof(param->serial_code));

    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->charge_user_id,&datas[count],sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    return param;
}


xj_cmd_305* recv_bms_305_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_305* param = &recv_protocol.cmd_305;
    if(param == NULL)
    {
        return param;
    }

    memset(param,0x00,sizeof(xj_cmd_305));
    recv_protocol.recv_cmd_305_flag = ECO_TRUE;

    int count=DATA_FIELD_COUNT;
    count +=2;

    memcpy(&param->serial_code,&datas[8],sizeof(param->serial_code));

    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->charge_user_id,&datas[count],sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    return param;
}
xj_cmd_307* recv_bms_307_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_307* param = &recv_protocol.cmd_307;
    if(param == NULL)
    {
        return param;
    }

    memset(param,0x00,sizeof(xj_cmd_307));
    recv_protocol.recv_cmd_307_flag = ECO_TRUE;

    int count=DATA_FIELD_COUNT;
    count +=2;

    memcpy(&param->serial_code,&datas[8],sizeof(param->serial_code));

    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->charge_user_id,&datas[count],sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    return param;
}
xj_cmd_309* recv_bms_309_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_309* param = &recv_protocol.cmd_309;
    if(param == NULL)
    {
        return param;
    }

    memset(param,0x00,sizeof(xj_cmd_309));
    recv_protocol.recv_cmd_309_flag = ECO_TRUE;

    int count=DATA_FIELD_COUNT;
    count +=2;

    memcpy(&param->serial_code,&datas[8],sizeof(param->serial_code));

    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->charge_user_id,&datas[count],sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    return param;
}

xj_cmd_311* recv_bms_311_protocol(uint8_t* datas,uint16_t len)
{
    xj_cmd_311* param = &recv_protocol.cmd_311;
    if(param == NULL)
    {
        return param;
    }

    memset(param,0x00,sizeof(xj_cmd_311));
    recv_protocol.recv_cmd_311_flag = ECO_TRUE;

    int count=DATA_FIELD_COUNT;
    count +=2;

    memcpy(&param->serial_code,&datas[8],sizeof(param->serial_code));

    memcpy(&param->gun_index,&datas[count],sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy((uint8_t*)&param->equipment_id,&datas[count],sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy((uint8_t*)&param->charge_user_id,&datas[count],sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    return param;
}




// 409
int32_t recv_ask_log_409_protocol(xj_cmd_409* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }

    int count=DATA_FIELD_COUNT;
    count +=4;

    strcpy((uint8_t*)&param->equipment_id,&datas[count]);
    count += sizeof(param->equipment_id);
    strcpy((uint8_t*)&param->log_name,&datas[count]);
    count += sizeof(param->log_name);
    return 0;
}

// 1
int32_t recv_set_int_501_protocol(xj_cmd_501* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    int i = 0;
//    printf("=============================== len %d\n",len);
//   	 for(i = 0;i < len;i++){
//   		 printf("%x\t",datas[i]);
//   		 if(i == 33){
//   			 printf("\n");
//   		 }
//   	 }
//   	 printf("\n");
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_501));
    param->cmd_len = len - BASE_LENGTH;//data区的长度

    if(6 < param->cmd_len && param->cmd_len <= CMD501_MAX_DATA_LEN)
    {
        memcpy(param->data,&datas[DATA_FIELD_COUNT],param->cmd_len);

    }
    else
    {
        xj_pal_print_log(xj_log_warning,"recv_set_501_protocol data length error,len = %d\n",param->cmd_len);
        return 1;
    }


    return 0;
}


int32_t recv_set_int_503_protocol(xj_cmd_503* param,uint8_t* datas,uint16_t len)
{
    uint8_t equipment_id_len = sizeof(param->equipment_id);

    if(param == NULL)
    {
        return 1;
    }
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_503));

    if((len - BASE_LENGTH) > equipment_id_len && (len - BASE_LENGTH - equipment_id_len) <=  CMD503_MAX_DATA_LEN)
    {
        param->data_len = len - BASE_LENGTH - equipment_id_len;//data区的长度
        memcpy(param->data,&datas[DATA_FIELD_COUNT + equipment_id_len],param->data_len);
    }
    else
    {
        xj_pal_print_log(xj_log_warning,"recv_set_503_protocol data length error,len = %d\n",param->data_len);
        return 1;
    }


    return 0;
}

/*
int32_t recv_set_int_505_protocol(xj_cmd_505* param,uint8_t* datas,uint16_t len)
{
	 if(param == NULL)
	 {
		 return 1;
	 }
	 memset(param,0x00,sizeof(xj_cmd_505));
	 param->cmd_len = len - BASE_LENGTH;//data区的长度

	 if(6 < param->cmd_len && param->cmd_len <= CMD501_MAX_DATA_LEN)
	 {
		 memcpy(param->data,&datas[DATA_FIELD_COUNT],param->cmd_len);

	 }
	 else
	 {
		 xj_pal_print_log(xj_log_warning,"recv_set_503_protocol data length error,len = %d\n",param->cmd_len);
		 return 1;
	 }
	 return 0;
}
*/

int32_t recv_set_int_505_protocol(xj_cmd_505* param,uint8_t* datas,uint16_t len)
{
	int i = 0;
	 if(param == NULL)
	 {
		 return 1;
	 }
	 memset(param,0x00,sizeof(xj_cmd_505));
	 param->data_len = len - BASE_LENGTH;//data区的长度

	 if(37 < param->data_len && param->data_len <= CMD505_MAX_DATA_LEN)
	 {
		 memcpy(param->data,&datas[DATA_FIELD_COUNT],param->data_len);
	 }
	 else
	 {
		 xj_pal_print_log(xj_log_warning,"recv_set_505_protocol data length error,len = %d\n",param->data_len);
		 return 1;
	 }
	 return 0;
}


int32_t recv_set_int_507_protocol(xj_cmd_507* param,uint8_t* datas,uint16_t len)
{
	int i = 0;
	 if(param == NULL)
	 {
		 return 1;
	 }
	 memset(param,0x00,sizeof(xj_cmd_507));
	 param->data_len = len - BASE_LENGTH;//data区的长度

	 if(4 < param->data_len && param->data_len <= CMD507_MAX_DATA_LEN)
	 {
		 memcpy(param->data, &datas[DATA_FIELD_COUNT], param->data_len);
	 }
	 else
	 {
		 xj_pal_print_log(xj_log_warning,"recv_set_507_protocol data length error,len = %d\n",param->data_len);
		 return 1;
	 }
	 return 0;
}

int32_t recv_set_int_511_protocol(xj_cmd_511* param,uint8_t* datas,uint16_t len)
{
	int i = 0;
	 if(param == NULL)
	 {
		 return 1;
	 }
	 memset(param,0x00,sizeof(xj_cmd_511));
	 param->data_len = len - BASE_LENGTH;//data区的长度

	 if(4 < param->data_len && param->data_len <= CMD511_MAX_DATA_LEN)
	 {
		 memcpy(param->data, &datas[DATA_FIELD_COUNT], param->data_len);
	 }
	 else
	 {
		 xj_pal_print_log(xj_log_warning,"recv_set_511_protocol data length error,len = %d\n",param->data_len);
		 return 1;
	 }
	 return 0;
}

int32_t recv_set_int_513_protocol(xj_cmd_513* param,uint8_t* datas,uint16_t len)
{
	int i = 0;
	 if(param == NULL)
	 {
		 return 1;
	 }
	 memset(param,0x00,sizeof(xj_cmd_513));
	 param->data_len = len - BASE_LENGTH;//data区的长度

	 if(4 < param->data_len && param->data_len <= CMD513_MAX_DATA_LEN)
	 {
		 memcpy(param->data, &datas[DATA_FIELD_COUNT], param->data_len);
	 }
	 else
	 {
		 xj_pal_print_log(xj_log_warning,"recv_set_513_protocol data length error,len = %d\n",param->data_len);
		 return 1;
	 }
	 return 0;
}
// 509
int32_t recv_ask_bms_log_509_protocol(xj_cmd_509* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    memset(param,0x00,sizeof(xj_cmd_509));

    int count=DATA_FIELD_COUNT;
    count +=4;

    strcpy((uint8_t*)&param->equipment_id,&datas[count]);
    count += sizeof(param->equipment_id);
    strcpy((uint8_t*)&param->log_name,&datas[count]);
    count += sizeof(param->log_name);
    return 0;
}

// 801
//xj_cmd_801* recv_safety_certification_801_protocol(uint8_t* datas,uint16_t len)
//{
//	xj_cmd_801* param = (xj_cmd_801*)malloc(sizeof(xj_cmd_801));
//	if(param == NULL)
//	{
//      xj_pal_print_log(xj_log_message,"recv_safety_certification_801_protocol malloc filed\n");
//		return param;
//	}
//	memset(param,0x00,sizeof(xj_cmd_801));
//
//	int count=DATA_FIELD_COUNT;
//	memcpy((uint8_t*)&(param->key_len),&datas[count],sizeof(param->key_len));
//	count += sizeof(param->key_len);
//
//	param->key_datas = (uint8_t*)malloc(param->key_len);
//	if(param->key_datas == NULL)
//	{
//      xj_pal_print_log(xj_log_message,"recv_safety_certification_801_protocol.key_datas malloc filed\n");
//		return param;
//	}
//	memset(param->key_datas,0x00,param->key_len);
//
//	strncpy(param->key_datas,&datas[count],param->key_len);
//	count += param->key_len;
//	strcpy(param->equipment_id,&datas[count]);
//	count += sizeof(param->equipment_id);
//	memcpy(&param->encrypted_type,&datas[count],sizeof(param->encrypted_type));
//	count += sizeof(param->encrypted_type);
//	memcpy(&param->encrypted_version,&datas[count],sizeof(param->encrypted_version));
//	count += sizeof(param->encrypted_version);
//	return param;
//}

// 1101
int32_t recv_xj_soft_update_1101_protocol(xj_cmd_1101* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_1101));

    int count=DATA_FIELD_COUNT;

    param->soft_type = datas[count++];
    param->soft_param = datas[count++];
    strcpy(param->download_url,&datas[count]);
    count += sizeof(param->download_url);
    strncpy((uint8_t*)&param->md5,&datas[count],sizeof(param->md5));
    count += sizeof(param->md5);
    return 0;
}

// 1303
int32_t recv_set_electricity_fee_1303_protocol(xj_cmd_1303* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_1303));

    int count=DATA_FIELD_COUNT;

    param->cmd_type = datas[count++];
    memcpy((uint8_t*)&param->fee_data,&datas[count],sizeof(param->fee_data));
    count += sizeof(param->fee_data);

    return 0;
}

// 1305
int32_t recv_set_serice_fee_1305_protocol(xj_cmd_1305* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_1305));

    int count=DATA_FIELD_COUNT;

    param->cmd_type = datas[count++];
    param->gun_index = datas[count++];
    memcpy((uint8_t*)&param->fee_data,&datas[count],sizeof(param->fee_data));
    count += sizeof(param->fee_data);

    return 0;
}

// 1309
int32_t recv_set_electricity_fee_1309_protocol(xj_cmd_1309* param,uint8_t* datas,uint16_t len)
{
    if(param == NULL)
    {
        return 1;
    }
    memset((uint8_t*)param,0x00,sizeof(xj_cmd_1309));

    int count = DATA_FIELD_COUNT;

    param->class_num = datas[count] + ((uint16_t)datas[count+1]<<8);
    param->data_len = len - BASE_LENGTH - 2;//去掉分组数包含的数据

    if(param->data_len < CMD1309_MAX_DATA_LEN)
    {
        memcpy((uint8_t*)&param->data,&datas[count+2],param->data_len);
    }
    else
    {
        xj_pal_print_log(xj_log_warning,"recv_set_1309_protocol data length error,len = %d\n",param->data_len);
        memcpy((uint8_t*)&param->data,&datas[count+2],CMD1309_MAX_DATA_LEN);
    }

    return 0;
}

void proc_public_message(uint8_t* datas,protocol_base* protocol,uint16_t len)
{

    xj_pal_print_log(xj_log_message,"Received cmd:%d\n",protocol->cmd_code);

    switch(protocol->cmd_code)
    {

    case xj_cmd_type_1:
    {
        if(recv_set_int_1_protocol((xj_cmd_1*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_1*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_1*)_g_cmd_bytes_buf,xj_cmd_type_1);
        }
        break;
    }
    case xj_cmd_type_3:
    {
        if(recv_set_string_3_protocol((xj_cmd_3*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_3*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_3*)_g_cmd_bytes_buf,xj_cmd_type_3);
        }
        break;
    }
    case xj_cmd_type_5:
    {
        if(recv_control_5_protocol((xj_cmd_5*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_5*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_5*)_g_cmd_bytes_buf,xj_cmd_type_5);
        }
        break;
    }
    case xj_cmd_type_7:
    {
        if(recv_start_charge_7_protocol((xj_cmd_7*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_7*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_7*)_g_cmd_bytes_buf,xj_cmd_type_7);
        }
        break;
    }
    case xj_cmd_type_11:
    {
        if(recv_stop_charge_11_protocol((xj_cmd_11*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_11*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_11*)_g_cmd_bytes_buf,xj_cmd_type_11);
        }
        break;
    }
    case xj_cmd_type_19:
    {
        if(recv_power_control_19_protocol((xj_cmd_19*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_19*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_19*)_g_cmd_bytes_buf,xj_cmd_type_19);
        }
        break;
    }
    case xj_cmd_type_23:
    {
        if(recv_control_lock_23_protocol((xj_cmd_23*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_23*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_23*)_g_cmd_bytes_buf,xj_cmd_type_23);
        }
        break;
    }
    case xj_cmd_type_33:
    {
        recv_card_auth_33_protocol(datas,len);
        break;
    }
    case xj_cmd_type_35:
    {
        recv_card_charge_35_protocol(datas,len);
        break;
    }
    case xj_cmd_type_37:
    {
    	if(recv_clean_card_charge_37_protocol((xj_cmd_37*)_g_cmd_bytes_buf,datas,len) == 0){
    		((xj_cmd_37*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_37*)_g_cmd_bytes_buf,xj_cmd_type_37);
    	}
    	break;
    }
    case xj_cmd_type_41:
    {
        recv_card_charge_41_protocol(datas,len);
        break;
    }
    case xj_cmd_type_43:
    {
    	if(recv_vin_offlin_charge_43_protocol((xj_cmd_43*)_g_cmd_bytes_buf,datas,len) == 0){
    		((xj_cmd_43*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_43*)_g_cmd_bytes_buf,xj_cmd_type_43);
    	}
    	break;
    }
    case xj_cmd_type_45:
    {
    	if(recv_clean_vin_charge_45_protocol((xj_cmd_45*)_g_cmd_bytes_buf,datas,len) == 0){
    		((xj_cmd_45*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_45*)_g_cmd_bytes_buf,xj_cmd_type_45);
    	}
    	 break;
    }
    case xj_cmd_type_101:
    {
        recv_heart_101_protocol(datas,len);
        break;
    }
    case xj_cmd_type_103:
    {
        recv_charge_status_103_protocol(datas,len);
        break;
    }
    case xj_cmd_type_105:
    {
        if(recv_fault_105_protocol((xj_cmd_105*)_g_cmd_bytes_buf,datas,len)==0)
        {
            mqttMessageBase.callback((xj_cmd_105*)_g_cmd_bytes_buf,xj_cmd_type_105);
        }
        break;
    }
    case xj_cmd_type_107:
    {
        recv_fault_107_protocol(datas,len);
        break;
    }
    case xj_cmd_type_113:
    {
        recv_fault_113_protocol(datas,len);
        break;
    }

    case xj_cmd_type_117:
    {
        recv_fault_117_protocol(datas,len);
        break;
    }
    case xj_cmd_type_119:
    {
        recv_fault_119_protocol(datas,len);
        break;
    }
    case xj_cmd_type_201:
    {
        recv_bill_201_protocol(datas,len);
        break;
    }
    case xj_cmd_type_205:
    {
        if(recv_history_bill_205_protocol((xj_cmd_205*)_g_cmd_bytes_buf,datas,len)==0)
        {
            mqttMessageBase.callback((xj_cmd_205*)_g_cmd_bytes_buf,xj_cmd_type_205);
        }
        break;
    }
    case xj_cmd_type_301:
    {
        recv_bms_status_301_protocol(datas,len);
        break;
    }
    case xj_cmd_type_303:
    {
        recv_bms_303_protocol(datas,len);
        break;
    }
    case xj_cmd_type_305:
    {
        recv_bms_305_protocol(datas,len);
        break;
    }
    case xj_cmd_type_307:
    {
        recv_bms_307_protocol(datas,len);
        break;
    }
    case xj_cmd_type_309:
    {
        recv_bms_309_protocol(datas,len);
        break;
    }
    case xj_cmd_type_311:
    {
        recv_bms_311_protocol(datas,len);
        break;
    }
    case xj_cmd_type_331:
    {
    	if(recv_clean_card_charge_331_protocol((xj_cmd_331*)_g_cmd_bytes_buf,datas,len) == 0){
    		((xj_cmd_331*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_331*)_g_cmd_bytes_buf,xj_cmd_type_331);
    	}
    	break;
    }
    case xj_cmd_type_501:
    {
        if(0 == recv_set_int_501_protocol((xj_cmd_501*)_g_cmd_bytes_buf,datas,len))
        {
            ((xj_cmd_501*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_501*)_g_cmd_bytes_buf,xj_cmd_type_501);
        }
        break;
    }
    case xj_cmd_type_503:
    {
        if(0 == recv_set_int_503_protocol((xj_cmd_503*)_g_cmd_bytes_buf,datas,len))
        {
            ((xj_cmd_503*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_503*)_g_cmd_bytes_buf,xj_cmd_type_503);
        }
        break;
    }
    case xj_cmd_type_505:
    {
    	if(0 == recv_set_int_505_protocol((xj_cmd_505*)_g_cmd_bytes_buf,datas,len))
    	{
    		((xj_cmd_505*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_505*)_g_cmd_bytes_buf,xj_cmd_type_505);
    	}
    	break;
    }
    case xj_cmd_type_507:
    {
    	if(0 == recv_set_int_507_protocol((xj_cmd_507*)_g_cmd_bytes_buf,datas,len))
    	{
    		((xj_cmd_507*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_507*)_g_cmd_bytes_buf,xj_cmd_type_507);
    	}
    	break;
    }
    case xj_cmd_type_511:
    {
        if(0 == recv_set_int_511_protocol((xj_cmd_511*)_g_cmd_bytes_buf,datas,len))
    	{
    		((xj_cmd_511*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_511*)_g_cmd_bytes_buf,xj_cmd_type_511);
    	}
    	break;
    }
    case xj_cmd_type_513:
    {
        if(0 == recv_set_int_513_protocol((xj_cmd_513*)_g_cmd_bytes_buf,datas,len))
    	{
    		((xj_cmd_513*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
    		mqttMessageBase.callback((xj_cmd_513*)_g_cmd_bytes_buf,xj_cmd_type_513);
    	}
    	break;
    }
    case xj_cmd_type_801:
    {
// 				xj_cmd_801* certification_param = recv_safety_certification_801_protocol(datas,len);
//
//				if(certification_param != NULL)
//				{
//					certification_param->serial_code = protocol->serial_code;
//					if(certification_param->key_datas != NULL)
//						free(certification_param->key_datas);
//					free(certification_param);
//				}
        break;
    }
    case xj_cmd_type_409:
    {
        if(recv_ask_log_409_protocol((xj_cmd_409*)_g_cmd_bytes_buf,datas,len) == 0)
        {
            ((xj_cmd_409*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_409*)_g_cmd_bytes_buf,xj_cmd_type_409);
        }
        break;
    }
    case xj_cmd_type_509:
    {
        if(recv_ask_bms_log_509_protocol((xj_cmd_509*)_g_cmd_bytes_buf,datas,len) == 0)
        {
            ((xj_cmd_509*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_509*)_g_cmd_bytes_buf,xj_cmd_type_509);
        }
        break;
    }
    case xj_cmd_type_1101:
    {
        if(recv_xj_soft_update_1101_protocol((xj_cmd_1101*)_g_cmd_bytes_buf,datas,len) == 0)
        {
            ((xj_cmd_1101*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_1101*)_g_cmd_bytes_buf,xj_cmd_type_1101);
        }
        break;
    }
    case xj_cmd_type_1303:
    {
        if(recv_set_electricity_fee_1303_protocol((xj_cmd_1303*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_1303*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_1303*)_g_cmd_bytes_buf,xj_cmd_type_1303);
        }
        break;
    }
    case xj_cmd_type_1305:
    {
        if(recv_set_serice_fee_1305_protocol((xj_cmd_1305*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_1305*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_1305*)_g_cmd_bytes_buf,xj_cmd_type_1305);
        }
        break;
    }
    case xj_cmd_type_1309:
    {
        if(recv_set_electricity_fee_1309_protocol((xj_cmd_1309*)_g_cmd_bytes_buf,datas,len)==0)
        {
            ((xj_cmd_1309*)_g_cmd_bytes_buf)->serial_code = protocol->serial_code;
            mqttMessageBase.callback((xj_cmd_1309*)_g_cmd_bytes_buf,xj_cmd_type_1309);
        }
        break;
    }

    default:
    {
        xj_pal_print_log(xj_log_remind,"recv buf have wrong cmd\n");
        break;
    }
    }
}

// 2
uint8_t  send_set_int_2_protocol(xj_cmd_2* param,uint8_t Qos)
{
    if(param == NULL)
    {
        xj_pal_print_log(xj_log_warning,"send_set_int_2_protocol.param is NULL\n");
        return ECO_FALSE;
    }

    protocol_base	protocol_package;
    int totollen = BASE_LENGTH + 43 + param->data_len;
    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_2,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;

    count+=4;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->cmd_type;
    memcpy(&protocol_package.datas[count],&param->start_addr,sizeof(param->start_addr));
    count += sizeof(param->start_addr);
    protocol_package.datas[count++] = param->cmd_num;
    protocol_package.datas[count++] = param->result;

    memcpy(&protocol_package.datas[count],(uint8_t*)param->data,param->data_len);
    count += param->data_len;

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:2 send.\n");
    return ret;
}

// 4
uint8_t  send_set_string_4_protocol(xj_cmd_4* param,uint8_t Qos)
{
    if(param == NULL)
    {
        xj_pal_print_log(xj_log_warning,"send_set_string_4_protocol.param is NULL\n");
        return ECO_FALSE;
    }

    if(param->data== NULL)
    {
        xj_pal_print_log(xj_log_warning,"xj_cmd_4.param.data is NULL\n");
        return ECO_FALSE;
    }

    protocol_base	protocol_package;
    int totollen = BASE_LENGTH+42+param->dataLen;
    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_4,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;

    count+=4;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->cmd_type;
    memcpy(&protocol_package.datas[count],&param->start_addr,sizeof(param->start_addr));
    count += sizeof(param->start_addr);
    protocol_package.datas[count++] = param->result;

    strncpy(&protocol_package.datas[count],param->data,(param->dataLen));
    count += (param->dataLen);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:4 send.\n");
    return ret;
}
// 106
uint8_t  send_signin_106_protocol(xj_cmd_106* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totollen = SIGNIN_106_LEGTH;
    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_106,++mqttMessageBase.masterSerialNum,SIGNIN_106_LEGTH);

    int count = DATA_FIELD_COUNT;

    memcpy(&protocol_package.datas[count],&param->charge_mode_num,sizeof(param->charge_mode_num));
    count += sizeof(param->charge_mode_num);
    memcpy(&protocol_package.datas[count],&param->charge_mode_rate,sizeof(param->charge_mode_rate));
    count += sizeof(param->charge_mode_rate);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->offline_charge_flag,sizeof(param->offline_charge_flag));
    count += sizeof(param->offline_charge_flag);
    memcpy(&protocol_package.datas[count],&param->stake_version,sizeof(param->stake_version));
    count += sizeof(param->stake_version);
    memcpy(&protocol_package.datas[count],&param->stake_type,sizeof(param->stake_type));
    count += sizeof(param->stake_type);
    memcpy(&protocol_package.datas[count],&param->stake_start_times,sizeof(param->stake_start_times));
    count += sizeof(param->stake_start_times);
    memcpy(&protocol_package.datas[count],&param->data_up_mode,sizeof(param->data_up_mode));
    count += sizeof(param->data_up_mode);
    memcpy(&protocol_package.datas[count],&param->sign_interval,sizeof(param->sign_interval));
    count += sizeof(param->sign_interval);
    memcpy(&protocol_package.datas[count],&param->reserve,sizeof(param->reserve));
    count += sizeof(param->reserve);
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy(&protocol_package.datas[count],&param->heartInterval,sizeof(param->heartInterval));
    count += sizeof(param->heartInterval);
    memcpy(&protocol_package.datas[count],&param->heart_out_times,sizeof(param->heart_out_times));
    count += sizeof(param->heart_out_times);
    memcpy(&protocol_package.datas[count],&param->stake_charge_record_num,sizeof(param->stake_charge_record_num));
    count += sizeof(param->stake_charge_record_num);
    memcpy(&protocol_package.datas[count],&param->stake_systime,sizeof(param->stake_systime));
    count += sizeof(param->stake_systime);
    memcpy(&protocol_package.datas[count],&param->stake_last_charge_time,sizeof(param->stake_last_charge_time));
    count += sizeof(param->stake_last_charge_time);
    memcpy(&protocol_package.datas[count],&param->stake_last_start_time,sizeof(param->stake_last_start_time));
    count += sizeof(param->stake_last_start_time);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->signCode);
    count += sizeof(param->signCode);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->mac);
    count += sizeof(param->mac);
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->ccu_version,sizeof(param->ccu_version));
    count += sizeof(param->ccu_version);
    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:106 send.\n");
    return ret;
}
//114
uint8_t  send_ask_addr_114_protocol(xj_cmd_114* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totollen = BASE_LENGTH+36;
    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    mqttMessageBase.masterSerialNum=0;
    parse_protocol_header(&protocol_package,xj_cmd_type_114,++mqttMessageBase.masterSerialNum,totollen);

    int count = DATA_FIELD_COUNT;

    count+=4;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:114 send.\n");
    return ret;
}
// 6ºÅ
uint8_t  send_contorl_6_protocol(xj_cmd_6* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+4+32+10;

    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_6,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;

    memcpy(&protocol_package.datas[count],&param->equipment_id,sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);

    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy(&protocol_package.datas[count],&param->addr,sizeof(param->addr));
    count += sizeof(param->addr);
    memcpy(&protocol_package.datas[count],&param->cmd_num,sizeof(param->cmd_num));
    count += sizeof(param->cmd_num);
    memcpy(&protocol_package.datas[count],&param->result,sizeof(param->result));
    count += sizeof(param->result);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:6 send.\n");
    return ret;
}

uint8_t  send_start_charge_8_protocol(xj_cmd_8* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+4+32+5+32;

    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_8,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;

    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);

    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);

    memcpy(&protocol_package.datas[count],&param->result,sizeof(param->result));
    count += sizeof(param->result);

    memcpy(&protocol_package.datas[count],&param->charge_user_id,sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:8 send.\n");
    return ret;
}

uint8_t  send_stop_charge_12_protocol(xj_cmd_12* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+69;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_12,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);

    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);

    memcpy(&protocol_package.datas[count],(uint8_t*)&param->charge_seq,sizeof(param->charge_seq));
    count += sizeof(param->charge_seq);

    memcpy(&protocol_package.datas[count],(uint8_t*)&param->result,sizeof(param->result));
    count += sizeof(param->result);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:12 send.\n");
    return ret;
}

uint8_t send_power_kwh_cntl_20_protocol(xj_cmd_20* param,uint8_t Qos)
{
	 protocol_base	protocol_package;
	 int totolLen = BASE_LENGTH+34;

	 protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
	 parse_protocol_header(&protocol_package,xj_cmd_type_20,param->serial_code,totolLen);

	 int count = DATA_FIELD_COUNT;
	 memcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,sizeof(param->equipment_id));
	 count += sizeof(param->equipment_id);

	 memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
	 count += sizeof(param->gun_index);

	 memcpy(&protocol_package.datas[count],(uint8_t*)&param->result,sizeof(param->result));
	 count += sizeof(param->result);

	 protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
	 memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

	 uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:20 send.\n");
	 return ret;
}

// 24ºÅ
uint8_t  send_control_lock_24_protocol(xj_cmd_24* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+38;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_24,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    count+=4;

    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy(&protocol_package.datas[count],&param->result,sizeof(param->result));
    count += sizeof(param->result);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:24 send.\n");
    return ret;
}

uint8_t	send_card_white_list_38_protocol(xj_cmd_38 *param,uint8_t Qos)
{
	protocol_base	protocol_package;
	int totolLen = BASE_LENGTH+32+2+(param->white_list_num*17);
	if(totolLen > 512){

		return 0;
	}
	protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
	parse_protocol_header(&protocol_package,xj_cmd_type_38,param->serial_code,totolLen);

	int count = DATA_FIELD_COUNT;

	strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
	count += sizeof(param->equipment_id);

	memcpy(&protocol_package.datas[count],&param->white_list_num,sizeof(param->white_list_num));
	count += sizeof(param->white_list_num);
	memcpy(&protocol_package.datas[count],&param->card_white_list,param->white_list_num*sizeof(white_list_card));
	count += param->white_list_num*sizeof(white_list_card);

	protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
	memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

	uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:38 send.\n");
	return ret;
}

uint8_t	send_vin_white_list_42_protocol(xj_cmd_42 *param,uint8_t Qos)
{
	protocol_base	protocol_package;
	int totolLen = BASE_LENGTH+32+2+(param->white_list_num*18);
	if(totolLen > 512){

		return 0;
	}
	protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
	parse_protocol_header(&protocol_package,xj_cmd_type_42,param->serial_code,totolLen);

	int count = DATA_FIELD_COUNT;

	strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
	count += sizeof(param->equipment_id);

	memcpy(&protocol_package.datas[count],&param->white_list_num,sizeof(param->white_list_num));
	count += sizeof(param->white_list_num);
	memcpy(&protocol_package.datas[count],&param->vin_white_list,param->white_list_num*sizeof(white_list));
	count += param->white_list_num*sizeof(white_list);

	protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
	memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

	uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:42 send.\n");
	return ret;
}

uint8_t send_clear_vin_list_44_protocol(xj_cmd_44 *param,uint8_t Qos)
{
	protocol_base	protocol_package;
	int totolLen = BASE_LENGTH+38;

	protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
	parse_protocol_header(&protocol_package,xj_cmd_type_44,param->serial_code,totolLen);

	int count = DATA_FIELD_COUNT;
	count+=2;

	strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
	count += sizeof(param->equipment_id);
	memcpy(&protocol_package.datas[count],&param->success_sign,sizeof(param->success_sign));
	count += sizeof(param->success_sign);
//	memcpy(&protocol_package.datas[count],&param->serial_code,sizeof(param->serial_code));
//	count += sizeof(param->serial_code);
	protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
	memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

	uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:44 send.\n");
	return ret;
}



uint8_t  send_heart_102_protocol(xj_cmd_102* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+38;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_102,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->heart_index,sizeof(param->heart_index));
    count += sizeof(param->heart_index);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:102 send.\n");
    return ret;
}

uint8_t  send_gun_status_104_protocol(xj_cmd_104* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = 192;//184;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_104,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->gun_cnt,sizeof(param->gun_cnt));
    count += sizeof(param->gun_cnt);
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    protocol_package.datas[count++] = param->gun_type;
    protocol_package.datas[count++] = param->work_stat;
    memcpy(&protocol_package.datas[count],&param->soc_percent,sizeof(param->soc_percent));
    count += sizeof(param->soc_percent);
    memcpy(&protocol_package.datas[count],&param->alarm_stat,sizeof(param->alarm_stat));
    count += sizeof(param->alarm_stat);

    protocol_package.datas[count++] = param->car_connection_stat;
    memcpy(&protocol_package.datas[count],&param->cumulative_charge_fee,sizeof(param->cumulative_charge_fee));
    count += sizeof(param->cumulative_charge_fee);
    count +=8;
    memcpy(&protocol_package.datas[count],&param->dc_charge_voltage,sizeof(param->dc_charge_voltage));
    count += sizeof(param->dc_charge_voltage);
    memcpy(&protocol_package.datas[count],&param->dc_charge_current,sizeof(param->dc_charge_current));
    count += sizeof(param->dc_charge_current);
    memcpy(&protocol_package.datas[count],&param->bms_need_voltage,sizeof(param->bms_need_voltage));
    count += sizeof(param->bms_need_voltage);
    memcpy(&protocol_package.datas[count],&param->bms_need_current,sizeof(param->bms_need_current));
    count += sizeof(param->bms_need_current);
    protocol_package.datas[count++] = param->bms_charge_mode;
    memcpy(&protocol_package.datas[count],&param->ac_a_vol,sizeof(param->ac_a_vol));
    count += sizeof(param->ac_a_vol);
    memcpy(&protocol_package.datas[count],&param->ac_b_vol,sizeof(param->ac_b_vol));
    count += sizeof(param->ac_b_vol);
    memcpy(&protocol_package.datas[count],&param->ac_c_vol,sizeof(param->ac_c_vol));
    count += sizeof(param->ac_c_vol);
    memcpy(&protocol_package.datas[count],&param->ac_a_cur,sizeof(param->ac_a_cur));
    count += sizeof(param->ac_a_cur);
    memcpy(&protocol_package.datas[count],&param->ac_b_cur,sizeof(param->ac_b_cur));
    count += sizeof(param->ac_b_cur);
    memcpy(&protocol_package.datas[count],&param->ac_c_cur,sizeof(param->ac_c_cur));
    count += sizeof(param->ac_c_cur);
    memcpy(&protocol_package.datas[count],&param->charge_full_time_left,sizeof(param->charge_full_time_left));
    count += sizeof(param->charge_full_time_left);
    memcpy(&protocol_package.datas[count],&param->charged_sec,sizeof(param->charged_sec));
    count += sizeof(param->charged_sec);
    memcpy(&protocol_package.datas[count],&param->cum_charge_kwh_amount,sizeof(param->cum_charge_kwh_amount));
    count += sizeof(param->cum_charge_kwh_amount);
    memcpy(&protocol_package.datas[count],&param->before_charge_meter_kwh_num,sizeof(param->before_charge_meter_kwh_num));
    count += sizeof(param->before_charge_meter_kwh_num);
    memcpy(&protocol_package.datas[count],&param->now_meter_kwh_num,sizeof(param->now_meter_kwh_num));
    count += sizeof(param->now_meter_kwh_num);
    protocol_package.datas[count++] = param->start_charge_type;
    protocol_package.datas[count++] = param->charge_policy;
    memcpy(&protocol_package.datas[count],&param->charge_policy_param,sizeof(param->charge_policy_param));
    count += sizeof(param->charge_policy_param);
    memcpy(&protocol_package.datas[count],&param->book_flag,sizeof(param->book_flag));
    count += sizeof(param->book_flag);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id);
    count += sizeof(param->charge_user_id);
    memcpy(&protocol_package.datas[count],&param->book_timeout_min,sizeof(param->book_timeout_min));
    count += sizeof(param->book_timeout_min);
    memcpy(&protocol_package.datas[count],&param->book_start_charge_time,sizeof(param->book_start_charge_time));
    count += sizeof(param->book_start_charge_time);
    memcpy(&protocol_package.datas[count],&param->before_charge_card_account,sizeof(param->before_charge_card_account));
    count += sizeof(param->before_charge_card_account);
    memcpy(&protocol_package.datas[count],&param->parallel_charging,sizeof(param->parallel_charging));
    count += sizeof(param->parallel_charging);
    memcpy(&protocol_package.datas[count],&param->charge_power_kw,sizeof(param->charge_power_kw));
    count += sizeof(param->charge_power_kw);
    memcpy(&protocol_package.datas[count],&param->charging_temp,sizeof(param->charging_temp));
    count += sizeof(param->charging_temp);
    count += 8;

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));


    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:104 send.\n");
    return ret;
}


uint8_t  send_card_auth_34_protocol(xj_cmd_34* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+102;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_34,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;

    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy(&protocol_package.datas[count],&param->card_id,sizeof(param->card_id));
    count += sizeof(param->card_id);
    memcpy(&protocol_package.datas[count],&param->random_id,sizeof(param->random_id));
    count += sizeof(param->random_id);
    memcpy(&protocol_package.datas[count],&param->phy_card_id,sizeof(param->phy_card_id));
    count += sizeof(param->phy_card_id);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));


    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:34 send.\n");
    return ret;
}

uint8_t  send_card_ask_start_36_protocol(xj_cmd_36* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+50;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_36,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;

    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy(&protocol_package.datas[count],&param->card_id,sizeof(param->card_id));
    count += sizeof(param->card_id);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));


    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:36 send.\n");
    return ret;
}



uint8_t  send_vin_request_40_protocol(xj_cmd_40* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+52;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_40,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT + 2;

    strncpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    memcpy(&protocol_package.datas[count],&param->vin,sizeof(param->vin));
    count += sizeof(param->vin);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));


    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:40 send.\n");
    return ret;
}

uint8_t  send_encry_data_80_protocol(xj_cmd_80* param,uint8_t Qos)
{
	uint8_t i = 0;
 	protocol_base	protocol_package;
	int totolLen = BASE_LENGTH+105;
	protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
	parse_protocol_header(&protocol_package,xj_cmd_type_80,++mqttMessageBase.masterSerialNum,totolLen);

	int count = DATA_FIELD_COUNT;

	strncpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,sizeof(param->equipment_id));
	count += sizeof(param->equipment_id);
	memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
	count += sizeof(param->gun_index);
	memcpy(&protocol_package.datas[count],&param->charge_user_id,sizeof(param->charge_user_id));
	count += sizeof(param->charge_user_id);
	memcpy(&protocol_package.datas[count],&param->encry_data,sizeof(param->encry_data));
	count += sizeof(param->encry_data);
	memcpy(&protocol_package.datas[count],&param->meter_addr,sizeof(param->meter_addr));
	count += sizeof(param->meter_addr);

	protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
	memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));


	uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:80 send.\n");
	return ret;
}

// 108ºÅ
uint8_t  send_event_108_protocol(xj_cmd_108* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+13+32;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_108,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;
    protocol_package.datas[count++] = param->gun_index;
    memcpy(&protocol_package.datas[count],&param->event_addr,sizeof(param->event_addr));
    count += sizeof(param->event_addr);
    memcpy(&protocol_package.datas[count],&param->event_param,sizeof(param->event_param));
    count += sizeof(param->event_param);
    memcpy(&protocol_package.datas[count],&param->charge_user_id,sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:108 send.\n");
    return ret;
}

uint8_t send_fault_118_protocol(xj_cmd_118* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+38;

    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_118,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->gun_index;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->err_code);
    count += sizeof(param->err_code);
    protocol_package.datas[count++] = param->err_status;

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:118 send.\n");
    return ret;
}

uint8_t send_fault_120_protocol(xj_cmd_120* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+78;

    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_120,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->gun_index;

    memcpy(&protocol_package.datas[count],(uint8_t*)&param->warning_code,sizeof(param->warning_code));
    count += sizeof(param->warning_code);
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id,sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->type,sizeof(param->type));
    count += sizeof(param->type);
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->threshold,sizeof(param->threshold));
    count += sizeof(param->threshold);
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->retain,sizeof(param->retain));
    count += sizeof(param->retain);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:120 send.\n");
    return ret;
}


uint8_t  send_bill_202_protocol(xj_cmd_202* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+256+104;

    if(param == NULL)
    {
        xj_pal_print_log(xj_log_remind,"xj_cmd_202 illegal\n");
        return ECO_FALSE;
    }

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_202,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->gun_type;
    protocol_package.datas[count++] = param->gun_index;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id);
    count += sizeof(param->charge_user_id);
    memcpy(&protocol_package.datas[count],&param->charge_start_time,sizeof(param->charge_start_time));
    count += sizeof(param->charge_start_time);
    memcpy(&protocol_package.datas[count],&param->charge_end_time,sizeof(param->charge_end_time));
    count += sizeof(param->charge_end_time);
    memcpy(&protocol_package.datas[count],&param->charge_time,sizeof(param->charge_time));
    count += sizeof(param->charge_time);
    memcpy(&protocol_package.datas[count],&param->start_soc,sizeof(param->start_soc));
    count += sizeof(param->start_soc);
    memcpy(&protocol_package.datas[count],&param->end_soc,sizeof(param->end_soc));
    count += sizeof(param->end_soc);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->err_no);
    count += sizeof(param->err_no);
    memcpy(&protocol_package.datas[count],&param->charge_kwh_amount,sizeof(param->charge_kwh_amount));
    count += sizeof(param->charge_kwh_amount);
    memcpy(&protocol_package.datas[count],&param->start_charge_kwh_meter,sizeof(param->start_charge_kwh_meter));
    count += sizeof(param->start_charge_kwh_meter);
    memcpy(&protocol_package.datas[count],&param->end_charge_kwh_meter,sizeof(param->end_charge_kwh_meter));
    count += sizeof(param->end_charge_kwh_meter);
    memcpy(&protocol_package.datas[count],&param->total_charge_fee,sizeof(param->total_charge_fee));
    count += sizeof(param->total_charge_fee);
    memcpy(&protocol_package.datas[count],&param->is_not_stoped_by_card,sizeof(param->is_not_stoped_by_card));
    count += sizeof(param->is_not_stoped_by_card);
    memcpy(&protocol_package.datas[count],&param->start_card_money,sizeof(param->start_card_money));
    count += sizeof(param->start_card_money);
    memcpy(&protocol_package.datas[count],&param->end_card_money,sizeof(param->end_card_money));
    count += sizeof(param->end_card_money);
    memcpy(&protocol_package.datas[count],&param->total_service_fee,sizeof(param->total_service_fee));
    count += sizeof(param->total_service_fee);
    protocol_package.datas[count++] = param->is_paid_by_offline;
    protocol_package.datas[count++] = param->charge_policy;
    memcpy(&protocol_package.datas[count],&param->charge_policy_param,sizeof(param->charge_policy_param));
    count += sizeof(param->charge_policy_param);
    memcpy(&protocol_package.datas[count],&param->car_vin,sizeof(param->car_vin));
    count += sizeof(param->car_vin);
    memcpy(&protocol_package.datas[count],&param->car_plate_no,sizeof(param->car_plate_no));
    count += sizeof(param->car_plate_no);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_1,sizeof(param->kwh_amount_1));
    count += sizeof(param->kwh_amount_1);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_2,sizeof(param->kwh_amount_2));
    count += sizeof(param->kwh_amount_2);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_3,sizeof(param->kwh_amount_3));
    count += sizeof(param->kwh_amount_3);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_4,sizeof(param->kwh_amount_4));
    count += sizeof(param->kwh_amount_4);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_5,sizeof(param->kwh_amount_5));
    count += sizeof(param->kwh_amount_5);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_6,sizeof(param->kwh_amount_6));
    count += sizeof(param->kwh_amount_6);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_7,sizeof(param->kwh_amount_7));
    count += sizeof(param->kwh_amount_7);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_8,sizeof(param->kwh_amount_8));
    count += sizeof(param->kwh_amount_8);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_9,sizeof(param->kwh_amount_9));
    count += sizeof(param->kwh_amount_9);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_10,sizeof(param->kwh_amount_10));
    count += sizeof(param->kwh_amount_10);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_11,sizeof(param->kwh_amount_11));
    count += sizeof(param->kwh_amount_11);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_12,sizeof(param->kwh_amount_12));
    count += sizeof(param->kwh_amount_12);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_13,sizeof(param->kwh_amount_13));
    count += sizeof(param->kwh_amount_13);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_14,sizeof(param->kwh_amount_14));
    count += sizeof(param->kwh_amount_14);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_15,sizeof(param->kwh_amount_15));
    count += sizeof(param->kwh_amount_15);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_16,sizeof(param->kwh_amount_16));
    count += sizeof(param->kwh_amount_16);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_17,sizeof(param->kwh_amount_17));
    count += sizeof(param->kwh_amount_17);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_18,sizeof(param->kwh_amount_18));
    count += sizeof(param->kwh_amount_18);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_19,sizeof(param->kwh_amount_19));
    count += sizeof(param->kwh_amount_19);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_20,sizeof(param->kwh_amount_20));
    count += sizeof(param->kwh_amount_20);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_21,sizeof(param->kwh_amount_21));
    count += sizeof(param->kwh_amount_21);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_22,sizeof(param->kwh_amount_22));
    count += sizeof(param->kwh_amount_22);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_23,sizeof(param->kwh_amount_23));
    count += sizeof(param->kwh_amount_23);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_24,sizeof(param->kwh_amount_24));
    count += sizeof(param->kwh_amount_24);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_25,sizeof(param->kwh_amount_25));
    count += sizeof(param->kwh_amount_25);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_26,sizeof(param->kwh_amount_26));
    count += sizeof(param->kwh_amount_26);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_27,sizeof(param->kwh_amount_27));
    count += sizeof(param->kwh_amount_27);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_28,sizeof(param->kwh_amount_28));
    count += sizeof(param->kwh_amount_28);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_29,sizeof(param->kwh_amount_29));
    count += sizeof(param->kwh_amount_29);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_30,sizeof(param->kwh_amount_30));
    count += sizeof(param->kwh_amount_30);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_31,sizeof(param->kwh_amount_31));
    count += sizeof(param->kwh_amount_31);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_32,sizeof(param->kwh_amount_32));
    count += sizeof(param->kwh_amount_32);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_33,sizeof(param->kwh_amount_33));
    count += sizeof(param->kwh_amount_33);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_34,sizeof(param->kwh_amount_34));
    count += sizeof(param->kwh_amount_34);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_35,sizeof(param->kwh_amount_35));
    count += sizeof(param->kwh_amount_35);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_36,sizeof(param->kwh_amount_36));
    count += sizeof(param->kwh_amount_36);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_37,sizeof(param->kwh_amount_37));
    count += sizeof(param->kwh_amount_37);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_38,sizeof(param->kwh_amount_38));
    count += sizeof(param->kwh_amount_38);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_39,sizeof(param->kwh_amount_39));
    count += sizeof(param->kwh_amount_39);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_40,sizeof(param->kwh_amount_40));
    count += sizeof(param->kwh_amount_40);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_41,sizeof(param->kwh_amount_41));
    count += sizeof(param->kwh_amount_41);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_42,sizeof(param->kwh_amount_42));
    count += sizeof(param->kwh_amount_42);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_43,sizeof(param->kwh_amount_43));
    count += sizeof(param->kwh_amount_43);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_44,sizeof(param->kwh_amount_44));
    count += sizeof(param->kwh_amount_44);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_45,sizeof(param->kwh_amount_45));
    count += sizeof(param->kwh_amount_45);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_46,sizeof(param->kwh_amount_46));
    count += sizeof(param->kwh_amount_46);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_47,sizeof(param->kwh_amount_47));
    count += sizeof(param->kwh_amount_47);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_48,sizeof(param->kwh_amount_48));
    count += sizeof(param->kwh_amount_48);

    memcpy(&protocol_package.datas[count],&param->start_charge_type,sizeof(param->start_charge_type));
    count += sizeof(param->start_charge_type);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:202 send.\n");
    return ret;
}


// 205ºÅ
uint8_t  send_history_bill_206_protocol(xj_cmd_202* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+256; ;

    if(param == NULL)
    {
        xj_pal_print_log(xj_log_warning,"xj_cmd_206 illegal\n");
        return ECO_FALSE;
    }

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_206,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->gun_type;
    protocol_package.datas[count++] = param->gun_index;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id);
    count += sizeof(param->charge_user_id);
    memcpy(&protocol_package.datas[count],&param->charge_start_time,sizeof(param->charge_start_time));
    count += sizeof(param->charge_start_time);
    memcpy(&protocol_package.datas[count],&param->charge_end_time,sizeof(param->charge_end_time));
    count += sizeof(param->charge_end_time);
    memcpy(&protocol_package.datas[count],&param->charge_time,sizeof(param->charge_time));
    count += sizeof(param->charge_time);
    memcpy(&protocol_package.datas[count],&param->start_soc,sizeof(param->start_soc));
    count += sizeof(param->start_soc);
    memcpy(&protocol_package.datas[count],&param->end_soc,sizeof(param->end_soc));
    count += sizeof(param->end_soc);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->err_no);
    count += sizeof(param->err_no);
    memcpy(&protocol_package.datas[count],&param->charge_kwh_amount,sizeof(param->charge_kwh_amount));
    count += sizeof(param->charge_kwh_amount);
    memcpy(&protocol_package.datas[count],&param->start_charge_kwh_meter,sizeof(param->start_charge_kwh_meter));
    count += sizeof(param->start_charge_kwh_meter);
    memcpy(&protocol_package.datas[count],&param->end_charge_kwh_meter,sizeof(param->end_charge_kwh_meter));
    count += sizeof(param->end_charge_kwh_meter);
    memcpy(&protocol_package.datas[count],&param->total_charge_fee,sizeof(param->total_charge_fee));
    count += sizeof(param->total_charge_fee);
    memcpy(&protocol_package.datas[count],&param->is_not_stoped_by_card,sizeof(param->is_not_stoped_by_card));
    count += sizeof(param->is_not_stoped_by_card);
    memcpy(&protocol_package.datas[count],&param->start_card_money,sizeof(param->start_card_money));
    count += sizeof(param->start_card_money);
    memcpy(&protocol_package.datas[count],&param->end_card_money,sizeof(param->end_card_money));
    count += sizeof(param->end_card_money);
    memcpy(&protocol_package.datas[count],&param->total_service_fee,sizeof(param->total_service_fee));
    count += sizeof(param->total_service_fee);
    protocol_package.datas[count++] = param->is_paid_by_offline;
    protocol_package.datas[count++] = param->charge_policy;
    memcpy(&protocol_package.datas[count],&param->charge_policy_param,sizeof(param->charge_policy_param));
    count += sizeof(param->charge_policy_param);
    memcpy(&protocol_package.datas[count],&param->car_vin,sizeof(param->car_vin));
    count += sizeof(param->car_vin);
    memcpy(&protocol_package.datas[count],&param->car_plate_no,sizeof(param->car_plate_no));
    count += sizeof(param->car_plate_no);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_1,sizeof(param->kwh_amount_1));
    count += sizeof(param->kwh_amount_1);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_2,sizeof(param->kwh_amount_2));
    count += sizeof(param->kwh_amount_2);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_3,sizeof(param->kwh_amount_3));
    count += sizeof(param->kwh_amount_3);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_4,sizeof(param->kwh_amount_4));
    count += sizeof(param->kwh_amount_4);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_5,sizeof(param->kwh_amount_5));
    count += sizeof(param->kwh_amount_5);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_6,sizeof(param->kwh_amount_6));
    count += sizeof(param->kwh_amount_6);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_7,sizeof(param->kwh_amount_7));
    count += sizeof(param->kwh_amount_7);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_8,sizeof(param->kwh_amount_8));
    count += sizeof(param->kwh_amount_8);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_9,sizeof(param->kwh_amount_9));
    count += sizeof(param->kwh_amount_9);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_10,sizeof(param->kwh_amount_10));
    count += sizeof(param->kwh_amount_10);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_11,sizeof(param->kwh_amount_11));
    count += sizeof(param->kwh_amount_11);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_12,sizeof(param->kwh_amount_12));
    count += sizeof(param->kwh_amount_12);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_13,sizeof(param->kwh_amount_13));
    count += sizeof(param->kwh_amount_13);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_14,sizeof(param->kwh_amount_14));
    count += sizeof(param->kwh_amount_14);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_15,sizeof(param->kwh_amount_15));
    count += sizeof(param->kwh_amount_15);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_16,sizeof(param->kwh_amount_16));
    count += sizeof(param->kwh_amount_16);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_17,sizeof(param->kwh_amount_17));
    count += sizeof(param->kwh_amount_17);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_18,sizeof(param->kwh_amount_18));
    count += sizeof(param->kwh_amount_18);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_19,sizeof(param->kwh_amount_19));
    count += sizeof(param->kwh_amount_19);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_20,sizeof(param->kwh_amount_20));
    count += sizeof(param->kwh_amount_20);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_21,sizeof(param->kwh_amount_21));
    count += sizeof(param->kwh_amount_21);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_22,sizeof(param->kwh_amount_22));
    count += sizeof(param->kwh_amount_22);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_23,sizeof(param->kwh_amount_23));
    count += sizeof(param->kwh_amount_23);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_24,sizeof(param->kwh_amount_24));
    count += sizeof(param->kwh_amount_24);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_25,sizeof(param->kwh_amount_25));
    count += sizeof(param->kwh_amount_25);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_26,sizeof(param->kwh_amount_26));
    count += sizeof(param->kwh_amount_26);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_27,sizeof(param->kwh_amount_27));
    count += sizeof(param->kwh_amount_27);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_28,sizeof(param->kwh_amount_28));
    count += sizeof(param->kwh_amount_28);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_29,sizeof(param->kwh_amount_29));
    count += sizeof(param->kwh_amount_29);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_30,sizeof(param->kwh_amount_30));
    count += sizeof(param->kwh_amount_30);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_31,sizeof(param->kwh_amount_31));
    count += sizeof(param->kwh_amount_31);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_32,sizeof(param->kwh_amount_32));
    count += sizeof(param->kwh_amount_32);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_33,sizeof(param->kwh_amount_33));
    count += sizeof(param->kwh_amount_33);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_34,sizeof(param->kwh_amount_34));
    count += sizeof(param->kwh_amount_34);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_35,sizeof(param->kwh_amount_35));
    count += sizeof(param->kwh_amount_35);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_36,sizeof(param->kwh_amount_36));
    count += sizeof(param->kwh_amount_36);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_37,sizeof(param->kwh_amount_37));
    count += sizeof(param->kwh_amount_37);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_38,sizeof(param->kwh_amount_38));
    count += sizeof(param->kwh_amount_38);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_39,sizeof(param->kwh_amount_39));
    count += sizeof(param->kwh_amount_39);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_40,sizeof(param->kwh_amount_40));
    count += sizeof(param->kwh_amount_40);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_41,sizeof(param->kwh_amount_41));
    count += sizeof(param->kwh_amount_41);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_42,sizeof(param->kwh_amount_42));
    count += sizeof(param->kwh_amount_42);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_43,sizeof(param->kwh_amount_43));
    count += sizeof(param->kwh_amount_43);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_44,sizeof(param->kwh_amount_44));
    count += sizeof(param->kwh_amount_44);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_45,sizeof(param->kwh_amount_45));
    count += sizeof(param->kwh_amount_45);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_46,sizeof(param->kwh_amount_46));
    count += sizeof(param->kwh_amount_46);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_47,sizeof(param->kwh_amount_47));
    count += sizeof(param->kwh_amount_47);
    memcpy(&protocol_package.datas[count],&param->kwh_amount_48,sizeof(param->kwh_amount_48));
    count += sizeof(param->kwh_amount_48);

    memcpy(&protocol_package.datas[count],&param->start_charge_type,sizeof(param->start_charge_type));
    count += sizeof(param->start_charge_type);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));


    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:206 send.\n");
    return ret;
}

// 302ºÅ
uint8_t  send_bms_status_302_protocol(xj_cmd_302* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+191;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_302,++mqttMessageBase.masterSerialNum,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 2;

    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->work_stat;
    protocol_package.datas[count++] = param->car_connect_stat;
    memcpy(&protocol_package.datas[count],&param->brm_bms_connect_version,sizeof(param->brm_bms_connect_version));
    count += sizeof(param->brm_bms_connect_version);
    protocol_package.datas[count++] = param->brm_battery_type;
    memcpy(&protocol_package.datas[count],&param->brm_battery_power,sizeof(param->brm_battery_power));
    count += sizeof(param->brm_battery_power);
    memcpy(&protocol_package.datas[count],&param->brm_battery_voltage,sizeof(param->brm_battery_voltage));
    count += sizeof(param->brm_battery_voltage);
    memcpy(&protocol_package.datas[count],&param->brm_battery_supplier,sizeof(param->brm_battery_supplier));
    count += sizeof(param->brm_battery_supplier);
    memcpy(&protocol_package.datas[count],&param->brm_battery_seq,sizeof(param->brm_battery_seq));
    count += sizeof(param->brm_battery_seq);
    memcpy(&protocol_package.datas[count],&param->brm_battery_produce_year,sizeof(param->brm_battery_produce_year));
    count += sizeof(param->brm_battery_produce_year);
    memcpy(&protocol_package.datas[count],&param->brm_battery_produce_month,sizeof(param->brm_battery_produce_month));
    count += sizeof(param->brm_battery_produce_month);
    memcpy(&protocol_package.datas[count],&param->brm_battery_produce_day,sizeof(param->brm_battery_produce_day));
    count += sizeof(param->brm_battery_produce_day);
    memcpy(&protocol_package.datas[count],&param->brm_battery_charge_count,sizeof(param->brm_battery_charge_count));
    count += sizeof(param->brm_battery_charge_count);
    memcpy(&protocol_package.datas[count],&param->brm_battery_property_identification,sizeof(param->brm_battery_property_identification));
    count += sizeof(param->brm_battery_property_identification);
    count ++;
    memcpy(&protocol_package.datas[count],&param->brm_vin,sizeof(param->brm_vin));
    count += sizeof(param->brm_vin);
    memcpy(&protocol_package.datas[count],&param->brm_BMS_version,sizeof(param->brm_BMS_version));
    count += sizeof(param->brm_BMS_version);
    memcpy(&protocol_package.datas[count],&param->bcp_max_voltage,sizeof(param->bcp_max_voltage));
    count += sizeof(param->bcp_max_voltage);
    memcpy(&protocol_package.datas[count],&param->bcp_max_current,sizeof(param->bcp_max_current));
    count += sizeof(param->bcp_max_current);
    memcpy(&protocol_package.datas[count],&param->bcp_max_power,sizeof(param->bcp_max_power));
    count += sizeof(param->bcp_max_power);
    memcpy(&protocol_package.datas[count],&param->bcp_total_voltage,sizeof(param->bcp_total_voltage));
    count += sizeof(param->bcp_total_voltage);
    memcpy(&protocol_package.datas[count],&param->bcp_max_temperature,sizeof(param->bcp_max_temperature));
    count += sizeof(param->bcp_max_temperature);
    memcpy(&protocol_package.datas[count],&param->bcp_battery_soc,sizeof(param->bcp_battery_soc));
    count += sizeof(param->bcp_battery_soc);
    memcpy(&protocol_package.datas[count],&param->bcp_battery_soc_current_voltage,sizeof(param->bcp_battery_soc_current_voltage));
    count += sizeof(param->bcp_battery_soc_current_voltage);
    memcpy(&protocol_package.datas[count],&param->bro_BMS_isReady,sizeof(param->bro_BMS_isReady));
    count += sizeof(param->bro_BMS_isReady);
    memcpy(&protocol_package.datas[count],&param->bcl_voltage_need,sizeof(param->bcl_voltage_need));
    count += sizeof(param->bcl_voltage_need);
    memcpy(&protocol_package.datas[count],&param->bcl_current_need,sizeof(param->bcl_current_need));
    count += sizeof(param->bcl_current_need);
    memcpy(&protocol_package.datas[count],&param->bcl_charge_mode,sizeof(param->bcl_charge_mode));
    count += sizeof(param->bcl_charge_mode);
    memcpy(&protocol_package.datas[count],&param->bcs_test_voltage,sizeof(param->bcs_test_voltage));
    count += sizeof(param->bcs_test_voltage);
    memcpy(&protocol_package.datas[count],&param->bcs_test_current,sizeof(param->bcs_test_current));
    count += sizeof(param->bcs_test_current);
    memcpy(&protocol_package.datas[count],&param->bcs_max_single_voltage,sizeof(param->bcs_max_single_voltage));
    count += sizeof(param->bcs_max_single_voltage);
    memcpy(&protocol_package.datas[count],&param->bcs_max_single_no,sizeof(param->bcs_max_single_no));
    count += sizeof(param->bcs_max_single_no);
    memcpy(&protocol_package.datas[count],&param->bcs_current_soc,sizeof(param->bcs_current_soc));
    count += sizeof(param->bcs_current_soc);
    memcpy(&protocol_package.datas[count],&param->last_charge_time,sizeof(param->last_charge_time));
    count += sizeof(param->last_charge_time);
    memcpy(&protocol_package.datas[count],&param->bsm_single_no,sizeof(param->bsm_single_no));
    count += sizeof(param->bsm_single_no);
    memcpy(&protocol_package.datas[count],&param->bsm_max_temperature,sizeof(param->bsm_max_temperature));
    count += sizeof(param->bsm_max_temperature);


    memcpy(&protocol_package.datas[count],&param->bsm_max_temperature_check_no,sizeof(param->bsm_max_temperature_check_no));
    count += sizeof(param->bsm_max_temperature_check_no);
    memcpy(&protocol_package.datas[count],&param->bsm_min_temperature,sizeof(param->bsm_min_temperature));
    count += sizeof(param->bsm_min_temperature);
    memcpy(&protocol_package.datas[count],&param->bsm_min_temperature_check_no,sizeof(param->bsm_min_temperature_check_no));
    count += sizeof(param->bsm_min_temperature_check_no);
    memcpy(&protocol_package.datas[count],&param->bsm_voltage_too_high_or_too_low,sizeof(param->bsm_voltage_too_high_or_too_low));
    count += sizeof(param->bsm_voltage_too_high_or_too_low);
    memcpy(&protocol_package.datas[count],&param->bsm_car_battery_soc_too_high_or_too_low,sizeof(param->bsm_car_battery_soc_too_high_or_too_low));
    count += sizeof(param->bsm_car_battery_soc_too_high_or_too_low);
    memcpy(&protocol_package.datas[count],&param->bsm_car_battery_charge_over_current,sizeof(param->bsm_car_battery_charge_over_current));
    count += sizeof(param->bsm_car_battery_charge_over_current);
    memcpy(&protocol_package.datas[count],&param->bsm_battery_temperature_too_high,sizeof(param->bsm_battery_temperature_too_high));
    count += sizeof(param->bsm_battery_temperature_too_high);
    memcpy(&protocol_package.datas[count],&param->bsm_battery_insulation_state,sizeof(param->bsm_battery_insulation_state));
    count += sizeof(param->bsm_battery_insulation_state);
    memcpy(&protocol_package.datas[count],&param->bsm_battery_connect_state,sizeof(param->bsm_battery_connect_state));
    count += sizeof(param->bsm_battery_connect_state);
    memcpy(&protocol_package.datas[count],&param->bsm_allow_charge,sizeof(param->bsm_allow_charge));
    count += sizeof(param->bsm_allow_charge);
    memcpy(&protocol_package.datas[count],&param->bst_BMS_soc_target,sizeof(param->bst_BMS_soc_target));
    count += sizeof(param->bst_BMS_soc_target);
    memcpy(&protocol_package.datas[count],&param->bst_BMS_voltage_target,sizeof(param->bst_BMS_voltage_target));
    count += sizeof(param->bst_BMS_voltage_target);
    memcpy(&protocol_package.datas[count],&param->bst_single_voltage_target,sizeof(param->bst_single_voltage_target));
    count += sizeof(param->bst_single_voltage_target);
    memcpy(&protocol_package.datas[count],&param->bst_finish,sizeof(param->bst_finish));
    count += sizeof(param->bst_finish);
    memcpy(&protocol_package.datas[count],&param->bst_isolation_error,sizeof(param->bst_isolation_error));
    count += sizeof(param->bst_isolation_error);
    memcpy(&protocol_package.datas[count],&param->bst_connect_over_temperature,sizeof(param->bst_connect_over_temperature));
    count += sizeof(param->bst_connect_over_temperature);
    memcpy(&protocol_package.datas[count],&param->bst_BMS_over_temperature,sizeof(param->bst_BMS_over_temperature));
    count += sizeof(param->bst_BMS_over_temperature);
    memcpy(&protocol_package.datas[count],&param->bst_connect_error,sizeof(param->bst_connect_error));
    count += sizeof(param->bst_connect_error);
    memcpy(&protocol_package.datas[count],&param->bst_battery_over_temperature,sizeof(param->bst_battery_over_temperature));
    count += sizeof(param->bst_battery_over_temperature);
    memcpy(&protocol_package.datas[count],&param->bst_high_voltage_relay_error,sizeof(param->bst_high_voltage_relay_error));
    count += sizeof(param->bst_high_voltage_relay_error);
    memcpy(&protocol_package.datas[count],&param->bst_point2_test_error,sizeof(param->bst_point2_test_error));
    count += sizeof(param->bst_point2_test_error);
    memcpy(&protocol_package.datas[count],&param->bst_other_error,sizeof(param->bst_other_error));
    count += sizeof(param->bst_other_error);
    memcpy(&protocol_package.datas[count],&param->bst_current_too_high,sizeof(param->bst_current_too_high));
    count += sizeof(param->bst_current_too_high);
    memcpy(&protocol_package.datas[count],&param->bst_voltage_too_high,sizeof(param->bst_voltage_too_high));
    count += sizeof(param->bst_voltage_too_high);
    memcpy(&protocol_package.datas[count],&param->bst_stop_soc,sizeof(param->bst_stop_soc));
    count += sizeof(param->bst_stop_soc);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_low_voltage,sizeof(param->bsd_battery_low_voltage));
    count += sizeof(param->bsd_battery_low_voltage);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_high_voltage,sizeof(param->bsd_battery_high_voltage));
    count += sizeof(param->bsd_battery_high_voltage);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_low_temperature,sizeof(param->bsd_battery_low_temperature));
    count += sizeof(param->bsd_battery_low_temperature);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_high_temperature,sizeof(param->bsd_battery_high_temperature));
    count += sizeof(param->bsd_battery_high_temperature);
    memcpy(&protocol_package.datas[count],&param->error_68,sizeof(param->error_68));
    count += sizeof(param->error_68);
    memcpy(&protocol_package.datas[count],&param->error_69,sizeof(param->error_69));
    count += sizeof(param->error_69);
    memcpy(&protocol_package.datas[count],&param->error_70,sizeof(param->error_70));
    count += sizeof(param->error_70);
    memcpy(&protocol_package.datas[count],&param->error_71,sizeof(param->error_71));
    count += sizeof(param->error_71);
    memcpy(&protocol_package.datas[count],&param->error_72,sizeof(param->error_72));
    count += sizeof(param->error_72);
    memcpy(&protocol_package.datas[count],&param->error_73,sizeof(param->error_73));
    count += sizeof(param->error_73);
    memcpy(&protocol_package.datas[count],&param->error_74,sizeof(param->error_74));
    count += sizeof(param->error_74);
    memcpy(&protocol_package.datas[count],&param->error_75,sizeof(param->error_75));
    count += sizeof(param->error_75);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:302 send.\n");
    return ret;
}


uint8_t  send_bms_status_304_protocol(xj_cmd_304* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+147;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_304,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,32);
    count += sizeof(param->equipment_id);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id,32);
    count += sizeof(param->charge_user_id);

    protocol_package.datas[count++] = param->work_stat;
    memcpy(&protocol_package.datas[count],&param->brm_bms_connect_version,sizeof(param->brm_bms_connect_version));
    count += sizeof(param->brm_bms_connect_version);
    protocol_package.datas[count++] = param->brm_battery_type;
    memcpy(&protocol_package.datas[count],&param->brm_battery_power,sizeof(param->brm_battery_power));
    count += sizeof(param->brm_battery_power);
    memcpy(&protocol_package.datas[count],&param->brm_battery_voltage,sizeof(param->brm_battery_voltage));
    count += sizeof(param->brm_battery_voltage);
    memcpy(&protocol_package.datas[count],&param->brm_battery_supplier,sizeof(param->brm_battery_supplier));
    count += sizeof(param->brm_battery_supplier);
    memcpy(&protocol_package.datas[count],&param->brm_battery_seq,sizeof(param->brm_battery_seq));
    count += sizeof(param->brm_battery_seq);
    memcpy(&protocol_package.datas[count],&param->brm_battery_produce_year,sizeof(param->brm_battery_produce_year));
    count += sizeof(param->brm_battery_produce_year);
    memcpy(&protocol_package.datas[count],&param->brm_battery_produce_month,sizeof(param->brm_battery_produce_month));
    count += sizeof(param->brm_battery_produce_month);
    memcpy(&protocol_package.datas[count],&param->brm_battery_produce_day,sizeof(param->brm_battery_produce_day));
    count += sizeof(param->brm_battery_produce_day);
    memcpy(&protocol_package.datas[count],&param->brm_battery_charge_count,sizeof(param->brm_battery_charge_count));
    count += sizeof(param->brm_battery_charge_count);
    memcpy(&protocol_package.datas[count],&param->brm_battery_property_identification,sizeof(param->brm_battery_property_identification));
    count += sizeof(param->brm_battery_property_identification);
    count ++;
    memcpy(&protocol_package.datas[count],&param->brm_vin,sizeof(param->brm_vin));
    count += sizeof(param->brm_vin);
    memcpy(&protocol_package.datas[count],&param->brm_BMS_version,sizeof(param->brm_BMS_version));
    count += sizeof(param->brm_BMS_version);
    memcpy(&protocol_package.datas[count],&param->bcp_max_voltage,sizeof(param->bcp_max_voltage));
    count += sizeof(param->bcp_max_voltage);
    memcpy(&protocol_package.datas[count],&param->bcp_max_current,sizeof(param->bcp_max_current));
    count += sizeof(param->bcp_max_current);
    memcpy(&protocol_package.datas[count],&param->bcp_max_power,sizeof(param->bcp_max_power));
    count += sizeof(param->bcp_max_power);
    memcpy(&protocol_package.datas[count],&param->bcp_total_voltage,sizeof(param->bcp_total_voltage));
    count += sizeof(param->bcp_total_voltage);
    memcpy(&protocol_package.datas[count],&param->bcp_max_temperature,sizeof(param->bcp_max_temperature));
    count += sizeof(param->bcp_max_temperature);
    memcpy(&protocol_package.datas[count],&param->bcp_battery_soc,sizeof(param->bcp_battery_soc));
    count += sizeof(param->bcp_battery_soc);
    memcpy(&protocol_package.datas[count],&param->bcp_battery_soc_current_voltage,sizeof(param->bcp_battery_soc_current_voltage));
    count += sizeof(param->bcp_battery_soc_current_voltage);
    memcpy(&protocol_package.datas[count],&param->bro_BMS_isReady,sizeof(param->bro_BMS_isReady));
    count += sizeof(param->bro_BMS_isReady);
    memcpy(&protocol_package.datas[count],&param->CRO_isReady,sizeof(param->CRO_isReady));
    count += sizeof(param->CRO_isReady);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:304 send.\n");
    return ret;
}

uint8_t  send_bms_status_306_protocol(xj_cmd_306* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+107;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_306,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,32);
    count += sizeof(param->equipment_id);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id,32);
    count += sizeof(param->charge_user_id);

    protocol_package.datas[count++] = param->work_stat;
    memcpy(&protocol_package.datas[count],&param->bcl_voltage_need,sizeof(param->bcl_voltage_need));
    count += sizeof(param->bcl_voltage_need);
    memcpy(&protocol_package.datas[count],&param->bcl_current_need,sizeof(param->bcl_current_need));
    count += sizeof(param->bcl_current_need);
    memcpy(&protocol_package.datas[count],&param->bcl_charge_mode,sizeof(param->bcl_charge_mode));
    count += sizeof(param->bcl_charge_mode);
    memcpy(&protocol_package.datas[count],&param->bcs_test_voltage,sizeof(param->bcs_test_voltage));
    count += sizeof(param->bcs_test_voltage);
    memcpy(&protocol_package.datas[count],&param->bcs_test_current,sizeof(param->bcs_test_current));
    count += sizeof(param->bcs_test_current);
    memcpy(&protocol_package.datas[count],&param->bcs_max_single_voltage,sizeof(param->bcs_max_single_voltage));
    count += sizeof(param->bcs_max_single_voltage);
    memcpy(&protocol_package.datas[count],&param->bcs_max_single_no,sizeof(param->bcs_max_single_no));
    count += sizeof(param->bcs_max_single_no);
    memcpy(&protocol_package.datas[count],&param->bcs_current_soc,sizeof(param->bcs_current_soc));
    count += sizeof(param->bcs_current_soc);
    memcpy(&protocol_package.datas[count],&param->last_charge_time,sizeof(param->last_charge_time));
    count += sizeof(param->last_charge_time);
    memcpy(&protocol_package.datas[count],&param->bsm_single_no,sizeof(param->bsm_single_no));
    count += sizeof(param->bsm_single_no);
    memcpy(&protocol_package.datas[count],&param->bsm_max_temperature,sizeof(param->bsm_max_temperature));
    count += sizeof(param->bsm_max_temperature);
    memcpy(&protocol_package.datas[count],&param->bsm_max_temperature_check_no,sizeof(param->bsm_max_temperature_check_no));
    count += sizeof(param->bsm_max_temperature_check_no);
    memcpy(&protocol_package.datas[count],&param->bsm_min_temperature,sizeof(param->bsm_min_temperature));
    count += sizeof(param->bsm_min_temperature);
    memcpy(&protocol_package.datas[count],&param->bsm_min_temperature_check_no,sizeof(param->bsm_min_temperature_check_no));
    count += sizeof(param->bsm_min_temperature_check_no);
    memcpy(&protocol_package.datas[count],&param->bsm_voltage_too_high_or_too_low,sizeof(param->bsm_voltage_too_high_or_too_low));
    count += sizeof(param->bsm_voltage_too_high_or_too_low);
    memcpy(&protocol_package.datas[count],&param->bsm_car_battery_soc_too_high_or_too_low,sizeof(param->bsm_car_battery_soc_too_high_or_too_low));
    count += sizeof(param->bsm_car_battery_soc_too_high_or_too_low);
    memcpy(&protocol_package.datas[count],&param->bsm_car_battery_charge_over_current,sizeof(param->bsm_car_battery_charge_over_current));
    count += sizeof(param->bsm_car_battery_charge_over_current);
    memcpy(&protocol_package.datas[count],&param->bsm_battery_temperature_too_high,sizeof(param->bsm_battery_temperature_too_high));
    count += sizeof(param->bsm_battery_temperature_too_high);
    memcpy(&protocol_package.datas[count],&param->bsm_battery_insulation_state,sizeof(param->bsm_battery_insulation_state));
    count += sizeof(param->bsm_battery_insulation_state);
    memcpy(&protocol_package.datas[count],&param->bsm_battery_connect_state,sizeof(param->bsm_battery_connect_state));
    count += sizeof(param->bsm_battery_connect_state);
    memcpy(&protocol_package.datas[count],&param->bsm_allow_charge,sizeof(param->bsm_allow_charge));
    count += sizeof(param->bsm_allow_charge);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:306 send.\n");
    return ret;
}


uint8_t  send_bms_308_protocol(xj_cmd_308* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+71;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_308,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,32);
    count += sizeof(param->equipment_id);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id,32);
    count += sizeof(param->charge_user_id);
    protocol_package.datas[count++] = param->work_stat;


    memcpy(&protocol_package.datas[count],&param->CST_stop_reason,sizeof(param->CST_stop_reason));
    count += sizeof(param->CST_stop_reason);
    memcpy(&protocol_package.datas[count],&param->CST_fault_reason,sizeof(param->CST_fault_reason));
    count += sizeof(param->CST_fault_reason);
    memcpy(&protocol_package.datas[count],&param->CST_error_reason,sizeof(param->CST_error_reason));
    count += sizeof(param->CST_error_reason);


    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:308 send.\n");
    return ret;
}

uint8_t  send_bms_310_protocol(xj_cmd_310* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+71;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_310,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,32);
    count += sizeof(param->equipment_id);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id,32);
    count += sizeof(param->charge_user_id);
    protocol_package.datas[count++] = param->work_stat;


    memcpy(&protocol_package.datas[count],&param->BST_stop_reason,sizeof(param->BST_stop_reason));
    count += sizeof(param->BST_stop_reason);
    memcpy(&protocol_package.datas[count],&param->BST_fault_reason,sizeof(param->BST_fault_reason));
    count += sizeof(param->BST_fault_reason);
    memcpy(&protocol_package.datas[count],&param->BST_error_reason,sizeof(param->BST_error_reason));
    count += sizeof(param->BST_error_reason);


    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:310 send.\n");
    return ret;
}

uint8_t  send_bms_312_protocol(xj_cmd_312* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+82;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_312,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],&param->gun_index,sizeof(param->gun_index));
    count += sizeof(param->gun_index);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id,sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    strncpy(&protocol_package.datas[count],(uint8_t*)&param->charge_user_id,sizeof(param->charge_user_id));
    count += sizeof(param->charge_user_id);
    protocol_package.datas[count++] = param->work_stat;


    memcpy(&protocol_package.datas[count],&param->bsd_stop_soc,sizeof(param->bsd_stop_soc));
    count += sizeof(param->bsd_stop_soc);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_low_voltage,sizeof(param->bsd_battery_low_voltage));
    count += sizeof(param->bsd_battery_low_voltage);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_high_voltage,sizeof(param->bsd_battery_high_voltage));
    count += sizeof(param->bsd_battery_high_voltage);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_low_temperature,sizeof(param->bsd_battery_low_temperature));
    count += sizeof(param->bsd_battery_low_temperature);
    memcpy(&protocol_package.datas[count],&param->bsd_battery_high_temperature,sizeof(param->bsd_battery_high_temperature));
    count += sizeof(param->bsd_battery_high_temperature);
    memcpy(&protocol_package.datas[count],&param->error_68,sizeof(param->error_68));
    count += sizeof(param->error_68);
    memcpy(&protocol_package.datas[count],&param->error_69,sizeof(param->error_69));
    count += sizeof(param->error_69);
    memcpy(&protocol_package.datas[count],&param->error_70,sizeof(param->error_70));
    count += sizeof(param->error_70);
    memcpy(&protocol_package.datas[count],&param->error_71,sizeof(param->error_71));
    count += sizeof(param->error_71);
    memcpy(&protocol_package.datas[count],&param->error_72,sizeof(param->error_72));
    count += sizeof(param->error_72);
    memcpy(&protocol_package.datas[count],&param->error_73,sizeof(param->error_73));
    count += sizeof(param->error_73);
    memcpy(&protocol_package.datas[count],&param->error_74,sizeof(param->error_74));
    count += sizeof(param->error_74);
    memcpy(&protocol_package.datas[count],&param->error_75,sizeof(param->error_75));
    count += sizeof(param->error_75);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:312 send.\n");
    return ret;
}


// 410ºÅ
uint8_t  send_ask_log_410_protocol(xj_cmd_410* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+164;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_410,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;

    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->log_name);
    count += sizeof(param->log_name);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:410 send.\n");
    return ret;
}


// 2
uint8_t  send_set_502_protocol(xj_cmd_502* param,uint8_t Qos)
{
    if(param == NULL)
    {
        xj_pal_print_log(xj_log_warning,"send_set_502_protocol param is NULL\n");
        return ECO_FALSE;
    }

    protocol_base	protocol_package;
    int totollen = BASE_LENGTH + 4 + sizeof(param->equipment_id) + sizeof(param->set_result);
    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_502,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;

    memcpy(&protocol_package.datas[count],(char*)&param->success_number,sizeof(param->success_number));
    count += sizeof(param->success_number);

    memcpy(&protocol_package.datas[count],(char*)&param->equipment_id,sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->set_result;

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:502 send.\n");
    return ret;
}


uint8_t  send_set_504_protocol(xj_cmd_504* param,uint8_t Qos)
{
    if(param == NULL)
    {
        xj_pal_print_log(xj_log_warning,"send_set_504_protocol param is NULL\n");
        return ECO_FALSE;
    }

    protocol_base	protocol_package;
    int totollen = BASE_LENGTH + sizeof(param->equipment_id) + sizeof(param->set_result);
    protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_504,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],(char*)&param->equipment_id,sizeof(param->equipment_id));
    count += sizeof(param->equipment_id);
    protocol_package.datas[count++] = param->set_result;

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:504 send.\n");
    return ret;
}

uint8_t  send_set_506_protocol(xj_cmd_506* param,uint8_t Qos)
{
	 if(param == NULL)
	 {
		 xj_pal_print_log(xj_log_warning,"send_set_506_protocol param is NULL\n");
		 return ECO_FALSE;
	 }

	 protocol_base	protocol_package;
	 int totollen = BASE_LENGTH + 4 + sizeof(param->gun_index) +  sizeof(param->charge_user_id) + sizeof(param->set_result);
	 protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
	 parse_protocol_header(&protocol_package,xj_cmd_type_506,param->serial_code,totollen);

	 int count = DATA_FIELD_COUNT;
	 count += 4;
	 memcpy(&protocol_package.datas[count],(char*)&param->gun_index,sizeof(param->gun_index));
	 count += sizeof(param->gun_index);

	 memcpy(&protocol_package.datas[count],(char*)&param->charge_user_id,sizeof(param->charge_user_id));
	 count += sizeof(param->charge_user_id);

	 memcpy(&protocol_package.datas[count],(char*)&param->set_result,sizeof(param->set_result));
	 count += sizeof(param->set_result);

	 protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
	 memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

	 uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:506 send.\n");
	 return ret;
}

uint8_t  send_set_508_protocol(xj_cmd_508* param,uint8_t Qos)
{
	 if(param == NULL)
	 {
		 xj_pal_print_log(xj_log_warning,"send_set_508_protocol param is NULL\n");
		 return ECO_FALSE;
	 }

	 protocol_base	protocol_package;
     int data_len = sizeof(xj_cmd_508) - sizeof(param->fail_item) + param->fail_cnt * sizeof(xj_cmd_508_fail_item);
	 int totollen = BASE_LENGTH + data_len;

     protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
	 parse_protocol_header(&protocol_package,xj_cmd_type_508,param->serial_code,totollen);

	 int count = DATA_FIELD_COUNT;

     memcpy(&protocol_package.datas[count], param, data_len);
     count += data_len;

	 protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
	 memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

//    ///----------zgq调试打印-------------------///
//    printf("***********************************************\n");
//    printf("zgq 508 send = ");
//    int i;
//    for (i = 0; i < totollen; i++)
//    {
//        printf("%02x", protocol_package.datas[i]);
//    }
//    printf("\n");
//    printf("***********************************************\n");
//    //------------------------------------------//

	 uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:508 send.\n");
	 return ret;
}

uint8_t  send_set_512_protocol(xj_cmd_512* param,uint8_t Qos)
{
	 if(param == NULL)
	 {
		 xj_pal_print_log(xj_log_warning,"send_set_512_protocol param is NULL\n");
		 return ECO_FALSE;
	 }
     
	 protocol_base	protocol_package;
     int data_len = sizeof(xj_cmd_512);
	 int totollen = BASE_LENGTH + data_len;

     protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
	 parse_protocol_header(&protocol_package,xj_cmd_type_512,param->serial_code,totollen);

	 int count = DATA_FIELD_COUNT;

     memcpy(&protocol_package.datas[count], param, data_len);
     count += data_len;

	 protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
	 memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

//    ///----------zgq调试打印-------------------///
//    printf("***********************************************\n");
//    printf("zgq 512 send = ");
//    int i;
//    for (i = 0; i < totollen; i++)
//    {
//        printf("%02x", protocol_package.datas[i]);
//    }
//    printf("\n");
//    printf("***********************************************\n");
//    //------------------------------------------//

	 uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:512 send.\n");
	 return ret;
}

uint8_t  send_set_514_protocol(xj_cmd_514* param,uint8_t Qos)
{
	 if(param == NULL)
	 {
		 xj_pal_print_log(xj_log_warning,"send_set_514_protocol param is NULL\n");
		 return ECO_FALSE;
	 }
     
	 protocol_base	protocol_package;

     int data_len = sizeof(xj_cmd_514) - sizeof(param->data);
     int i;
     int start = 0;
     /// 获取消息体长度
     for (i = 0; i < param->resp_cnt; i++)
     {
         start += 2;
         start += (param->data[start] + 1);
     }
     data_len += start;
	 int totollen = BASE_LENGTH + data_len;

     protocol_package.datas= (uint8_t*)_g_send_data_bytes_buf;
	 parse_protocol_header(&protocol_package,xj_cmd_type_514,param->serial_code,totollen);

	 int count = DATA_FIELD_COUNT;

     memcpy(&protocol_package.datas[count], param, data_len);
     count += data_len;

	 protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
	 memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

//    ///----------zgq调试打印-------------------///
//    printf("***********************************************\n");
//    printf("zgq 514 send = ");
//    for (i = 0; i < totollen; i++)
//    {
//        printf("%02x", protocol_package.datas[i]);
//    }
//    printf("\n");
//    printf("***********************************************\n");
//    //------------------------------------------//

	 uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:514 send.\n");
	return ret;
}

// 510ºÅ
uint8_t  send_ask_bms_log_510_protocol(xj_cmd_510* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+164;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_510,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    count += 4;

    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->log_name);
    count += sizeof(param->log_name);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:510 send.\n");
    return ret;
}
// 802ºÅ
uint8_t  send_safety_certification_802_protocol(xj_cmd_802* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totolLen = BASE_LENGTH+43+param->key_len;

    if(param->key_datas == NULL)
    {
        xj_pal_print_log(xj_log_warning,"xj_cmd_802.datas illegal\n");
        return ECO_FALSE;
    }

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_802,param->serial_code,totolLen);

    int count = DATA_FIELD_COUNT;
    memcpy(&protocol_package.datas[count],&param->key_len,sizeof(param->key_len));
    count += sizeof(param->key_len);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->key_datas);
    count += sizeof(param->key_datas);
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->equipment_id);
    count += sizeof(param->equipment_id);
    memcpy(&protocol_package.datas[count],&param->encrypted_type,sizeof(param->encrypted_type));
    count += sizeof(param->encrypted_type);
    memcpy(&protocol_package.datas[count],&param->encrypted_version,sizeof(param->encrypted_version));
    count += sizeof(param->encrypted_version);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totolLen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totolLen,Qos,"cmd:802 send.\n");
    return ret;
}
// 1102ºÅ
uint8_t  send_soft_update_1102_protocol(xj_cmd_1102* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totollen = BASE_LENGTH+33;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_1102,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;
    protocol_package.datas[count++] = param->update_result;
    strcpy(&protocol_package.datas[count],(uint8_t*)&param->md5);
    count += sizeof(param->md5);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:1102 send.\n");
    return ret;
}


// 1304ºÅ
uint8_t  send_set_electricity_fee_1304_protocol(xj_cmd_1304* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totollen = BASE_LENGTH+48*4;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_1304,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;

    memcpy(&protocol_package.datas[count],(uint8_t*)&param->fee_data,sizeof(param->fee_data));
    count += sizeof(param->fee_data);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:1304 send.\n");
    return ret;
}

uint8_t  send_set_serice_fee_1306_protocol(xj_cmd_1306* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totollen = BASE_LENGTH+48*4+2;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_1306,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;
    protocol_package.datas[count++]=param->cmd_type;
    protocol_package.datas[count++]=param->gun_index;
    memcpy(&protocol_package.datas[count],(uint8_t*)&param->fee_data,sizeof(param->fee_data));
    count += sizeof(param->fee_data);

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:1306 send.\n");
    return ret;
}


uint8_t  send_set_fee_1310_protocol(xj_cmd_1310* param,uint8_t Qos)
{
    protocol_base	protocol_package;
    int totollen = BASE_LENGTH+1;

    protocol_package.datas=(uint8_t*)_g_send_data_bytes_buf;
    parse_protocol_header(&protocol_package,xj_cmd_type_1310,param->serial_code,totollen);

    int count = DATA_FIELD_COUNT;
    protocol_package.datas[count++]=param->set_result;

    protocol_package.check_code= get_check_sum(&protocol_package.datas[0],totollen-1);
    memcpy(&protocol_package.datas[count],&protocol_package.check_code,sizeof(protocol_package.check_code));

    uint8_t ret = mqtt_send_message(&protocol_package,totollen,Qos,"cmd:1310 send.\n");
    return ret;
}


uint8_t recv_buf_check(uint8_t* datas,uint16_t len,protocol_base* protocol)
{
    int8_t xor = get_check_sum(datas,len-1);

    //if(xor != datas[len-1])
		
	if((uint8_t)xor != datas[len-1])
    {
        xj_pal_print_log(xj_log_remind,"xor check failed, xor:%x,recv:%x\n",xor,datas[len-1]);
        return ECO_FALSE;
    }

    int count=0;
    memcpy((uint8_t*)&(protocol->header_code),&datas[count],sizeof(protocol->header_code));
    count += sizeof(protocol->header_code);
    memcpy((uint8_t*)&(protocol->length_code),&datas[count],sizeof(protocol->length_code));
    count += sizeof(protocol->length_code);
    memcpy((uint8_t*)&(protocol->version_code),&datas[count],sizeof(protocol->version_code));
    count += sizeof(protocol->version_code);
    memcpy((uint8_t*)&(protocol->serial_code),&datas[count],sizeof(protocol->serial_code));
    count += sizeof(protocol->serial_code);
    memcpy((uint8_t*)&(protocol->cmd_code),&datas[count],sizeof(protocol->cmd_code));
    count += sizeof(protocol->cmd_code);
    protocol->check_code = datas[len-1];

    if(protocol->header_code != REMOTE_HEADER)
    {
        xj_pal_print_log(xj_log_remind,"header check failed, header:%x,recv:%x\n",REMOTE_HEADER,protocol->header_code);
        return ECO_FALSE;
    }
    if(protocol->length_code != len)
    {
        xj_pal_print_log(xj_log_remind,"length check failed, length:%x,recv:%x\n",len,protocol->length_code);
        return ECO_FALSE;
    }

    return ECO_TRUE;
}

void exit_example(xj_pal_socket_handle* sockfd, xj_pal_thread_t *client_daemon)
{
    xj_pal_print_log(xj_log_message,"close mqtt conn.\n");
    _g_is_login = 0;

    if (sockfd != NULL)
    {
        mqttMessageBase.client.Connect_status = _disconnect;
        xj_Socket_state = _DISCONNECT_;
        //mqtt_disconnect(&mqttMessageBase.client);//S端好像会主动断开TCP
        //__mqtt_send(&mqttMessageBase.client);

        xj_pal_close_socket(sockfd);
    }
    if (client_daemon != NULL)
    {
        /* it's dangerous to call thread_cancel in unix like system. exit the thread through quiting the while loop.
         * but in freertos like system, the thread must be canceled explicitly rather than quiting the while loop.
         */
    }
}

void publish_callback(void** unused, struct mqtt_response_publish *published)
{
    if(published->application_message_size < 12 || published->application_message_size > 1024)
    {
        return ;
    }
    protocol_base protocol;
    int num = utf8_to_iso((uint8_t*) published->application_message,published->application_message_size,_g_recv_data_bytes_buf);
    if(num < 0)
        return;
    uint8_t checkFlag= recv_buf_check(_g_recv_data_bytes_buf,num,&protocol);

    if(ECO_TRUE == checkFlag)
    {
        proc_public_message(_g_recv_data_bytes_buf,&protocol,num);
    }
    else
    {
        xj_pal_print_log(xj_log_remind,"Failed to receive data validation!\n");
    }

}

void* client_refresher(void* client)
{
    for(;;)
    {
        while(_CONNECT_ != xj_Socket_state)
        {
            xj_pal_msleep(10U);
        }

        enum MQTTErrors ret = mqtt_sync((struct mqtt_client*) client);
        if(ret != MQTT_OK)
        {
            //   xj_pal_print_log(xj_log_remind,"client_refresher ret ERROR\n");
        }
        xj_pal_msleep(10U);
    }
    return NULL;
}

char xj_mqtt_connect(char* addr,int port,char* username,char* password,char* client_identifier, void*(*callback_cmd_received)(void* param,xj_mqtt_cmd_enum cmd_type))
{

    char port_str[20];
    const uint8_t clear_session = 0x02;
    uint16_t message_Time = 0;
    static char Create_lock_flag = FALSE;

    sprintf(port_str,"%d",port);


    mqttMessageBase.address = addr;
    mqttMessageBase.port = port_str;
    mqttMessageBase.password = password;
    mqttMessageBase.username = username;
    mqttMessageBase.topic = client_identifier;
    memset((char*)&mqttMessageBase.client_daemon,0,sizeof(mqttMessageBase.client_daemon));

    if(FALSE == Create_lock_flag)
    {
        MQTT_PAL_MUTEX_INIT(&mqttMessageBase.sendlock);
        Create_lock_flag = TRUE;
    }

    xj_pal_print_log(xj_log_message,"Try opening socket %s :%d ,ID:%s\n",addr,port,client_identifier);
    mqttMessageBase.sockfd = xj_pal_open_nb_socket(addr, port_str);

    if (mqttMessageBase.sockfd == NULL)
    {
        xj_pal_print_log(xj_log_remind,"xj_pal_open_nb_socket failed!\n");
        return ECO_FALSE;
    }
    else
    {
        xj_Socket_state = _CONNECT_;
    }

    mqtt_init(&mqttMessageBase.client, *(mqttMessageBase.sockfd), mqttMessageBase.sendbuf, sizeof(mqttMessageBase.sendbuf), mqttMessageBase.recvbuf, sizeof(mqttMessageBase.recvbuf), publish_callback);
    mqttMessageBase.callback = callback_cmd_received;
    mqttMessageBase.masterSerialNum = 0;
    mqttMessageBase.heart_index = 0;


    mqttMessageBase.client.Connect_status = _Connect_wait;

    /*mqtt_connect(&mqttMessageBase.client, mqttMessageBase.topic, mqttMessageBase.topic, mqttMessageBase.topic, strlen(mqttMessageBase.topic), mqttMessageBase.username, mqttMessageBase.password, 0|clear_session, 20);*/
    mqtt_connect(&mqttMessageBase.client, mqttMessageBase.topic, NULL, NULL, strlen(mqttMessageBase.topic), mqttMessageBase.username, mqttMessageBase.password, 0|clear_session, 20);

    for(message_Time = 0 ; message_Time < (TIME_OUT_3_SEC * 100) ; message_Time++)
    {
        xj_pal_msleep(10);
        if( _Connect_wait != mqttMessageBase.client.Connect_status )
        {
            break;
        }
    }

    if(_connack == mqttMessageBase.client.Connect_status)
    {
        mqttMessageBase.client.error = MQTT_OK;
    }
    else
    {
        mqttMessageBase.client.error = MQTT_ERROR_CONNECT_NOT_CALLED;

        xj_pal_print_log(xj_log_remind,"mqtt_connect error.time= %d\n",message_Time);
        xj_mqtt_disconnect();
        return ECO_FALSE;
    }

    mqttMessageBase.client.Subscribe_status = _Subscribe_wait;
    mqtt_subscribe(&mqttMessageBase.client, mqttMessageBase.topic, 0);

    for(message_Time = 0 ; message_Time < (TIME_OUT_3_SEC * 100) ; message_Time++)
    {
        xj_pal_msleep(10);
        if( _Subscribe_wait != mqttMessageBase.client.Subscribe_status )
        {
            break;
        }
    }

    if(_suback == mqttMessageBase.client.Subscribe_status)
    {
        mqttMessageBase.client.error = MQTT_OK;
    }
    else
    {
        mqttMessageBase.client.error = MQTT_ERROR_SUBSCRIBE_FAILED;
        xj_pal_print_log(xj_log_remind,"mqtt_subscribe error!\n");
        xj_mqtt_disconnect();
        return ECO_FALSE;
    }

    return ECO_TRUE;
}
void xj_mqtt_disconnect()
{
    exit_example(mqttMessageBase.sockfd,&mqttMessageBase.client_daemon);
}
void* xj_mqtt_send_cmd(xj_mqtt_cmd_enum cmd_type, void * cmd, uint16_t time_out, uint8_t Qos)
{
    uint16_t i=0;
    void* param_ret = NULL;

    if(cmd == NULL)
    {
        xj_pal_print_log(xj_log_Error,"xj_mqtt_send_cmd.param illegal\n");
        return NULL;
    }
    if((_g_is_login == 0) && (cmd_type != xj_cmd_type_114))
        return NULL;


    MQTT_PAL_MUTEX_LOCK(&mqttMessageBase.sendlock);

    switch (cmd_type)
    {
    case xj_cmd_type_34:
    {
        recv_protocol.recv_cmd_33_flag = ECO_FALSE;
        xj_cmd_34* cmd_34_param = (xj_cmd_34*)cmd;
        send_card_auth_34_protocol(cmd_34_param,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_33_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_33_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_33;
        }
        break;
    }
    case xj_cmd_type_36:
    {
        recv_protocol.recv_cmd_35_flag = ECO_FALSE;
        xj_cmd_36* cmd_36_param = (xj_cmd_36*)cmd;
        send_card_ask_start_36_protocol(cmd_36_param,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_35_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_35_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_35;
        }
        break;
    }
    case xj_cmd_type_38:
    {
    	break;
    }
    case xj_cmd_type_40:
    {
        recv_protocol.recv_cmd_41_flag = ECO_FALSE;
        xj_cmd_40* cmd_40_param = (xj_cmd_40*)cmd;
        send_vin_request_40_protocol(cmd_40_param,Qos);
        while((i < time_out*100) &&(recv_protocol.recv_cmd_41_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_41_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_41;
        }
        break;
    }
    case xj_cmd_type_80:
    {
    	recv_protocol.recv_cmd_81_flag = ECO_FALSE;
    	send_encry_data_80_protocol((xj_cmd_80 *)cmd,Qos);
    	while((i<time_out*100) &&(recv_protocol.recv_cmd_81_flag == ECO_FALSE))
    	{
    		xj_pal_msleep(10U);
    		i++;
    	}
    	if(recv_protocol.recv_cmd_81_flag == ECO_TRUE)
    	{
    		param_ret = &recv_protocol.cmd_81;
    	}
    	break;
    }
    case xj_cmd_type_102:
    {
        recv_protocol.recv_cmd_101_flag = ECO_FALSE;
        xj_cmd_102* cmd_102_param = (xj_cmd_102*)cmd;
        cmd_102_param->heart_index = ++mqttMessageBase.heart_index;
        send_heart_102_protocol(cmd_102_param,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_101_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_101_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_101;
        }
        break;
    }
    case xj_cmd_type_104:
    {
        recv_protocol.recv_cmd_103_flag = ECO_FALSE;
        send_gun_status_104_protocol((xj_cmd_104 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_103_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_103_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_103;
        }
        break;
    }
    case xj_cmd_type_106:
    {
        recv_protocol.recv_cmd_105_flag = ECO_FALSE;
        send_signin_106_protocol((xj_cmd_106 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_105_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_105_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_105;
        }
        break;
    }
    case xj_cmd_type_108:
    {
        recv_protocol.recv_cmd_107_flag = ECO_FALSE;
        send_event_108_protocol((xj_cmd_108 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_107_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_107_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_107;
        }
        break;
    }
    case xj_cmd_type_114:
    {
        recv_protocol.recv_cmd_113_flag = ECO_FALSE;
        send_ask_addr_114_protocol((xj_cmd_114 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_113_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_113_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_113;
        }
        break;
    }
    case xj_cmd_type_118:
    {
        recv_protocol.recv_cmd_117_flag = ECO_FALSE;
        send_fault_118_protocol((xj_cmd_118 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_117_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_117_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_117;
        }
        break;
    }
    case xj_cmd_type_120:
    {
        recv_protocol.recv_cmd_119_flag = ECO_FALSE;
        send_fault_120_protocol((xj_cmd_120 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_119_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_119_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_119;
        }
        break;
    }
    case xj_cmd_type_202:
    {
        recv_protocol.recv_cmd_201_flag = ECO_FALSE;
        send_bill_202_protocol((xj_cmd_202 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_201_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_201_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_201;
        }
        break;
    }
    case xj_cmd_type_206:
    {
        recv_protocol.recv_cmd_205_flag = ECO_FALSE;
        send_history_bill_206_protocol((xj_cmd_202 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_205_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_205_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_205;
        }
        break;
    }
    case xj_cmd_type_302:
    {
        recv_protocol.recv_cmd_301_flag = ECO_FALSE;
        send_bms_status_302_protocol((xj_cmd_302 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_301_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_301_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_301;
        }
        break;
    }
    case xj_cmd_type_304:
    {
        recv_protocol.recv_cmd_303_flag = ECO_FALSE;
        send_bms_status_304_protocol((xj_cmd_304 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_303_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_303_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_303;
        }
        break;
    }
    case xj_cmd_type_306:
    {
        recv_protocol.recv_cmd_305_flag = ECO_FALSE;
        send_bms_status_306_protocol((xj_cmd_306 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_305_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_305_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_305;
        }
        break;
    }
    case xj_cmd_type_308:
    {
        recv_protocol.recv_cmd_307_flag = ECO_FALSE;
        send_bms_308_protocol((xj_cmd_308 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_307_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_307_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_307;
        }
        break;
    }
    case xj_cmd_type_310:
    {
        recv_protocol.recv_cmd_309_flag = ECO_FALSE;
        send_bms_310_protocol((xj_cmd_310 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_309_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_309_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_309;
        }
        break;
    }
    case xj_cmd_type_312:
    {
        recv_protocol.recv_cmd_311_flag = ECO_FALSE;
        send_bms_312_protocol((xj_cmd_312 *)cmd,Qos);
        while((i<time_out*100) &&(recv_protocol.recv_cmd_311_flag == ECO_FALSE))
        {
            xj_pal_msleep(10U);
            i++;
        }
        if(recv_protocol.recv_cmd_311_flag == ECO_TRUE)
        {
            param_ret = &recv_protocol.cmd_311;
        }
        break;
    }
    default:
    {
        xj_pal_print_log(xj_log_remind,"xj_mqtt_send_cmd.cmd:%d illegal\n",cmd_type);
        break;
    }
    }
    MQTT_PAL_MUTEX_UNLOCK(&mqttMessageBase.sendlock);

    if(NULL == param_ret)
    {
        xj_pal_print_log(xj_log_warning,"xj code %d response timeout\n",cmd_type);
    }

    return param_ret;
}

void xj_mqtt_send_resp(xj_mqtt_cmd_enum cmd_type, void * cmd, uint8_t Qos)
{

    MQTT_PAL_MUTEX_LOCK(&mqttMessageBase.sendlock);
    switch (cmd_type)
    {
    case xj_cmd_type_2:
    {
        send_set_int_2_protocol((xj_cmd_2 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_4:
    {
        send_set_string_4_protocol((xj_cmd_4 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_6:
    {
        send_contorl_6_protocol((xj_cmd_6 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_8:
    {
        send_start_charge_8_protocol((xj_cmd_8 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_12:
    {
        send_stop_charge_12_protocol((xj_cmd_12 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_20:
    {
    	send_power_kwh_cntl_20_protocol((xj_cmd_20 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_24:
    {
        send_control_lock_24_protocol((xj_cmd_24 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_38:
    {
    	send_card_white_list_38_protocol((xj_cmd_38 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_42:
    {
    	send_vin_white_list_42_protocol((xj_cmd_42 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_44:
    {
    	send_clear_vin_list_44_protocol((xj_cmd_44 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_410:
    {
        send_ask_log_410_protocol((xj_cmd_410 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_502:
    {
        send_set_502_protocol((xj_cmd_502 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_504:
    {
        send_set_504_protocol((xj_cmd_504 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_506:
    {
    	send_set_506_protocol((xj_cmd_506 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_508:
    {
    	send_set_508_protocol((xj_cmd_508 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_512:
    {
    	send_set_512_protocol((xj_cmd_512 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_514:
    {
    	send_set_514_protocol((xj_cmd_514 *)cmd, Qos);
    	break;
    }
    case xj_cmd_type_1102:
    {
        send_soft_update_1102_protocol((xj_cmd_1102 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_1304:
    {
        send_set_electricity_fee_1304_protocol((xj_cmd_1304 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_1306:
    {
        send_set_serice_fee_1306_protocol((xj_cmd_1306 *)cmd, Qos);
        break;
    }
    case xj_cmd_type_1310:
    {
        send_set_fee_1310_protocol((xj_cmd_1310 *)cmd, Qos);
        break;
    }
    default:
    {
        xj_pal_print_log(xj_log_remind,"xj_mqtt_send_resp.cmd_type illegal,cmdtype=%d\n",cmd_type);
        break;
    }

    }
    MQTT_PAL_MUTEX_UNLOCK(&mqttMessageBase.sendlock);
}

int utf8_to_iso(uint8_t*in,int iLen,uint8_t *out)
{
    unsigned char *ptr_iso=out;
    unsigned char *ptr_utf8=in;
    while(ptr_utf8-in<iLen)
    {
        if(*ptr_utf8>=0x80)
        {
            if(*ptr_utf8==0xc2)
            {
                *(ptr_iso++)=*(ptr_utf8+1);
            }
            else if(*ptr_utf8 == 0xc3)
            {
                *(ptr_iso++)=*(ptr_utf8+1)+0x40;
            }
            ptr_utf8+=2;
        }
        else
        {
            *(ptr_iso++)=*(ptr_utf8++);
        }
    }
    return ptr_iso-out;
}

int8_t  get_check_sum(int8_t* datas,uint16_t len)
{
    int i=0;
    int8_t ret=0;
    int32_t sum=0;

    for(i=12; i<len; i++)
    {
        sum+=datas[i];
    }
    ret = sum % 127;
    return ret;
}
