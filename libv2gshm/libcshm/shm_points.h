// // BY ZF: 共享内存点位定义模板，供手动补充
// #pragma once


// // BY ZF: YX 遥信点位索引
// enum YXPoint {
//     dualChargerLock = 18,
//     ddbFlushLock,
//     errorAll,
//     warningAll,

//     resetAfterUpgrade = 24,
//     runtimeSendAllow,
//     gpsValid,

//     registerSync                 = 30,   // 注册校验同步标识
//     esamVisitLock,                     // ESAM访问锁
//     dualAuthSync,                      // 双向认证同步标识
//     esamRandomSync,                    // ESAM随机数同步标识
//     esamRandomValid,                   // ESAM随机数有效标识
//     devicePowerOff,                    // 设备掉电状态
//     clockSync,                         // 时钟同步标志
//     bmsVoltageCurrentCheckAllow,       // BMS电压电流校验允许
//     trickleEnable,                     // 涓流使能标识

//     tcuFull                     = 41,   // TCU空间满标志
//     readerFault,                       // 读卡器故障
//     esamFault,                         // ESAM故障
//     tradeRecordFullWarning,            // 交易记录已满告警
//     tradeRecordSaveFailure,            // 交易记录存储失败
//     RegisterVerificationFailure,       // 平台注册校验失败
//     programVerificationFailure,        // 程序文件校验失败
//     libError,                          // lib库文件错误
//     esamIllegal,                       // ESAM非法
//     meterAbnormal,                     // 电表进程运行异常
//     assetFileMissing,                  // 资产文件损坏或不存在
//     tradeRecordAbnormal,               // 交易记录文件异常
//     configFileInvalid,                 // config配置文件不存在或非法
//     appFileInvalid,                    // app配置文件不存在或异常
//     feeModelValid,                     // 计费模型有效标志
//     paramFileInvalid,                  // param配置文件不存在或异常
//     offlineCharging,                   // 离线停机标志


// /*  desname[0]:组件类型,
//     desname[1]:上送方式,
//     desname[2]:数据上送周期,
//     desname[3]:数据上送周期值：true=生效/false=无效"*/
//     powerControlUnit = 59,          // 功率控制模块
//     chargeModule,                    // 充电模块
//     switchModule,                    // 开关模块
//     envModule, // 环境信息采集模块
//     chargeModuleInfo, // 充电模块运行信息统计数据


//     esamTerminalId = 72,                 // ESAM终端机编号
//     onlineUnlockEntry,   // 联机解扣数据条目1/卡余额查询结果
//     onlineUnlockEntry2,             // 联机解扣数据条目2
//     onlineUnlockEntry3,             // 联机解扣数据条目3
//     onlineUnlockEntry4,             // 联机解扣数据条目4
//     onlineUnlockEntry5,             // 联机解扣数据条目5
//     blacklistTimestamp,             // 黑名单时间戳
//     blacklistCardFlag,              // 黑名单卡标志位

//     configInitFlag = 81,                 // 配置初始化标识

//     ipAddrValid = 83,                    // IP地址True有效
//     portValid,                      // Port端口号True有效
//     stationShortName,               // 站简称
//     pileShortName,                  // 桩简称



//     ashRetryCardSeek = 88,                // 解灰二次寻卡标识
//     adFreeze,                        // 广告冻结


    
//     platformUnlockControl = 96,           // 平台控制解锁
//     securityAuthFlag,                // 安全认证标识

//     portTotalFault = 99,                // 端口总故障
//     controllerTotalFault,          // 控制器总故障
//     totalAlarm,                    // 总告警
//     emgStopButtonActionFault,      // 急停按钮动作故障
//     smokeSensorFault,              // 烟感故障
//     acInputBreakerFault,           // 交流输入断路器故障
//     dcBusContactorFail,            // 直流母线输出接触器拒动/误动故障
//     dcBusFuseFault,                // 直流母线输出熔断器故障
//     chargeSocketLockFault,         // 充电接口电子锁故障
//     chargerFanFault,               // 充电机风扇故障
//     lightningArresterFault,        // 避雷器故障
//     insulationMonitorFault,        // 绝缘监测故障
//     batteryReverseFault,           // 电池反接故障
//     vehicleGuideFaultCharging,     // 充电中车辆控制导引故障
//     chargerOverTempFault,          // 充电桩过温故障
//     chargeSocketOverTempFault,     // 充电接口过温故障
//     chargeGunNotDockedWarn,        // 充电枪未归位告警
//     bmsCommFault,                  // BMS通信故障
//     inputVoltOverFault,            // 输入电压过压故障
//     inputVoltUnderFault,           // 输入电压欠压故障
//     dcBusOverVoltFault,            // 直流母线输出过压故障
//     dcBusUnderVoltFault,           // 直流母线输出欠压故障
//     dcBusOverCurrFault,            // 直流母线输出过流故障
//     chargerModuleFault,            // 充电模块故障
//     chargerModuleAcInputWarn,      // 充电模块交流输入告警
//     chargerModuleAcInputOverWarn,  // 充电模块交流输入过压告警
//     chargerModuleAcInputUnderWarn, // 充电模块交流输入欠压告警
//     chargerModuleAcPhaseLostWarn,  // 充电模块交流输入缺相告警
//     chargerModuleOutShortWarn,     // 充电模块输出短路告警
//     chargerModuleOutOverCurrWarn,  // 充电模块输出过流告警
//     chargerModuleOutOverVoltWarn,  // 充电模块输出过压告警
//     chargerModuleOutUnderVoltWarn, // 充电模块输出欠压告警
//     chargerModuleOverTempWarn,     // 充电模块过温告警
//     chargerModuleCommWarn,         // 充电模块通信告警
//     chargerModuleFanWarn,          // 充电模块风扇告警
//     vehicleConnectStatusA,         // 车辆连接状态A


//     chargeGunSocketStatus,            // 充电桩充电枪座状态
//     plugELockStatus,                  // 充电接口电子锁状态
//     dcOutputContactorStatus,          // 直流输出接触器状态
//     pileDoorAccessFault,              // 充电桩门禁故障
//     dcOutputContactorStickFault,      // 直流输出接触器粘连故障
//     insulationMonitorAlarm,           // 绝缘监测告警
//     dischargeLoopFault,               // 泄放回路故障
//     pileOverTempAlarm,                // 充电桩过温告警
//     gunOverTempAlarm,                 // 充电枪过温告警
//     acInputContactorActFault,         // 交流输入接触器据动/误动故障
//     acInputContactorStickFault,       // 交流输入接触器粘连故障
//     auxPowerFault,                    // 辅助电源故障
//     parallelContactorActFault,        // 并联接触器据动/误动故障
//     parallelContactorStickFault,      // 并联接触器粘连故障
//     bridgeContactorStatus,            // 桥接接触器状态
//     moduleEmgStopStatus,              // 模块紧急停机状态
//     pileWaterImmersionFault,          // 充电桩水浸故障
//     chargerCabinetDoorFault,          // 充电机柜门禁故障
//     chargerCabinetImmersionFault,     // 充电机柜水浸故障
//     chargerCabinetSmokeFault,         // 充电机柜烟雾故障

//     powerCtrlModuleCommFault,          // 功率控制模块通信故障
//     powerCtrlModuleCommWarn,           // 功率控制模块通信告警
//     powerCtrlModuleFault,              // 功率控制模块故障
//     powerCtrlModuleWarn,               // 功率控制模块告警
//     switchModuleCommFault,             // 开关模块通信故障
//     switchModuleCommWarn,              // 开关模块通信告警
//     switchModuleFault,                 // 开关模块故障
//     switchModuleWarn,                  // 开关模块告警
//     dcMainCtrlBmsDetectFault,          // 直流充电主控模块判断BMS故障
//     bmsSendFault,                      // BMS发送故障
//     chargeModuleDischargeWarn,         // 充电模块泄放告警
//     powerDataAbnormal,                 // 电能数据异常
//     chargerOtherFault,                 // 充电机其他故障
//     canVersionCheckError,              // CAN通讯版本校验异常（TCU与控制器CAN通讯握手版本失败）
//     tcuOtherFault,                     // TCU其他故障（数据校验异常）
//     otherTypeFault,                    // 其他类型故障
//     reserveStartChargeFlag,            // 预约启动充电标识（3.1.8）
//     userFirstAuthSuccFlag,             // 用户第一次鉴权成功标识
//     userSecondAuthSuccFlag,            // 用户第二次鉴权成功标识
//     meterStartChargeFlag,              // 电表启动充电的标识
//     canModuleStartChargeFlag,          // CAN通讯模块启动充电标识


//     userFirstRecordStoreFlag,          // 用户初次存储交易记录
//     authPassFlag,                      // 鉴权通过标识（给runtime标识在整个充电过程中）
//     recordSecondStoreFlag,             // 交易记录二次存储标识
//     canStopChargeFlag,                 // CAN停止充电标识
//     settlementWindowPopupFlag,         // 结算窗口弹出（hmi）
//     fullAmountFlag,                    // 金额已充满
//     stopChargeDoneFlag,                // 停机完成
//     gunUnsettlePullOutFlag,            // 未结算拔枪
//     notifyCardUnpullSettleFlag,        // 通知卡未拔枪结算
//     chargerStartFlag,                  // 充电机启动标识
//     chargerStartFailFlag,              // 充电机启动失败
//     hmiChargeStartTimeout90sFlag,      // hmi充电启动90s超时
//     currPageStatus,                    // 当前所处页面
//     backendPowerAdjustCmdRecvFlag,     // 接到后台功率调节指令
//     ctrlPowerAdjustFeedbackRecvFlag,   // 收到控制器功率调节反馈
//     powerAdjustingFlag,                // 功率调节中标识
//     backendServiceStartStopCmdRecvFlag,// 接到后台服务启停指令
//     ctrlStartStopFeedbackRecvFlag,     // 收到控制器启停指令反馈
//     trickleTimeoutStopFlag,            // 涓流充电超时停机标识
//     ecpuPlatformAuthSuccFlag,          // 即充即充平台鉴权成功标识
//     chargerReqECpuStartFlag,           // 充电机请求即插即充启动
//     acInputContactorStatus,            // 交流输入接触器状态
//     acContactorCtrlStatus,             // 交流接触器控制状态
//     dcPortContactorK1CtrlStatus,       // 充电端口直流接触器K1控制状态
//     dcPortContactorK2CtrlStatus,       // 充电端口直流接触器K2控制状态
//     dcPortAuxPowerSwitchStatus,        // 充电端口辅助电源输出开关状态
//     dcPortAuxPowerSwitchCtrlStatus,    // 充电端口辅助电源输出开关控制状态
//     chargerCabinetFanSwitchCtrlStatus, // 充电机柜风机开关控制状态
//     dcPortELockCtrlStatus,             // 充电端口电子锁控制状态
//     peDisconnectFault,                 // PE断线故障
//     ctrlGuideVoltAbnormal,             // 控制导引电压异常
//     chargerModuleStartFail,            // 充电模块启动失败
//     chargerModuleShutdownFail,         // 充电模块关机失败
//     chargerModuleAddressAbnormal,      // 充电模块地址异常
//     coolingSystemFault,                // 散热系统故障
//     chargerCabinetOverTempWarn,        // 充电机柜过温告警
//     chargerCabinetOverTempFault,       // 充电机柜过温故障
//     powerDistributeStrategyFail,       // 充电桩执行功率分配策略失败故障
//     noAvailableModuleFault,            // 无空闲模块可用故障
//     chargerModule                     // 充电模块（补录：如为集合标识可后续细拆）

    
//     powerCtrlModuleCommTimeout,            // 功率控制模块间通信超时
//     chargerEnvHumidityWarn,                // 充电机环境湿度告警
//     qrResultReturnFlag,                    // QR生成后返回标识
//     dualGunChargeOneCarFlag,               // 双枪充一车返回标识
//     screenUnlockCtrl,                      // 屏幕解锁控制
//     canCommAbnormalDetect,                 // CAN通讯异常识别
//     plugAndPlayTypeChargeStopDuringStartA, // 启动中停止A枪即插即用类型充电
//     plugAndPlayTypeChargeStartAOrV2gJump,  // 启动A枪即插即用类型充电/v2g模式下通知界面跳转
//     occupiedStopFlag,                      // 已占用（停机标识）
//     meterEnergyAbnormal,                   // 电表电能值异常
//     canModuleCommAbnormal,                 // CAN模块通讯异常
//     meterCommFault,                        // 电表通讯故障
//     meteringReadyFlag,                     // 计量就绪标志
//     tcuChargePowerAdjustReplyTimeout,      // TCU充电功率调节应答超时
//     tcuVehicleAuthFrameRecvTimeout,        // TCU车辆验证数据帧接收超时
//     tcuChargeParamsCmdReplyTimeout,        // TCU下发充电参数应答超时
//     tcuChargeStartCmdReplyTimeout,         // TCU充电启动命令应答超时
//     tcuWaitChargeStartFinishTimeout,       // TCU等待充电启动完成状态超时
//     tcuChargeStopCmdReplyTimeout,          // TCU充电停止命令应答超时
//     tcuWaitChargeStopFinishTimeout,        // TCU等待充电停止完成状态超时
//     tcuSyncTimeOpReplyTimeout,             // TCU对时操作应答超时

//     tcuChargeServiceStartStopReplyTimeout,        // TCU充电服务启停应答超时
//     tcuELockCtrlReplyTimeout,                     // TCU电子锁控制应答超时
//     tcuChargerConfigQueryReplyTimeout,            // TCU充电桩配置信息查询应答超时
//     tcuRemoteSignalFrameRecvTimeout,              // TCU遥信报文接收超时
//     tcuTelemetryFrameRecvTimeout,                 // TCU遥测报文接收超时
//     tcuStartDownloadCmdReplyTimeout,              // TCU启动下载命令应答超时
//     tcuReqSendDataFrameReplyTimeout,              // TCU请求发送数据帧应答超时
//     tcuSendDataFrameReplyTimeout,                 // TCU数据发送帧应答超时
//     tcuProgramVerifyCmdReplyTimeout,              // TCU程序校验命令帧应答超时
//     ccuRemoteSignalFrameRecvTimeout,              // CCU遥信报文接收超时
//     ccuTelemetryFrameRecvTimeout,                 // CCU遥测报文接收超时
//     ccuSendChargeParamsFrameRecvTimeout,          // CCU下发充电参数报文接收超时
//     ccuReqSendDataFrameRecvTimeout,               // CCU接收请求发送数据帧超时
//     ccuSendDataFrameRecvTimeout,                  // CCU接收数据发送帧超时
//     ccuProgramVerifyCmdFrameRecvTimeout,          // CCU接收程序校验命令帧超时
//     ccuChargeStartFinishConfirmTimeout,           // CCU充电启动完成状态确认超时
//     ccuChargeStopFinishConfirmTimeout,            // CCU充电停止完成状态确认超时
//     ccuVehicleAuthFrameRecvTimeout,               // CCU车辆识别鉴权帧接收超时
//     ccuRecvImmediateResetCmdFrameTimeout,         // CCU接收立即复位命令帧超时
//     bstBatteryOverTemp,                           // BST电池组过温
//     bstOverCurrent,                               // BST电流过大
//     bstVoltageAbnormal,                           // BST电压异常
//     bstDetectPoint2VoltageFault,                  // BST检测点2电压检测故障
//     bstInsulationFault,                           // BST绝缘故障
//     bstConnectorFault,                            // BST连接器故障
//     bstOtherFault,                                // BST其他故障
//     bstStopUnknownFault,                          // BST停止未知故障



//     bstComponentOrOutputConnectorOvertemp,    // BST元件、输出连接器过温
//     bstStopChargeMsgTimeout,                  // BST中止充电报文超时
//     gunElectrolockLockFault,                  // 充电枪电子锁上锁故障
//     gunElectrolockUnlockFault,                // 充电枪电子锁解锁故障
//     chargeModuleOvertemp,                     // 充电模块过温
//     dischargeRelayRejectAct,                  // 泄放继电器拒动
//     dischargeRelaySticking,                   // 泄放继电器粘连
//     lvAuxPowerOutputRelayFault,               // 低压辅助电源输出继电器故障
//     chargerCabinetHeaterFault,                // 充电机柜加热部件故障
//     outputCurrentGreaterThanVehicleDemand,    // 输出电流大于车辆需求电流
//     powerControllerNotOnDuringCharging,       // 充电过程中功率控制器工作状态非开机状态
//     powerControllerRemoteSignalOrTelemetryTimeout, // 功率控制器接收遥控、遥信报文超时故障
//     powerControllerEnvCommTimeout,            // 功率控制器与环境信息采集模块通讯超时
//     chargeModuleNoOutputAlarm,                // 充电模块不输出告警
//     switchModuleRejectOrFalseActFault,        // 开关模块拒动/误动故障
//     switchModuleStickingFault,                // 开关模块粘连故障
//     switchModuleRejectOrFalseActAlarm,        // 开关模块拒动/误动告警
//     switchModuleStickingAlarm,                // 开关模块粘连告警
//     chargeModuleInputUnbalanceAlarm,          // 充电模块输入不平衡告警


//     chargeModulePFCOverTempFault,                 // 充电模块PFC电路过温故障
//     chargeModuleMainTransformerOverTempFault,     // 充电模块主变压器过温故障
//     chargeModuleDCDCOverTempFault,                // 充电模块DC/DC电路过温故障
//     chargeModuleOutputIsolationDiodeFault,        // 充电模块输出隔离二极管故障
//     chargeModuleStartFault,                       // 充电模块启动故障（非保护动作造成的充电模块启动失败）
//     chargeModulePFCTotalFault,                    // 充电模块PFC电路总故障
//     chargeModulePFCOverVoltageFault,              // 充电模块PFC电路过压故障
//     chargeModulePFCPinVoltageFault,               // 充电模块PFC电路偏压故障
//     chargeModuleDCDCTotalFault,                   // 充电模块DC/DC电路总故障
//     chargeModuleHighLowSwitchFault,               // 充电模块高低压切换电路故障
//     chargeModuleOutputCircuitUnbalanceFault,      // 充电模块输出电路不平衡故障
//     dualGunOneVehicleCtrlCommTimeout,             // 两枪同充一车模式下充电控制器间通信超时故障
//     powerExpansionCtrlAddrConflictFault,          // 功率扩容模式下功率控制器地址冲突故障
//     chargeControllerAddrConflictFault,            // 充电控制器地址冲突故障
//     reserved,                                    // 预留
//     controllerSelfCheckTimeoutFault,              // 控制器自检超时故障
//     parkingLockFault,                             // 车位锁故障
//     parkingLockBatteryDepletionFault,             // 车位锁电池耗尽故障
//     parkingLockLockFailFault,                     // 车位锁落锁失败故障
//     chargeCtrlModuleClockAbnormalAlarm,           // 充电控制模块时钟异常告警
//     chargeCtrlModuleTimeSyncCycleAlarm,           // 充电控制模块检测对时操作超周期告警
//     chargeCtrlModuleTimeSyncDataInvalidAlarm,     // 充电控制模块检测下发对时数据非法告警


//     dcFusePosStatus = 329,                      // DC+熔断器状态
//     dcFuseNegStatus,                      // DC-熔断器状态


//     pwrModuleStatistics = 333,                  // 电源模块统计信息
//     chargeControlModuleACDC,              // 充电控制模块（交/直）流
//     v2gAuthRequestConfirm,                // V2G认证请求确认帧（向控制器下发充电方向）
//     assemblyDataTransferCtrl,             // 组部件数据传输控制位
//     stopChargingFlag,                     // 停止充电标识
//     stopChargingMeteringFlag,             // 停止充电计量标识
//     controllerReadyFlag,                  // 控制器就绪标识
//     electrolockRemoteCtrlFlag1,           // 电子锁远程控制标志
//     electrolockRemoteCtrlFlag2,           // 电子锁远程控制标志 (重复，保留第二份以示存在两处同名中文)
    
    
//     startupCompleted = 343,                     // 启动完成
//     deviceMaintainFlag,                   // 设备维护标志
//     licensePlateAuthResult,               // 车牌鉴权结果
//     pulseChargeFlag,                      // 脉冲充电标识
//     v2gSOCUpperLimit,                     // V2G模式下达到SOC极限值
//     v2gSOCUpperLimitEvent,                // V2G模式下达到SOC极限事件


// };




// // BY ZF: YC 遥测点位索引
// enum YCPoint {
//     // BY ZF: 示例 —— 请将下方名称改为实际点位英文/拼音，保持注释中的中文描述
//     YC_SAMPLE_PLACEHOLDER = 0, // 电压实时值（示例，使用前请修改或删除）
//     // BY ZF: TODO: 在此追加更多 YC 点位，如：
//     // YC_DIANYA_X = 1, // 电压X
// };

// // BY ZF: DD 电度点位索引
// enum DDPoint {
//     DD_SAMPLE_PLACEHOLDER = 0, // 总电度（示例，使用前请修改或删除）
//     // BY ZF: TODO: 在此追加更多 DD 点位
// };

// // BY ZF: 如果存在额外的共享内存块，可在此继续追加枚举或常量定义


