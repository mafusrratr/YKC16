// BY ZF 智能城市平台接入规约实现文件
#include "smartCityPlat.h"
#include "log.h"
#include <time.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <sstream>
// BY ZF 引入TCU接口以获取SIM卡信息
#include "zk/inc/libtcu.h"
// BY ZF 引入配置文件读写接口
#include "inifile.h"
// BY ZF 引入充电逻辑接口以访问充电控制标志
#include "zk/inc/charge_logic.h"
#include "zk/inc/deal_card_esam.h"

// BY ZF cmd_interface.h中的函数是C函数，需要用extern "C"包含以确保C++能正确链接
extern "C" {
	#include "zk/inc/cmd_interface.h"
}

#include "zk/inc/billing_control_unit.h"
#include "zk/inc/hmi_interface.h"
#include "zk/inc/Global_Varible.h"  // BY ZF 包含charge_record_info全局变量定义

// BY ZF 声明C函数，使用extern "C"确保C++能正确链接C函数
extern "C" {
	// BY ZF init_charge_start_info在charge_logic.c中定义，但未在头文件中声明
	void init_charge_start_info(unsigned char charge_port_no, unsigned char charge_addr_no);
}
#ifdef Error
#undef Error
#endif
#ifdef WIN32
#define getSecTimer (GetTickCount()/1000)
#else
#include "uicommon.h"
#include "Global_Varible.h"
#define getSecTimer g_Time2048msCounter
#endif

// BY ZF 构造函数：初始化智能城市平台接入规约
CSmartCityPlat::CSmartCityPlat()
:CProtBase(true), CTcpClient("smartCityPlat")  // BY ZF 修复：协议使用大端序，CProtBase应传入true
{
	// BY ZF 初始化接收和发送缓冲区（协议使用大端序：高字节在前）
	rxbuf=new MyBuffer(true);
	txbuf=new MyBuffer(true);
	
	// BY ZF 初始化发送序号
	sendNo=0;
	
	// BY ZF 初始化最后发送心跳时间，使其立即发送一次心跳
	lastTxHeart=getSecTimer-100;
	heartbeatCounter = 0;
	// BY ZF 初始化充电记录上送时间，使用负数确保第一次立即检查
	lastChargeRecordUploadTime = getSecTimer - 100;
	
	// BY ZF 初始化登录状态为未开始
	loginState = LOGIN_STATE_NONE;
	lastTxLoginTime = 0;
	
	// BY ZF 读取首次初始化标识和检查枪ID初始化状态
	CEVHOST_INI* ini = &(g_hmi_info->cevhost_ini);
	int firstInitFlag = read_profile_int("互动化平台", "首次初始化", 1, "cui.ini");
	
	// BY ZF 从配置文件初始化充电桩ID（如果配置文件中已有值）
	platDownloadParams.cdzId = ini->jzqAddr;
	if(platDownloadParams.cdzId == 0 || platDownloadParams.cdzId == 9999)
	{
		// BY ZF 如果配置文件中是默认值，则等待平台下发
	platDownloadParams.cdzId = 0;
	}
	
	// BY ZF 检查枪ID是否已初始化（至少有一把枪的ID不为0且不为0xFFFFFFFF）
	bool gunIdInitialized = false;
	int validGunCount = 0;
	for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
	{
		if(ini->plugId[i] != 0 && ini->plugId[i] != 0xFFFFFFFF)
		{
			gunIdInitialized = true;
			validGunCount++;
		}
	}
	
	// BY ZF 根据首次初始化标识和枪ID状态决定初始登录状态
	// 1. 如果首次初始化标识为0（已初始化），则跳过配置请求，直接进入CONFIG_OK状态
	// 2. 如果枪ID未初始化（所有枪ID都是0或0xFFFFFFFF），则必须进入初始化流程
	if(firstInitFlag == 0 && gunIdInitialized)
	{
		// BY ZF 已初始化：跳过配置请求，直接进入CONFIG_OK状态，准备发送签到请求
		loginState = LOGIN_STATE_CONFIG_OK;
		ldebug << "First init flag=0 and gun IDs initialized, skip config request, go to CONFIG_OK";
	}
	else if(!gunIdInitialized)
	{
		// BY ZF 枪ID未初始化：必须进入初始化流程（发送配置请求）
		loginState = LOGIN_STATE_NONE;
		ldebug << "Gun IDs not initialized, must enter init flow (send config request)";
	}
	else
	{
		// BY ZF 首次初始化标识为1但枪ID已初始化：正常流程，发送配置请求
		loginState = LOGIN_STATE_NONE;
		ldebug << "First init flag=1, enter normal init flow (send config request)";
	}
	
	// BY ZF 初始化远程控制待应答列表
	for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
	{
		remoteCtrlPending[i].pending = false;
		remoteCtrlPending[i].gunId = 0;
		remoteCtrlPending[i].controlCmd = 0;
		remoteCtrlPending[i].expectedState = 0;
		remoteCtrlPending[i].currentState = 0;
		remoteCtrlPending[i].result = 0x01; // 默认不成功
		remoteCtrlPending[i].waitStartTime = 0;
		memset(remoteCtrlPending[i].orderId, 0, sizeof(remoteCtrlPending[i].orderId));
		memset(remoteCtrlPending[i].userId, 0, sizeof(remoteCtrlPending[i].userId));
		// BY ZF 初始化充电信息上报时间戳
		lastTxChargeInfoTime[i] = 0;
	}

	// BY ZF 初始化接收状态为false
	setRxok(false);
	lastRxtime=getSecTimer;
	
	// BY ZF 初始化平台下发参数为默认值
	platDownloadParams.heartbeatInterval = 30;      // 默认30秒
	platDownloadParams.heartbeatTimeoutCount = 5;   // 默认5次
	platDownloadParams.chargeInfoInterval = 10;    // 默认10秒
	platDownloadParams.chargeInfoTimeoutCount = 5;  // 默认5次
	platDownloadParams.chargeVehicleInterval = 10;  // 默认10秒
	platDownloadParams.chargeVehicleTimeoutCount = 5; // 默认5次
	// BY ZF 初始化枪ID数组
	memset(platDownloadParams.gunId, 0, sizeof(platDownloadParams.gunId));
	memset(platDownloadParams.gunType, 0, sizeof(platDownloadParams.gunType));
	// BY ZF 从配置文件初始化枪ID和枪数量（如果配置文件中已有值）
	// BY ZF 注意：ini变量已在上面声明，这里直接使用
	int configGunCount = 0;
	for(int i = 0; i < MAX_PLAT_GUN_NUM && i < MAX_DC_CHARGER_NUM; i++)
	{
		if(ini->plugId[i] != 0 && ini->plugId[i] != 0xFFFFFFFF)
		{
			platDownloadParams.gunId[i] = ini->plugId[i];
			configGunCount++;
			// BY ZF 默认枪类型：根据全局充电桩类型设置
			extern unsigned int g_charger_type;
			platDownloadParams.gunType[i] = (g_charger_type == AC_CHARGER_TYPE) ? 0x02 : 0x01;
		}
	}
	// BY ZF 设置枪数量（从配置文件读取的有效枪ID数量）
	platDownloadParams.gunCount = configGunCount;
	platDownloadParams.onlineMode = 1;
	
	// BY ZF 初始化平台下发参数（文件上传相关）为空或默认值
	memset(platDownloadParams.operatorId, 0, sizeof(platDownloadParams.operatorId));      // 运营商ID初始化为空
	memset(platDownloadParams.uploadUrl, 0, sizeof(platDownloadParams.uploadUrl));        // 上传地址初始化为空
	memset(platDownloadParams.uploadSKey, 0, sizeof(platDownloadParams.uploadSKey));       // 上传密钥初始化为空
	memset(platDownloadParams.uploadParamName, 0, sizeof(platDownloadParams.uploadParamName)); // 上传参数名初始化为空
	
	// BY ZF 从配置文件读取MAC地址并转换为12字节
	// BY ZF 协议要求：MAC地址全部为实际值，不再使用0xFF填充尾部
	// BY ZF 使用上面已声明的ini变量
	memset(macAddrBytes, 0, sizeof(macAddrBytes));
	bool hasMacValue = false;
	if(ini->macAddr[0] != 0)
	{
		// BY ZF 兼容两种配置形式：十六进制字符串或直接的12字节二进制
		bool looksLikeAscii = true;
		int asciiLen = 0;
		for(int idx = 0; idx < (int)sizeof(ini->macAddr); idx++)
		{
			uchar ch = ini->macAddr[idx];
			if(ch == 0)
				break;
			asciiLen++;
			if(!((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')))
			{
				looksLikeAscii = false;
				break;
			}
		}

		if(looksLikeAscii && asciiLen > 0)
		{
			// BY ZF 将十六进制字符串转换为字节序列
			int byteCount = asciiLen / 2;
			if(byteCount > (int)sizeof(macAddrBytes))
				byteCount = sizeof(macAddrBytes);
			for(int i = 0; i < byteCount; i++)
			{
				uchar highChar = ini->macAddr[i * 2];
				uchar lowChar = 0;
				if(i * 2 + 1 < asciiLen)
					lowChar = ini->macAddr[i * 2 + 1];

				uchar byteVal = 0;
				if(highChar >= '0' && highChar <= '9')
					byteVal = (highChar - '0') << 4;
				else if(highChar >= 'A' && highChar <= 'F')
					byteVal = (highChar - 'A' + 10) << 4;
				else if(highChar >= 'a' && highChar <= 'f')
					byteVal = (highChar - 'a' + 10) << 4;

				if(lowChar >= '0' && lowChar <= '9')
					byteVal |= (lowChar - '0');
				else if(lowChar >= 'A' && lowChar <= 'F')
					byteVal |= (lowChar - 'A' + 10);
				else if(lowChar >= 'a' && lowChar <= 'f')
					byteVal |= (lowChar - 'a' + 10);
				
				macAddrBytes[i] = byteVal;
			}
			hasMacValue = (byteCount > 0);
	}
	else
	{
			// BY ZF 直接使用配置中给出的12字节原始数据
			memcpy(macAddrBytes, ini->macAddr, sizeof(macAddrBytes));
			for(int i = 0; i < (int)sizeof(macAddrBytes); i++)
			{
				if(macAddrBytes[i] != 0x00)
				{
					hasMacValue = true;
					break;
				}
			}
		}
	}

	if(!hasMacValue)
	{
		// BY ZF 若未配置有效MAC地址，使用0xFF填充以便排查配置问题
		memset(macAddrBytes, 0xFF, sizeof(macAddrBytes));
	}
	
	// BY ZF 从配置文件读取充电枪ID数组
	for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
	{
		plugIdArray[i] = ini->plugId[i];
	}
	
	// BY ZF 从配置文件读取桩企信用编码（9字节ASCII）
	memcpy(factoryCreditCode, ini->factoryCreditCode, 9);
	
	ldebug << "Platform download params initialized with default values";
	ldebug << "MAC address loaded: " << (char*)ini->macAddr << " -> converted to 12 bytes";
}

// BY ZF 析构函数
CSmartCityPlat::~CSmartCityPlat(void)
{
}

// BY ZF 单例模式：获取实例指针
CSmartCityPlat* CSmartCityPlat::instance=NULL;
CSmartCityPlat* CSmartCityPlat::getInstance()
{
	if(instance==NULL)
	{
		instance=new CSmartCityPlat();
	}
	return instance;
}

// BY ZF 连接状态标志（static 避免与其他模块的全局变量冲突）
static unsigned char connent_flag;

// BY ZF 主运行循环：处理网络连接和报文收发
void CSmartCityPlat::run()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	while(1)
	{
		try{
			// BY ZF 如果超过300秒未收到数据，认为连接异常，断开连接
			if(abs(getSecTimer-lastRxtime)>300)
			{
				setRxok(false);
				disconn();
			}
			
			// BY ZF 如果未连接，尝试连接
			if(!isConnected())
			{
				Sleep(2000);
				conn();
				if(isConnected())
					{
					ldebug << "Platform connection established successfully!";
					// BY ZF 连接建立后重新检查首次初始化状态，决定初始登录状态
					CEVHOST_INI* ini = &(g_hmi_info->cevhost_ini);
					int firstInitFlag = read_profile_int("互动化平台", "首次初始化", 1, "cui.ini");
					
					// BY ZF 检查枪ID是否已初始化
					bool gunIdInitialized = false;
					for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
					{
						if(ini->plugId[i] != 0 && ini->plugId[i] != 0xFFFFFFFF)
						{
							gunIdInitialized = true;
							break;
						}
					}
					
					// BY ZF 根据首次初始化标识和枪ID状态决定初始登录状态
					if(firstInitFlag == 0 && gunIdInitialized)
					{
						loginState = LOGIN_STATE_CONFIG_OK;
						ldebug << "Connection established: First init flag=0, skip config request";
					}
					else
					{
					loginState = LOGIN_STATE_NONE;
						ldebug << "Connection established: Enter normal init flow";
					}
					lastTxLoginTime = 0;
					lastRxtime=getSecTimer - 13;
				}
				connent_flag = NO;
				ldebug << "Platform connection status: NO";
			}
			
			// BY ZF 如果已连接，处理数据收发和登录流程
			if(isConnected())
			{
				// BY ZF 读取接收缓冲区数据
				if(read()>0)
				{
					setRxok(true);
					handleRead();
				}
				
				// BY ZF 执行登录流程（状态机）
				// 目前只实现0x00配置请求，其他步骤待协议明确后实现
				if(loginState != LOGIN_STATE_SUCCESS)
				{
					// BY ZF 根据当前状态执行相应的登录步骤
					bool needRetry = false;
					if(lastTxLoginTime == 0 || abs(getSecTimer - lastTxLoginTime) >= 10)
						needRetry = true;
					
					if(needRetry)
					{
						switch(loginState)
						{
							case LOGIN_STATE_NONE:
							case LOGIN_STATE_CONFIG_REQ:
								// BY ZF 步骤1：发送配置请求(0x00)
								txConfigSetAsk();
								loginState = LOGIN_STATE_CONFIG_REQ;
								lastTxLoginTime = getSecTimer;
								ldebug << "Login step 1: Config request (0x00) sent";
								break;
							case LOGIN_STATE_CONFIG_OK:
								// BY ZF 步骤2：发送签到请求(0x20) - TODO: 待实现
								txSignInRequest();
								loginState = LOGIN_STATE_SIGNIN_REQ;
								lastTxLoginTime = getSecTimer;
								break;
							case LOGIN_STATE_SIGNIN_OK:
								// BY ZF 步骤3：发送登录认证请求(0xd0) - TODO: 待实现
								txLoginAuthRequest();
								loginState = LOGIN_STATE_AUTH_REQ;
								lastTxLoginTime = getSecTimer;
								break;
							// case LOGIN_STATE_SIGNIN_REQ:
							// 	// BY ZF 等待签到响应，超时后重新发送 - TODO: 待实现
							// 	if(needRetry)
							// 	{
							// 		txSignInRequest();
							// 		lastTxLoginTime = getSecTimer;
							// 	}
							// 	break;
							// case LOGIN_STATE_AUTH_REQ:
							// 	// BY ZF 等待登录认证响应，超时后重新发送 - TODO: 待实现
							// 	if(needRetry)
							// 	{
							// 		txLoginAuthRequest();
							// 		lastTxLoginTime = getSecTimer;
							// 	}
							// 	break;
							default:
								break;
						}
					}
				}
				
				// BY ZF 处理待发送数据（只有在登录成功后才发送心跳等数据）
				if(loginState >= LOGIN_STATE_SIGNIN_REQ)
				{
				handleWrite();
				}
				
				// BY ZF 发送数据
				write();
			}
			Sleep(10);
		}
		catch(...)
		{
			// BY ZF 捕获异常，断开连接并重置状态
			disconn();
			setRxok(false);
		}
	}
}

// BY ZF 处理接收到的数据：解析新协议格式的报文
void CSmartCityPlat::handleRead()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	uchar buf[2048];
	
	// BY ZF 新协议最小报文长度：前导字(4) + 版本(1) + 时间戳(8) + 长度(2) = 15字节
	// 至少需要读取到长度字段才能知道完整报文长度
	while(rxbuf->getLen() >= 15)
	{
		// BY ZF 标记读取位置，用于可能的回退
		rxbuf->markReadp();
		
		// BY ZF 读取报文头（至少15字节：前导字+版本+时间戳+长度）
		int len = rxbuf->getData(buf, 15);
		
		// BY ZF 检查前导字：0xF5 0xE2 0xF3 0x06
		if(buf[0] != 0xF5 || buf[1] != 0xE2 || buf[2] != 0xF3 || buf[3] != 0x06)
		{
			rxbuf->clearUnread();
			// BY ZF 尝试查找下一个前导字
			bool found = false;
			while(rxbuf->getLen() >= 4)
			{
				rxbuf->markReadp();
				uchar test[4];
				rxbuf->getData(test, 4);
				if(test[0] == 0xF5 && test[1] == 0xE2 && test[2] == 0xF3 && test[3] == 0x06)
				{
					found = true;
					rxbuf->resetReadp(); // 回退，让下次循环处理
					break;
				}				
				rxbuf->clearUnread();
				// BY ZF 丢弃一个字节继续查找
				rxbuf->getData(buf, 1);
			}
			if(!found)
			{
				ldebug << "Invalid frame header, expected 0xF5 0xE2 0xF3 0x06";
				return;
			}
			continue;
		}
		
		
		// BY ZF 读取时间戳（8字节BCD码）
		uchar timestamp[8];
		memcpy(timestamp, buf + 5, 8);
		
		// BY ZF 获取长度字段（在时间戳之后）
		int totalLen = makeushort(buf + 13); // 跳过前导字(4) + 版本(1) + 时间戳(8)
		
		// BY ZF 检查是否有足够的数据接收完整报文
		// 总长度 = 前导字(4) + 版本(1) + 时间戳(8) + 长度(2) + totalLen
		int expectedLen = 4 + 1 + 8 + 2 + totalLen;
		if(rxbuf->getLen() < expectedLen)
		{
			rxbuf->resetReadp();
			return;
		}			
		
		// BY ZF 读取完整报文
		rxbuf->resetReadp();
		rxbuf->getData(buf, expectedLen);
		
		// BY ZF 使用帧解封函数解析报文（协议使用大端序）
		uchar cmd;
		MyBuffer infoBody(true, 512);
		if(parseProtocolFrame(buf, expectedLen, &cmd, &infoBody))
		{
			// BY ZF 收到任何有效帧都更新接收时间，防止超时断开连接
			lastRxtime = getSecTimer;
			// BY ZF 更新心跳接收时间（任何平台下发的报文都会更新此时间）
			lastRxHeartbeatTime = getSecTimer;
			
			// BY ZF 根据命令字进行处理
			switch(cmd)
			{
				case 0x10: // 配置确认（0x10，对应配置请求0x00）
				{
					// BY ZF 处理配置确认 - TODO: 后续根据协议文档实现
					rxConfigSetConfirm(&infoBody);
							break;
						}
				case 0x30: // 签到应答（0x30，对应签到请求0x20）- TODO: 待实现
				{
					// BY ZF 处理签到应答 - TODO: 后续根据协议文档实现
					rxSignInResponse(&infoBody);
						break;
				}
				case 0xd1: // 登录认证结果（0xd1，对应登录认证请求0xd0）- TODO: 待实现
				{
					// BY ZF 处理登录认证结果 - TODO: 后续根据协议文档实现
					rxLoginAuthResult(&infoBody);
					break;
				}
				case 0x31: // 心跳包应答（0x31）
				{
					if(infoBody.getLen() >= 4)
					{
						uchar result = infoBody.getUByte(); // 反馈结果
						ushort errorReason = infoBody.getUShort(); // 异常原因（2字节）
						uchar heartbeatCount = infoBody.getUByte(); // 心跳计数
						
						if(result == 0x00)
						{
							ldebug << "Heartbeat response (0x31): Success, count=" << (int)heartbeatCount;
						}
						else if(result == 0x01)
						{
							ldebug << "Heartbeat response (0x31): Failed, count=" << (int)heartbeatCount;
						}
						else if(result == 0x03)
						{
							ldebug << "Heartbeat response (0x31): Server exception, count=" << (int)heartbeatCount;
						}
						
						// BY ZF 如果平台下发心跳间隔和超时次数，后续在此处理
					}
					break;
				}
				case 0x14: // 扫码后账户信息推送（启动充电，0x14）
				{
					// BY ZF 处理启动充电请求
					rxRemoteStart(&infoBody);
					break;
				}
				case 0x15: // 移动端停止充电请求（0x15）
				{
					// BY ZF 处理停止充电请求
					rxRemoteStop(&infoBody);
					break;
				}
				case 0x32: // 充电信息应答（0x32）无需响应
				{
					break;
				}
				case 0x70: // 充电记录包请求应答（0x70）
				{
					// BY ZF 处理充电记录上送应答
					rxChargeRecordResponse(&infoBody);
					break;
				}
				default:
				{
					char szCmd[20];
					sprintf(szCmd, "0x%02x", cmd);
					ldebug << "Unhandled command: " << szCmd;
					break;
				}
			}
		}
		else
		{
			ldebug << "Failed to parse protocol frame";
		}
	}
}

// BY ZF 处理待发送的数据：定期发送心跳，后续可扩展其他数据发送
void CSmartCityPlat::handleWrite()
{
	// BY ZF 使用平台下发的心跳间隔（单位：秒），若无效则默认30秒
	int interval = platDownloadParams.heartbeatInterval;
	if(interval <= 0)
		interval = 30;
	// 防止过小间隔导致频繁发送，最小1秒
	if(interval < 1)
		interval = 1;

	if(abs(getSecTimer-lastTxHeart) >= interval/2)
	{
		txHeartbeatActive();
		lastTxHeart=getSecTimer;
	}
	
	// BY ZF 检查远程控制状态变化并发送应答
	checkRemoteControlStatus();
	
	// BY ZF 检查并上送充电记录（每30秒只处理一次）
	if(abs(getSecTimer - lastChargeRecordUploadTime) >= 30)
	{
		checkChargeRecordUpload();
		lastChargeRecordUploadTime = getSecTimer;
	}
	
	// BY ZF 后续可在此添加其他数据发送逻辑
	// 例如：状态变化上报等
}





// BY ZF 发送心跳激活帧（心跳包请求 0x21）
void CSmartCityPlat::txHeartbeatActive()
{
	// BY ZF 构建心跳包信息体
	MyBuffer infoBody(true, 256);
	
	uint chargerId = platDownloadParams.cdzId;
	infoBody.putUInt(chargerId);
	
	// 2. 充电桩类型（1字节）：按协议版本03后的定义，0x07=电池检测混充电桩
	extern unsigned int g_charger_type;
	uchar chargerType = 0x07;
	if(g_charger_type == AC_CHARGER_TYPE)
	{
		chargerType = 0x02; // 交流设备
	}
	else
	{
		chargerType = 0x01; // 默认直流设备
	}
	infoBody.putUByte(chargerType);
	
	// 3. 充电枪数量（1字节）
	uchar gunCount = platDownloadParams.gunCount;
	// BY ZF 如果gunCount为0，使用MAX_DC_CHARGER_NUM作为后备（从配置文件读取的枪数量）
	if(gunCount == 0)
	{
		gunCount = MAX_DC_CHARGER_NUM;
	}
	infoBody.putUByte(gunCount);
	// 4~：逐枪信息
	for(int i = 0; i < gunCount; i++)
	{
		uint gunIdVal = 0;
		if(i < MAX_PLAT_GUN_NUM && platDownloadParams.gunId[i] != 0)
			gunIdVal = platDownloadParams.gunId[i];
		else if(i < MAX_PLAT_GUN_NUM)
			gunIdVal = plugIdArray[i];
		infoBody.putUInt(gunIdVal);
		uchar gunTypeVal = 0;
		if(i < MAX_PLAT_GUN_NUM)
			gunTypeVal = platDownloadParams.gunType[i];
		if(gunTypeVal == 0)
		{
			gunTypeVal = (g_charger_type == AC_CHARGER_TYPE) ? 0x02 : 0x01;
		}
		infoBody.putUByte(gunTypeVal);
		
		// 充电枪状态
		uchar gunStatus = 0x00; // 默认空闲
		if(i < MAX_DC_CHARGER_NUM)
		{
			INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + i;

			if(evse->error)
			{
				gunStatus = 0x03; // 故障
			}

			else if(evse->charging)
			{
				gunStatus = 0x02; // 充电中
			}
			else if(evse->cable)
			{
				gunStatus = 0x01; // 已连接
			}


			infoBody.putUByte(gunStatus);
			// 车位检测仪状态（按用户要求固定0xFF表示未配置）
			infoBody.putUByte(0xFF);
			// 车辆停车状态（按用户要求固定0x02）
			infoBody.putUByte(0x02);
			// 地锁状态（按用户要求固定0xFF）
			infoBody.putUByte(0xFF);
			// 车辆N连接状态（0未连接，1已连接）
			infoBody.putUByte(evse->cable ? 0x01 : 0x00);
		}
		else
		{
			infoBody.putUByte(0x00);
			infoBody.putUByte(0xFF);
			infoBody.putUByte(0x02);
			infoBody.putUByte(0xFF);
			infoBody.putUByte(0x00);
		}
	}
	
	// 19. 心跳计数
	infoBody.putUByte(heartbeatCounter);
	heartbeatCounter++;
	
	// 20. 信号强度：暂用0x11（默认值，可根据实际信号填充0~31）
	infoBody.putUByte(0x11);
	
	// 21. 能源路由器状态：未配置时填0xFF
	infoBody.putUByte(0xFF);
	
	// 封装并发送
	buildProtocolFrame(0x21, &infoBody);
	ldebug << "tx heartbeat active (0x21): chargerId=" << chargerId << ", gunCount=" << (int)gunCount;
}

// // BY ZF 发送心跳确认帧（如果平台要求桩端发送确认，使用此函数）
// void CSmartCityPlat::txHeartbeatConfirm()
// {
// 	// BY ZF 设置 logid 供 ldebug 宏使用
// 	char* logid = this->logid;
	
// 	// BY ZF 注意：根据协议文档，心跳确认(0x31)通常是平台下发给桩端的
// 	// 桩端一般不需要发送确认，此函数保留用于特殊需求
// 	ldebug << "txHeartbeatConfirm: Not used in standard protocol";
// }

// BY ZF 配置请求帧（命令字0x00）
void CSmartCityPlat::txConfigSetAsk()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 构建配置请求信息体（协议使用大端序）
	MyBuffer infoBody(true, 512);
	
	// 1. 充电桩MAC地址（12字节）
	// BY ZF 使用类成员变量macAddrBytes（已在构造函数中从配置文件读取并转换）
	infoBody.putData(macAddrBytes, 12);
	
	// 2. 充电桩类型（1字节）
	// BY ZF 协议版本03及以后：0x01=直流设备，0x02=交流设备
	extern unsigned int g_charger_type;
	uchar chargerType = 0x01; // 默认直流设备
	if(g_charger_type == AC_CHARGER_TYPE)
		chargerType = 0x02; // 交流设备
	else
		chargerType = 0x01; // 直流设备
	infoBody.putUByte(chargerType);
	
	// 3. 充电枪数量（1字节）
	uchar gunCount = MAX_DC_CHARGER_NUM;
	infoBody.putUByte(gunCount);
	
	// 4. 循环添加每个枪的信息（枪ID 4字节 + 枪类型 1字节）
	for(int iGun = 0; iGun < MAX_DC_CHARGER_NUM; iGun++)
	{
		// 4.1 充电桩N枪ID（4字节，大端序：高字节在前）
		// BY ZF 使用类成员变量plugIdArray（已在构造函数中从配置文件读取）
		// MyBuffer已配置大端序，putUInt会自动按大端序输出
		// infoBody.putUInt(plugIdArray[iGun]);
		infoBody.putUInt(0xffffffff);
		
		// 4.2 充电枪N类型（1字节）
		// BY ZF 协议版本03及以后：0x01=直流设备，0x02=交流设备
		// 直流枪使用0x01
		uchar gunType = 0x01; // 直流枪
		infoBody.putUByte(gunType);
	}
	
	// 5. 是否支持加密（1字节）：0x00=支持，0x01=不支持
	uchar encryptSupport = 0x01; // 不支持加密
	infoBody.putUByte(encryptSupport);
	
	// BY ZF 使用帧封装函数构建完整报文并自动发送（命令字0x00）
	buildProtocolFrame(0x00, &infoBody);
	ldebug << "tx config set ask (0x00): type=" << (int)chargerType << ", gunCount=" << (int)gunCount;
}

// BY ZF 配置确认处理（命令字0x10）：解析平台下发的配置参数
void CSmartCityPlat::rxConfigSetConfirm(MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 按照协议帧定义顺序解析
	
	// 0. 反馈结果（1字节）：平台对0x00帧提交信息的鉴权结果
	// 0x00=成功，0x01=失败
	uchar feedbackResult = infoBody->getUByte();
	if(feedbackResult == 0x00 && loginState == LOGIN_STATE_CONFIG_REQ)
	{
		loginState = LOGIN_STATE_CONFIG_OK;
	}
	
	// BY ZF 继续解析配置参数
	
	// 1. 充电桩ID（4字节）- 保存平台下发的充电桩ID
	platDownloadParams.cdzId = infoBody->getUInt();
	// BY ZF 将平台下发的充电桩ID保存到配置文件和全局变量
	write_profile_int("互动化平台", "集中器地址", platDownloadParams.cdzId, "cui.ini");
	g_hmi_info->cevhost_ini.jzqAddr = platDownloadParams.cdzId;
	
	// 2. 充电桩类型（1字节）- 跳过，不需要处理
	infoBody->getUByte();
	
	// 3. 心跳间隔周期（1字节，秒）
	platDownloadParams.heartbeatInterval = infoBody->getUByte();
	
	// 4. 心跳超时检测次数（1字节）
	platDownloadParams.heartbeatTimeoutCount = infoBody->getUByte();
	
	// 5. 充电信息监测数据间隔周期（1字节，秒）
	platDownloadParams.chargeInfoInterval = infoBody->getUByte();
	
	// 6. 充电信息监测检测次数（1字节）
	platDownloadParams.chargeInfoTimeoutCount = infoBody->getUByte();
	
	// 7. 充电中车监测数据间隔周期（1字节，秒）
	platDownloadParams.chargeVehicleInterval = infoBody->getUByte();
	
	// 8. 充电中车监测检测次数（1字节）
	platDownloadParams.chargeVehicleTimeoutCount = infoBody->getUByte();
	
	// 9. 脱机联机标志（1字节）- 平台下发“2”表示必须在线充电
	uchar onlineMode = infoBody->getUByte();
	platDownloadParams.onlineMode = onlineMode;
	if(onlineMode == 0x02)
	{
		g_bcu_info->set_info.online_flag = 1; // BY ZF 平台要求在线启动
	}
	else
	{
		g_bcu_info->set_info.online_flag = 0; // BY ZF 其他值视为离线模式关闭
	}
 
	// 10. 时段数（1字节）- TODO: 后续完善计费模型保存功能
	uchar numOfTimePeriods = infoBody->getUByte();
	
	// TODO: 计费模型相关字段（时段费率等）后续完善保存功能
	// 包括：时段数、时段费率1-N（每个包含开始时间、结束时间、电费、服务费等）
	
	// 11. 时段费率1-N（每个6字节：开始时间、结束时间、电费、服务费）- TODO: 后续完善计费模型保存功能
	// BY ZF 跳过时段费率数据：时段数 * 6字节
	uchar skipBuffer[6];
	for(int i = 0; i < numOfTimePeriods; i++)
	{
		infoBody->getData(skipBuffer, 6);
	}
	
 	// 12. 充电枪数量（1字节）以及后续每把枪的参数
	uchar gunCount = infoBody->getUByte();
 	platDownloadParams.gunCount = gunCount;
 	memset(platDownloadParams.gunId, 0, sizeof(platDownloadParams.gunId));
 	memset(platDownloadParams.gunType, 0, sizeof(platDownloadParams.gunType));
	for(int i = 0; i < gunCount; i++)
	{
 		uint gunIdVal = infoBody->getUInt();
 		uchar gunTypeVal = infoBody->getUByte();
 		if(i < MAX_PLAT_GUN_NUM)
 		{
 			platDownloadParams.gunId[i] = gunIdVal;
 			platDownloadParams.gunType[i] = gunTypeVal;
 			// BY ZF 将平台下发的枪ID保存到配置文件和全局变量
 			char keyName[32];
 			sprintf(keyName, "枪%dID", i+1);
 			write_profile_int("互动化平台", keyName, gunIdVal, "cui.ini");
			// BY ZF 同步更新全局变量，供其他模块使用
			if(i < MAX_DC_CHARGER_NUM)
			{
				g_hmi_info->cevhost_ini.plugId[i] = gunIdVal;
			}
		}
	}

	// 16. 运营商ID（9字节16进制ASCII码）
	if(infoBody->getData((uchar*)platDownloadParams.operatorId, 9) == 9)
	{
		platDownloadParams.operatorId[9] = '\0'; // 确保字符串结束
	}
	
	// BY ZF 以下三个字段暂时不做解析，直接注释掉
	// 17. 设备终端文件上传地址（128字节ASCII）
	// if(infoBody->getData((uchar*)platDownloadParams.uploadUrl, 128) == 128)
	// {
	// 	platDownloadParams.uploadUrl[127] = '\0'; // 确保字符串结束
	// }
	
	// 18. 上传密钥SKey（32字节ASCII）
	// if(infoBody->getData((uchar*)platDownloadParams.uploadSKey, 32) == 32)
	// {
	// 	platDownloadParams.uploadSKey[31] = '\0'; // 确保字符串结束
	// }
	
	// 19. 上传文件参数名称（10字节ASCII）
	// if(infoBody->getData((uchar*)platDownloadParams.uploadParamName, 10) == 10)
	// {
	// 	platDownloadParams.uploadParamName[9] = '\0'; // 确保字符串结束
	// }
	
	// BY ZF 跳过这三个字段的字节数（128 + 32 + 10 = 170字节）
	uchar skipData[170];
	infoBody->getData(skipData, 170);
	
	// BY ZF 所有参数解析完成后，如果反馈结果为成功，将"首次初始化"标识更新为0（已初始化）
	// BY ZF 这样下次启动时就可以跳过配置请求，直接发送签到请求
	if(feedbackResult == 0x00)
	{
		// write_profile_int("互动化平台", "首次初始化", 0, "cui.ini");
		// ldebug << "Config confirmed successfully, all parameters parsed, set first init flag to 0";
	}
	
	ldebug << "rxConfigSetConfirm: heartbeatInterval=" << (int)platDownloadParams.heartbeatInterval;
	std::ostringstream summary;
	summary << "rxConfigSetConfirm summary: result=" << (int)feedbackResult
		<< ", cdzId=" << platDownloadParams.cdzId
		<< ", heartbeatInterval=" << (int)platDownloadParams.heartbeatInterval
		<< ", heartbeatTimeoutCount=" << (int)platDownloadParams.heartbeatTimeoutCount
		<< ", chargeInfoInterval=" << (int)platDownloadParams.chargeInfoInterval
		<< ", chargeInfoTimeoutCount=" << (int)platDownloadParams.chargeInfoTimeoutCount
		<< ", chargeVehicleInterval=" << (int)platDownloadParams.chargeVehicleInterval
		<< ", chargeVehicleTimeoutCount=" << (int)platDownloadParams.chargeVehicleTimeoutCount
		<< ", onlineMode=" << (int)platDownloadParams.onlineMode
		<< ", onlineFlag=" << g_bcu_info->set_info.online_flag
		<< ", gunCount=" << (int)platDownloadParams.gunCount
		<< ", operatorId=" << platDownloadParams.operatorId
		<< ", uploadUrl=" << platDownloadParams.uploadUrl
		<< ", uploadSKey=" << platDownloadParams.uploadSKey
		<< ", uploadParamName=" << platDownloadParams.uploadParamName;
	ldebug << summary.str().c_str();
	if(platDownloadParams.gunCount > 0)
	{
		std::ostringstream gunStream;
		for(int i = 0; i < platDownloadParams.gunCount && i < MAX_PLAT_GUN_NUM; i++)
		{
			gunStream << " [" << i << "]id=" << platDownloadParams.gunId[i]
				<< ",type=" << (int)platDownloadParams.gunType[i];
		}
		ldebug << "rxConfigSetConfirm guns:" << gunStream.str().c_str();
	}
	
	// BY ZF 生成二维码字符串：hlht://[枪ID].[运营商ID]/[MAC地址]
	// BY ZF 将MAC地址（12字节）转换为24字符的16进制ASCII字符串
	char macAddrStr[25] = {0}; // 24字符 + 结束符
	for(int i = 0; i < 12; i++)
	{
		uchar byteVal = macAddrBytes[i];
		uchar high = (byteVal >> 4) & 0x0F;
		uchar low = byteVal & 0x0F;
		macAddrStr[i * 2] = (high < 10) ? ('0' + high) : ('A' + high - 10);
		macAddrStr[i * 2 + 1] = (low < 10) ? ('0' + low) : ('A' + low - 10);
	}
	macAddrStr[24] = '\0';
	
	// BY ZF 为每把枪生成二维码字符串并存储到pole_ini.poleId
	for(int i = 0; i < platDownloadParams.gunCount && i < MAX_PLAT_GUN_NUM && i < MAX_DC_CHARGER_NUM; i++)
	{
		// BY ZF 格式：hlht://[枪ID].[运营商ID]/[MAC地址]
		char qrCodeStr[80] = {0}; // poleId字段大小为80字节
		snprintf(qrCodeStr, sizeof(qrCodeStr), "hlht://%u.%s/%s", 
			platDownloadParams.gunId[i], 
			platDownloadParams.operatorId, 
			macAddrStr);
		
		// BY ZF 存储到pole_ini.poleId（用于显示二维码）
		strncpy(g_hmi_info->bcu_info_to_hmi[i].pole_ini.poleId, qrCodeStr, sizeof(g_hmi_info->bcu_info_to_hmi[i].pole_ini.poleId) - 1);
		g_hmi_info->bcu_info_to_hmi[i].pole_ini.poleId[sizeof(g_hmi_info->bcu_info_to_hmi[i].pole_ini.poleId) - 1] = '\0';
		
		ldebug << "rxConfigSetConfirm: Generated QR code for gun " << i 
			<< " (gunId=" << platDownloadParams.gunId[i] << "): " << qrCodeStr;
	}
}

// BY ZF 签到请求（命令字0x20）
void CSmartCityPlat::txSignInRequest()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 构建签到请求信息体（协议使用大端序）
	MyBuffer infoBody(true, 512);
	
	// 1. 充电桩ID（4字节，使用平台下发的地址）
	infoBody.putUInt(platDownloadParams.cdzId);
	
	// 2. 充电桩类型（1字节）：0x01=直流设备
	infoBody.putUByte(0x01);
	
	// 3. 充电枪数量（1字节）
	uchar gunCount = MAX_DC_CHARGER_NUM;
	infoBody.putUByte(gunCount);
	
	// 4. 固件版本号（2字节，如258表示V2.58）
	infoBody.putUShort(PLAT_FIRMWARE_VERSION);
	
	// 5. 循环添加每把枪的信息（枪ID 4字节 + 枪状态 1字节）
	for(int iGun = 0; iGun < MAX_DC_CHARGER_NUM; iGun++)
	{
		// 5.1 充电桩N枪ID（4字节，大端序：高字节在前）
		// BY ZF 使用类成员变量plugIdArray（已在构造函数中从配置文件读取）
		// MyBuffer已配置大端序，putUInt会自动按大端序输出
		infoBody.putUInt(plugIdArray[iGun]);
		
		// 5.2 充电枪N状态（1字节）：0x00=空闲，0x01=连接，0x02=工作，0x03=故障
		INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + iGun;
		uchar gunStatus = 0x00; // 默认空闲
		if(evse->error)
		{
			gunStatus = 0x03; // 故障
		}
		else if(evse->cable)
		{
			if(evse->charging)
			{
				gunStatus = 0x02; // 工作（充电中，此时必然已插枪）
		}
		else
		{
				gunStatus = 0x01; // 连接（已插枪但未充电）
			}
	}
	else
	{
			gunStatus = 0x00; // 空闲
		}
		infoBody.putUByte(gunStatus);
	}
	
	// 6. DTU版本号（3字节BCD码，如V1.1.1 -> 0x010101）
	uchar dtuVersion[3] = {0x01, 0x01, 0x01}; // 默认V1.1.1
	infoBody.putData(dtuVersion, 3);
	
	// 7. 桩企信用编码（9字节ASCII码）
	// BY ZF 使用类成员变量factoryCreditCode（已在构造函数中从配置文件读取）
	infoBody.putData(factoryCreditCode, 9);
	
	// 8. SIM卡ICCID（20字节ASCII）
	// BY ZF 默认清零，若能获取到SIM卡号则写入
	uchar simIccid[20] = {0};
#ifndef WIN32
	// BY ZF 调用TCU库获取SIM卡号
	char simBuf[64] = {0};
	int simLen = 0;
	if(GetSimNum(simBuf, &simLen) == ERROR_OK && simLen > 0)
	{
		// BY ZF 拷贝前20个字节，多余部分截断，不足部分保持0填充
		int copyLen = simLen < 20 ? simLen : 20;
		memcpy(simIccid, simBuf, copyLen);
	}
#endif
	infoBody.putData(simIccid, 20);
	
	// BY ZF 使用帧封装函数构建完整报文并自动发送（命令字0x20）
	buildProtocolFrame(0x20, &infoBody);
	ldebug << "tx sign in request (0x20): pileId=" << jzqAddr << ", type=0x01, gunCount=" << (int)gunCount;
}

// BY ZF 签到应答处理（命令字0x30）
void CSmartCityPlat::rxSignInResponse(MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 按照协议帧定义顺序解析
	
	// 1. 反馈结果（1字节）：0x00=成功，0x01=失败，0x02=枪数量不匹配，0x03=服务器处理异常
	uchar feedbackResult = infoBody->getUByte();
	if(feedbackResult == 0x00 && loginState == LOGIN_STATE_SIGNIN_REQ)
	{
		// loginState = LOGIN_STATE_SIGNIN_OK;
		loginState = LOGIN_STATE_SUCCESS;
		ldebug << "rxSignInResponse: feedback result=0x00 (success), loginState -> SIGNIN_OK";
		}
		else
		{
		ldebug << "rxSignInResponse: feedback result=" << (int)feedbackResult << " (failure)";
	}
	
	// 2. 异常原因（2字节）
	ushort exceptionReason = infoBody->getUShort();
	if(exceptionReason != 0x0000)
	{
		char szReason[20];
		sprintf(szReason, "0x%04x", exceptionReason);
		ldebug << "rxSignInResponse: exception reason=" << szReason;
	}
	
	// 3. 校时时间（8字节BCD码：YYYYMMDDHHmmssFF）
	uchar bcdTime[8];
	infoBody->getData(bcdTime, 8);
	
	// BY ZF 将BCD码转换为CFETime结构并校时
	CFETime ct;
	ct.ms = 0; // BCD时间中没有毫秒信息，设为0
	// BY ZF BCD解码：每个字节的高4位和低4位分别是十位和个位
	ct.year = ((bcdTime[0] >> 4) & 0x0F) * 1000 + 
	          (bcdTime[0] & 0x0F) * 100 + 
	          ((bcdTime[1] >> 4) & 0x0F) * 10 + 
	          (bcdTime[1] & 0x0F);
	ct.month = ((bcdTime[2] >> 4) & 0x0F) * 10 + (bcdTime[2] & 0x0F);
	ct.day = ((bcdTime[3] >> 4) & 0x0F) * 10 + (bcdTime[3] & 0x0F);
	ct.hour = ((bcdTime[4] >> 4) & 0x0F) * 10 + (bcdTime[4] & 0x0F);
	ct.minute = ((bcdTime[5] >> 4) & 0x0F) * 10 + (bcdTime[5] & 0x0F);
	ct.sec = ((bcdTime[6] >> 4) & 0x0F) * 10 + (bcdTime[6] & 0x0F);
	
	// BY ZF 调用系统校时函数（参考cev5500.cpp的实现）
	setSystime(ct);
	ldebug << "rxSignInResponse: time sync " << ct.year << "-" << ct.month << "-" << ct.day 
		<< " " << ct.hour << ":" << ct.minute << ":" << ct.sec;
	
}

// BY ZF 登录认证请求（命令字0xd0）- 待实现
void CSmartCityPlat::txLoginAuthRequest()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF TODO: 后续根据协议文档实现登录认证请求
	ldebug << "txLoginAuthRequest (0xd0) - TODO: Not implemented yet";
}

// BY ZF 登录认证结果处理（命令字0xd1）- 待实现
void CSmartCityPlat::rxLoginAuthResult(MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF TODO: 后续根据协议文档实现登录认证结果处理
	ldebug << "rxLoginAuthResult (0xd1) - TODO: Not implemented yet";
}

// BY ZF 远程启动充电处理（命令字0x14：扫码后账户信息推送）
void CSmartCityPlat::rxRemoteStart(MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 按照协议0x14帧定义顺序解析（扫码后账户信息推送，每次只能单枪请求）
	// 协议格式：
	// 1. 充电桩X枪ID (4字节)
	// 2. 订单ID (10字节BCD码)
	// 3. 用户ID (5字节BCD码)
	// 4. 用户状态 (1字节): 0x00=正常, 0x01=锁定
	// 5. 能量块数量 (4字节): 分辨率0.01，如100表示1元
	// 6. 计费标志 (1字节): 0x00=充电桩计费, 0x01=充电桩不计费
	// 7. 时段数 (1字节)
	// 8. 时段费率1-N (每个6字节): 开始时间(1) + 结束时间(1) + 电价(2) + 服务费(2)
	
	// BY ZF 检查信息体最小长度
	const int MIN_DATA_SIZE = 4 + 10 + 5 + 1 + 4 + 1 + 1; // 枪ID + 订单ID + 用户ID + 用户状态 + 能量块 + 计费标志 + 时段数 = 26字节
	if(infoBody->getLen() < MIN_DATA_SIZE)
	{
		ldebug << "rxRemoteStart (0x14): InfoBody too short, len=" << infoBody->getLen() << ", expected at least " << MIN_DATA_SIZE;
		return;
	}
	
	// 1. 读取枪ID（4字节）
	uint gunId = infoBody->getUInt();
	
	// 2. 读取订单ID（10字节BCD码）
	uchar orderId[10];
	infoBody->getData(orderId, 10);
	
	// 3. 读取用户ID（5字节BCD码）
	uchar userId[5];
	infoBody->getData(userId, 5);
	
	// 4. 读取用户状态（1字节）
	uchar userStatus = infoBody->getUByte();
	
	// 5. 读取能量块数量（4字节）
	uint energyBlock = infoBody->getUInt();
	
	// 6. 读取计费标志（1字节）
	uchar billingFlag = infoBody->getUByte();
	
	// 7. 读取时段数（1字节）
	uchar periodCount = infoBody->getUByte();
	
	// BY ZF 根据枪ID查找对应的充电口索引（需要在保存时段数据之前确定chargeAddrNo）
	int chargeAddrNo = -1;
	for(int i = 0; i < platDownloadParams.gunCount && i < MAX_PLAT_GUN_NUM && i < MAX_DC_CHARGER_NUM; i++)
	{
		if(platDownloadParams.gunId[i] == gunId)
		{
			chargeAddrNo = i;
			break;
		}
	}
	
	if(chargeAddrNo < 0)
	{
		ldebug << "rxRemoteStart (0x14): Gun ID " << gunId << " not found, ignored";
		// BY ZF 即使找不到枪，也需要发送失败应答
		// TODO: 发送失败应答
		// BY ZF 跳过时段数据（因为找不到对应的充电口）
		for(int i = 0; i < periodCount; i++)
		{
			if(infoBody->getLen() >= 6)
			{
				uchar skipData[6];
				infoBody->getData(skipData, 6);
			}
		}
		return;
	}
	
	// 8. 读取时段费率1-N（每个6字节）
	// BY ZF 保存计费模型（供外部计算模块使用）
	// BY ZF 每个时段包含：开始时间(1) + 结束时间(1) + 电价(2) + 服务费(2) = 6字节
	// BY ZF 最多支持24个时段
	uchar periodCountToSave = (periodCount > 24) ? 24 : periodCount;
	BILLING_MODEL* billingModel = &(remoteCtrlPending[chargeAddrNo].billingModel);
	billingModel->periodCount = periodCountToSave;
	
	for(int i = 0; i < periodCountToSave; i++)
	{
		if(infoBody->getLen() >= 6)
		{
			uchar periodData[6];
			infoBody->getData(periodData, 6);
			
			// BY ZF 解析并存储到计费模型结构体
			billingModel->periodStartTime[i] = periodData[0]; // 开始时间（BCD码）
			billingModel->periodEndTime[i] = periodData[1];   // 结束时间（BCD码）
			// BY ZF 电价（2字节，单位0.01元，协议使用大端序：高字节在前）
			billingModel->periodChargeFee[i] = (ushort)((periodData[2] << 8) | periodData[3]);
			// BY ZF 服务费（2字节，单位0.01元，协议使用大端序：高字节在前）
			billingModel->periodServiceFee[i] = (ushort)((periodData[4] << 8) | periodData[5]);
		}
		else
		{
			ldebug << "rxRemoteStart (0x14): Not enough data for period " << i;
			break;
		}
	}
	// BY ZF 如果还有多余的时段数据，跳过
	for(int i = periodCountToSave; i < periodCount; i++)
	{
		if(infoBody->getLen() >= 6)
		{
			uchar skipData[6];
			infoBody->getData(skipData, 6);
		}
	}
	
	// BY ZF 初始化实际费用数据（等待外部计算模块回传）
	memset(&(remoteCtrlPending[chargeAddrNo].chargeFeeData), 0, sizeof(CHARGE_FEE_DATA));
	remoteCtrlPending[chargeAddrNo].chargeFeeData.periodCount = periodCountToSave;
	
	// BY ZF 保存订单ID到LOGIC_RAM（用于后续上报）
	memcpy(port_charge_info[chargeAddrNo].tradeID, orderId, 10);
	
	// BY ZF 保存能量块（卡余额）到M1_CARD_INFO
	// BY ZF 能量块数量：分辨率0.01，如100表示1元，单位是"分"（100分=1元）
	// BY ZF card_balance的单位也是"分"，所以可以直接赋值
	// BY ZF 使用bcu_info（在Global_Varible.h中定义为extern BCU_INFO bcu_info）
	extern BCU_INFO bcu_info;
	bcu_info.m1_card_info[chargeAddrNo].card_balance = energyBlock;
	ldebug << "rxRemoteStart (0x14): Saved energy block (card balance) " 
		<< (energyBlock * 0.01) << " yuan (" << energyBlock << " cents) to card_balance["
		<< chargeAddrNo << "]";
	
	// BY ZF 参考cev5500.cpp的方式，将接收到的消息组装成CHARGE_CONTROL_COMMAND帧
	// BY ZF 放入cmd_HOST_to_BCU队列，让cmd_interface.c的recv_cmd_from_HOST函数来处理
	extern CMD_INFO cmd_info;
	UNI_CMD_QUEUE* q = &(cmd_info.cmd_HOST_to_BCU);
	BYTE cmd_in_pos = q->cmd_in_pos;
	
	// BY ZF 设置命令类型为CHARGE_CONTROL_COMMAND (0x41)
	q->uni_cmd_interface[cmd_in_pos].cmd_type = CHARGE_CONTROL_COMMAND;
	q->uni_cmd_interface[cmd_in_pos].cmd_port_no = chargeAddrNo;
	
	// BY ZF 组装53字节的cmd_in_buf（43字节原有数据 + 10字节订单ID BCD）
	memset(q->uni_cmd_interface[cmd_in_pos].cmd_in_buf, 0, 53);
	
	// BY ZF cmd_in_buf[9] & 0x03 = 0x01 表示启动充电
	q->uni_cmd_interface[cmd_in_pos].cmd_in_buf[9] = 0x01;
	
	// BY ZF 将订单ID（10字节BCD）转换为16字节ASCII卡号，用于欺骗流程
	// BY ZF 每个BCD字节包含2个数字（高4位和低4位），10字节BCD可以转换为20个ASCII字符
	// BY ZF 但card_no字段只需要16字节，所以取前8字节BCD（16个数字）
	int cardNoPos = 0;  // BY ZF card_no字段的当前位置（cmd_in_buf[27]开始）
	for(int i = 0; i < 8 && cardNoPos < 16; i++)  // BY ZF 处理前8字节BCD（16个数字）
	{
		// BY ZF 提取BCD字节的两个数字
		uchar highDigit = (orderId[i] >> 4) & 0x0F;  // 高4位
		uchar lowDigit = orderId[i] & 0x0F;          // 低4位
		
		// BY ZF 转换为ASCII格式（数字+0x30），存储在cmd_in_buf[27+cardNoPos]
		if(cardNoPos < 16)
		{
			q->uni_cmd_interface[cmd_in_pos].cmd_in_buf[27 + cardNoPos] = highDigit + 0x30;
			cardNoPos++;
		}
		if(cardNoPos < 16)
		{
			q->uni_cmd_interface[cmd_in_pos].cmd_in_buf[27 + cardNoPos] = lowDigit + 0x30;
			cardNoPos++;
		}
	}
	// BY ZF 如果订单ID转换后不足16字节，用'0'（0x30）填充剩余部分
	while(cardNoPos < 16)
	{
		q->uni_cmd_interface[cmd_in_pos].cmd_in_buf[27 + cardNoPos] = 0x30;
		cardNoPos++;
	}
	
	// BY ZF 将订单ID（10字节BCD）直接存储到报文末尾（cmd_in_buf[43]开始）
	memcpy(&q->uni_cmd_interface[cmd_in_pos].cmd_in_buf[43], orderId, 10);
	
	// BY ZF 设置数据长度（43字节原有数据 + 10字节订单ID）
	q->uni_cmd_interface[cmd_in_pos].cmd_len = 53;
	
	// BY ZF 更新队列指针
	cmd_in_pos++;
	if(cmd_in_pos >= MAX_CMD_NUM)
		cmd_in_pos = 0;
	q->cmd_in_pos = cmd_in_pos;
	
	ldebug << "rxRemoteStart (0x14): Assembled CHARGE_CONTROL_COMMAND frame and queued to cmd_HOST_to_BCU";
	
	// BY ZF 记录待发送应答信息：等待charging状态变为1（充电中）
	remoteCtrlPending[chargeAddrNo].pending = true;
	remoteCtrlPending[chargeAddrNo].gunId = gunId;
	remoteCtrlPending[chargeAddrNo].controlCmd = 0x00; // 启动命令
	remoteCtrlPending[chargeAddrNo].expectedState = 1; // 期望状态：充电中
	INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + chargeAddrNo;
	remoteCtrlPending[chargeAddrNo].currentState = evse->charging; // 记录当前状态
	remoteCtrlPending[chargeAddrNo].result = 0x01; // 初始化为不成功，状态匹配后改为成功
	remoteCtrlPending[chargeAddrNo].waitStartTime = getSecTimer;
	
	// BY ZF 保存用户ID、订单ID、计费标志和时段信息到pending结构（用于应答和充电信息上报）
	memcpy(remoteCtrlPending[chargeAddrNo].orderId, orderId, 10);
	memcpy(remoteCtrlPending[chargeAddrNo].userId, userId, 5);
	remoteCtrlPending[chargeAddrNo].billingFlag = billingFlag;
	// BY ZF 注意：计费模型已在上面保存到billingModel，实际费用数据已初始化为0等待外部计算
	
	ldebug << "rxRemoteStart (0x14): Start charge command for gun ID " << gunId 
		<< " (chargeAddrNo=" << chargeAddrNo << "), userStatus=" << (int)userStatus 
		<< ", energyBlock=" << (energyBlock * 0.01) << ", billingFlag=" << (int)billingFlag
		<< ", periodCount=" << (int)periodCount << ", waiting for charging state change";
}

// BY ZF 远程停止充电处理（命令字0x15：移动端停止充电请求）
void CSmartCityPlat::rxRemoteStop(MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 按照协议0x15帧定义顺序解析（每次只能单枪请求）
	// 协议格式：
	// 1. 充电桩X枪ID (4字节)
	// 2. 订单ID (10字节BCD码)
	// 3. 操作类型 (1字节): 0x00=启动, 0x01=停止
	
	// BY ZF 检查信息体最小长度
	const int MIN_DATA_SIZE = 4 + 10 + 1; // 枪ID + 订单ID + 操作类型 = 15字节
	if(infoBody->getLen() < MIN_DATA_SIZE)
	{
		ldebug << "rxRemoteStop (0x15): InfoBody too short, len=" << infoBody->getLen() << ", expected at least " << MIN_DATA_SIZE;
		return;
	}
	
	// 1. 读取枪ID（4字节）
	uint gunId = infoBody->getUInt();
	
	// 2. 读取订单ID（10字节BCD码）
	uchar orderId[10];
	infoBody->getData(orderId, 10);
	
	// 3. 读取操作类型（1字节）
	uchar operationType = infoBody->getUByte();
	
	// BY ZF 验证操作类型
	if(operationType != 0x01)
	{
		ldebug << "rxRemoteStop (0x15): Invalid operation type 0x" << std::hex << (int)operationType 
			<< std::dec << " for gun ID " << gunId << ", expected 0x01 (Stop)";
		// BY ZF 即使操作类型错误，也需要发送失败应答
		// TODO: 发送失败应答
		return;
	}
	
	// BY ZF 根据枪ID查找对应的充电口索引
	int chargeAddrNo = -1;
	for(int i = 0; i < platDownloadParams.gunCount && i < MAX_PLAT_GUN_NUM && i < MAX_DC_CHARGER_NUM; i++)
	{
		if(platDownloadParams.gunId[i] == gunId)
		{
			chargeAddrNo = i;
			break;
		}
	}
	
	if(chargeAddrNo < 0)
	{
		ldebug << "rxRemoteStop (0x15): Gun ID " << gunId << " not found, ignored";
		// BY ZF 即使找不到枪，也需要发送失败应答
		// TODO: 发送失败应答
		return;
	}
	
	// BY ZF 参考cev5500.cpp的方式，将接收到的消息组装成CHARGE_CONTROL_COMMAND帧（停止充电）
	// BY ZF 放入cmd_HOST_to_BCU队列，让cmd_interface.c的recv_cmd_from_HOST函数来处理
	extern CMD_INFO cmd_info;
	UNI_CMD_QUEUE* q = &(cmd_info.cmd_HOST_to_BCU);
	BYTE cmd_in_pos = q->cmd_in_pos;
	
	// BY ZF 设置命令类型为CHARGE_CONTROL_COMMAND (0x41)
	q->uni_cmd_interface[cmd_in_pos].cmd_type = CHARGE_CONTROL_COMMAND;
	q->uni_cmd_interface[cmd_in_pos].cmd_port_no = chargeAddrNo;
	
	// BY ZF 组装43字节的cmd_in_buf（参考cmd_interface.c中memcpy的43字节）
	memset(q->uni_cmd_interface[cmd_in_pos].cmd_in_buf, 0, 43);
	
	// BY ZF cmd_in_buf[9] & 0x03 = 0x00 表示停止充电
	q->uni_cmd_interface[cmd_in_pos].cmd_in_buf[9] = 0x00;
	
	// BY ZF 设置数据长度
	q->uni_cmd_interface[cmd_in_pos].cmd_len = 43;
	
	// BY ZF 更新队列指针
	cmd_in_pos++;
	if(cmd_in_pos >= MAX_CMD_NUM)
		cmd_in_pos = 0;
	q->cmd_in_pos = cmd_in_pos;
	
	ldebug << "rxRemoteStop (0x15): Assembled CHARGE_CONTROL_COMMAND frame (stop) and queued to cmd_HOST_to_BCU";
	
	// BY ZF 记录待发送应答信息：等待charging状态变为0（未充电）
	remoteCtrlPending[chargeAddrNo].pending = true;
	remoteCtrlPending[chargeAddrNo].gunId = gunId;
	remoteCtrlPending[chargeAddrNo].controlCmd = 0x01; // 停止命令
	remoteCtrlPending[chargeAddrNo].expectedState = 0; // 期望状态：未充电
	INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + chargeAddrNo;
	remoteCtrlPending[chargeAddrNo].currentState = evse->charging; // 记录当前状态
	remoteCtrlPending[chargeAddrNo].result = 0x01; // 初始化为不成功，状态匹配后改为成功
	remoteCtrlPending[chargeAddrNo].waitStartTime = getSecTimer;
	
	// BY ZF 保存订单ID到pending结构（用于应答）
	memcpy(remoteCtrlPending[chargeAddrNo].orderId, orderId, 10);
	
	ldebug << "rxRemoteStop (0x15): Directly sent stop charge command to chargeAddrNo=" << chargeAddrNo 
		<< ", waiting for charging state change";
}


// BY ZF 检查远程控制状态变化并发送应答
void CSmartCityPlat::checkRemoteControlStatus()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 标记是否有需要发送的应答
	bool needSendResponse = false;
	
	// BY ZF 遍历所有充电口，检查是否有待发送的应答
	for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
	{
		if(!remoteCtrlPending[i].pending)
			continue;
		
		// BY ZF 获取当前充电状态
		INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + i;
		uchar currentCharging = evse->charging;
		
		// BY ZF 检查状态是否已变更到期望状态
		if(currentCharging == remoteCtrlPending[i].expectedState)
		{
			// BY ZF 状态已变更到期望状态，标记为成功
			remoteCtrlPending[i].result = 0x00; // 成功
			remoteCtrlPending[i].currentState = currentCharging;
			needSendResponse = true;
			
			ldebug << "checkRemoteControlStatus: Gun ID " << remoteCtrlPending[i].gunId 
				<< " (chargeAddrNo=" << i << ") state changed to expected state " 
				<< (int)remoteCtrlPending[i].expectedState << ", ready to send response";
		}
		else
		{
			// BY ZF 检查是否超时（30秒超时）
			if(abs(getSecTimer - remoteCtrlPending[i].waitStartTime) > 30)
			{
				// BY ZF 超时，标记为不成功
				remoteCtrlPending[i].result = 0x01; // 不成功
				needSendResponse = true;
				ldebug << "checkRemoteControlStatus: Gun ID " << remoteCtrlPending[i].gunId 
					<< " (chargeAddrNo=" << i << ") timeout waiting for state change";
			}
			else
			{
				// BY ZF 更新当前状态，继续等待
				remoteCtrlPending[i].currentState = currentCharging;
			}
		}
	}
	
	// BY ZF 如果有需要发送的应答，根据控制命令类型发送相应的应答
	if(needSendResponse)
	{
		// BY ZF 分别处理启动和停止的应答
		bool hasStartPending = false;
		bool hasStopPending = false;
		
		for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
		{
			if(remoteCtrlPending[i].pending && remoteCtrlPending[i].controlCmd == 0x00)
			{
				hasStartPending = true;
			}
			if(remoteCtrlPending[i].pending && remoteCtrlPending[i].controlCmd == 0x01)
			{
				hasStopPending = true;
			}
		}
		
		if(hasStartPending)
		{
			txRemoteStartResponse();
		}
		if(hasStopPending)
		{
			txRemoteStopResponse();
		}
	}
	
	// BY ZF 检测枪的充电状态，如果在充电中，则每8秒上送该枪的充电信息一次
	for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
	{
		INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + i;
		
		// BY ZF 检查是否在充电中（charging == 1）
		if(evse->charging == 1)
		{
			// BY ZF 检查距离上次发送是否已超过8秒
			if(lastTxChargeInfoTime[i] == 0 || abs(getSecTimer - lastTxChargeInfoTime[i]) >= 8)
			{
				// BY ZF 发送充电信息上报（0x22）
				txChargeInfo(i);
				lastTxChargeInfoTime[i] = getSecTimer;
			}
		}
		else
		{
			// BY ZF 如果不在充电中，重置时间戳（下次开始充电时立即上报）
			if(lastTxChargeInfoTime[i] != 0)
			{
				lastTxChargeInfoTime[i] = 0;
			}
		}
	}
}

// BY ZF 发送启动充电应答（命令字0x04：扫码后账户信息推送应答）
void CSmartCityPlat::txRemoteStartResponse()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 构建应答信息体（协议0x04格式）
	MyBuffer infoBody(true, 256);
	
	// BY ZF 收集所有待发送的启动应答（可能有多个枪）
	int pendingCount = 0;
	for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
	{
		if(remoteCtrlPending[i].pending && remoteCtrlPending[i].controlCmd == 0x00)
		{
			// BY ZF 按照协议0x04帧格式构建应答
			// 1. 反馈结果（1字节）：0x00=成功, 0x01=不成功
			infoBody.putUByte(remoteCtrlPending[i].result);
			
			// 2. 充电桩X枪ID（4字节）
			infoBody.putUInt(remoteCtrlPending[i].gunId);
			
			// 3. 充电枪X状态（1字节）：0x00=空闲, 0x01=连接, 0x02=工作, 0x03=故障
			INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + i;
			uchar gunStatus = 0x00; // 默认空闲
			if(evse->error)
			{
				gunStatus = 0x03; // 故障
			}
			else if(evse->charging)
			{
				gunStatus = 0x02; // 工作（充电中）
			}
			else if(evse->cable)
			{
				gunStatus = 0x01; // 连接（已插枪但未充电）
			}
			else
			{
				gunStatus = 0x00; // 空闲
			}
			infoBody.putUByte(gunStatus);
			
			// 4. 订单ID（10字节BCD码）
			infoBody.putData(remoteCtrlPending[i].orderId, 10);
			
			// 5. 用户ID（5字节BCD码）
			infoBody.putData(remoteCtrlPending[i].userId, 5);
			
			// BY ZF 记录充电起始时间（从发送0x04应答时开始）
			// BY ZF 使用当前时间生成8字节BCD码：YYYYMMDDHHmmssFF
			uchar startTime[8];
			getTimestampBCD(startTime);
			memcpy(remoteCtrlPending[i].chargeStartTime, startTime, 8);
			
			pendingCount++;
			
			// BY ZF 标记为已发送，清除pending标志
			remoteCtrlPending[i].pending = false;
			
			char szResult[20];
			sprintf(szResult, remoteCtrlPending[i].result == 0x00 ? "Success" : "Failure");
			ldebug << "txRemoteStartResponse (0x04): Gun ID " << remoteCtrlPending[i].gunId 
				<< ", Status=" << (int)gunStatus << ", Result=" << szResult;
		}
	}
	
	// BY ZF 如果有待发送的应答，封装并发送
	if(pendingCount > 0)
	{
		buildProtocolFrame(0x04, &infoBody);
		ldebug << "txRemoteStartResponse (0x04): Sent response for " << pendingCount << " gun(s)";
	}
}

// BY ZF 发送停止充电应答（命令字0x05：移动端停止充电请求应答）
void CSmartCityPlat::txRemoteStopResponse()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 构建应答信息体（协议0x05格式）
	MyBuffer infoBody(true, 256);
	
	// BY ZF 收集所有待发送的停止应答（可能有多个枪）
	int pendingCount = 0;
	for(int i = 0; i < MAX_DC_CHARGER_NUM; i++)
	{
		if(remoteCtrlPending[i].pending && remoteCtrlPending[i].controlCmd == 0x01)
		{
			// BY ZF 按照协议0x05帧格式构建应答
			// 1. 反馈结果（1字节）：0x00=成功, 0x01=不成功
			infoBody.putUByte(remoteCtrlPending[i].result);
			
			// 2. 充电桩X枪ID（4字节）
			infoBody.putUInt(remoteCtrlPending[i].gunId);
			
			// 3. 充电枪X状态（1字节）：0x00=空闲, 0x01=连接, 0x02=工作, 0x03=故障
			INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + i;
			uchar gunStatus = 0x00; // 默认空闲
			if(evse->error)
			{
				gunStatus = 0x03; // 故障
			}
			else if(evse->charging)
			{
				gunStatus = 0x02; // 工作（充电中）
			}
			else if(evse->cable)
			{
				gunStatus = 0x01; // 连接（已插枪但未充电）
			}
			else
			{
				gunStatus = 0x00; // 空闲
			}
			infoBody.putUByte(gunStatus);
			
			// 4. 订单ID（10字节BCD码）
			infoBody.putData(remoteCtrlPending[i].orderId, 10);
			
			pendingCount++;
			
			// BY ZF 标记为已发送，清除pending标志
			remoteCtrlPending[i].pending = false;
			
			char szResult[20];
			sprintf(szResult, remoteCtrlPending[i].result == 0x00 ? "Success" : "Failure");
			ldebug << "txRemoteStopResponse (0x05): Gun ID " << remoteCtrlPending[i].gunId 
				<< ", Status=" << (int)gunStatus << ", Result=" << szResult;
		}
	}
	
	// BY ZF 如果有待发送的应答，封装并发送
	if(pendingCount > 0)
	{
		buildProtocolFrame(0x05, &infoBody);
		ldebug << "txRemoteStopResponse (0x05): Sent response for " << pendingCount << " gun(s)";
	}
}

// BY ZF 发送充电信息上报（命令字0x22）
void CSmartCityPlat::txChargeInfo(int chargeAddrNo)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 检查充电口索引是否有效
	if(chargeAddrNo < 0 || chargeAddrNo >= MAX_DC_CHARGER_NUM)
	{
		ldebug << "txChargeInfo (0x22): Invalid chargeAddrNo " << chargeAddrNo;
		return;
	}
	
	// BY ZF 获取充电口信息
	INFO_FROM_EVSE* evse = g_hmi_info->info_from_evse + chargeAddrNo;
	LOGIC_RAM* logicRam = &(port_charge_info[chargeAddrNo]);
	
	// BY ZF 获取枪ID
	uint gunId = 0;
	if(chargeAddrNo < platDownloadParams.gunCount && chargeAddrNo < MAX_PLAT_GUN_NUM)
	{
		gunId = platDownloadParams.gunId[chargeAddrNo];
	}
	if(gunId == 0)
	{
		ldebug << "txChargeInfo (0x22): Gun ID not found for chargeAddrNo " << chargeAddrNo;
		return;
	}
	
	// BY ZF 构建充电信息上报信息体（协议使用大端序）
	MyBuffer infoBody(true, 512);
	
	// 1. 充电桩A枪ID（4字节）
	infoBody.putUInt(gunId);
	
	// 2. 充电枪A状态（1字节）：0x00=空闲, 0x01=连接, 0x02=工作, 0x03=故障
	uchar gunStatus = 0x00; // 默认空闲
	if(evse->error)
	{
		gunStatus = 0x03; // 故障
	}
	else if(evse->charging)
	{
		gunStatus = 0x02; // 工作（充电中）
	}
	else if(evse->cable)
	{
		gunStatus = 0x01; // 连接（已插枪但未充电）
	}
	else
	{
		gunStatus = 0x00; // 空闲
	}
	infoBody.putUByte(gunStatus);
	
	// 3. 订单ID（10字节BCD码）：使用LOGIC_RAM中的tradeID
	infoBody.putData(logicRam->tradeID, 10);
	
	// 4-9. 充电枪A电压和电流（A/B/C三相，每相2字节）
	// BY ZF 直流充电桩：只有A相有数据，B相和C相传0
	// BY ZF 电压：分辨率0.1V，如发送3000表示300V
	// BY ZF 根据hmi_interface.h定义：charged_voltage单位是0.1V，协议要求也是0.1V，直接使用
	ushort voltageA = (ushort)evse->charged_voltage;
	
	// BY ZF 电流：分辨率0.01A，如发送3000表示30A
	// BY ZF 根据hmi_interface.h定义：直流charged_current单位是0.1A，需要转换为0.01A单位（乘以10）
	// BY ZF 例如：30A = 300 * 0.1A，需要转换为3000（300 * 10 = 3000）
	ushort currentA = (ushort)(evse->charged_current * 10);
	
	infoBody.putUShort(voltageA); // A相电压（直流充电桩使用此值）
	infoBody.putUShort(currentA); // A相电流（直流充电桩使用此值）
	infoBody.putUShort(0); // B相电压（直流充电桩传0）
	infoBody.putUShort(0); // B相电流（直流充电桩传0）
	infoBody.putUShort(0); // C相电压（直流充电桩传0）
	infoBody.putUShort(0); // C相电流（直流充电桩传0）
	
	// 10. 充电枪A输出功率（2字节，单位0.01KW）
	// 功率 = 电压 * 电流 / 1000，单位KW，再乘以100转换为0.01KW
	ushort outputPower = 0;
	if(voltageA > 0 && currentA > 0)
	{
		// 电压单位0.1V，电流单位0.01A，功率 = (voltageA/10) * (currentA/100) / 1000 * 100 = voltageA * currentA / 10000
		outputPower = (ushort)((voltageA * currentA) / 10000);
	}
	infoBody.putUShort(outputPower);
	
	// 11. 计费标志（1字节）：0x00=充电桩计费, 0x01=充电桩不计费
	// BY ZF 从0x14启动命令中获取的计费标志（保存在remoteCtrlPending中）
	// BY ZF 如果未设置（为0），默认使用充电桩计费（0x00）
	uchar billingFlag = remoteCtrlPending[chargeAddrNo].billingFlag;
	if(billingFlag != 0x00 && billingFlag != 0x01)
	{
		billingFlag = 0x00; // 默认充电桩计费
	}
	infoBody.putUByte(billingFlag);
	
	// 12-13. 充电起始电量和结束电量（各4字节，单位0.01KWH）
	// TODO: 需要从充电记录中获取起始和结束电量
	uint startEnergy = 0; // 起始电量，单位0.01KWH
	uint endEnergy = (uint)(evse->charged_power); // 当前累计电量，单位0.01KWH
	infoBody.putUInt(startEnergy);
	infoBody.putUInt(endEnergy);
	
	// 14. 充电枪A累计电量（4字节，单位0.01KWH）
	infoBody.putUInt(endEnergy);
	
	// 15-17. 累计总金额、累计电费、累计服务费（各4字节，单位0.01元）
	// TODO: 需要从计费系统中获取详细费用信息
	uint totalAmount = (uint)(evse->charged_money); // 总金额，单位分（需要转换为0.01元，即乘以100）
	uint electricityFee = totalAmount; // 电费（简化处理，实际需要区分电费和服务费）
	uint serviceFee = 0; // 服务费
	infoBody.putUInt(totalAmount * 100); // 转换为0.01元单位
	infoBody.putUInt(electricityFee * 100); // 转换为0.01元单位
	infoBody.putUInt(serviceFee * 100); // 转换为0.01元单位
	
	// 18. 充电枪A时段数（1字节）
	// BY ZF 从0x14启动命令中获取的时段数（从计费模型中获取）
	uchar timePeriodCount = remoteCtrlPending[chargeAddrNo].billingModel.periodCount;
	if(timePeriodCount == 0)
	{
		timePeriodCount = 0x04; // 如果未设置，默认4个时段
	}
	// BY ZF 最多支持24个时段，如果超过则限制为24
	if(timePeriodCount > 24)
	{
		timePeriodCount = 24;
	}
	infoBody.putUByte(timePeriodCount);
	
	// 19-20. 充电枪A时段费用1-N（每个14字节）
	// BY ZF 使用外部计算模块回传的实际充电费用数据
	// BY ZF 每个时段包含：开始时间(1) + 结束时间(1) + 总电费(4) + 总服务费(4) + 总电量(4) = 14字节
	CHARGE_FEE_DATA* feeData = &(remoteCtrlPending[chargeAddrNo].chargeFeeData);
	BILLING_MODEL* billingModel = &(remoteCtrlPending[chargeAddrNo].billingModel);
	
	for(int i = 0; i < timePeriodCount; i++)
	{
		uchar periodData14[14] = {0};
		if(i < billingModel->periodCount)
		{
			// BY ZF 从计费模型中获取开始时间和结束时间
			periodData14[0] = billingModel->periodStartTime[i]; // 开始时间（BCD码）
			periodData14[1] = billingModel->periodEndTime[i];   // 结束时间（BCD码）
			
			// BY ZF 从实际费用数据中获取总电费、总服务费和总电量（由外部计算模块计算后回传）
			if(i < feeData->periodCount)
			{
				// BY ZF 总电费（4字节，单位0.01元，协议使用大端序：高字节在前）
				uint totalChargeFee = feeData->periodTotalChargeFee[i];
				periodData14[2] = (uchar)((totalChargeFee >> 24) & 0xFF);
				periodData14[3] = (uchar)((totalChargeFee >> 16) & 0xFF);
				periodData14[4] = (uchar)((totalChargeFee >> 8) & 0xFF);
				periodData14[5] = (uchar)(totalChargeFee & 0xFF);
				
				// BY ZF 总服务费（4字节，单位0.01元，协议使用大端序：高字节在前）
				uint totalServiceFee = feeData->periodTotalServiceFee[i];
				periodData14[6] = (uchar)((totalServiceFee >> 24) & 0xFF);
				periodData14[7] = (uchar)((totalServiceFee >> 16) & 0xFF);
				periodData14[8] = (uchar)((totalServiceFee >> 8) & 0xFF);
				periodData14[9] = (uchar)(totalServiceFee & 0xFF);
				
				// BY ZF 总电量（4字节，单位0.01KWH，协议使用大端序：高字节在前）
				uint totalEnergy = feeData->periodTotalEnergy[i];
				periodData14[10] = (uchar)((totalEnergy >> 24) & 0xFF);
				periodData14[11] = (uchar)((totalEnergy >> 16) & 0xFF);
				periodData14[12] = (uchar)((totalEnergy >> 8) & 0xFF);
				periodData14[13] = (uchar)(totalEnergy & 0xFF);
			}
			// BY ZF 如果外部计算模块还未回传数据，则使用0（表示该时段暂无费用数据）
		}
		infoBody.putData(periodData14, 14);
	}
	
	// 21. 充电枪A充电起始时间（8字节BCD码）
	// BY ZF 从发送0x04启动确认应答时记录的时间
	uchar startTime[8];
	if(remoteCtrlPending[chargeAddrNo].chargeStartTime[0] != 0)
	{
		// BY ZF 使用记录的启动时间
		memcpy(startTime, remoteCtrlPending[chargeAddrNo].chargeStartTime, 8);
	}
	else
	{
		// BY ZF 如果未记录，使用当前时间
		getTimestampBCD(startTime);
	}
	infoBody.putData(startTime, 8);
	
	// BY ZF 封装并发送
	buildProtocolFrame(0x22, &infoBody);
	ldebug << "txChargeInfo (0x22): Gun ID " << gunId << " (chargeAddrNo=" << chargeAddrNo 
		<< "), status=" << (int)gunStatus << ", energy=" << (endEnergy * 0.01) << "KWH";
}

// BY ZF 初始化协议参数（集中器地址/桩编码）
void CSmartCityPlat::initProt(int jzqAddr)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	this->jzqAddr=jzqAddr;
	ldebug<<"JZQ address (charger code) initialized: "<<jzqAddr;
}

// BY ZF 设置系统时间
void CSmartCityPlat::setSystime(CFETime ct)
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

// BY ZF 设置接收状态
void CSmartCityPlat::setRxok(bool rxok)
{
	this->rxok=rxok;
	for(int iPort=0; iPort<MAX_DC_CHARGER_NUM; iPort++)
		g_hmi_info->bcu_info_to_hmi[iPort].comm_state.cevhost_comm_flag=rxok?0xAA:0x55;
	if(rxok)
		lastRxtime=getSecTimer;
}

// BY ZF ========== 帧封装和解封函数实现 ==========

// BY ZF 计算CRC校验和（从前导字到数据字段，不包括校验和字段本身）
// BY ZF 使用CRC16 ModBus算法计算校验和
ushort CSmartCityPlat::calculateCRC(uchar* data, int len)
{
	// BY ZF 使用MyBuffer的CRC16 ModBus算法计算校验和
	// BY ZF 创建一个临时的MyBuffer对象来调用crc方法
	MyBuffer tempBuf(true, 1);
	short crcResult = tempBuf.crc(data, len);
	// BY ZF crc方法返回的是short类型，需要转换为ushort
	// BY ZF 注意：crc方法返回的CRC16已经是字节序调整后的（大端序）
	return ((crcResult & 0xFF) << 8) | ((crcResult >> 8) & 0xFF);
}

// BY ZF 获取当前时间戳（BCD码格式，8字节）
void CSmartCityPlat::getTimestampBCD(uchar* timestamp)
{
	struct tm *ptm;
	time_t now;
	time(&now);
	ptm = localtime(&now);
	
	// BY ZF BCD码格式: YYYYMMDDHHmmssFF
	// 年(2字节，完整4位年份) + 月(1字节) + 日(1字节) + 时(1字节) + 分(1字节) + 秒(1字节) + 标志(1字节) = 8字节
	int year = ptm->tm_year + 1900;
	// BY ZF 年份编码为2字节BCD：2025 -> 0x20 0x25
	timestamp[0] = (year / 100 / 10) * 16 + (year / 100 % 10); // 年份前两位：20 -> 0x20
	timestamp[1] = (year % 100 / 10) * 16 + (year % 10); // 年份后两位：25 -> 0x25
	timestamp[2] = ((ptm->tm_mon + 1) / 10) * 16 + ((ptm->tm_mon + 1) % 10); // 月：BCD编码，如11月->0x11
	timestamp[3] = (ptm->tm_mday / 10) * 16 + (ptm->tm_mday % 10); // 日：BCD编码，如03日->0x03
	timestamp[4] = (ptm->tm_hour / 10) * 16 + (ptm->tm_hour % 10); // 时：BCD编码，如16时->0x16
	timestamp[5] = (ptm->tm_min / 10) * 16 + (ptm->tm_min % 10); // 分：BCD编码，如44分->0x44
	timestamp[6] = (ptm->tm_sec / 10) * 16 + (ptm->tm_sec % 10); // 秒：BCD编码，如13秒->0x13
	timestamp[7] = 0xFF; // 最后一个字节：0xFF表示未加密，0x00表示加密
}

// BY ZF 帧封装函数：根据命令字和信息体构建完整报文并自动发送
// cmd: 命令字（1字节），表示报文的类型
// infoBody: 信息体数据（可变长度），包含该命令字对应的业务数据
void CSmartCityPlat::buildProtocolFrame(uchar cmd, MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 报文序号循环累加
	sendNo++;
	if(sendNo == 0) // 如果溢出，跳过0，从1开始
		sendNo = 1;
	
	// BY ZF 清空发送缓冲区并开始构建报文
	txbuf->clearAll();
	
	// 1. 前导字 (4字节): 0xF5 0xE2 0xF3 0x06
	txbuf->putUByte(0xF5);
	txbuf->putUByte(0xE2);
	txbuf->putUByte(0xF3);
	txbuf->putUByte(0x06);
	
	// 2. 协议版本 (1字节): 0x04
	txbuf->putUByte(0x04);
	
	// 3. 报文上报时间戳 (8字节): BCD码 YYYYMMDDHHmmssFF
	uchar timestamp[8];
	getTimestampBCD(timestamp);
	txbuf->putData(timestamp, 8);
	
	// 4. 长度 (2字节): 序号(1) + 命令字(1) + 信息体长度 + 校验和(2)
	int infoBodyLen = infoBody->getLen(); // 信息体长度
	int totalLen = 1 + 1 + infoBodyLen + 2; // 序号 + 命令字 + 信息体 + 校验和
	txbuf->putUShort(totalLen);
	
	// 5. 序号 (1字节)
	txbuf->putUByte(sendNo);
	
	// 6. 命令字 (1字节)
	txbuf->putUByte(cmd);
	
	// 7. 信息体 (N字节)
	txbuf->putMyBuffer(infoBody);
	
	// 8. 校验和 (2字节): 从前导字到信息体字段计算校验和
	// 注意：需要先计算前面所有字段的校验和，然后再添加到缓冲区
	int checkDataLen = txbuf->getLen(); // 当前缓冲区长度（前导字到信息体）
	uchar* checkData = txbuf->getBuf(); // 获取缓冲区指针
	ushort checksum = calculateCRC(checkData, checkDataLen);
	txbuf->putUShort(checksum);
	
	char szCmd[20];
	sprintf(szCmd, "0x%02x", cmd);
	ldebug << "Frame built: cmd=" << szCmd << ", seq=" << (int)sendNo << ", infoBodyLen=" << infoBodyLen << ", totalLen=" << totalLen;
	
	// BY ZF 封装完成后自动发送
	write();
	ldebug << "Frame sent";
}

// BY ZF 帧解封函数：解析接收到的报文，提取命令字和信息体
// frameData: 接收到的完整帧数据
// frameLen: 帧数据长度
// cmd: 输出参数，解析得到的命令字
// infoBody: 输出参数，解析得到的信息体数据
bool CSmartCityPlat::parseProtocolFrame(uchar* frameData, int frameLen, uchar* cmd, MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 最小报文长度检查
	if(frameLen < 4 + 1 + 8 + 2 + 1 + 1 + 2) // 前导字+版本+时间戳+长度+序号+命令字+校验和（最小）
	{
		ldebug << "Frame too short, len=" << frameLen;
		return false;
	}
	
	// BY ZF 检查前导字
	if(frameData[0] != 0xF5 || frameData[1] != 0xE2 || frameData[2] != 0xF3 || frameData[3] != 0x06)
	{
		ldebug << "Invalid preamble";
		return false;
	}
	
	// // BY ZF 检查协议版本
	// if(frameData[4] != 0x06)
	// {
	// 	char szVer[20];
	// 	sprintf(szVer, "0x%02x", frameData[4]);
	// 	ldebug << "Invalid protocol version: " << szVer;
	// 	return false;
	// }
	
	// BY ZF 读取时间戳（8字节BCD码：YYYYMMDDHHmmssFF）
	uchar timestamp[8];
	memcpy(timestamp, frameData + 5, 8);
	
	// BY ZF 获取长度字段
	int totalLen = makeushort(frameData + 13); // 前导字(4) + 版本(1) + 时间戳(8) = 13
	
	// BY ZF 验证报文长度
	int expectedLen = 4 + 1 + 8 + 2 + totalLen; // 前导字(4) + 版本(1) + 时间戳(8) + 长度(2) + 数据部分
	if(frameLen < expectedLen)
	{
		ldebug << "Frame length mismatch, expected=" << expectedLen << ", got=" << frameLen;
		return false;
	}
	
	// BY ZF 提取序号
	uchar seqNo = frameData[15]; // 前导字(4) + 版本(1) + 时间戳(8) + 长度(2) = 15
	
	// BY ZF 提取命令字
	*cmd = frameData[16]; // 序号位置 + 1
	
	// BY ZF 提取信息体部分
	int infoBodyLen = totalLen - 1 - 1 - 2; // 总长度 - 序号 - 命令字 - 校验和
	if(infoBodyLen < 0 || infoBodyLen > 2048)
	{
		ldebug << "Invalid infoBody length: " << infoBodyLen;
		return false;
	}
	
	// BY ZF 验证校验和
	ushort receivedChecksum = makeushort(frameData + expectedLen - 2);
	ushort calculatedChecksum = calculateCRC(frameData, expectedLen - 2); // 不包括校验和字段本身
	if(receivedChecksum != calculatedChecksum)
	{
		char szRecv[20], szCalc[20];
		sprintf(szRecv, "0x%04x", receivedChecksum);
		sprintf(szCalc, "0x%04x", calculatedChecksum);
		ldebug << "Checksum error, received=" << szRecv << ", calculated=" << szCalc;
		return false;
	}
	
	// BY ZF 提取信息体到缓冲区
	infoBody->clearAll();
	if(infoBodyLen > 0)
	{
		infoBody->putData(frameData + 17, infoBodyLen); // 信息体在序号和命令字之后：15(序号) + 1(命令字) + 1 = 17
	}
	
	char szCmd[20];
	sprintf(szCmd, "0x%02x", *cmd);
	ldebug << "Frame parsed: cmd=" << szCmd << ", seq=" << (int)seqNo << ", infoBodyLen=" << infoBodyLen;
	return true;
}

// BY ZF 提供给外部计算模块的接口函数实现

// BY ZF 获取指定充电口的计费模型（供外部计算模块使用）
const CSmartCityPlat::BILLING_MODEL* CSmartCityPlat::getBillingModel(int chargeAddrNo)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 检查充电口索引是否有效
	if(chargeAddrNo < 0 || chargeAddrNo >= MAX_DC_CHARGER_NUM)
	{
		ldebug << "getBillingModel: Invalid chargeAddrNo " << chargeAddrNo;
		return NULL;
	}
	
	// BY ZF 返回计费模型指针
	return &(remoteCtrlPending[chargeAddrNo].billingModel);
}

// BY ZF 外部计算模块回传计算结果
// BY ZF 设置指定充电口的实际充电费用（由外部计算模块调用）
void CSmartCityPlat::setChargeFeeData(int chargeAddrNo, const CHARGE_FEE_DATA* feeData)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 检查充电口索引是否有效
	if(chargeAddrNo < 0 || chargeAddrNo >= MAX_DC_CHARGER_NUM)
	{
		ldebug << "setChargeFeeData: Invalid chargeAddrNo " << chargeAddrNo;
		return;
	}
	
	// BY ZF 检查输入参数
	if(feeData == NULL)
	{
		ldebug << "setChargeFeeData: feeData is NULL for chargeAddrNo " << chargeAddrNo;
		return;
	}
	
	// BY ZF 累加费用数据（feeData中的值是增量值，需要累加到现有的费用数据中）
	CHARGE_FEE_DATA* existingFeeData = &(remoteCtrlPending[chargeAddrNo].chargeFeeData);
	
	// BY ZF 确保时段数一致
	if(existingFeeData->periodCount == 0)
	{
		existingFeeData->periodCount = feeData->periodCount;
	}
	
	// BY ZF 累加各时段的费用数据
	for(int i = 0; i < feeData->periodCount && i < 24; i++)
	{
		existingFeeData->periodTotalEnergy[i] += feeData->periodTotalEnergy[i];
		existingFeeData->periodTotalChargeFee[i] += feeData->periodTotalChargeFee[i];
		existingFeeData->periodTotalServiceFee[i] += feeData->periodTotalServiceFee[i];
	}
	
	// BY ZF 重新计算总充电费用、总服务费和总电量（所有时段之和）
	existingFeeData->totalChargeFee = 0;
	existingFeeData->totalServiceFee = 0;
	existingFeeData->totalEnergy = 0;
	for(int i = 0; i < existingFeeData->periodCount && i < 24; i++)
	{
		existingFeeData->totalChargeFee += existingFeeData->periodTotalChargeFee[i];
		existingFeeData->totalServiceFee += existingFeeData->periodTotalServiceFee[i];
		existingFeeData->totalEnergy += existingFeeData->periodTotalEnergy[i];
	}
	
	ldebug << "setChargeFeeData: Updated charge fee data for chargeAddrNo=" << chargeAddrNo 
		<< ", periodCount=" << (int)feeData->periodCount
		<< ", totalChargeFee=" << existingFeeData->totalChargeFee
		<< ", totalServiceFee=" << existingFeeData->totalServiceFee
		<< ", totalEnergy=" << existingFeeData->totalEnergy;
}

// BY ZF 获取指定充电口的实际充电费用（供外部计算模块使用，用于累加计算）
const CSmartCityPlat::CHARGE_FEE_DATA* CSmartCityPlat::getChargeFeeData(int chargeAddrNo)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 检查充电口索引是否有效
	if(chargeAddrNo < 0 || chargeAddrNo >= MAX_DC_CHARGER_NUM)
	{
		ldebug << "getChargeFeeData: Invalid chargeAddrNo " << chargeAddrNo;
		return NULL;
	}
	
	// BY ZF 返回费用数据指针
	return &(remoteCtrlPending[chargeAddrNo].chargeFeeData);
}

// BY ZF ========== C接口函数（供C代码调用） ==========
// BY ZF 包含C接口头文件
extern "C" {
	#include "zk/inc/charge_fee_calc_plat.h"
}

// BY ZF 获取指定充电口的计费模型（C接口）
const BILLING_MODEL_PLAT* get_billing_model_plat(int chargeAddrNo)
{
	CSmartCityPlat* platInstance = CSmartCityPlat::getInstance();
	if (!platInstance)
	{
		return NULL;
	}
	
	// BY ZF 调用C++成员函数获取计费模型
	const CSmartCityPlat::BILLING_MODEL* billingModel = platInstance->getBillingModel(chargeAddrNo);
	if (!billingModel)
	{
		return NULL;
	}
	
	// BY ZF 将C++结构体转换为C结构体（类型兼容，直接转换）
	return (const BILLING_MODEL_PLAT*)billingModel;
}

// BY ZF 设置指定充电口的实际充电费用（C接口）
void set_charge_fee_data_plat(int chargeAddrNo, const CHARGE_FEE_DATA_PLAT* feeData)
{
	CSmartCityPlat* platInstance = CSmartCityPlat::getInstance();
	if (!platInstance || !feeData)
	{
		return;
	}
	
	// BY ZF 将C结构体转换为C++结构体（类型兼容，直接转换）
	const CSmartCityPlat::CHARGE_FEE_DATA* cppFeeData = (const CSmartCityPlat::CHARGE_FEE_DATA*)feeData;
	
	// BY ZF 调用C++成员函数设置费用数据
	platInstance->setChargeFeeData(chargeAddrNo, cppFeeData);
}

// BY ZF 获取指定充电口的实际充电费用（C接口）
const CHARGE_FEE_DATA_PLAT* get_charge_fee_data_plat(int chargeAddrNo)
{
	CSmartCityPlat* platInstance = CSmartCityPlat::getInstance();
	if (!platInstance)
	{
		return NULL;
	}
	
	// BY ZF 调用C++成员函数获取费用数据
	const CSmartCityPlat::CHARGE_FEE_DATA* cppFeeData = platInstance->getChargeFeeData(chargeAddrNo);
	if (!cppFeeData)
	{
		return NULL;
	}
	
	// BY ZF 将C++结构体转换为C结构体（类型兼容，直接转换）
	return (const CHARGE_FEE_DATA_PLAT*)cppFeeData;
}

// BY ZF ========== 充电记录上送功能 ==========

// BY ZF 检查并上送充电记录（监控in_pos和out_pos的差异）
void CSmartCityPlat::checkChargeRecordUpload()
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 遍历所有充电口，检查是否有待上送的充电记录
	for(int chargeAddrNo = 0; chargeAddrNo < MAX_DC_CHARGER_NUM; chargeAddrNo++)
	{
		// BY ZF 检查是否有待上送的记录（in_pos != out_pos）
		if(charge_record_info[chargeAddrNo].in_pos != charge_record_info[chargeAddrNo].out_pos)
		{
			// BY ZF 有待上送的记录，发送0x60帧（直接传递已找到的chargeAddrNo，避免重复查找）
			txChargeRecordRequest(chargeAddrNo);
			// BY ZF 每次只发送一条记录，发送后退出，等待应答后再发送下一条
			break;
		}
	}
}

// BY ZF 发送充电记录包请求（命令字0x60）
void CSmartCityPlat::txChargeRecordRequest(int chargeAddrNo)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 检查充电口索引是否有效（防御性编程，防止未来其他调用者传入无效值）
	if(chargeAddrNo < 0 || chargeAddrNo >= MAX_DC_CHARGER_NUM)
	{
		ldebug << "txChargeRecordRequest (0x60): Invalid chargeAddrNo " << chargeAddrNo;
		return;
	}
	
	// BY ZF 注意：调用者（checkChargeRecordUpload）已经检查了 in_pos != out_pos，此处不再重复检查
	
	// BY ZF 获取当前要上送的记录索引（out_pos指向的记录）
	unsigned short recordOutPos = charge_record_info[chargeAddrNo].out_pos;
	SINGLE_CHARGE_RECORD* record = &(charge_record_info[chargeAddrNo].record_info[recordOutPos]);
	
	// BY ZF 获取枪ID
	uint gunId = 0;
	if(chargeAddrNo < platDownloadParams.gunCount && chargeAddrNo < MAX_PLAT_GUN_NUM)
	{
		gunId = platDownloadParams.gunId[chargeAddrNo];
	}
	if(gunId == 0)
	{
		ldebug << "txChargeRecordRequest (0x60): Gun ID not found for chargeAddrNo " << chargeAddrNo;
		return;
	}
	
	// BY ZF 构建充电记录包请求信息体（协议使用大端序）
	MyBuffer infoBody(true, 1024);
	
	// 1. 充电桩X枪ID（4字节）
	infoBody.putUInt(gunId);
	
	// 2. IC卡卡号（8字节BCD码）- 全用0上送
	uchar cardNo[8] = {0};
	infoBody.putData(cardNo, 8);
	
	// 3. 订单ID（10字节BCD码）- 从billi_no_bcd获取
	infoBody.putData(record->billi_no_bcd, 10);
	
	// 4. 充电电量（4字节，分辨率0.01，单位0.01KWH）
	// BY ZF 使用平台协议的总电量（从chargeFeeData获取），如果没有则使用charged_power
	uint totalEnergy = 0;
	if(chargeAddrNo < MAX_DC_CHARGER_NUM)
	{
		const CHARGE_FEE_DATA_PLAT* chargeFeeData = get_charge_fee_data_plat(chargeAddrNo);
		if(chargeFeeData && chargeFeeData->totalEnergy > 0)
		{
			totalEnergy = chargeFeeData->totalEnergy;
		}
		else
		{
			// BY ZF 如果没有平台协议数据，使用charged_power（单位度，需要转换为0.01KWH）
			totalEnergy = (uint)(record->charged_power * 100);
		}
	}
	infoBody.putUInt(totalEnergy);
	
	// 5. 充电金额（4字节，分辨率0.01，单位0.01元）
	// BY ZF 充电金额 = 总电费 + 总服务费
	uint totalAmount = 0;
	if(chargeAddrNo < MAX_DC_CHARGER_NUM)
	{
		const CHARGE_FEE_DATA_PLAT* chargeFeeData = get_charge_fee_data_plat(chargeAddrNo);
		if(chargeFeeData && (chargeFeeData->totalChargeFee > 0 || chargeFeeData->totalServiceFee > 0))
		{
			totalAmount = chargeFeeData->totalChargeFee + chargeFeeData->totalServiceFee;
		}
		else
		{
			// BY ZF 如果没有平台协议数据，使用charged_money（单位元，需要转换为0.01元）
			totalAmount = (uint)(record->charged_money * 100);
		}
	}
	infoBody.putUInt(totalAmount);
	
	// 6. 服务费（4字节，分辨率0.01，单位0.01元）
	uint serviceFee = 0;
	if(chargeAddrNo < MAX_DC_CHARGER_NUM)
	{
		const CHARGE_FEE_DATA_PLAT* chargeFeeData = get_charge_fee_data_plat(chargeAddrNo);
		if(chargeFeeData && chargeFeeData->totalServiceFee > 0)
		{
			serviceFee = chargeFeeData->totalServiceFee;
		}
	}
	infoBody.putUInt(serviceFee);
	
	// 7. 充电起始电量（4字节，分辨率0.01，单位0.01KWH）
	uint startEnergy = (uint)(record->meter_reader_start * 100);
	infoBody.putUInt(startEnergy);
	
	// 8. 充电结束电量（4字节，分辨率0.01，单位0.01KWH）
	uint endEnergy = (uint)(record->meter_reader_end * 100);
	infoBody.putUInt(endEnergy);
	
	// 9. 充电起始时间（8字节BCD码）
	// BY ZF 将start_time[6]转换为8字节BCD码：YYYYMMDDHHmmssFF
	uchar startTime[8] = {0};
	// BY ZF start_time格式：秒-分-时-日-月-(年-2000)
	// BY ZF 需要转换为：YYYYMMDDHHmmssFF
	int year = 2000 + record->start_time[5];
	startTime[0] = ((year / 100 / 10) & 0x0F) << 4 | ((year / 100 % 10) & 0x0F); // 年份前两位
	startTime[1] = ((year % 100 / 10) & 0x0F) << 4 | ((year % 10) & 0x0F); // 年份后两位
	startTime[2] = ((record->start_time[4] / 10) & 0x0F) << 4 | ((record->start_time[4] % 10) & 0x0F); // 月
	startTime[3] = ((record->start_time[3] / 10) & 0x0F) << 4 | ((record->start_time[3] % 10) & 0x0F); // 日
	startTime[4] = ((record->start_time[2] / 10) & 0x0F) << 4 | ((record->start_time[2] % 10) & 0x0F); // 时
	startTime[5] = ((record->start_time[1] / 10) & 0x0F) << 4 | ((record->start_time[1] % 10) & 0x0F); // 分
	startTime[6] = ((record->start_time[0] / 10) & 0x0F) << 4 | ((record->start_time[0] % 10) & 0x0F); // 秒
	startTime[7] = 0xFF; // 最后一个字节默认0xFF
	infoBody.putData(startTime, 8);
	
	// 10. 充电结束时间（8字节BCD码）
	// BY ZF 将end_time[6]转换为8字节BCD码：YYYYMMDDHHmmssFF
	uchar endTime[8] = {0};
	int endYear = 2000 + record->end_time[5];
	endTime[0] = ((endYear / 100 / 10) & 0x0F) << 4 | ((endYear / 100 % 10) & 0x0F); // 年份前两位
	endTime[1] = ((endYear % 100 / 10) & 0x0F) << 4 | ((endYear % 10) & 0x0F); // 年份后两位
	endTime[2] = ((record->end_time[4] / 10) & 0x0F) << 4 | ((record->end_time[4] % 10) & 0x0F); // 月
	endTime[3] = ((record->end_time[3] / 10) & 0x0F) << 4 | ((record->end_time[3] % 10) & 0x0F); // 日
	endTime[4] = ((record->end_time[2] / 10) & 0x0F) << 4 | ((record->end_time[2] % 10) & 0x0F); // 时
	endTime[5] = ((record->end_time[1] / 10) & 0x0F) << 4 | ((record->end_time[1] % 10) & 0x0F); // 分
	endTime[6] = ((record->end_time[0] / 10) & 0x0F) << 4 | ((record->end_time[0] % 10) & 0x0F); // 秒
	endTime[7] = 0xFF; // 最后一个字节默认0xFF
	infoBody.putData(endTime, 8);
	
	// 11. 充电停止原因（1字节）：0x00=正常, 0x01=异常
	// BY ZF 先用0x01返回
	infoBody.putUByte(0x01);
	
	// 12. 充电停止原因异常代码（4字节）
	// BY ZF 暂时用0填充
	infoBody.putUInt(0);
	
	// 13. 计费标志（1字节）：0x00=充电桩计费, 0x01=充电桩不计费
	// BY ZF 从billing_mode获取，如果为1表示已从卡扣值支付，否则为充电桩计费
	uchar billingFlag = (charge_record_info[chargeAddrNo].billing_mode[recordOutPos] == 1) ? 0x01 : 0x00;
	infoBody.putUByte(billingFlag);
	
	// 14. 时段数（1字节）
	uchar periodCount = record->period_count;
	if(periodCount > 24)
		periodCount = 24;
	infoBody.putUByte(periodCount);
	
	// 15. 时段费用1-N（每个14字节）
	// BY ZF 每个时段包含：开始时间(1) + 结束时间(1) + 总电费(4) + 总服务费(4) + 总电量(4) = 14字节
	for(int i = 0; i < periodCount; i++)
	{
		uchar periodData[14] = {0};
		if(i < record->period_count)
		{
			// BY ZF 开始时间（1字节BCD码）
			periodData[0] = record->period_start_time[i];
			// BY ZF 结束时间（1字节BCD码）
			periodData[1] = record->period_end_time[i];
			
			// BY ZF 总电费（4字节，单位0.01元，协议使用大端序：高字节在前）
			uint totalChargeFee = record->period_total_charge_fee[i];
			periodData[2] = (uchar)((totalChargeFee >> 24) & 0xFF);
			periodData[3] = (uchar)((totalChargeFee >> 16) & 0xFF);
			periodData[4] = (uchar)((totalChargeFee >> 8) & 0xFF);
			periodData[5] = (uchar)(totalChargeFee & 0xFF);
			
			// BY ZF 总服务费（4字节，单位0.01元，协议使用大端序：高字节在前）
			uint totalServiceFee = record->period_total_service_fee[i];
			periodData[6] = (uchar)((totalServiceFee >> 24) & 0xFF);
			periodData[7] = (uchar)((totalServiceFee >> 16) & 0xFF);
			periodData[8] = (uchar)((totalServiceFee >> 8) & 0xFF);
			periodData[9] = (uchar)(totalServiceFee & 0xFF);
			
			// BY ZF 总电量（4字节，单位0.01KWH，协议使用大端序：高字节在前）
			uint totalPeriodEnergy = record->period_total_energy[i];
			periodData[10] = (uchar)((totalPeriodEnergy >> 24) & 0xFF);
			periodData[11] = (uchar)((totalPeriodEnergy >> 16) & 0xFF);
			periodData[12] = (uchar)((totalPeriodEnergy >> 8) & 0xFF);
			periodData[13] = (uchar)(totalPeriodEnergy & 0xFF);
		}
		infoBody.putData(periodData, 14);
	}
	
	// BY ZF 封装并发送
	buildProtocolFrame(0x60, &infoBody);
	ldebug << "txChargeRecordRequest (0x60): Gun ID " << gunId << " (chargeAddrNo=" << chargeAddrNo 
		<< "), recordOutPos=" << recordOutPos << ", periodCount=" << (int)periodCount;
}

// BY ZF 充电记录包请求应答处理（命令字0x70）
void CSmartCityPlat::rxChargeRecordResponse(MyBuffer* infoBody)
{
	// BY ZF 设置 logid 供 ldebug 宏使用
	char* logid = this->logid;
	
	// BY ZF 按照协议0x70帧定义顺序解析
	// 协议格式：
	// 1. 反馈结果（1字节）：0x00=成功, 0x01=失败
	// 2. 充电桩X枪ID（4字节）
	// 3. 订单ID（10字节BCD码）
	
	// BY ZF 检查信息体最小长度
	const int MIN_DATA_SIZE = 1 + 4; // 反馈结果 + 枪ID
	if(infoBody->getLen() < MIN_DATA_SIZE)
	{
		ldebug << "rxChargeRecordResponse (0x70): InfoBody too short, len=" << infoBody->getLen() 
			<< ", expected at least " << MIN_DATA_SIZE;
		return;
	}
	
	// 1. 读取枪ID（4字节）
	uint gunId = infoBody->getUInt();
	// 2. 读取反馈结果（1字节）
	uchar feedbackResult = infoBody->getUByte();
	
	
	// BY ZF 根据枪ID查找对应的充电口索引
	int chargeAddrNo = -1;
	for(int i = 0; i < platDownloadParams.gunCount && i < MAX_PLAT_GUN_NUM && i < MAX_DC_CHARGER_NUM; i++)
	{
		if(platDownloadParams.gunId[i] == gunId)
		{
			chargeAddrNo = i;
			break;
		}
	}
	
	if(chargeAddrNo < 0)
	{
		ldebug << "rxChargeRecordResponse (0x70): Gun ID " << gunId << " not found, ignored";
		return;
	}
	
	// BY ZF 如果反馈结果为成功（0x00），更新out_pos
	if(feedbackResult == 0x00)
	{
		// BY ZF 平台已确认成功，直接更新out_pos
		// BY ZF 注意：已通过枪ID找到对应充电口，每个充电口同一时间只有一条待上送记录
		charge_record_info[chargeAddrNo].out_pos++;
		if(charge_record_info[chargeAddrNo].out_pos >= MAX_RECORD_NUM)
		{
			charge_record_info[chargeAddrNo].out_pos = 0;
		}
		
		ldebug << "rxChargeRecordResponse (0x70): Success, Gun ID " << gunId 
			<< " (chargeAddrNo=" << chargeAddrNo << "), out_pos updated to " 
			<< charge_record_info[chargeAddrNo].out_pos;
	}
	else
	{
		ldebug << "rxChargeRecordResponse (0x70): Failed, Gun ID " << gunId 
			<< " (chargeAddrNo=" << chargeAddrNo << "), feedbackResult=" << (int)feedbackResult;
	}
}

// BY ZF ========== 最小系统完成 ==========
// 后续可根据平台协议实现以下功能：
// 1. 遥信上报 (txAllYx)
// 2. 遥测上报 (txAllYc)  
// 3. 遥脉上报 (txAllYm)
// 4. 充电控制相关
// 等等...




