#include "cev5500.h"
#include "log.h"
#include "uicommon.h"
#include <string>
#ifdef WIN32
#define getSecTimer (GetTickCount()/1000)
#else
#include "uicommon.h"
#include "Global_Varible.h"
#define getSecTimer g_Time2048msCounter
#endif
#define INIFILETIME "price_time.ini"
#define INIFILEPRICE "price_info.ini"
const int YXCOUNT_PER=6;//每口的遥信数量
const int YCCOUNT_PER=7;
const int YMCOUNT_PER=2;
extern void load_time();

BYTE gabyCRCHi[] =
{
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,
0x80,0x41,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,
0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x00,0xc1,
0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x00,0xc1,
0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,
0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,
0x81,0x40,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,
0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x01,0xc0,
0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,
0x80,0x41,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,
0x80,0x41,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x01,0xc0,0x80,0x41,0x00,0xc1,0x81,0x40,0x01,0xc0,
0x80,0x41,0x00,0xc1,0x81,0x40,0x00,0xc1,0x81,0x40,
0x01,0xc0,0x80,0x41,0x01,0xc0,0x80,0x41,0x00,0xc1,
0x81,0x40,0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,
0x00,0xc1,0x81,0x40,0x01,0xc0,0x80,0x41,0x01,0xc0,
0x80,0x41,0x00,0xc1,0x81,0x40

};
BYTE	gabyCRCLo[] =
{
0x00,0xc0,0xc1,0x01,0xc3,0x03,0x02,0xc2,0xc6,0x06,
0x07,0xc7,0x05,0xc5,0xc4,0x04,0xcc,0x0c,0x0d,0xcd,
0x0f,0xcf,0xce,0x0e,0x0a,0xca,0xcb,0x0b,0xc9,0x09,
0x08,0xc8,0xd8,0x18,0x19,0xd9,0x1b,0xdb,0xda,0x1a,
0x1e,0xde,0xdf,0x1f,0xdd,0x1d,0x1c,0xdc,0x14,0xd4,
0xd5,0x15,0xd7,0x17,0x16,0xd6,0xd2,0x12,0x13,0xd3,
0x11,0xd1,0xd0,0x10,0xf0,0x30,0x31,0xf1,0x33,0xf3,
0xf2,0x32,0x36,0xf6,0xf7,0x37,0xf5,0x35,0x34,0xf4,
0x3c,0xfc,0xfd,0x3d,0xff,0x3f,0x3e,0xfe,0xfa,0x3a,
0x3b,0xfb,0x39,0xf9,0xf8,0x38,0x28,0xe8,0xe9,0x29,
0xeb,0x2b,0x2a,0xea,0xee,0x2e,0x2f,0xef,0x2d,0xed,
0xec,0x2c,0xe4,0x24,0x25,0xe5,0x27,0xe7,0xe6,0x26,
0x22,0xe2,0xe3,0x23,0xe1,0x21,0x20,0xe0,0xa0,0x60,
0x61,0xa1,0x63,0xa3,0xa2,0x62,0x66,0xa6,0xa7,0x67,
0xa5,0x65,0x64,0xa4,0x6c,0xac,0xad,0x6d,0xaf,0x6f,
0x6e,0xae,0xaa,0x6a,0x6b,0xab,0x69,0xa9,0xa8,0x68,
0x78,0xb8,0xb9,0x79,0xbb,0x7b,0x7a,0xba,0xbe,0x7e,
0x7f,0xbf,0x7d,0xbd,0xbc,0x7c,0xb4,0x74,0x75,0xb5,
0x77,0xb7,0xb6,0x76,0x72,0xb2,0xb3,0x73,0xb1,0x71,
0x70,0xb0,0x50,0x90,0x91,0x51,0x93,0x53,0x52,0x92,
0x96,0x56,0x57,0x97,0x55,0x95,0x94,0x54,0x9c,0x5c,
0x5d,0x9d,0x5f,0x9f,0x9e,0x5e,0x5a,0x9a,0x9b,0x5b,
0x99,0x59,0x58,0x98,0x88,0x48,0x49,0x89,0x4b,0x8b,
0x8a,0x4a,0x4e,0x8e,0x8f,0x4f,0x8d,0x4d,0x4c,0x8c,
0x44,0x84,0x85,0x45,0x87,0x47,0x46,0x86,0x82,0x42,
0x43,0x83,0x41,0x81,0x80,0x40
};
WORD ModbusCRC(BYTE * pData, unsigned char len)
{
    BYTE byCRCHi = 0xff;
    BYTE byCRCLo = 0xff;
    BYTE byIdx;
    WORD crc;
    while(len--)
    {
        byIdx = byCRCHi ^* pData++;
        byCRCHi = byCRCLo ^ gabyCRCHi[byIdx];
        byCRCLo = gabyCRCLo[byIdx];
    }
    crc = byCRCHi;
    crc <<= 8;
    crc += byCRCLo;
    return crc;
}


unsigned short ProCon1(unsigned char *tmp,unsigned char *login_char,unsigned short size,unsigned char *time,unsigned char *sm4_data)
{
   login_char[0] = 0x68;
    login_char[1] = 0x01;//dc
    login_char[2] = 0x14;//version
    login_char[3] = 0x01;//bin
    login_char[4] = (tmp[1]+7)&0xff;//length
    login_char[5] = ((tmp[1]+7)>>8)&0xff;//length
    login_char[6] = tmp[2];//send_id
    login_char[7] = tmp[3];//send_id
    unsigned short j = time[0] * 1000 + time[1];
    login_char[8] = j&0xff;
    login_char[9] = (j>>8)&0xff;
    login_char[10] = time[2]&0x3f;
    login_char[11] = time[3]&0x1f;
    login_char[12] = time[4]&0x1f;
    login_char[13] = time[5]&0xf;
    login_char[14] = time[6]%100;
    login_char[15] = tmp[4];
    login_char[16] = tmp[5];
    if(login_char[15] == 1)
    {
        size = sm4_encry(sm4_data,&login_char[17],&tmp[6],size);
    }
    else
    {
        memcpy(&login_char[17],&tmp[6],size);
    }
    login_char[4] = (size+11)&0xff;//length
    login_char[5] = ((size+11)>>8)&0xff;//length
    unsigned short crc_result = ModbusCRC(&login_char[6],size+11);
    login_char[17+size] = crc_result&0xff;
    login_char[18+size] = (crc_result>>8)&0xff;
    return (19+size);
}

Cev5500::Cev5500()
:CProtBase(false), CTcpClient("cev5500")
{
	rxbuf=new MyBuffer(false);
	txbuf=new MyBuffer(false);
	sendNo=0;
	lastTxHeart=getSecTimer-100;
        tcp_status = 0;//0:初始状态    1:发送登录    2：收到应答   5：计费模型认证请求   6:收到模型应答    9:请求计费模型   a:收到计费模型      aa:登录完成
	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++){
                //INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+iPort;
                PILE_TOHOST_INFO* evse=g_bcu_info->pile_tohost_info+iPort;
                evseyx[iPort].tcharging=evse->status;
                //evseyx[iPort].tbespeaking=evse->bespeaking;
                //evseyx[iPort].trepair=evse->repair_flag;
                //evseyx[iPort].terror=evse->error;
                evseyx[iPort].tcable=evse->gun_status;
                //evseyx[iPort].temstop=evse->emstop;
                //evseyx[iPort].nerror=0;
                //evseyx[iPort].hasConfirmCR=true;
	}
        send_id = 0;
	setRxok(false);
        char s10[50];
        sprintf(s10,"device offline");
        Debug_info_file1(s10);
	lastRxtime=getSecTimer;
}

Cev5500::~Cev5500(void)
{
}

Cev5500* Cev5500::instance=NULL;
Cev5500* Cev5500::getInstance()
{
	if(instance==NULL)
	{
		instance=new Cev5500();
	}
	return instance;
}

unsigned short change17to20(unsigned char *tmp,unsigned char *login_char,unsigned short size)
{
    login_char[0] = 0x68;
    login_char[1] = 0x01;//dc
    login_char[2] = 0x14;//version
    login_char[3] = 0x01;//bin
    login_char[4] = byte0(tmp[1]+7);//length
    login_char[5] = byte1(tmp[1]+7);//length
    login_char[6] = tmp[2];//send_id
    login_char[7] = tmp[3];//send_id
    unsigned short j = m_sec * 1000 + m_msec;
    login_char[8] = j&0xff;
    login_char[9] = (j>>8)&0xff;
    login_char[10] = m_min&0x3f;
    login_char[11] = m_hour&0x1f;
    login_char[12] = m_date&0x1f;
    login_char[13] = m_month&0xf;
    login_char[14] = m_year%100;
    login_char[15] = tmp[4];
    login_char[16] = tmp[5];
    if(login_char[15] == 1)
    {
        size = sm4_encry(sm4_data,&login_char[17],&tmp[6],size);
    }
    else
    {
        memcpy(&login_char[17],&tmp[6],size);
    }
    login_char[4] = byte0(size+11);//length
    login_char[5] = byte1(size+11);//length
    unsigned short crc_result = ModbusCRC(&login_char[6],size+11);
    login_char[17+size] = crc_result&0xff;
    login_char[18+size] = (crc_result>>8)&0xff;
    return (19+size);
}

/*************************************************/

unsigned char connent_flag;
void Cev5500::run()
{

    unsigned char mw;
    for(mw = 0;mw<16;mw++)
    {
        sm4_data[mw] = rand()%255;
    }
	while(1)
	{
		try{
                        if(abs(getSecTimer-lastRxtime)>40)//20220101
			{
				if(getSecTimer < lastRxtime)
                                {
                                        if((getSecTimer + 0x10000 - lastRxtime) > 40)
                                        {
                                            setRxok(false);
                                            disconn();
                                            if(tcp_status != 0)
                                            {
                                                tcp_status = 0;
                                                char s10[50];
                                                sprintf(s10,"get heart out time device offline_1");
                                                Debug_info_file1(s10);
                                            }
                                            ldebug<<"recv out time";
                                        }
                                }else
                                {
                                    setRxok(false);
                                    disconn();
                                    if(tcp_status != 0)
                                    {
                                        tcp_status = 0;
                                        char s10[50];
                                        sprintf(s10,"get heart out time device offline");
                                        Debug_info_file1(s10);
                                    }
                                    ldebug<<"recv out time";
                                }
			}
			if(!isConnected())
			{
				Sleep(2000);
				conn();
				if(isConnected())
					{
                                                lastRxtime=getSecTimer - 13;//sy20211218确保一旦连接就发送心跳
					}
				connent_flag = NO;
				printf("NO connent_flag = %x",connent_flag);
			}
                        if(recv_sm == 2)
                        {
                            disconn();
                            recv_sm =0;
                        }
			if(isConnected())
			{
                            //ldebug<<"is connected";
                           // if(tcp_status == 0)
                          //      Send_Login();

				if(read()>0)
				{
                                        //setRxok(true);
					handleRead();
                                        lastRxtime=getSecTimer;
				}
				handleWrite();
				write();
			}
			Sleep(100);
		}
		catch(...)
		{
			disconn();
                        ldebug<<"try out catch";
			setRxok(false);
                        tcp_status = 0;
                        char s10[50];
                        sprintf(s10,"try catch device offline");
                        Debug_info_file1(s10);
		}
	}
}




void Cev5500::Send_Login()
{

    //ldebug<<"send login";
    unsigned char m;
    unsigned char login_char[500];
    login_char[0] = 0x68;
    login_char[1] = 0x01;//dc
    login_char[2] = 0x1;//version
    login_char[3] = 0x15;//bin

    login_char[4] = byte0(349);//length
    login_char[5] = byte1(349);//length

    login_char[6] = 0;//send_id
    login_char[7] = 0;//send_id


    unsigned short j = m_sec * 1000 + m_msec;
    login_char[8] = j&0xff;
    login_char[9] = (j>>8)&0xff;
    login_char[10] = m_min&0x3f;
    login_char[11] = m_hour&0x1f;
    login_char[12] = m_date&0x1f;
    login_char[13] = m_month&0xf;
    login_char[14] = m_year%100;


    login_char[15] = 0;//sm4
    login_char[16] = 1;//id

    memcpy(&login_char[17],sm2_data,152);
    printf("Base64 Encoded: %s\n", sm2_data);
    //memcpy(&login_char[17],sm2_data,164);
    memcpy(&login_char[169],sm2_data_2,140);
    printf("Base64 Encoded: %s\n", sm2_data_2);
    unsigned short mw = 152+140;

    for(m=0;m<14;)
    {
        login_char[17+m/2+mw] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }

    //login_char[26+mw] = 0;
    login_char[24+mw] = (g_bcu_info->set_info.three_phase_flag ==1)?1:2;

    login_char[25+mw] = 7;

    login_char[26+mw] = 0x56;
    login_char[27+mw] = 0x32;
    login_char[28+mw] = 0x2E;
    login_char[29+mw] = 0x30 + VerStat/1000;
    login_char[30+mw] = 0x30 + VerStat/100%10;
    login_char[31+mw] = 0x30 + VerStat/10%10;
    login_char[32+mw] = 0x30 + VerStat%10;

   // txbuf->putUInt(0x56342e31);
  //  txbuf->putUInt(0x2e353000);
    login_char[33+mw] = 0;
    //txbuf->putByte(1);
    for(m=0;m<10;m++)
        login_char[34+m+mw] = 0;
    login_char[44+mw] = 0;
    memset(&login_char[45+mw],0,8);
    login_char[53+mw] = 7;
    login_char[54+mw] = 4;
    memset(&login_char[55+mw],0,8);

    //ldebug<<"3";
    unsigned short crc_short = ModbusCRC(&login_char[6],349);
    //ldebug<<"4";
    login_char[mw + 63] = crc_short &0xff;
    login_char[mw + 64] = (crc_short>>8)&0xff;
    //ldebug<<"5";
    txbuf->putData(login_char,357);
    //ldebug<<"6";
    write();
    //ldebug<<"7";
}


void Cev5500::handleRead()
{
        uchar buf[2048];
        uchar fee_time[49];
        char fee_time_char[10];
        uchar m=0,n=100,x,f=0,g=0,j=0,p=0;
        unsigned short server_send_id;
        unsigned char dec_buf[2048];
        unsigned short dec_len;
        while(rxbuf->getLen()>=16)
	{
		rxbuf->markReadp();
                int len=rxbuf->getData(buf, 16);
                printf("recv len = %d\n",len);
		if(buf[0]!=0x68)
		{
			rxbuf->clearUnread();
                        ldebug << "hello wanggang";
			return;
		}
                int datalen=buf[4] + (buf[5]<<8);
                if(rxbuf->getLen() >= (datalen -8))//ok datagram
		{
                        //uint condomain=makeuint(buf+3);
			//uint dstaddr=makeuint(buf+7);
                        //int dstJzq=makeushort(buf+7);
			int dstPole=makeushort(buf+9);//桩地址从1开始
                        //int fee[8];
                        //uint srcaddr=makeuint(buf+11);
                        unsigned short bill_version;
                        server_send_id = makeushort(buf+6);
                        //if(datalen==12)//链路心跳测试
                        /*{
				switch(condomain){
				case 0x43:
					ldebug<<"rx heartbeat request";
					txTestConfirm();
					break;
				case 0x83:
					ldebug<<"rx heartbeat confirm";
					break;
				}				
			}
                        else if(datalen-12>=6)//最基本的ASDU,不含信息体*/
			{
                                len = rxbuf->getData(buf, datalen-8);
                                printf("recv len = %d    %d\n",len,datalen-8);
				int frameType=buf[0];
                                //printf("frameType=%02x %02x %02x %02x %02x %02x\n",buf[0],buf[1],buf[2],buf[3],buf[4],buf[5]);
                                //int infoCount=buf[1];
                                //int cot=makeushort(buf+2);
                                //int pubAddr=makeushort(buf+4);//公共地址
                                //int infoAddr=0;
                                //if(datalen-12>=9)
                                //	infoAddr=buf[6]|(buf[7]<<8)|(buf[8]);//信息体地址
                                switch(frameType){
                                        case 0x02:
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    print_bin("update",buf+1,dec_len);
                                    if(buf[8] == 0)
                                    {
                                        ldebug<<"login success!";
                                        tcp_status = 6;
                                        send_id = 0;
                                        //setRxok(true);
                                        
                                        g_bcu_info->pile_tohost_info[0].send_id = 1;
                                        char s10[50];
                                        sprintf(s10,"device online success");
                                        Debug_info_file1(s10);
                                        char sm2_tmp[200];
                                        memcpy(sm2_tmp,&buf[9],130);
                                        printf("recv sm2 = %s\n",sm2_tmp);
                                        write_profile_string("互动化平台", "公钥", sm2_tmp, "cui.ini");
                                        unsigned short i;
                                        char hex_byte[3];
                                        for (i = 0; i < 65; i++) {
                                            sprintf(hex_byte, "%02X", sm2_tmp[i]);
                                            sm2_pub[i] = (unsigned char)strtol(hex_byte, NULL, 16);
                                        }
                                    }else
                                    {
                                        ldebug<<"login error!!!";
                                    }
                                        break;
                                case 0x04:
                                    ldebug<<"rx heartbeat confirm";
                                    break;
                                case 0x06:
                                    ldebug<<"bill model version";
                                    tcp_status = 6;
                                    break;
                                case 0x70:
                                    ldebug<<"recv update vin";
                                    if(recv_host_vin.updating == 0)
                                    {
                                        printf("len = %d\n",datalen-11);

                                        dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                        if(dec_len == 0)
                                            break;
                                        memcpy(buf+1,dec_buf,dec_len);
                                        recv_host_vin.updating = 1;
                                        print_bin("update",buf+1,dec_len);
                                        recv_host_vin.recv_number = makeushort(buf[8],buf[9]);
                                        memcpy(recv_host_vin.host_update_vin,buf+10,23*recv_host_vin.recv_number);
                                        //recv_host_vin.recv_status =  buf[8];
                                        sqlite_preupdate(1,recv_host_vin.recv_status,recv_host_vin.recv_number,recv_host_vin.host_update_vin);
                                        recv_host_vin.updating = 0;
                                    }
                                    reply_vin(1);
                                    break;
                                case 0x72:
                                    ldebug<<"recv delete vin";
                                    if(recv_host_vin.updating == 0)
                                    {
                                        printf("len = %d\n",datalen-11);

                                        dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                        if(dec_len == 0)
                                            break;
                                        memcpy(buf+1,dec_buf,dec_len);
                                        recv_host_vin.updating = 1;
                                        print_bin("update",buf+1,dec_len);
                                        recv_host_vin.recv_number = makeushort(buf[8],buf[9]);
                                        memcpy(recv_host_vin.host_update_vin,buf+10,17*recv_host_vin.recv_number);
                                        recv_host_vin.recv_status =  buf[8];
                                        sqlite_preupdate(0,recv_host_vin.recv_status,recv_host_vin.recv_number,recv_host_vin.host_update_vin);
                                        recv_host_vin.updating = 0;
                                    }
                                    reply_vin(0);
                                    break;
                                case 0x54:
                                    ldebug<<"read ddb info";
                                    txDDBConfirm(&buf[1],server_send_id);
                                    break;
                                case 0x50:
                                    ldebug<<"recv error confirm";
                                    break;
                                case 0x96:
                                {
                                    ldebug<<"recv update sm2";
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    print_bin("update",buf+1,dec_len);
                                    char sm2_tmp[200];
                                    memcpy(sm2_tmp,&buf[9],buf[8]);
                                    printf("recv sm2 = %s\n",sm2_tmp);
                                    write_profile_string("互动化平台", "公钥", sm2_tmp, "cui.ini");
                                    unsigned short i;
                                    char hex_byte[3];
                                    for (i = 0; i < 65; i++) {
                                        sprintf(hex_byte, "%02X", sm2_tmp[i]);
                                        sm2_pub[i] = (unsigned char)strtol(hex_byte, NULL, 16);
                                    }
                                    reply(0);
                                    if(buf[9+buf[8]] == 1)
                                        disconn();
                                    else
                                        recv_sm = 1;
                                    break;
                                }
                                case 0x8:
                                    ldebug<<"recv other fee";
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    print_bin("other fee",buf+1,dec_len);
                                    tcp_status = 0xaa;
                                    setRxok(true);
                                    break;
                                case 0x94:
                                {
                                    ldebug<<"recv update!!!";
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    print_bin("update",buf+1,dec_len);

                                    UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=0;
                                    memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                    q->cmd_in_pos++;
                                    if(q->cmd_in_pos>=MAX_CMD_NUM)
                                            q->cmd_in_pos=0;

                                    reply_update(0);
                                }
                                    break;
                                case 0x4e:
                                    ldebug<<"Start complete reply";
                                    break;
                                case 0x0a:
                                case 0x58:
                                {
                                    ldebug<<"response bill model";
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    f=0,g=0,j=0,p=0;
                                    recv_bill_model = 1;
                                    tcp_status = 0xaa;

                                    //tcp_status = 7;
                                    check_send_flag = 1;
                                    bill_version = makeushort(buf+8+1);
                                    price_number_recv = buf[10+1];
                                    //for(m=0;m<8;m++)
                                    printf("bill_mode = %d\n",bill_version);
                                    for(m = 0;m<2*price_number_recv;)
                                    {
                                        price_recv[m/2] = makeuint(buf+11+m*4+1);
                                        price_service_recv[m/2] = makeuint(buf+15+m*4+1);
                                        m += 2;
                                    }
                                    //printf("m=%d\n",m);


                                    memcpy(&bill_model[0],&buf[12+m*4+1],48);
                                    for(m=0;m<48;m++){
                                        printf("%02X ",bill_model[m]);
                                    }
                                    printf("\n*****************************************\n");
                                    //load_time();
                                    if(frameType == 0x58)
                                        txPriceSet(buf[8]);
                                    else
                                        setRxok(true);
                                    txTime();
                                    break;
                                }
                                case 0x60:
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    if(buf[8] == 0){
                                        recv_de_p[0] = (buf[9] + (buf[10]<<8))/2;
                                        recv_de_p[1] = (buf[9] + (buf[10]<<8))/2;
                                        if((recv_de_p[0] != 0)&&(recv_de_p[0]<send_max_power))
                                        {
                                            memcpy(&des_time[0][0],&buf[12],6);
                                            memcpy(&dee_time[0][0],&buf[19],6);
                                            memcpy(&des_time[1][0],&buf[12],6);
                                            memcpy(&dee_time[1][0],&buf[19],6);
                                        }else{
                                            recv_de_p[0] = 0;
                                            recv_de_p[1] = 0;
                                        }
                                    }else if((buf[8] > 0)&&(buf[8] < 3))
                                    {
                                        recv_de_p[buf[8]] = (buf[9] + (buf[10]<<8))/2;
                                        if((recv_de_p[buf[8]] != 0)&&(recv_de_p[buf[8] < send_max_power]))
                                        {
                                            memcpy(&des_time[buf[8]][0],&buf[12],6);
                                            memcpy(&dee_time[buf[8]][0],&buf[19],6);
                                        }else
                                            recv_de_p[buf[8]] = 0;
                                    }
                                    txPowerSet(buf[8]);
                                    break;
                                case 0x5c:
                                    {
                                        short sport1,sport2;
                                        dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                        if(dec_len == 0)
                                            break;
                                        txSet(buf[8]);
                                        memcpy(buf+1,dec_buf,dec_len);
                                        sport1 = makeushort(&buf[buf[8]+29]);
                                        sport2 = makeushort(&buf[buf[8]+51]);
                                        if(buf[8] != 0)
                                        {
                                            if(memcmp(g_hmi_info->cevhost_ini.ip,&buf[9],buf[8]))
                                            {
                                                char s10[1024];
                                                char tmport[5];
                                                memset(s10,0,1024);
                                                memcpy(s10,&buf[9],buf[8]);
                                                {
                                                    sprintf(tmport,"%d",sport1);
                                                    write_profile_string("互动化平台", "主站IP", s10, "/usr/test/cui.ini");
                                                    read_profile_string("互动化平台", "主站IP",  g_hmi_info->cevhost_ini.ip, sizeof(g_hmi_info->cevhost_ini.ip), "180.96.17.121", "cui.ini");
                                                    write_profile_string("互动化平台", "主站端口", tmport, "/usr/test/cui.ini");
                                                    g_hmi_info->cevhost_ini.port = read_profile_int("互动化平台", "主站端口", 2409, "cui.ini");
                                                    getInstance()->initComm(s10,sport1);
                                                    disconn();
                                                }
                                             }
                                        }
                                    }

                                    break;
                                case 0x92:
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    txRebootSet(buf[8]);
                                    recv_reboot = 1;
                                    break;
                                case 0x12:
                                    ldebug<<"rx callall info";
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    printf("recall %d\n",buf[8]);
                                    txPileInfo((buf[8]-1),server_send_id);
                                    //txPileInfo(1);
                                    break;
                                case 0x4C:  //start
                                    {
                                        dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                        if(dec_len == 0)
                                            break;
                                        memcpy(buf+1,dec_buf,dec_len);
                                        char szFt[20];
                                        sprintf(szFt, "0x%02x", frameType);
                                        ldebug<<"zf to zk_comm : "<<szFt;
                                        request_id[buf[24]-1][77] = server_send_id;
                                        UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=buf[24]-1;
                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                    }
                                    break;
                                case 0xA6:
                                case 0xA8:  //start
                                    {
                                        dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                        if(dec_len == 0)
                                            break;
                                        memcpy(buf+1,dec_buf,dec_len);
                                        //memcpy(&g_bcu_info->pile_tohost_info[buf[24]-1].bill_number[0],&buf[1],24);
                                        memcpy(&order_numb[buf[24]-1][0],&buf[1],24);
                                        char szFt[20];
                                        sprintf(szFt, "0x%02x", frameType);
                                        ldebug<<"zf to zk_comm : "<<szFt;
                                        UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=buf[24]-1;
                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                    }
                                    break;
                                case 0xA2:  //start
                                    {
                                        dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                        if(dec_len == 0)
                                            break;
                                        memcpy(buf+1,dec_buf,dec_len);
                                        //memcpy(&g_bcu_info->pile_tohost_info[buf[24]-1].bill_number[0],&buf[1],24);
                                        memcpy(&order_numb[buf[24]-1][0],&buf[1],24);
                                        char szFt[20];
                                        sprintf(szFt, "0x%02x", frameType);
                                        ldebug<<"zf to zk_comm : "<<szFt;
                                        UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=buf[24]-1;
                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                    }
                                    break;
                                case 0x52:
                                {
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                    if(buf[8] == 0)
                                    {
                                        recv_all = 1;
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=0;
                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=1;
                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                    }else
                                    {
                                        recv_all = 0;
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=buf[8]-1;
                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                    }
                                }
                                break;
                                case 0x42:  //recv update yue
                                    {
                                        dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                        if(dec_len == 0)
                                            break;
                                        memcpy(buf+1,dec_buf,dec_len);
                                        //memcpy(&g_bcu_info->pile_tohost_info[buf[24]-1].bill_number[0],&buf[1],24);
                                        //memcpy(&order_numb[buf[24]-1][0],&buf[1],24);
                                        //char szFt[20];
                                        //sprintf(szFt, "0x%02x", frameType);
                                        //ldebug<<"zf to zk_comm : "<<szFt;
                                        UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=buf[8]-1;
                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-11);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-11;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                    }
                                    break;
                                case 0x36:    //stop
                                {
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    charge_stop_reason[buf[8]-1] = 0x701;
                                    stop_reason_ym[buf[8]-1] = 0xA0;
                                    //memcpy(&g_bcu_info->pile_tohost_info[buf[24]-1].pile_id[0],&buf[1],8);
                                    //memcpy(&g_bcu_info->pile_tohost_info[buf[24]-1].card_no_show[0],&buf[25],16);
                                    char szFt[20];
                                    sprintf(szFt, "0x%02x", frameType);
                                    ldebug<<"zf to zk_comm : "<<szFt;
                                    UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=buf[8]-1;
                                    memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-3);
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-3;
                                    q->cmd_in_pos++;
                                    if(q->cmd_in_pos>=MAX_CMD_NUM)
                                            q->cmd_in_pos=0;
                                }
                                    break;
                                case 0x40:    //charge record confirm
                                {
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    char szFt[20];
                                    sprintf(szFt, "0x%02x", frameType);
                                    ldebug<<"zf to zk_comm : "<<szFt;
                                    unsigned char bill_charge[16];
                                    memcpy(bill_charge,buf+1,16);
                                    for(m=0;m<16;m++)
                                    {
                                        printf(" %02X",bill_charge[m]);
                                        printf(" %02X",bill_info[0][m]);
                                        printf(" %02X",bill_info[1][m]);
                                        printf("\n");
                                    }
                                    UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                    for(m=0;m<2;m++)
                                    {
                                        if(memcmp(bill_info[m],bill_charge,16) == 0)
                                        {
                                            q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no = m;
                                            m = 9;
                                        }
                                    }
                                    if(m == 10)
                                    {

                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;

                                        memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-3);
                                        q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-3;
                                        q->cmd_in_pos++;
                                        if(q->cmd_in_pos>=MAX_CMD_NUM)
                                                q->cmd_in_pos=0;
                                    }
                                }
                                break;
                                case 0x03:
						break;
                                case 0xA4:
                                {
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len == 0)
                                        break;
                                    memcpy(buf+1,dec_buf,dec_len);
                                    //memcpy(&g_bcu_info->pile_tohost_info[buf[24]-1].bill_number[0],&buf[1],24);
                                    memcpy(&order_numb[buf[24]-1][0],&buf[1],24);
                                    char szFt[20];
                                    sprintf(szFt, "0x%02x", frameType);
                                    ldebug<<"zf to zk_comm : "<<szFt;
                                    UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_HOST_to_BCU);
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_type=frameType;
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_port_no=buf[24]-1;
                                    memcpy(q->uni_cmd_interface[q->cmd_in_pos].cmd_in_buf, buf+1, datalen-3);
                                    q->uni_cmd_interface[q->cmd_in_pos].cmd_len=datalen-3;
                                    q->cmd_in_pos++;
                                    if(q->cmd_in_pos>=MAX_CMD_NUM)
                                            q->cmd_in_pos=0;
                                }
                                break;
                                case 0x5A:
                                {
                                    unsigned char tmp1;
                                    for(tmp1 = 0;tmp1<16;tmp1++)
                                        printf("%02x\n",sm4_data[tmp1]);
                                    dec_len = sm4_decry(sm4_data,dec_buf,buf+1,datalen-11);
                                    if(dec_len != 0)
                                    {
                                        printf("len = %d\n",dec_len);
                                        memcpy(buf+1,dec_buf,dec_len);
                                        printf("1\n");
                                        if(buf[8] == 1)
                                        {
                                            printf("2\n");

                                            char s10[1024];
                                            memset(s10,0,1024);
                                            memcpy(s10,&buf[12],makeushort(buf[10],buf[11]));
                                            write_profile_string("互动化平台", "桩序列号一", s10, "/usr/test/cui.ini");
                                            read_profile_string("互动化平台", "桩序列号一",  g_hmi_info->bcu_info_to_hmi[0].pole_ini.poleId, sizeof(g_hmi_info->bcu_info_to_hmi[0].pole_ini.poleId), NULL, "/usr/test/cui.ini");
                                            printf("poleId1 %s .........\n",g_hmi_info->bcu_info_to_hmi[0].pole_ini.poleId);
                                        }else
                                        {
                                            printf("3\n");

                                            char s10[1024];
                                            memset(s10,0,1024);
                                            memcpy(s10,&buf[12],makeushort(buf[10],buf[11]));
                                            write_profile_string("互动化平台", "桩序列号二", s10, "/usr/test/cui.ini");
                                            read_profile_string("互动化平台", "桩序列号二",  g_hmi_info->bcu_info_to_hmi[1].pole_ini.poleId, sizeof(g_hmi_info->bcu_info_to_hmi[1].pole_ini.poleId), NULL, "/usr/test/cui.ini");
                                            printf("poleId2 %s .........\n",g_hmi_info->bcu_info_to_hmi[1].pole_ini.poleId);
                                        }
                                        printf("recv host QR code information\n");
                                        txtwocode((buf[8]));
                                    }
                                }
                                    break;
#ifndef WIN32
#else
					case 0x41://充电启停
						if(datalen!=0x38)
						{
							ldebug<<"充电启停报文信息体长度不对";
							txChargeConfirm(dstPole-1, false, false);
							break;
						}
						else
						{
							int ykno=buf[9];//遥控点号：充电口
							int cartype=buf[10]>>2;//遥控性质:data2~7车辆类型，
							int onoff=(buf[10]&0x01)==0x01;//遥控性质:data0遥控启停位
							int coninfo=buf[11];//控制信息
							int startCondition=coninfo>>7;//启动条件，0:即时，1定时
							int conStyle=(coninfo>>6)&0x1;//控制方式：0BMS控，1充电机控
							int stopCondition=coninfo&0x3f;//1电量停止；2时间停止，3金额停止，4充满为止
							char* stopCondesc[5]={"", "充一定电量", "充一定时间", "充一定金额", "充满为止"};
							char* stopDataDesc[5]={"", "kWh", "分钟", "元", ""};
							
							int startMin=buf[12]%60;
							int startHour=buf[13]%24;
							int startDay=buf[14]%32;
							int startMonth=buf[15]%13;

							float maxU=0, maxI=0, stopData=0;
							memcpy(&maxU, buf+16, 4);
							memcpy(&maxI, buf+20, 4);
							memcpy(&stopData, buf+24, 4);
							char cardno[20];
							memcpy(cardno, buf+28, 16);
							cardno[16]=0;

							if(onoff)
								ldebug<<"收到充电启动报文"<<stopCondesc[stopCondition]<<", 条件"<<stopData<<stopDataDesc[stopCondition]<<", 卡号:"<<cardno;
							else
								ldebug<<"收到充电停止报文";

							txChargeConfirm(dstPole-1, onoff, true);
							write();
							txAllYc();
							txAllYx();
							txAllYm();
						}
						break;
					case 0x42://充电记录确认
						if(cot==4)
							evseyx[dstPole-1].hasConfirmCR=true;
						break;
					case 0x93://预约
						if(datalen-12!=9+17)
						{
							txBespeakConfirm(dstPole-1, true, false);
						}
						else
						{
							bool besOrCancel=buf[9]==0x02;//预约设置
							char cardno[17];
							memcpy(cardno, buf+10, 16);
							cardno[16]=0;
							ldebug<<"卡号:"<<cardno<<(besOrCancel?"预约":"取消预约");

							txBespeakConfirm(dstPole-1, besOrCancel, true);
							write();
							txAllYx();
						}
						break;
					case 0x94://刷卡请求结果 
						{
							int ret=buf[9];
							int szlen=buf[10];
							char sz[256];
							memcpy(sz, buf+11, szlen);
							sz[szlen]=0;
							ldebug<<"刷卡请求结果："<<(ret?"成功":"失败")<<", 原因："<<sz;
						}
						break;
#endif
                                        case 0xc:
                                        case 0x56://对时
						{
							CFETime ct;
                                                        int mss=makeushort(buf+8);
							ct.ms=mss%1000;
							ct.sec=mss/1000;
                                                        ct.minute=buf[10];
                                                        ct.hour=buf[11];
                                                        ct.day=buf[12];
                                                        ct.month=buf[13];
                                                        ct.year=buf[14]+2000;
							ldebug<<"rx synctime："<<ct.year<<"-"<<ct.month<<"-"<<ct.day<<" "<<ct.hour<<":"<<ct.minute<<":"<<ct.sec<<":"<<ct.ms;
							setSystime(ct);
                                                        if(frameType == 0x56)
                                                            txTimingConfirm(&buf[1],server_send_id);
                                                        system("hwclock -w");
						}
						break;
				}
			}
		}else{//报文不够长度，回退
                    printf("%d %d\n",rxbuf->getLen(),(datalen - 8));
                        printf("recv data error\n");
			rxbuf->resetReadp();
			return;
		}			
	}
}
unsigned char sy_test_timer;

void Cev5500::handleWrite()
{
        //ldebug<<"handlewrite";
	if(abs(getSecTimer-lastTxHeart)>10)
	{
            if(tcp_status == 0)
            {
                sm2_login(sm2_pub,sm4_data,sm2_data);
                sm2_key(sm2_pub,g_hmi_info->cevhost_ini.jzq_my,sm2_data_2);
                char s10[50];

                sprintf(s10,"LoGin_%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",sm4_data[0],sm4_data[1],sm4_data[2],sm4_data[3],sm4_data[4],sm4_data[5]
                        ,sm4_data[6],sm4_data[7],sm4_data[8],sm4_data[9],sm4_data[10],sm4_data[11],sm4_data[12],sm4_data[13],sm4_data[14],sm4_data[15]);
                Debug_info_file1(s10);
                Send_Login();
            }
            else if(tcp_status == 9)
                txBillMod();
            else if(tcp_status == 5)
                txBillVer();
            else if(tcp_status == 8)
                txOtherFee();
            else
            {
                txTestActive(1);
                //txPileInfo_1(0);
                //txTestActive(2);
            }
		lastTxHeart=getSecTimer;
	}
	else
	{
		sy_test_timer++;
		if(sy_test_timer % 50 == 0)//5s打印一次
		{
			ldebug<<"lastTxHeart "<<lastTxHeart;
			ldebug<<"getSecTimer "<<getSecTimer;
		}
	}
        switch(tcp_status)
        {
            case 2:txBillVer();tcp_status = 5;
            break;
            case 6:txBillMod();tcp_status = 9;
            break;
            case 7:txOtherFee();tcp_status = 8;
            break;
            default:break;
        }



        if(send_bill_flag)
        {
            send_bill_flag = 0;
            txBillMod();
        }
        if(tcp_status != 0xaa)
            return;
	UNI_CMD_QUEUE* q=&(g_cmd_info->cmd_BCU_to_HOST);
	if(q->cmd_in_pos!=q->cmd_out_pos)
	{
		UNI_CMD_INTERFACE* intf=q->uni_cmd_interface+q->cmd_out_pos;
		txBcuDatagram(intf->cmd_port_no, intf->cmd_type, intf->cmd_in_buf, intf->cmd_len);

		q->cmd_out_pos++;
		if(q->cmd_out_pos>=MAX_CMD_NUM)
			q->cmd_out_pos=0;
	}

	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++){
                //INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+iPort;
                PILE_TOHOST_INFO* evse = g_bcu_info->pile_tohost_info + iPort;
                bool send_flag = false;
                if(evseyx[iPort].tcharging!=evse->status)
		{
                        //txPileInfo_1(iPort);
                        send_flag = true;
                        evseyx[iPort].tcharging=evse->status;
                        //txChangeYx(iPort, 0, evseyx[iPort].tcharging);
		}
                /*
		if(evseyx[iPort].terror!=evse->error)
		{
			evseyx[iPort].terror=evse->error;
			txChangeYx(iPort, 1, evseyx[iPort].terror);
		}
		if(evseyx[iPort].tbespeaking!=evse->bespeaking)
		{
			evseyx[iPort].tbespeaking=evse->bespeaking;
			txChangeYx(iPort, 2, evseyx[iPort].tbespeaking);
		}
		if(evseyx[iPort].trepair!=evse->repair_flag)
		{
			evseyx[iPort].trepair=evse->repair_flag;
			txChangeYx(iPort, 2, evseyx[iPort].trepair);
                }	*/
                if(evseyx[iPort].tcable!=evse->gun_status)
		{
                        //txPileInfo_1(iPort);
                        send_flag = true;
                        evseyx[iPort].tcable=evse->gun_status;
                        //txChangeYx(iPort, 3, evseyx[iPort].tcable);
		}
                if(send_flag)
                {
                    txPileInfo_1(iPort);
                    printf("****send_0x13\n");
                    send_host_13_time[iPort]	= g_Time2048msCounter;
                }
                /*
		//4:通讯：永远为true
		if(connent_flag == NO)
		{
			connent_flag = YES;
			printf("YES connent_flag = %x",connent_flag);
			txChangeYx(iPort, 4, 1);
		}
		if(evseyx[iPort].temstop!=evse->emstop)
		{
			evseyx[iPort].temstop=evse->emstop;
			txChangeYx(iPort, 5, evseyx[iPort].temstop);
                }*/
                /*
		if(evseyx[iPort].terror)
		{
			checkErrorYc();
			//printf("erryc %x %x %x..........",evseyx[iPort].terror,evseyx[iPort].nerror,iPort);
		
		}
		else
		{
			evseyx[iPort].nerror = 0 ;
			//printf("erryc1 %x  %x  %x..........",evseyx[iPort].terror,evseyx[iPort].nerror,iPort);

                }*/
        }
/*	if((!hasConfirmCR)&&(getSecTimer-lastTxCR>10))
	{
		txChargeRecord();
	}
	int cc=getSecTimer;
	int co=getSecTimer-lastTxCardRequest;
	if(0)
	{
		txCardRequest("3050120151008003");
		lastTxCardRequest=getSecTimer+9999;
	}*/
}

void Cev5500::txAllYx()
{
	ldebug<<"tx AllYx";
	MyBuffer mb(false, 1024);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(0xffff);
	
	mb.putUByte(0x05);//帧类型	
	uchar inf=(1<<7)|(YXCOUNT_PER*MAX_DC_CHARGER_NUM);
	mb.putUByte(inf);
	mb.putUShort(0x05);
	mb.putUShort(0);
	mb.put3Bytes(0);

	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++){
		//充电中、故障、预约、电缆状态、桩与集中器通讯状态、急停按钮
		INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+iPort;
#ifdef WIN32
		int yxvalue[YXCOUNT_PER]={evse->charging, 0, evse->bespeaking, 1, 1, 0};//demo
#else
		int yxvalue[YXCOUNT_PER]={
			evse->charging,
			evse->error,
			evse->repair_flag,
			evse->cable,
			1,/*通讯状态*/
			evse->emstop};
#endif
			for(int i=0; i<YXCOUNT_PER; i++)
				mb.putUByte(yxvalue[i]);
	}

	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
	write();
}
void Cev5500::txAllYc()
{
	ldebug<<"tx AllYc";
	MyBuffer mb(false, 1024);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(0xffff);

	{
		mb.putUByte(0x5b);//帧类型
	}
	uchar inf=(1<<7)|(YCCOUNT_PER*MAX_DC_CHARGER_NUM);
	mb.putUByte(inf);
	mb.putUShort(0x05);
	mb.putUShort(0);//公共地址???
	mb.put3Bytes(0);//信息体首地址
	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++){
		INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+iPort;
		//电压、电流、时、分、功率、soc、故障代码
#ifdef WIN32
		float ycvalue[YCCOUNT_PER]={220, 12, 3, 16, 2.2, 82, 1};//demo
#else
		float ycvalue[YCCOUNT_PER]={
			evse->charged_voltage*0.1f,
			evse->charged_current*0.1f,
			(evse->charged_time/60)*1.0f,
			(evse->charged_time%60)*1.0f,
			(evse->charged_voltage*0.1f)*(evse->charged_current*0.1f)*0.001f,
			evse->charged_soc*1.0f,
			evseyx[iPort].nerror*1.0f};
#endif
		int yccoef[YCCOUNT_PER]={80, 160, 160, 160, 100, 100, 1};
		for(int i=0; i<YCCOUNT_PER; i++)
		{
			int val=ycvalue[i]*yccoef[i];
			//mb.putUShort(val);
			//mb.putUByte(0);
			mb.putUByte(byte0(val));
			mb.putUByte(byte1(val));
			mb.putUByte(byte2(val));	
			mb.putUByte(byte3(val));
		}
	}
	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
	write();
}

void Cev5500::txAllYm()
{
	ldebug<<"tx AllYm";
	MyBuffer mb(false, 1024);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(0xffff);

	mb.putUByte(0x10);//帧类型
	uchar inf=(1<<7)|(YMCOUNT_PER*MAX_DC_CHARGER_NUM);
	mb.putUByte(inf);
	mb.putUShort(0x05);//cot
	mb.putUShort(0);
	mb.put3Bytes(0);
	
	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++){
		INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+iPort;
		//电量、金额
#ifdef WIN32
		float ymvalue[YMCOUNT_PER]={12.3, 13.29};//demo
#else
		float ymvalue[YMCOUNT_PER]={
			evse->charged_power*0.01f,
			evse->charged_money*0.01f};
#endif
		int ymcoef[YMCOUNT_PER]={100, 100};
		for(int i=0; i<YMCOUNT_PER; i++)
		{
			uint val=ymvalue[i]*ymcoef[i];
			mb.putUInt(val);
			mb.putUByte(0);
		}
	}
	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
	write();
}

void Cev5500::txAllAllerr()
{
	unsigned char i,j,k;
	MyBuffer mb(false, 1024);

	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++)
	{
		INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+iPort;

		ldebug<<"tx AllAllerr ";

		mb.putUInt(0);//控制域
		
		mb.putUInt(0);//dst addr

		mb.putUShort(jzqAddr);
		mb.putUByte(iPort+1);//
		mb.putUByte(0x00);//
		
		mb.putUByte(0x12);//帧类型
		if(evse->charge_err_type != 0)
		{
			mb.putUByte(0x01);//个数
		
			mb.putUByte(0x05);//传送原因
			mb.putUByte(0x00);//
		
			mb.putUShort(0);
			if(evse->charger_aux_errno != 0)
			{
				mb.putUShort(evse->charger_aux_errno);
			}
			else
			{
				mb.putUShort(evse->charge_err_type);
			}
			mb.putUByte(00);
			mb.putUByte(1);
		}
		else
		{
			mb.putUByte(0x00);//个数
			
			mb.putUByte(0x05);
			mb.putUByte(0x00);//
			
			mb.putUShort(0);

			mb.putUShort(0);
			mb.putUByte(00);
			mb.putUByte(0);
		}	

		txbuf->putUByte(0x68);
		txbuf->putUShort(mb.getLen());	
		txbuf->putMyBuffer(&mb);
		write();
	}
}

void Cev5500::txerrinfo(int iPort, int err)
{
	unsigned char i,j,k;
	ldebug<<"tx errinfo ";
	MyBuffer mb(false, 1024);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	//mb.putUShort(0xffff);

	if(iPort == 1)
	{
		mb.putUByte(0x02);//
		mb.putUByte(0x00);//
	}
	else
	{
		mb.putUByte(0x01);//
		mb.putUByte(0x00);//
	}

	mb.putUByte(0x12);
	//uchar inf=(1<<7)|(YCCOUNT_PER*MAX_DC_CHARGER_NUM);
	//mb.putUByte(inf);
	mb.putUByte(0x01);
	mb.putUByte(0x01);
	mb.putUByte(0x00);
	mb.putUShort(0);
	
	mb.putUShort(err);
	//mb.putUByte(0x65);
	//mb.putUByte(00);
	mb.putUByte(00);
	
	mb.putUByte(1);

	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
	write();
}



void Cev5500::txStartActive()
{
	txbuf->putUByte(0x68);
	txbuf->putUShort(12);
	txbuf->putUInt(0x07);
	txbuf->putUInt(0);
	txbuf->putUInt(0);
}

void Cev5500::txTestActive(unsigned char pile_id)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 18;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 0;//sm4
    crc_buf[16] = 3;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    //if(g_bcu_info->set_info.three_phase_flag ==1)
    {
    //    crc_buf[24] = pile_id;
    //    crc_buf[25] = 0;
        //crc_buf[26] = 0;
        //m = 0;
    }
    //unsigned short size = test_sm4(sm4_data,&crc_buf[17],&crc_buf[17],8);
    //crc_buf[4] = 9 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[24] = crc_result&0xff;
    crc_buf[25] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;

}

void Cev5500::txTime()
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 18;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 0;//sm4
    crc_buf[16] = 0xb;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[24] = crc_result&0xff;
    crc_buf[25] = (crc_result>>8)&0xff;
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}


void Cev5500::txTimingConfirm(unsigned char *set_time,unsigned short send_id)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 25;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = send_id&0xff;;
    crc_buf[7] = (send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 0;//sm4
    crc_buf[16] = 0x55;//id
    memcpy(&crc_buf[17],set_time,14);
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[31] = crc_result&0xff;
    crc_buf[32] = (crc_result>>8)&0xff;
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    //g_bcu_info->pile_tohost_info[0].send_id++;
}

void Cev5500::txDDBConfirm(unsigned char *set_time,unsigned short send_id)
{
    //unsigned char m;
    unsigned char crc_buf[300];
    //unsigned char tmp[500];
    unsigned short size;
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 48;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = send_id&0xff;;
    crc_buf[7] = (send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x53;//id
    memcpy(&crc_buf[17],set_time,8);
    crc_buf[25] = (dc_total[set_time[7]-1])%0x100;
    crc_buf[26] = (dc_total[set_time[7]-1]>>8)%0x100;
    crc_buf[27] = (dc_total[set_time[7]-1]>>16)%0x100;
    crc_buf[28] = (dc_total[set_time[7]-1]>>24)%0x100;
    crc_buf[29] = (dc_total[set_time[7]-1]>>32)%0x100;
    crc_buf[30] = 0;
    crc_buf[31] = 0;
    crc_buf[32] = 0;
    memset(&crc_buf[33],0,14);
    memcpy(&crc_buf[47],&crc_buf[8],7);
    size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],37);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    //g_bcu_info->pile_tohost_info[0].send_id++;
}

void Cev5500::txPileInfo_1(unsigned char x)
{
    unsigned char m;
    unsigned char crc_buf[100];
    unsigned char tmp[500];
        crc_buf[0] = 0x68;
        crc_buf[1] = 86;
        crc_buf[2] = g_bcu_info->pile_tohost_info[0].send_id&0xff;
        crc_buf[3] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
        //txbuf->putUShort(send_id);
        crc_buf[4] = 1;
        //txbuf->putUByte(0);
        crc_buf[5] = 0x13;
        //txbuf->putByte(3);
        for(m=0;m<16;m++)
            crc_buf[6+m] = g_bcu_info->pile_tohost_info[x].bill_number[m];//trade_number[m];
        for(m=0;m<14;)
        {
            crc_buf[22+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
            m += 2;
        }
        crc_buf[29] = x+1;
        //txbuf->putByte(1);
        crc_buf[30] = g_bcu_info->pile_tohost_info[x].status;
        crc_buf[31] = 2;
        crc_buf[32] = g_bcu_info->pile_tohost_info[x].gun_status;
        //crc_buf[33] = g_bcu_info->pile_tohost_info[x].out_vol & 0xff;
        //crc_buf[34] = (g_bcu_info->pile_tohost_info[x].out_vol>>8) & 0xff;
        mempcpy(&crc_buf[33],&g_bcu_info->pile_tohost_info[x].out_vol,15);
        mempcpy(&crc_buf[48],&g_bcu_info->pile_tohost_info[x].charge_time,4);
        mempcpy(&crc_buf[52],&g_bcu_info->pile_tohost_info[x].charge_ele,13);
        crc_buf[65] = (dc_total[x])%0x100;
        crc_buf[66] = (dc_total[x]>>8)%0x100;
        crc_buf[67] = (dc_total[x]>>16)%0x100;
        crc_buf[68] = (dc_total[x]>>24)%0x100;
        crc_buf[69] = (dc_total[x]>>32)%0x100;
        crc_buf[70] = 0;
        crc_buf[71] = 0;
        crc_buf[72] = 0;
        memset(&crc_buf[73],0,10);
        crc_buf[83] = 0;
        crc_buf[84] = 0;
        crc_buf[85] = 0;
        crc_buf[86] = 0;
        crc_buf[87] = 0;
        unsigned char tmptime[7];
        tmptime[0] = m_sec;
        tmptime[1] = m_msec;
        tmptime[2] = m_min;
        tmptime[3] = m_hour;
        tmptime[4] = m_date;
        tmptime[5] = m_month;
        tmptime[6] = m_year;
        unsigned short cmdlen = ProCon1(crc_buf,tmp,crc_buf[1]-4,tmptime,sm4_data);
        txbuf->putData(tmp,cmdlen);
        write();
        g_bcu_info->pile_tohost_info[0].send_id++;
}
void Cev5500::txPileInfo(unsigned char x,unsigned short send_id)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x13;//id
    for(m=0;m<16;m++)
        crc_buf[17+m] = trade_number[m];
    for(m=0;m<14;m+2)
    {
        crc_buf[33+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[40] = x+1;
    //txbuf->putByte(1);
    crc_buf[41] = g_bcu_info->pile_tohost_info[x].status;
    crc_buf[42] = 0;
    crc_buf[43] = g_bcu_info->pile_tohost_info[x].gun_status;
    //crc_buf[33] = g_bcu_info->pile_tohost_info[x].out_vol & 0xff;
    //crc_buf[34] = (g_bcu_info->pile_tohost_info[x].out_vol>>8) & 0xff;
    mempcpy(&crc_buf[44],&g_bcu_info->pile_tohost_info[x].out_vol,15);
    mempcpy(&crc_buf[59],&g_bcu_info->pile_tohost_info[x].charge_time,4);
    mempcpy(&crc_buf[63],&g_bcu_info->pile_tohost_info[x].charge_ele,14);
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],60);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
/*
    unsigned char m;
    unsigned char crc_buf[100];
        crc_buf[0] = 0x68;
        crc_buf[1] = 0x40;
        crc_buf[2] = send_id&0xff;
        crc_buf[3] = (send_id>>8)&0xff;
        //txbuf->putUShort(send_id);
        crc_buf[4] = 0;
        //txbuf->putUByte(0);
        crc_buf[5] = 0x13;
        //txbuf->putByte(3);
        for(m=0;m<16;m++)
            crc_buf[6+m] = trade_number[m];
        for(m=0;m<14;)
        {
            crc_buf[22+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
            m += 2;
        }
        crc_buf[29] = x+1;
        //txbuf->putByte(1);
        crc_buf[30] = g_bcu_info->pile_tohost_info[x].status;
        crc_buf[31] = 0;
        crc_buf[32] = g_bcu_info->pile_tohost_info[x].gun_status;
        //crc_buf[33] = g_bcu_info->pile_tohost_info[x].out_vol & 0xff;
        //crc_buf[34] = (g_bcu_info->pile_tohost_info[x].out_vol>>8) & 0xff;
        mempcpy(&crc_buf[33],&g_bcu_info->pile_tohost_info[x].out_vol,15);
        mempcpy(&crc_buf[48],&g_bcu_info->pile_tohost_info[x].charge_time,4);
        mempcpy(&crc_buf[52],&g_bcu_info->pile_tohost_info[x].charge_ele,14);
        //txbuf->putByte(0);

        unsigned short crc_result = ModbusCRC(&crc_buf[2],crc_buf[1]);
        crc_buf[66] = crc_result&0xff;
        crc_buf[67] = (crc_result>>8)&0xff;
        //txbuf->putUShort(jzqAddr);
        //txbuf->putUShort(0xffff);
        txbuf->putData(crc_buf,crc_buf[1]+4);
        write();
        //send_id++;*/
}
void Cev5500::txBillVer()
{

    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 5;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    if(g_bcu_info->set_info.three_phase_flag ==1)
    {
        crc_buf[24] = 1;
        crc_buf[25] = 0;
        crc_buf[26] = 0;
        m = 0;
    }else
    {
        crc_buf[24] = 1;
        crc_buf[25] = 0;
        crc_buf[26] = 0;
        crc_buf[27] = 2;
        crc_buf[28] = 0;
        crc_buf[29] = 0;
        m = 3;
    }
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],10+m);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
/*
    ldebug<<"txbillver";
    unsigned char m;
    unsigned char crc_buf[100];
        crc_buf[0] = 0x68;
        crc_buf[1] = 0x0d;
        crc_buf[2] = g_bcu_info->pile_tohost_info[0].send_id&0xff;
        crc_buf[3] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
        //txbuf->putUShort(send_id);
        crc_buf[4] = 0;
        //txbuf->putUByte(0);
        crc_buf[5] = 5;
        //txbuf->putByte(3);
        for(m=0;m<14;)
        {
            crc_buf[6+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
            m += 2;
        }
        crc_buf[13] = 0;
        //txbuf->putByte(1);
        crc_buf[14] = 0;
        //txbuf->putByte(0);

        unsigned short crc_result = ModbusCRC(&crc_buf[2],crc_buf[1]);
        crc_buf[15] = crc_result&0xff;
        crc_buf[16] = (crc_result>>8)&0xff;
        //txbuf->putUShort(jzqAddr);
        //txbuf->putUShort(0xffff);
        txbuf->putData(crc_buf,crc_buf[1]+4);
        write();
        g_bcu_info->pile_tohost_info[0].send_id++;*/
}

void Cev5500::txOtherFee()
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 7;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    //if(g_bcu_info->set_info.three_phase_flag ==1)
    {
        crc_buf[24] = 1;
        //crc_buf[25] = 0;
        //crc_buf[26] = 0;
        //m = 0;
    }
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],8);
    crc_buf[4] = 9 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}


void Cev5500::txtwocode(unsigned char pilenumber)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227+1;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x5b;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = pilenumber;
    crc_buf[25] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],9);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}

void Cev5500::txPowerSet(unsigned char pilenumber)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227+1;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x59;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = pilenumber;
    crc_buf[25] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],9);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}


void Cev5500::txRebootSet(unsigned char pilenumber)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227+1;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x91;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],8);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}
void Cev5500::txSet(unsigned char pilenumber)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227+1;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x5d;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],8);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}

void Cev5500::txPriceSet(unsigned char pilenumber)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227+1;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x57;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = pilenumber;
    crc_buf[25] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],9);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}
void Cev5500::txRebootConfirm(unsigned short send_id)
{
    ldebug<<"txbillver";
    unsigned char m;
    unsigned char crc_buf[100];
        crc_buf[0] = 0x68;
        crc_buf[1] = 0x0d;
        crc_buf[2] = send_id&0xff;
        crc_buf[3] = (send_id>>8)&0xff;
        crc_buf[4] = 0;
        crc_buf[5] = 5;
        for(m=0;m<14;)
        {
            crc_buf[6+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
            m += 2;
        }
        crc_buf[13] = 0;
        crc_buf[14] = 0;

        unsigned short crc_result = ModbusCRC(&crc_buf[2],crc_buf[1]);
        crc_buf[15] = crc_result&0xff;
        crc_buf[16] = (crc_result>>8)&0xff;
        //txbuf->putUShort(jzqAddr);
        //txbuf->putUShort(0xffff);
        txbuf->putData(crc_buf,crc_buf[1]+4);
        write();
        g_bcu_info->pile_tohost_info[0].send_id++;
}
void Cev5500::txBillMod()
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x0d;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],8);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}

void Cev5500::reply(unsigned char w)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x95;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],8);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}


void Cev5500::reply_update(unsigned short w)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    crc_buf[16] = 0x93;//id
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = 0;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],8);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}


void Cev5500::reply_vin(unsigned char w)
{
    unsigned char m;
    unsigned char crc_buf[300];
    crc_buf[0] = 0x68;
    crc_buf[1] = 0x01;//dc
    crc_buf[2] = 0x14;//version
    crc_buf[3] = 0x01;//bin
    crc_buf[4] = 227;//length
    crc_buf[5] = 0;//length
    crc_buf[6] = g_bcu_info->pile_tohost_info[0].send_id&0xff;;
    crc_buf[7] = (g_bcu_info->pile_tohost_info[0].send_id>>8)&0xff;
    unsigned short j = m_sec * 1000 + m_msec;
    crc_buf[8] = j&0xff;
    crc_buf[9] = (j>>8)&0xff;
    crc_buf[10] = m_min&0x3f;
    crc_buf[11] = m_hour&0x1f;
    crc_buf[12] = m_date&0x1f;
    crc_buf[13] = m_month&0xf;
    crc_buf[14] = m_year%100;
    crc_buf[15] = 1;//sm4
    if(w)
        crc_buf[16] = 0x71;//id
    else
        crc_buf[16] = 0x73;
    for(m=0;m<14;m+2)
    {
        crc_buf[17+m/2] = (((g_hmi_info->cevhost_ini.jzq_id[m]-0x30)<<4)+(g_hmi_info->cevhost_ini.jzq_id[m+1]-0x30));
        m += 2;
    }
    crc_buf[24] = 1;
    unsigned short size = sm4_encry(sm4_data,&crc_buf[17],&crc_buf[17],8);
    crc_buf[4] = 11 + size;
    unsigned short crc_result = ModbusCRC(&crc_buf[6],crc_buf[4]);
    crc_buf[17+size] = crc_result&0xff;
    crc_buf[18+size] = (crc_result>>8)&0xff;
    //txbuf->putUShort(jzqAddr);
    //txbuf->putUShort(0xffff);
    txbuf->putData(crc_buf,crc_buf[4]+8);
    write();
    g_bcu_info->pile_tohost_info[0].send_id++;
}

void Cev5500::txTestConfirm()
{
	txbuf->putUByte(0x68);
	txbuf->putUShort(12);
	txbuf->putUInt(0x83);
	txbuf->putUInt(0);
	txbuf->putUShort(jzqAddr);
	txbuf->putUShort(0xffff);
	write();
}

void Cev5500::txChargeConfirm(int portno, bool onoff, bool confirm)
{
	ldebug<<"发送充电"<<(onoff?"启动":"停止")<<"确认报文:"<<confirm;
	INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+portno;
	if(confirm)
	{
		evse->charging=onoff;
		if(onoff)
		{
			evse->charge_authorized_result=0xaa;
			evse->checkout_result=0;
			//evse->bespeaking=false;
		}
		else
		{
			//evse->bespeaking=false;
			evse->checkout_result=0xaa;
			evse->charge_authorized_result=0;
		}
	}
	MyBuffer mb(false, 100);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(portno+1);

	mb.putUByte(0x41);//类型号
	mb.putUByte(1);//信息体个数
	mb.putUShort(confirm?0x04:0x06);//传送原因:4确认，6否认
	mb.putUShort(0);//公共地址
	mb.put3Bytes(0);//三个字节的信息体地址
	mb.putUByte(0);//遥控点号
	mb.putUByte(onoff?1:0);//性质
	mb.putUByte(0);//控制信息
	mb.putUInt(0);//定时时间
	mb.putUInt(0);//最高电压
	mb.putUInt(0);//最高电流
	mb.putUInt(0);//停止条件数据
	mb.putUInt(0);//16个字节卡号
	mb.putUInt(0);//
	mb.putUInt(0);//
	mb.putUInt(0);//

	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
}

void Cev5500::initProt(int jzqAddr)
{
	this->jzqAddr=jzqAddr;
}

void Cev5500::startTxChargeRecord(int portno)
{
	evseyx[portno].hasConfirmCR=false;
	txChargeRecord(portno);
	write();
	evseyx[portno].lastTxCR=getSecTimer;
}

void Cev5500::txChargeRecord(int portno)
{
	CEVCR* cr=&(evseyx[portno].curCr);
	ldebug<<"发送充电记录";
	INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+portno;
	MyBuffer mb(false, 200);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(portno+1);

	mb.putUByte(0x42);//类型号
	mb.putUByte(1);//信息体个数
	mb.putUShort(3);//传送原因
	mb.putUShort(0);//公共地址:0平台卡 1国网卡 2市民卡
	mb.put3Bytes(1);//三个字节的信息体地址: 是否从卡内扣值
	mb.putUByte(cr->chargeStartTime.sec);
	mb.putUByte(cr->chargeStartTime.minute);
	mb.putUByte(cr->chargeStartTime.hour);
	mb.putUByte(cr->chargeStartTime.day);
	mb.putUByte(cr->chargeStartTime.month);
	mb.putUByte(cr->chargeStartTime.year-2000);

	mb.putUByte(cr->chargeStopTime.sec);
	mb.putUByte(cr->chargeStopTime.minute);
	mb.putUByte(cr->chargeStopTime.hour);
	mb.putUByte(cr->chargeStopTime.day);
	mb.putUByte(cr->chargeStopTime.month);
	mb.putUByte(cr->chargeStopTime.year-2000);

	mb.putData(cr->cardno, 16);
	mb.putData(&(cr->kwhBegin), 4);
	mb.putData(&(cr->kwhEnd), 4);
	mb.putData(&(cr->kwh), 4);
	mb.putData(&(cr->money), 4);
	mb.putData(&(cr->balanceBegin), 4);
	mb.putData(&(cr->balanceEnd), 4);
	mb.putData(&(cr->kwhF), 4);
	mb.putData(&(cr->kwhG), 4);
	mb.putData(&(cr->kwhJ), 4);
	mb.putData(&(cr->kwhP), 4);

	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);}

void Cev5500::txBespeakConfirm(int portno, bool besOrCancel, bool confirm)
{
	ldebug<<"发送"<<(besOrCancel?"预约":"取消预约")<<"确认报文:"<<confirm;
	INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+portno;
	if(confirm)
		evse->bespeaking=besOrCancel;
	MyBuffer mb(false, 100);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(portno+1);

	mb.putUByte(0x93);//类型号
	mb.putUByte(1);//信息体个数
	mb.putUShort(confirm?0x04:0x06);//传送原因:4确认，6否认
	mb.putUShort(0);//公共地址
	mb.put3Bytes(0);//三个字节的信息体地址
	mb.putUByte(besOrCancel?2:1);
	mb.putUInt(0);//16个字节卡号
	mb.putUInt(0);//
	mb.putUInt(0);//
	mb.putUInt(0);//

	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
}

void Cev5500::txCardRequest(int portno, char* cardno)
{
	ldebug<<"发送刷卡请求，卡号"<<cardno;
	MyBuffer mb(false, 100);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(portno+1);

	mb.putUByte(0x94);//类型号
	mb.putUByte(1);//信息体个数
	mb.putUShort(3);//传送原因:4确认，6否认
	mb.putUShort(0);//公共地址
	mb.put3Bytes(0);//三个字节的信息体地址
	mb.putUByte(2);//请求启动充电
	mb.putData(cardno, 16);
	mb.putUByte(4);//控制信息:4充满为止
	mb.putUInt(0);//定时时间
	mb.putUInt(0);//最高电压
	mb.putUInt(0);//最高电流
	mb.putUInt(0);//停止条件数据

	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
}

//BCU转发过来的报文，原封上送
void Cev5500::txBcuDatagram(int portno, int frametype, uchar* buf, int len)
{
	txbuf->putUByte(0x68);
	txbuf->putData(buf, len);
	write();
        g_bcu_info->pile_tohost_info[0].send_id++;
}

void Cev5500::txChangeYx(int portno, int yxno, bool value)
{
	ldebug<<"tx change yx"<<yxno;
	MyBuffer mb(false, 100);

	mb.putUInt(0);//控制域
	mb.putUInt(0);//dst addr
	mb.putUShort(jzqAddr);
	mb.putUShort(0xffff);

	mb.putUByte(0x15);//类型号
	mb.putUByte(1);//信息体个数
	mb.putUShort(1);//传送原因:1突发
	mb.putUShort(0);//公共地址
	mb.put3Bytes(portno*YXCOUNT_PER+yxno);//三个字节的信息体地址
	mb.putUByte(value?1:0);

	txbuf->putUByte(0x68);
	txbuf->putUShort(mb.getLen());	
	txbuf->putMyBuffer(&mb);
	write();
}

void Cev5500::setSystime(CFETime ct)
{
#ifndef WIN32
	struct tm _tm;  
	struct timeval tv;  
	time_t timep;  
	_tm.tm_sec = ct.sec;  
	_tm.tm_min = ct.minute;
	_tm.tm_hour = ct.hour;
	_tm.tm_mday = ct.day;  
	_tm.tm_mon = ct.month - 1;  
	_tm.tm_year = ct.year - 1900;  

	timep = mktime(&_tm);  
	tv.tv_sec = timep;  
	tv.tv_usec = 0;  
	int ret=settimeofday (&tv, (struct timezone *) 0);
	if(ret<0)
	{
		ldebug<<"Set system datatime error!";

	}  
#endif
}

void Cev5500::setRxok(bool rxok)
{
	this->rxok=rxok;
        if(!rxok)
        {
            check_send_flag = 0;
            //recv_bill_model = 0;
        }
	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++)
		g_hmi_info->bcu_info_to_hmi[iPort].comm_state.cevhost_comm_flag=rxok?0xAA:0x55;
	if(rxok)
		lastRxtime=getSecTimer;
}

void Cev5500::checkErrorYc()
{
	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++)
	{
		INFO_FROM_EVSE* evse=g_hmi_info->info_from_evse+iPort;


		int nowerror=0;
		
		switch(evse->charge_err_type)
		{
			case CHARGER_ERROR://充电机处于故障状态
				//nowerror=110;
				nowerror=CHARGER_ERROR;
				break;
			case CHARGER_COMM_ERROR://与充电机通信出错
				//nowerror=117;
				nowerror=CHARGER_COMM_ERROR;
				break;
			case DDB_COMM_ERROR://与电表通信出错
				//nowerror=114;
				nowerror=DDB_COMM_ERROR;
				break;
		}
		if(evse->charger_aux_errno != 0)
		{
			nowerror=evse->charger_aux_errno;		
		}

		if(nowerror!=evseyx[iPort].nerror)
		{
			//txAllYc();
			printf("nowerror1 %x  %x  %x..........",nowerror,evseyx[iPort].nerror,iPort);
			
			evseyx[iPort].nerror=nowerror;
			txAllYc();	
			txerrinfo(iPort,nowerror);	
			printf("nowerror2 %x  %x  %x..........",nowerror,evseyx[iPort].nerror,iPort);
			
		}
	}
}

