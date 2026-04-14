/**------------------------------------------------------------------------------------------
		Ver2.0   20240130 09:16
        Ver1.0   20231122 08:45  	
-------------------------------------------------------------------------------------------*/
//#ifndef SHM_INCLUDE_H
//#define SHM_INCLUDE_H

#pragma once
#include "stdio.h"
#include "stdlib.h"

#ifdef WIN32
#include "winsock.h"
#else
#include <unistd.h>
#include <pthread.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/shm.h>
#include <errno.h>
#include <sys/time.h>
#include <signal.h>
#include <alloca.h>
#include <string.h>

#endif

// BY ZF: shm_points.h 已移除，当前头文件未依赖其中定义。

#ifdef WIN32
#if defined(CSHMLIB)
#define	CSHMEXPORT __declspec(dllexport)
#else
#define	CSHMEXPORT __declspec(dllimport)
#endif
#else
#define CSHMEXPORT

#endif
#define DES_NAME_LEN 40
///Setting Port2********************************
#define YC_COUNT	(612)
#define YX_COUNT	(612)
#define DD_COUNT	(128)
#define PORT_COUNT	(2)
///Setting Port4********************************
//#define YC_COUNT	1124
//#define YX_COUNT	1124
//#define DD_COUNT	256
//#define PORT_COUNT	4
///*********************************************
#define CARDNO_LEN	20
#define FLAG_COUNT	128

#define FLAG_CHARGE_STATE 0

#define V2G_MAX_TRADE_LEN (40 + 1)		  //最大订单号长度：设备唯一标识（充电桩资产码：24位）+充电接口标识（2 位）+ 序列号(12 位)
#define V2G_MAX_USERID_LEN (255 + 1)	  //用户ID最大长度
#define V2G_MAX_CAR_VIN_LEN (17 + 1)	  //最大车辆唯一识别码长度
#define V2G_MAX_ENERGY_DATA_LEN (5)	      //充放电电量数据最大长度
#define V2G_MAX_TIMESTAMP_LEN (15 + 1)	  //时间戳长度
#define V2G_MAX_MODEL_DEVSEG (14)		  //最大计费模型时段数
#define EVS_MAX_TRADE_LEN (40 + 1)		  // 最大订单号长度：设备唯一标识（充电桩资产码：24位）+充电接口标识（2 位）+ 序列号(12 位)
#define EVS_MAX_CAR_VIN_LEN (17 + 1)	  // 最大车辆唯一识别码长度
#define EVS_MAX_MODEL_DEVSEG (96)		  // 最大计费模型时段数
#define EVS_MAX_MODEL_ID_LEN (16 + 1)	  // 最大计费模型id长度
#define EVS_MAX_AUTHCODE_LEN (255 + 1)	  // 本地鉴权鉴权码长度
/**************************************************************************************************************************/

enum CHARGE_STATE{STATE_IDLE, STATE_PLUGIN, STATE_FLASHCARD, STATE_WAITCHARGE, STATE_CHARGE, STATE_FULL, STATE_STOP, STATE_EMESTOP, STATE_UNPLUG};
typedef struct{
	int index;
	char desname[DES_NAME_LEN];
	float coef;
	unsigned int value;
}YC;

typedef struct{
    int index;
	char desname[DES_NAME_LEN];
	long long value;
}DD;

typedef struct{
	int index;
	char desname[DES_NAME_LEN];
	bool value;
}YX;
/*************************************************************************************************
*
*                              此结构体为充电桩状态及故障的数据
*
**************************************************************************************************/
typedef struct
{
    unsigned char PortId;                   //充电接口标识
    unsigned char WorkStatus;               //工作状态
    char QrCode[256];                       //二维码
} _tagChargePort;

/*************************************************************************************************
*
*                              此结构体为设备信息的数据
*
**************************************************************************************************/
typedef struct
{
    unsigned int main_crc;							 		    //主控模块校验码
    unsigned int port_crc;                                      //端口文件校验码
    unsigned int file_crc;                                      //系统校验文件校验码
    unsigned int ad_crc;                                        //广告文件校验码
    unsigned char devRegMethod;                                 ///设备注册方式
    char pileSoftwareVer[257];                                  ///充电桩软件版本号
    char pileHardwareVer[257];                                  ///充电桩硬件版本号
    char sdkVer[257];                                           ///SDK版本号
    unsigned char Dev_Charging_Number[12];		  		        ///设备信息充电桩编号
    unsigned char PatchSN[4];		                            ///软件补丁号
    unsigned char UpdateReboot;		                            ///设备重启标识
    unsigned int  GPS_Longitude;								///经度
    unsigned int  GPS_Latitude;									///纬度
    unsigned int  GPS_Altitude;								    ///海拔
    unsigned int  gridType;                                     ///坐标类型
    unsigned char EsamSN[8];			            	        ///ESAM序列号；station_info
    unsigned char btMAC[33];			              			///蓝牙MAC地址；
    char simNo[24];								                ///SIM卡号
	char stakeModel[20];						                ///充电桩型号
	char deSn[17];								                ///出厂编号//字符串
	char simMac[33];							                ///网络MAC地址//字符串
    unsigned int NumberOfChargPort ;                            //充电桩接口数量
    unsigned char EnmergencyLocked;                             //设备锁
    unsigned char Overhaul;                                     //检修状态
    unsigned char sPileVersion[2];                              ///充电控制器协议版本号
    unsigned char cVersionSNOfBMS;                              ///充电控制器支持BMS的协议版本号
    unsigned char szFactorySN[4];                               ///厂家编码 vendorCode
    unsigned short nUnitType;                                   ///设备型号
    unsigned char meaType;										///计量方式
    unsigned int CT;											///电流互感器系数 默认值1
	unsigned char isGateLock;									///是否有智能门锁
	unsigned char isGroundLock;									///是否有地锁
    unsigned int nPowerRating;                                  ///充电机额定功率
    unsigned int nChargerMaxOutVolt;                            ///充电机最高输出电压
    unsigned int nChargerMinOutVolt;                            ///充电机最低输出电压
    unsigned int nChargerMaxOutCurr;                            ///充电机最高输出电流
    unsigned int nChargerMinOutCurr;                            ///充电机最低输出电流
    unsigned int minChargingPower;							    ///最小充电功率
    unsigned int maxDischargeVoltage;							///最高放电电压
	unsigned int minDischargeVoltage;							///最低放电电压
	unsigned int maxDischargeCurrent;							///最大放电电流
	unsigned int minDischargeCurrent;							///最小放电电流
} _tagDeviceInfo;

/*************************************************************************************************
*
*                       			充电桩上传交易记录所需结构体信息
*
**************************************************************************************************/
typedef struct
{
	unsigned char gunNo;											// 1	充电枪编号
	char applySheetNo[V2G_MAX_TRADE_LEN]; 							// 2    订单编号
	char vehicleVIN[V2G_MAX_CAR_VIN_LEN]; 							// 3	vin码
	unsigned char executionStatus;									// 4	执行状态
	unsigned int chargingEnergyValue[V2G_MAX_ENERGY_DATA_LEN];		// 5	直流电表-充电电能示值
	unsigned int disChargingEnergyValue[V2G_MAX_ENERGY_DATA_LEN];	// 6	直流电表-放电电能示值
	unsigned int cumulativeTime;									// 7	累计充（放） 电时间
	unsigned int cumulativeCharge[V2G_MAX_ENERGY_DATA_LEN];			// 8	直流电表-累计充电电量
	unsigned int cumulativeDischarge[V2G_MAX_ENERGY_DATA_LEN];		// 9	直流电表-累计放电电量
	unsigned char socNumber;										// 10	当前荷电状态 SOC（%） 
	unsigned int acChargingEnergyValue[V2G_MAX_ENERGY_DATA_LEN];	// 11	交流电表-充电电能示值
	unsigned int acDisChargingEnergyValue[V2G_MAX_ENERGY_DATA_LEN];	// 12	交流电表-放电电能示值
	unsigned int acCumulativeCharge[V2G_MAX_ENERGY_DATA_LEN];		// 13	交流电表-累计充电电量
	unsigned int acCumulativeDischarge[V2G_MAX_ENERGY_DATA_LEN];	// 14	交流电表-累计放电电量
	unsigned int eventDesc;											// 15	事件描述
} _sdk_event_executionStatus;

// 交易记录事件上传参数
typedef struct
{
    unsigned char gunNo;                            // 1 充电枪编号
    char preTradeNo[EVS_MAX_TRADE_LEN];             // 2 平台交易流水号
    char tradeNo[EVS_MAX_TRADE_LEN];                // 3 设备交易流水号
    char vinCode[EVS_MAX_CAR_VIN_LEN];              // 4 VIN
    unsigned char timeDivType;                      // 5 计量计费类型
    unsigned char startType;                        // 6 启动方式
    unsigned int chargeStartTime;                   // 7 开始充电时间
    unsigned int chargeEndTime;                     // 8 结束充电时间
    unsigned char startSoc;                         // 9 启动时SOC
    unsigned char endSoc;                           // 10 停止时SOC
    unsigned int reason;                            // 11 停止充电原因
    char feeModelId[EVS_MAX_MODEL_ID_LEN];          // 12 计量计费模型编号
    long long sumStart;                             // 13 电表总起示值
    long long sumEnd;                               // 14 电表总止示值
    unsigned int totalElect;                        // 15 总电量
    unsigned int totalPowerCost;                    // 16 总电费
    unsigned int totalServCost;                     // 17 总服务费
    unsigned int totalCost;                         // 18 总消费金额
    unsigned char timeNum;                          // 19 时段数
    unsigned int partElect[EVS_MAX_MODEL_DEVSEG];   // 20 时段电量
    unsigned int chargeFee[EVS_MAX_MODEL_DEVSEG];   // 21 时段电费
    unsigned int serviceFee[EVS_MAX_MODEL_DEVSEG];  // 22 时段服务费
    unsigned char startPoint;                       // 23 起始点标识
    unsigned char crossPoints;                      // 24 跨越点数
    unsigned int pointsElect[EVS_MAX_MODEL_DEVSEG]; // 25 跨越点电量
} _evs_event_tradeInfo;
/************************************************************************************************
*
*                            此结构体为计费模型更新服务下发参数结构
*
*************************************************************************************************/
//for V2G
typedef struct
{
	unsigned char timeNum;				   		    // 1		电费模型时段数N 取值范围：1—48
	char timeSeg[V2G_MAX_MODEL_DEVSEG][5]; 		    // 2		电费模型时段开始时间点
	unsigned int  segFlag[V2G_MAX_MODEL_DEVSEG];    // 3		电费模型时段标志
	unsigned int chargeFee[4];			   		    // 4		电费模型
	unsigned int serviceFee[4];			   		    // 5		服务费费模型
} _sdk_service_issue_feeModel;
//for EVS
typedef struct
{
    char feeModelId[EVS_MAX_MODEL_ID_LEN];          // 1     电费计费模型编号
    unsigned char timeNum;                          // 2     电费模型时段数N 取值范围：1—96
    char timeSeg[EVS_MAX_MODEL_DEVSEG][5];          // 3     电费模型时段开始时间点
    unsigned int chargeFee[EVS_MAX_MODEL_DEVSEG];   // 4     电费模型
    unsigned int serviceFee[EVS_MAX_MODEL_DEVSEG];  // 5     服务费费模型
} _evs_service_issue_feeModel;

//HMI
typedef struct
{
    //状态
    unsigned int process_id;//
    //激活
    unsigned int register_request;
    unsigned int qr_autojump;// QR be auto jump
    //校验
    unsigned int veri_result;
    //取随机数
    unsigned int trade_random_request;
    //卡解灰
    unsigned int cardunlock_request;
    unsigned int cardunlock_result;
    //软件更新
    unsigned int soft_update_request;
    unsigned int soft_update_ready;
    //密钥更新
    unsigned int skey_update_request;
    unsigned int skey_update_ready;
    //黑名单
    unsigned int blacklist_update_request;
    unsigned int blacklist_update_ready;
    //鉴权
    unsigned int auth_request;  //0:null;2:验证码;3:账号
    unsigned int auth_okflag;   //0:null;1:ok;2:fail
    unsigned int  auth_failreason;
    //交易data send up request
    unsigned int trade_request;//0:null; 1:有卡交易;2:验证码;3:账号;4:二维码;

    unsigned int debit_okflag;   //0:null;1:ok;2:fail
    unsigned int  debit_failreason;

    unsigned int Second_authOk;
    unsigned int encoder_request; //0:null; 1:离线二维码鉴权;2:离线二维码结算;3:离线注册;4:数据准备中;5:数据完成;
    unsigned char EncoderBuff[256];
} _remote_public;

/*************************************************************************************************
*
*                              此结构体为启动订单结果
*
*************************************************************************************************/
typedef struct
{
	unsigned char gunNo;						// 1	充电枪编号
	char applyNo[V2G_MAX_TRADE_LEN]; 			// 2	订单编号
	char userId[V2G_MAX_USERID_LEN];			// 3	用户ID
	char VIN[V2G_MAX_CAR_VIN_LEN];				// 4	vin码
	unsigned char decisionType;					// 5	决策类型
	char decisionTime[V2G_MAX_TIMESTAMP_LEN]; 	// 6	决策时间
} _sdk_service_remoteStart;

typedef struct
{
    unsigned char gunNo;                        // 1   充电枪编号
    char preTradeNo[EVS_MAX_TRADE_LEN];         // 2   平台交易流水号
    char tradeNo[EVS_MAX_TRADE_LEN];            // 3   设备交易流水号
    unsigned char startType;                    // 4   启动方式
    char authCode[EVS_MAX_AUTHCODE_LEN];        // 5   鉴权码 256
    unsigned char chargeMode;                   // 7   充电模式
    unsigned int limitData;                     // 8   限制值
    unsigned int stopCode;                      // 9   停机码
    unsigned char startMode;                    // 10  启动模式
} _evs_service_startCharge;
/*************************************************************************************************
*
*                                 class CSHMEXPORT CShm
*
**************************************************************************************************/

class CSHMEXPORT CShm{
public:
    bool init();
    virtual void DataInit();
    int DelShm();
    YC* getYc(int ycno = 0);
    DD* getDd(int ddno = 0);
    YX* getYx(int yxno = 0);
    _tagChargePort* getChargePort(int PortNo = 0){return chargePort + PortNo;}
    _tagDeviceInfo* getDeviceInfo(){return deviceInfo;}

    _sdk_event_executionStatus * getTradeRecord(int no = 0){return tradeRecordInfo + no;}
    _remote_public* getRemotePublic(int PortNo=0){return remotePublic + PortNo;}

    _sdk_service_issue_feeModel * getFeeModel(){return feeModel;}
    _sdk_service_remoteStart * getRemoteStart(int PortNo = 0){return remoteStart + PortNo;}

    _evs_event_tradeInfo * evs_getTradeRecord(int no=0){return evsTradeRecord + no;}
    _evs_service_issue_feeModel * evs_getFeeModel(){return evsFeeModel;}
    _evs_service_startCharge * evs_getRemoteStart(int PortNo = 0){return evsRemoteStart + PortNo;}
    CShm();
    virtual ~CShm();
private:
    int shmid;
	void * CreateShm();

	void * shmbase;

	YC * yc;
	YX * yx;
    DD * dd;
    _tagChargePort *chargePort;
    _tagDeviceInfo *deviceInfo;
    _sdk_event_executionStatus * tradeRecordInfo;
    _remote_public * remotePublic;
    _sdk_service_remoteStart * remoteStart;
    _sdk_service_issue_feeModel * feeModel;

    _evs_event_tradeInfo * evsTradeRecord;
    _evs_service_startCharge * evsRemoteStart;
    _evs_service_issue_feeModel * evsFeeModel;
};
