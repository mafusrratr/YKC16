#pragma once
// BY ZF 智能城市平台接入规约头文件
#include "tcpclient.h"
#include "protbase.h"
#include "mybuffer.h"

// BY ZF 固件版本号（如258表示V2.58）
#define PLAT_FIRMWARE_VERSION  258

// BY ZF 智能城市平台接入规约类
class CSmartCityPlat:public CTcpClient, public CProtBase
{
public:
	static CSmartCityPlat* getInstance();
	~CSmartCityPlat(void);
	
	// BY ZF 主运行函数
	void run();
	
	// BY ZF 初始化协议参数（集中器地址/桩编码）
	void initProt(int jzqAddr);

	// BY ZF 设置系统时间
	void setSystime(CFETime ct);
	
	// BY ZF 计费模型结构体（从0x14帧获取，供外部计算模块使用）
	typedef struct {
		uchar periodCount;     // BY ZF 时段数（最多24个时段）
		uchar periodStartTime[24];  // BY ZF 时段开始时间（BCD码，小时，0-23）
		uchar periodEndTime[24];    // BY ZF 时段结束时间（BCD码，小时，0-23）
		ushort periodChargeFee[24]; // BY ZF 时段电价（单位0.01元）
		ushort periodServiceFee[24]; // BY ZF 时段服务费（单位0.01元）
	} BILLING_MODEL;
	
	// BY ZF 实际充电费用结构体（由外部计算模块计算后回传）
	typedef struct {
		uchar periodCount;     // BY ZF 时段数（与计费模型一致）
		uint periodTotalChargeFee[24];  // BY ZF 时段总电费（单位0.01元，4字节）
		uint periodTotalServiceFee[24]; // BY ZF 时段总服务费（单位0.01元，4字节）
		uint periodTotalEnergy[24];     // BY ZF 时段总电量（单位0.01KWH，4字节）
		uint totalChargeFee;           // BY ZF 总充电费用（所有时段电费之和，单位0.01元，4字节）
		uint totalServiceFee;           // BY ZF 总服务费（所有时段服务费之和，单位0.01元，4字节）
		uint totalEnergy;               // BY ZF 总充电电量（所有时段电量之和，单位0.01KWH，4字节）
	} CHARGE_FEE_DATA;
	
	// BY ZF 提供给外部计算模块的接口函数
	// BY ZF 获取指定充电口的计费模型（供外部计算模块使用）
	const BILLING_MODEL* getBillingModel(int chargeAddrNo);
	
	// BY ZF 外部计算模块回传计算结果
	// BY ZF 设置指定充电口的实际充电费用（由外部计算模块调用）
	void setChargeFeeData(int chargeAddrNo, const CHARGE_FEE_DATA* feeData);
	
	// BY ZF 获取指定充电口的实际充电费用（供外部计算模块使用，用于累加计算）
	const CHARGE_FEE_DATA* getChargeFeeData(int chargeAddrNo);
	
private:
	static CSmartCityPlat* instance;
	CSmartCityPlat();
	
	static const int MAX_PLAT_GUN_NUM = 20;
 
 	// BY ZF 平台下发参数结构体（存储平台下发的配置参数）
 	typedef struct{
 		// BY ZF 充电桩ID（平台下发，从配置确认帧中获取）
 		uint cdzId;
 		
 		// BY ZF 运行参数（心跳和监测间隔）
 		uchar heartbeatInterval;      // 心跳间隔周期(秒)，默认30秒(0x0a)
 		uchar heartbeatTimeoutCount; // 心跳超时检测次数，默认5次(0x03)
 		uchar chargeInfoInterval;    // 充电信息监测数据间隔周期(秒)，默认10秒(0x05)
 		uchar chargeInfoTimeoutCount;// 充电信息监测检测次数，默认5次(0x03)
 		uchar chargeVehicleInterval; // 充电中车监测数据间隔周期(秒)，默认10秒(0x05)
 		uchar chargeVehicleTimeoutCount; // 充电中车监测检测次数，默认5次(0x03)
 		
 		// BY ZF 枪参数（平台下发）
 		uchar gunCount;                 // 平台下发的充电枪数量
 		uint gunId[MAX_PLAT_GUN_NUM];   // 平台下发的每把枪ID
 		uchar gunType[MAX_PLAT_GUN_NUM];// 平台下发的每把枪类型
 		uchar onlineMode;               // 脱机/联机标志原始值
 		
 		// BY ZF 平台下发参数（文件上传相关）
 		char operatorId[30];        // 运营商ID（从社会信用统一代码提取：去掉前8位和最后1位，取中间部分），ASCII编码
 		char uploadUrl[128];        // 设备终端文件上传地址，128字节，ASCII编码，末尾0x00补位
 		char uploadSKey[32];        // 上传密钥SKey，32字节，ASCII编码
 		char uploadParamName[10];   // 上传文件参数名称，10字节，ASCII编码，末尾0x00补位
 	}PLAT_DOWNLOAD_PARAMS;

	// BY ZF 处理接收到的数据
	void handleRead();
	
	// BY ZF 处理待发送的数据
	void handleWrite();

	// BY ZF 登录流程函数
	// 1. 参数配置阶段
	void txConfigSetAsk();      // 配置请求（0x00）
	void rxConfigSetConfirm(MyBuffer* infoBody); // 配置确认（0x01）
	
	// 2. 签到阶段
	void txSignInRequest();     // 签到请求（0x20）
	void rxSignInResponse(MyBuffer* infoBody);   // 签到应答（0x30）
	
	// 3. 登录认证阶段
	void txLoginAuthRequest();   // 登录认证请求（0xd0）
	void rxLoginAuthResult(MyBuffer* infoBody); // 登录认证结果（0xd1）

	// 4. 远程控制阶段
	void rxRemoteStart(MyBuffer* infoBody); // 远程启动充电（0x14：扫码后账户信息推送）
	void rxRemoteStop(MyBuffer* infoBody); // 远程停止充电（0x15：移动端停止充电请求）
	void txRemoteStartResponse(); // 启动充电应答（0x04）
	void txRemoteStopResponse(); // 停止充电应答（0x05）
	void checkRemoteControlStatus(); // 检查远程控制状态变化并发送应答
	
	
	// 5. 充电信息上报阶段
	void txChargeInfo(int chargeAddrNo); // 发送充电信息上报（0x22）

	// 6. 充电记录上送阶段
	void txChargeRecordRequest(int chargeAddrNo); // 发送充电记录包请求（0x60）
	void rxChargeRecordResponse(MyBuffer* infoBody); // 充电记录包请求应答（0x70）
	void checkChargeRecordUpload(); // 检查并上送充电记录

	// BY ZF 发送心跳测试激活帧
	void txHeartbeatActive();
	
	// BY ZF 发送心跳测试确认帧
	void txHeartbeatConfirm();
	
	// BY ZF 设置接收状态
	void setRxok(bool rxok);
	
	// BY ZF 帧封装函数：根据命令字和信息体构建完整报文并自动发送
	// cmd: 命令字（1字节）
	// infoBody: 信息体数据（可变长度，通过MyBuffer传入）
	void buildProtocolFrame(uchar cmd, MyBuffer* infoBody);
	
	// BY ZF 帧解封函数：解析接收到的报文，提取命令字和信息体
	// frameData: 接收到的完整帧数据
	// frameLen: 帧数据长度
	// cmd: 输出参数，解析得到的命令字
	// infoBody: 输出参数，解析得到的信息体数据
	bool parseProtocolFrame(uchar* frameData, int frameLen, uchar* cmd, MyBuffer* infoBody);
	
	// BY ZF 计算CRC校验和（从前导字到数据字段）
	ushort calculateCRC(uchar* data, int len);
	
	// BY ZF 获取当前时间戳（BCD码格式，8字节）
	void getTimestampBCD(uchar* timestamp);
	
	// BY ZF 集中器地址/桩编码
	int jzqAddr;
	
	// BY ZF 最后接收数据时间
	int lastRxtime;
	
	// BY ZF 发送序号
	int sendNo;
	
	// BY ZF 最后发送心跳时间
	int lastTxHeart;
	// BY ZF 最后收到平台报文时间（用于检测连接是否正常，任何平台下发的报文都会更新此时间）
	int lastRxHeartbeatTime;
	// BY ZF 心跳计数器
	uchar heartbeatCounter;
	// BY ZF 每把枪上次发送充电信息(0x22)的时间戳（用于8秒间隔上报）
	int lastTxChargeInfoTime[20]; // 最多支持20把枪
	// BY ZF 上次检查并上送充电记录的时间，用于30秒间隔控制
	int lastChargeRecordUploadTime;
	
	// BY ZF 登录流程状态枚举
	enum LOGIN_STATE{
		LOGIN_STATE_NONE = 0,      // 未开始登录
		LOGIN_STATE_CONFIG_REQ,    // 已发送配置请求(0x00)，等待配置确认(0x01)
		LOGIN_STATE_CONFIG_OK,     // 配置确认完成，准备签到
		LOGIN_STATE_SIGNIN_REQ,    // 已发送签到请求(0x20)，等待签到应答(0x30)
		LOGIN_STATE_SIGNIN_OK,     // 签到应答完成，准备登录认证
		LOGIN_STATE_AUTH_REQ,      // 已发送登录认证请求(0xd0)，等待认证结果(0xd1)
		LOGIN_STATE_SUCCESS        // 登录成功
	};
	
	// BY ZF 当前登录流程状态
	LOGIN_STATE loginState;
	
	// BY ZF 最后发送登录请求时间（用于重试）
	int lastTxLoginTime;
	
	// BY ZF 平台下发参数（由平台下发，初始化时使用默认值）
	PLAT_DOWNLOAD_PARAMS platDownloadParams;
	
	// BY ZF MAC地址（12字节，从配置文件读取的24字符十六进制字符串转换而来）
	uchar macAddrBytes[12];
	
	// BY ZF 充电枪ID数组（从配置文件读取）
	uint plugIdArray[20]; // 最大支持20个充电枪
	
	// BY ZF 桩企信用编码（9字节ASCII，从配置文件读取）
	uchar factoryCreditCode[9];
	
	// BY ZF 远程控制应答待发送信息结构
	typedef struct {
		uint gunId;           // 枪ID
		uchar controlCmd;      // 控制命令：0x00=启动, 0x01=停止
		uchar expectedState;   // 期望的charging状态：1=充电中, 0=未充电
		uchar currentState;    // 当前charging状态（用于比较）
		uchar result;          // 执行结果：0x00=成功, 0x01=不成功（待确定）
		bool pending;         // 是否待发送应答
		int waitStartTime;     // 开始等待的时间戳
		uchar orderId[10];     // 订单ID（10字节BCD码，用于应答）
		uchar userId[5];       // 用户ID（5字节BCD码，用于启动应答）
		uchar billingFlag;     // BY ZF 计费标志：0x00=充电桩计费, 0x01=充电桩不计费（从0x14帧中获取）
		BILLING_MODEL billingModel; // BY ZF 计费模型（从0x14帧获取，供外部计算使用）
		CHARGE_FEE_DATA chargeFeeData; // BY ZF 实际充电费用（由外部计算后回传）
		uchar chargeStartTime[8]; // BY ZF 充电起始时间（8字节BCD码，从发送0x04应答时记录）
	} REMOTE_CTRL_PENDING;
	
	// BY ZF 远程控制待应答列表（最多支持20把枪，因本h文件不便于include define.h 所以在此设置大于等于最大充电口的数量即可）
	REMOTE_CTRL_PENDING remoteCtrlPending[20];
};
