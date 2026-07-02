#ifndef _XIAOJU_ERROR_H_
#define _XIAOJU_ERROR_H_
/**
  ******************************************************************************
  * @file    xiaoju_error.h
  * @author  周大侠
  * @email   598488992@qq.com
  * @version V1.0
  * @date    2019-10-21 10:48:38
  * @brief   小桔故障码表
  ******************************************************************************
  * @attention

  ******************************************************************************
  */



/****************************************************************

     《V2.0.0-小桔充电后台服务器与充电桩通信协议-20191021》
      附录中对错误码的定义



      
使用十六进制编码，占用4个字节，桩端上报时使用ASCII编码，分配规则如下：
1. 常规停止:“1000”-“1FFF”
2. 平台异常:“2000”-“2FFF”
3. 设备异常:“3000”-“3FFF”
4. 电源异常:“4000”-“4FFF”
5. 车辆故障:“5000”-“5FFF”
6. 内部使用:“9000”-“9FFF”

如正常充满停止，停止原因为”1000”传输Hex为0x31,0x30,0x30,0x30

****************************************************************/




#define XJ_ERR_RESULT_LEN                   4          //xj回调结果值长度
#define XJ_SUCCEED_RESULT                   "0000"     //xj回调结果 正常值

#define XJ_SDK_ERR_1000                     "1000"    //直流充满停止，正常结束，非故障
#define XJ_SDK_ERR_1001                     "1001"    //APP或者微信停止，正常结束，非故障
#define XJ_SDK_ERR_1002                     "1002"    //账户余额不足
#define XJ_SDK_ERR_1003                     "1003"    //触控屏手动停止，正常结束，非故障
#define XJ_SDK_ERR_1004                     "1004"    //后台停止充电，正常结束，非故障
#define XJ_SDK_ERR_1005                     "1005"    //达到设置充电时长停止，正常结束，非故障
#define XJ_SDK_ERR_1006                     "1006"    //达到设置充电电量停止，正常结束，非故障
#define XJ_SDK_ERR_1007                     "1007"    //达到设置充电金额停止，正常结束，非故障
#define XJ_SDK_ERR_1008                     "1008"    //刷卡停止，正常结束，非故障
#define XJ_SDK_ERR_1009                     "1009"    //无有效电流停止
#define XJ_SDK_ERR_100A                     "100A"    //BMS异常终止充电
#define XJ_SDK_ERR_100B                     "100B"    //BMS达到终止条件停止，正常结束，非故障
#define XJ_SDK_ERR_100C                     "100C"    //充电桩达到终止条件停止，正常结束，非故障
#define XJ_SDK_ERR_100D                     "100D"    //枪未正确连接
#define XJ_SDK_ERR_2000                     "2000"    //有未停止订单
#define XJ_SDK_ERR_2001                     "2001"    //创建订单异常
#define XJ_SDK_ERR_2002                     "2002"    //桩不存在或已禁用
#define XJ_SDK_ERR_2003                     "2003"    //桩无法启动充电
#define XJ_SDK_ERR_2004                     "2004"    //桩启动响应超时
#define XJ_SDK_ERR_2005                     "2005"    //桩停止充电响应超时
#define XJ_SDK_ERR_2006                     "2006"    //桩与平台通讯建立
#define XJ_SDK_ERR_2007                     "2007"    //桩与平台通讯断开
#define XJ_SDK_ERR_2008                     "2008"    //车辆鉴权不通过
#define XJ_SDK_ERR_2009                     "2009"    //固件升级失败
#define XJ_SDK_ERR_200A                     "200A"    //升级包异常
#define XJ_SDK_ERR_200B                     "200B"    //订单未支付，启动失败
#define XJ_SDK_ERR_200C                     "200C"    //桩连接充电云鉴权失败
#define XJ_SDK_ERR_200D                     "200D"    //用户在黑名单中，禁止充电
#define XJ_SDK_ERR_200E                     "200E"    //指令要求终止的订单号不存在或者和目标充电口当前订单不一致
#define XJ_SDK_ERR_201E                     "201E"    //系统正在升级，暂停充电，请稍后再试
#define XJ_SDK_ERR_201F                     "201F"    //系统维护中，暂停充电，请稍后再试
#define XJ_SDK_ERR_2020                     "2020"    //系统后台终止充电
#define XJ_SDK_ERR_2021                     "2021"    //此用户失败过于频繁，请稍后重试
#define XJ_SDK_ERR_2022                     "2022"    //此充电桩连续失败次数过多，请更换充电桩后重试
#define XJ_SDK_ERR_2023                     "2023"    //系统判定充电充满，终止充电
#define XJ_SDK_ERR_2024                     "2024"    //系统下发参数异常
#define XJ_SDK_ERR_2025                     "2025"    //此充电枪正在使用中
#define XJ_SDK_ERR_2026                     "2026"    //超时挂单
#define XJ_SDK_ERR_2027                     "2027"    //充电电流小
#define XJ_SDK_ERR_3000                     "3000"    //设备自检超时
#define XJ_SDK_ERR_3001                     "3001"    //充电桩离线，暂停充电
#define XJ_SDK_ERR_3002                     "3002"    //柜门被打开
#define XJ_SDK_ERR_3003                     "3003"    //急停按键被按下
#define XJ_SDK_ERR_3004                     "3004"    //系统风扇故障
#define XJ_SDK_ERR_3005                     "3005"    //模块风扇故障
#define XJ_SDK_ERR_3006                     "3006"    //充电枪未归位告警
#define XJ_SDK_ERR_3007                     "3007"    //读卡器异常
#define XJ_SDK_ERR_3008                     "3008"    //模块通讯故障
#define XJ_SDK_ERR_3009                     "3009"    //电源模块地址冲突
#define XJ_SDK_ERR_300A                     "300A"    //充电模块故障
#define XJ_SDK_ERR_300B                     "300B"    //电源模块过温告警
#define XJ_SDK_ERR_300C                     "300C"    //无空闲模块可用（限智能分配功率）
#define XJ_SDK_ERR_300D                     "300D"    //电表通讯故障
#define XJ_SDK_ERR_300E                     "300E"    //电表数据异常
#define XJ_SDK_ERR_300F                     "300F"    //输出接触器粘连
#define XJ_SDK_ERR_3010                     "3010"    //直流母线输出接触器拒动/误动故障
#define XJ_SDK_ERR_3011                     "3011"    //直流熔断器故障
#define XJ_SDK_ERR_3012                     "3012"    //中间继电器故障
#define XJ_SDK_ERR_3013                     "3013"    //辅助电源没有上电
#define XJ_SDK_ERR_3014                     "3014"    //绝缘监测故障
#define XJ_SDK_ERR_3015                     "3015"    //泄放回路故障
#define XJ_SDK_ERR_3016                     "3016"    //充电桩过温故障
#define XJ_SDK_ERR_3017                     "3017"    //充电接口过温故障
#define XJ_SDK_ERR_3018                     "3018"    //充电接口电子锁故障
#define XJ_SDK_ERR_3019                     "3019"    //液位报警
#define XJ_SDK_ERR_301A                     "301A"    //充电桩内部通讯故障
#define XJ_SDK_ERR_301B                     "301B"    //充电连接故障
#define XJ_SDK_ERR_301C                     "301C"    //枪口异常
#define XJ_SDK_ERR_301D                     "301D"    //充电桩其他故障
#define XJ_SDK_ERR_301E                     "301E"    //车位锁故障
#define XJ_SDK_ERR_301F                     "301F"    //车位锁电池耗尽
#define XJ_SDK_ERR_3020                     "3020"    //车位锁落锁失败
#define XJ_SDK_ERR_3021                     "3021"    //指令要求终止的订单号不存在或者和目标充电口当前订单不一致
#define XJ_SDK_ERR_3022                     "3022"    //充电桩执行远程功率分配策略失败
#define XJ_SDK_ERR_3023                     "3023"    //充电桩网络故障
#define XJ_SDK_ERR_3028                     "3028"    //充电桩设备损坏，暂停充电
#define XJ_SDK_ERR_3029                     "3029"    //充电桩暂停使用
#define XJ_SDK_ERR_302A                     "302A"    //交流接触器故障
#define XJ_SDK_ERR_302B                     "302B"    //枪头插拔次数预警
#define XJ_SDK_ERR_302C                     "302C"    //自检功率分配超时
#define XJ_SDK_ERR_302D                     "302D"    //母联粘连故障
#define XJ_SDK_ERR_302E                     "302E"    //预充完成超时
#define XJ_SDK_ERR_302F                     "302F"    //启动充电超时
#define XJ_SDK_ERR_3030                     "3030"    //启动完成应答失败
#define XJ_SDK_ERR_3031                     "3031"    //模块开机超时
#define XJ_SDK_ERR_3032                     "3032"    //计费控制单元通讯故障   
#define XJ_SDK_ERR_3033                     "3033"    //环境监控板通讯故障       
#define XJ_SDK_ERR_3034                     "3034"    //空调通讯故障        
#define XJ_SDK_ERR_3035                     "3035"    //无源开出盒通讯故障      
#define XJ_SDK_ERR_3036                     "3036"    //无源开入盒通讯故障      
#define XJ_SDK_ERR_3037                     "3037"    //绝缘采样盒通讯故障      
#define XJ_SDK_ERR_3038                     "3038"    //直流采样盒通讯故障      
#define XJ_SDK_ERR_3039                     "3039"    //导引板通讯故障             
#define XJ_SDK_ERR_303A                     "303A"    //灯板通讯故障                 
#define XJ_SDK_ERR_4000                     "4000"    //输入电源故障（过压、过流、欠压，跳闸）
#define XJ_SDK_ERR_4001                     "4001"    //输出电压过压故障
#define XJ_SDK_ERR_4002                     "4002"    //输出电流过流故障
#define XJ_SDK_ERR_4003                     "4003"    //输出电压欠压故障
#define XJ_SDK_ERR_4004                     "4004"    //输出短路故障
#define XJ_SDK_ERR_4005                     "4005"    //交流断路器故障
#define XJ_SDK_ERR_4006                     "4006"    //继电器外侧电压大于10v
#define XJ_SDK_ERR_4007                     "4007"    //检测点电压检测故障
#define XJ_SDK_ERR_4008                     "4008"    //桩群电容量超过额定限制
#define XJ_SDK_ERR_4009                     "4009"    //输入缺相报警
#define XJ_SDK_ERR_400A                     "400A"    //漏电保护
#define XJ_SDK_ERR_400B                     "400B"    //地线报警
#define XJ_SDK_ERR_400C                     "400C"    //交流防雷报警
#define XJ_SDK_ERR_400D                     "400D"    //其他电源故障
#define XJ_SDK_ERR_400E                     "400E"    //车/桩电压异常
#define XJ_SDK_ERR_400F                     "400F"    //模块保护
#define XJ_SDK_ERR_4010                     "4010"    //三相不平衡
#define XJ_SDK_ERR_4011                     "4011"    //交流输入过压
#define XJ_SDK_ERR_4012                     "4012"    //交流输入欠压
#define XJ_SDK_ERR_4013                     "4013"    //模块输入故障
#define XJ_SDK_ERR_5000                     "5000"    //BMS通讯异常，此为车辆故障。
#define XJ_SDK_ERR_5001                     "5001"    //BCP充电参数配置报文超时，此为车辆故障。
#define XJ_SDK_ERR_5002                     "5002"    //BRO充电准备就绪报文超时，此为车辆故障。
#define XJ_SDK_ERR_5003                     "5003"    //BCS电池充电状态报文超时，此为车辆故障。
#define XJ_SDK_ERR_5004                     "5004"    //BCL电池充电需求报文超时，此为车辆故障。
#define XJ_SDK_ERR_5005                     "5005"    //BST中止充电报文超时，此为车辆故障。
#define XJ_SDK_ERR_5006                     "5006"    //BSD充电统计数据报文超时，此为车辆故障。
#define XJ_SDK_ERR_5007                     "5007"    //BSM动力蓄电池状态报文超时，此为车辆故障。
#define XJ_SDK_ERR_5008                     "5008"    //BRO重大故障停止充电，此为车辆故障。
#define XJ_SDK_ERR_5009                     "5009"    //BHM桩的输出能力不匹配，此为车辆故障。
#define XJ_SDK_ERR_500A                     "500A"    //BRM车辆辨识报文超时，此为车辆故障。
#define XJ_SDK_ERR_500B                     "500B"    //BEM充电错误报文超时，此为车辆故障。
#define XJ_SDK_ERR_500C                     "500C"    //BMS需求电压过低/过高，此为车辆故障。
#define XJ_SDK_ERR_500D                     "500D"    //BMS绝缘故障，此为车辆故障。
#define XJ_SDK_ERR_500E                     "500E"    //BMS元件过温，此为车辆故障。
#define XJ_SDK_ERR_500F                     "500F"    //BMS电压过高，此为车辆故障。
#define XJ_SDK_ERR_5010                     "5010"    //BMS预充电压不匹配，此为车辆故障。
#define XJ_SDK_ERR_5011                     "5011"    //BMS其他故障，此为车辆故障。
#define XJ_SDK_ERR_5012                     "5012"    //单体动力蓄电池电压过高，此为车辆故障。
#define XJ_SDK_ERR_5013                     "5013"    //单体动力蓄电池电压过低，此为车辆故障。
#define XJ_SDK_ERR_5014                     "5014"    //整车动力蓄电池荷电状态SOC过高，此为车辆故障。
#define XJ_SDK_ERR_5015                     "5015"    //整车动力蓄电池荷电状态SOC过低，此为车辆故障。
#define XJ_SDK_ERR_5016                     "5016"    //动力蓄电池充电过流，此为车辆故障。
#define XJ_SDK_ERR_5017                     "5017"    //动力蓄电池温度过高，此为车辆故障。
#define XJ_SDK_ERR_5018                     "5018"    //动力蓄电池绝缘故障，此为车辆故障。
#define XJ_SDK_ERR_5019                     "5019"    //动力蓄电池连接器故障，此为车辆故障。
#define XJ_SDK_ERR_501A                     "501A"    //电池反接，此为车辆故障。
#define XJ_SDK_ERR_501B                     "501B"    //电池欠压，此为车辆故障。
#define XJ_SDK_ERR_501C                     "501C"    //电池电压异常，此为车辆故障。
#define XJ_SDK_ERR_501D                     "501D"    //CRO充电机输出就绪超时，此为车辆故障。
#define XJ_SDK_ERR_501E                     "501E"    //CCS充电机状态报文超时，此为车辆故障。
#define XJ_SDK_ERR_501F                     "501F"    //CST充电机终止充电报文超时，此为车辆故障。
#define XJ_SDK_ERR_5020                     "5020"    //CSD充电统计数据报文超时，此为车辆故障。
#define XJ_SDK_ERR_5021                     "5021"    //车辆电流不匹配，此为车辆故障。
#define XJ_SDK_ERR_5022                     "5022"    //车辆电量无法传送，此为车辆故障。
#define XJ_SDK_ERR_5023                     "5023"    //车辆占位超时，此为车辆故障。
#define XJ_SDK_ERR_5024                     "5024"    //新老国标探测超时，此为车辆故障。
#define XJ_SDK_ERR_5025                     "5025"    //BMS启动充电超时
#define XJ_SDK_ERR_5026                     "5026"    //BMS充电连接器故障，此为车辆故障。
#define XJ_SDK_ERR_5027                     "5027"    //BMS高压继电器故障，此为车辆故障。
#define XJ_SDK_ERR_5028                     "5028"    //BMS监测点2电压检测故障，此为车辆故障。
#define XJ_SDK_ERR_5029                     "5029"    //BRO准备就绪后取消
#define XJ_SDK_ERR_502A                     "502A"    //BMS暂停超时，此为车辆故障。
#define XJ_SDK_ERR_502B                     "502B"    //车辆未准备就绪，此为车辆故障。
#define XJ_SDK_ERR_502C                     "502C"    //车辆BCP报文和实际电压不符，此为车辆故障。
#define XJ_SDK_ERR_502D                     "502D"    //预充阶段调压失败
#define XJ_SDK_ERR_502E                     "502E"    //预充阶段等待BCL和BCS超时
#define XJ_SDK_ERR_502F                     "502F"    //电池软起失败
#define XJ_SDK_ERR_5030                     "5030"    //BST停止未知故障
#define XJ_SDK_ERR_5031                     "5031"    //BST绝缘故障
#define XJ_SDK_ERR_5032                     "5032"    //BST连接器过温
#define XJ_SDK_ERR_5033                     "5033"    //BST元件过温
#define XJ_SDK_ERR_5034                     "5034"    //BST连接器故障
#define XJ_SDK_ERR_5035                     "5035"    //BST电池组过温
#define XJ_SDK_ERR_5036                     "5036"    //BST其他故障
#define XJ_SDK_ERR_5037                     "5037"    //BST_02_4未定义
#define XJ_SDK_ERR_5038                     "5038"    //BST_02_6未定义
#define XJ_SDK_ERR_5039                     "5039"    //BST电流过大
#define XJ_SDK_ERR_503A                     "503A"    //BST电压异常
#define XJ_SDK_ERR_503B                     "503B"    //BST未知停机
#define XJ_SDK_ERR_503C                     "503C"    //电池最高允许充电电压小于充电机最小输出电压
#define XJ_SDK_ERR_503D                     "503D"    //启动充电前直流输出接触器外侧电压小于充电机最小输出电压
#define XJ_SDK_ERR_503E                     "503E"    //启动充电前直流输出接触器外侧电压大于充电机最大输出电压
#define XJ_SDK_ERR_503F                     "503F"    //电池端电压大于电池最高允许充电电压
#define XJ_SDK_ERR_5040                     "5040"    //BRM 报文数据项异常（报文数据长度错误，BMS 通信协议版本号错误，其他数据错误）
#define XJ_SDK_ERR_5041                     "5041"    //BCP 报文数据项异常（报文数据长度错误，电池当前电压越限，其他数据项错误）
#define XJ_SDK_ERR_5042                     "5042"    //BRO 报文（0x00）接收超时
#define XJ_SDK_ERR_5043                     "5043"    //BRO 报文（0xAA）接收超时
#define XJ_SDK_ERR_5044                     "5044"    //车辆验证确认帧超时
#define XJ_SDK_ERR_5045                     "5045"    //数据合法性校验失败（“充电启动帧”中“负荷控制开关”数据项合法性校验失败）
#define XJ_SDK_ERR_5046                     "5046"    //充电控制器与计费控制单元“版本校验”未完成
#define XJ_SDK_ERR_5047                     "5047"    //充电控制器与计费控制单元“下发充电参数” 未完成
#define XJ_SDK_ERR_5048                     "5048"    //充电桩处于“工作”状态
#define XJ_SDK_ERR_6000                     "6000"    //模块通信告警
#define XJ_SDK_ERR_6001                     "6001"    //模块输出过压告警
#define XJ_SDK_ERR_6002                     "6002"    //模块输出欠压告警
#define XJ_SDK_ERR_6003                     "6003"    //模块输出过温告警
#define XJ_SDK_ERR_6004                     "6004"    //模块风扇故障告警
#define XJ_SDK_ERR_6005                     "6005"    //模块均流告警
#define XJ_SDK_ERR_6006                     "6006"    //充电模块交流输入故障
#define XJ_SDK_ERR_6007                     "6007"    //模块缺相告警
#define XJ_SDK_ERR_6008                     "6008"    //模块PFC告警
#define XJ_SDK_ERR_6009                     "6009"    //模块输出短路告警
#define XJ_SDK_ERR_600A                     "600A"    //模块环境过问告警
#define XJ_SDK_ERR_600B                     "600B"    //模块三相不平衡告警
#define XJ_SDK_ERR_600C                     "600C"    //电能计量异常告警
#define XJ_SDK_ERR_600D                     "600D"    //并联接触器异常告警
#define XJ_SDK_ERR_600E                     "600E"    //充电模块输出过流告警
#define XJ_SDK_ERR_600F                     "600F"    //充电模块其他告警
#define XJ_SDK_ERR_6010                     "6010"    //充电模块直流输出过流故障
#define XJ_SDK_ERR_6011                     "6011"    //绝缘监测告警
#define XJ_SDK_ERR_6012                     "6012"    //充电桩过温告警
#define XJ_SDK_ERR_6013                     "6013"    //充电枪过温告警
#define XJ_SDK_ERR_6014                     "6014"    //并联接触器拒动/误动故障
#define XJ_SDK_ERR_6015                     "6015"    //计费控制单元自身故障终止
#define XJ_SDK_ERR_6016                     "6016"    //计费控制单元判断充电控制器故障停止
#define XJ_SDK_ERR_6017                     "6017"    //充电模块交流输入过压故障
#define XJ_SDK_ERR_6018                     "6018"    //充电模块交流输入欠压故障
#define XJ_SDK_ERR_8000                     "8000"    //业务分析正常
#define XJ_SDK_ERR_8001                     "8001"    //充电慢，车辆需求电流小
#define XJ_SDK_ERR_8002                     "8002"    //充电慢，充电桩性能限制
#define XJ_SDK_ERR_8003                     "8003"    //充电慢，模拟设备充电
#define XJ_SDK_ERR_8004                     "8004"    //充电慢，充电桩输出低于额定值
#define XJ_SDK_ERR_8005                     "8005"    //充电慢，其他原因
#define XJ_SDK_ERR_8006                     "8006"    //充电慢，充电时间过短
#define XJ_SDK_ERR_800A                     "800A"    //充不上电，模拟设备充电
#define XJ_SDK_ERR_800B                     "800B"    //充不上电，充电时间过短
#define XJ_SDK_ERR_8014                     "8014"    //用户拔枪终止充电
#define XJ_SDK_ERR_8015                     "8015"    //充电枪存在故障
#define XJ_SDK_ERR_9000                     "9000"    //全部电源模块通讯故障
#define XJ_SDK_ERR_9001                     "9001"    //全部电源模块故障
#define XJ_SDK_ERR_9002                     "9002"    //整流柜设备故障
#define XJ_SDK_ERR_9003                     "9003"    //充电时间达到最大值
#define XJ_SDK_ERR_9004                     "9004"    //BMS充电允许位禁止时间超许可时间
#define XJ_SDK_ERR_9005                     "9005"    //充电电压异常中止
#define XJ_SDK_ERR_9006                     "9006"    //充电电流异常中止
#define XJ_SDK_ERR_9007                     "9007"    //充电电压不匹配中止
#define XJ_SDK_ERR_9008                     "9008"    //系统模式类型转换中止
#define XJ_SDK_ERR_9009                     "9009"    //主动防护电池过温中止
#define XJ_SDK_ERR_900A                     "900A"    //主动防护电池低温中止
#define XJ_SDK_ERR_900B                     "900B"    //主动防护电池热失控中止
#define XJ_SDK_ERR_900C                     "900C"    //主动防护电池过充中止
#define XJ_SDK_ERR_900D                     "900D"    //主动防护BMS数据超范围中止
#define XJ_SDK_ERR_900E                     "900E"    //PDU过温中止
#define XJ_SDK_ERR_900F                     "900F"    //CP线低压故障中止
#define XJ_SDK_ERR_9010                     "9010"    //CAN地址冲突中止
#define XJ_SDK_ERR_9011                     "9011"    //车辆电池达到目标SOC中止
#define XJ_SDK_ERR_9012                     "9012"    //车辆达到总电压目标值中止
#define XJ_SDK_ERR_9013                     "9013"    //车辆达到单体电压目标值中止
#define XJ_SDK_ERR_9014                     "9014"    //启动放电超时中止
#define XJ_SDK_ERR_9015                     "9015"    //异常启动中止
#define XJ_SDK_ERR_9016                     "9016"    //错峰充电中止
#define XJ_SDK_ERR_9017                     "9017"    //放电过流保护中止
#define XJ_SDK_ERR_9018                     "9018"    //过放保护中止
#define XJ_SDK_ERR_9019                     "9019"    //电流反向保护中止
#define XJ_SDK_ERR_901A                     "901A"    //车载屏中止
#define XJ_SDK_ERR_901B                     "901B"    //弓急停中止
#define XJ_SDK_ERR_901C                     "901C"    //弓故障中止
#define XJ_SDK_ERR_901D                     "901D"    //PLC离线中止
#define XJ_SDK_ERR_901E                     "901E"    //WIFI连接异常
#define XJ_SDK_ERR_901F                     "901F"    //绝缘检测电压异常
#define XJ_SDK_ERR_9020                     "9020"    //BMS接收辨识未就绪报文超时
#define XJ_SDK_ERR_9021                     "9021"    //BMS接收辨识已就绪报文超时
#define XJ_SDK_ERR_9022                     "9022"    //BMS接收最大输出能力报文超时
#define XJ_SDK_ERR_9023                     "9023"    //BMS接收其它报文超时
#define XJ_SDK_ERR_9024                     "9024"    //BMS充电模式字异常
#define XJ_SDK_ERR_9025                     "9025"    //恒压时BMS需求电流超过最大允许充电电流
#define XJ_SDK_ERR_9026                     "9026"    //恒流时BMS需求电流超过最大允许充电电流
#define XJ_SDK_ERR_9027                     "9027"    //BMS停止报文数据异常
#define XJ_SDK_ERR_9028                     "9028"    //BMS超时报文数据异常
#define XJ_SDK_ERR_9029                     "9029"    //S2 开关动作超时
#define XJ_SDK_ERR_902A                     "902A"    //匹配阶段未完成，桩未准备好
#define XJ_SDK_ERR_902B                     "902B"    //整流柜内部通讯故障
#define XJ_SDK_ERR_902C                     "902C"    //烟感故障
#define XJ_SDK_ERR_93E0                     "93E0"    //启动失败(人)未知异常
#define XJ_SDK_ERR_93E1                     "93E1"    //启动失败(桩)未知异常
#define XJ_SDK_ERR_93E2                     "93E2"    //启动失败(车)未知异常
#define XJ_SDK_ERR_93E3                     "93E3"    //跳枪未知异常
#define XJ_SDK_ERR_93E4                     "93E4"    //启动失败(系统）未知异常
#define XJ_SDK_ERR_93E5                     "93E5"    //充电速度慢未知异常
#define XJ_SDK_ERR_93E6                     "93E6"    //充不上电未知异常
#define XJ_SDK_ERR_93E7                     "93E7"    //非法异常


#define XJ_SDK_ERR_OTHER                    "FFFF"    //其它故障或上述未列出故障
#define XJ_SDK_PLUGNED_OUT_INCHARGING       "FFFE"    //充电中，用户主动拔枪
#define XJ_SDK_STOP_BY_USER_ON_TCU_SCREEN   "FFFD"    //充电中，用户在屏幕主动停止



#endif
