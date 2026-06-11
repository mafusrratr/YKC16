### 命令帧数据，cmd_data
**充电启动业务**:
- 负荷控制开关,是否根据用户类型提供不同功率输出1 Byte
- 即插即充标识,01H：非即插即充1 Byte
- 辅助电源电压1 Byte
- 启动应答失败原因   1 Byte


**充电停止业务**:
- 停止充电原因	BIN	1 Byte
- tcu停机代码	BIN	1 Byte
- 主控确认标识	BIN	1 Byte


**版本校验业务**:
- 计费控制单元通信协议版本号	压缩BCD码	2 Byte	通信协议版本号组成分为：主版本号、次版本号，本协议规定的协议版本号为V.1.21。示例：通信协议版本号为 V1.21 ，则主版本号表示为：01H，次版本号表示为：21H，通信协议版本号表示为：01H 21H，报文字节发送顺序为01H 21H。
- 充电控制模块通信协议版本号	压缩BCD码	2 Byte	通信协议版本号组成分为：主版本号、次版本号，本协议规定的协议版本号为V.1.21。示例：通信协议版本号为 V1.21 ，则主版本号表示为：01H，次版本号表示为：21H，通信协议版本号表示为：01H 21H，报文字节发送顺序为01H 21H。


**版本校验业务**:
- 充电桩（机）编号	压缩BCD码	7 Byte
- 确认标识

**充电启停业务**:
- 充电服务启停操作指令	BIN	1 Byte	01H：充电服务停止
02H：充电服务启用 其它：无效

**电子锁控制业务**:
- 操作指令	BIN	1 Byte	01H：上锁 02H：解锁
- 确认标识	BIN	1 Byte	00H：成功 01H：失败

**功率调节业务**:
- 功率调节指令类型	BIN	1 Byte	功率调节指令参数类型分为：绝对值和百分比两种。
01H：功率绝对值，输出值=功率调节参数值；
02H：百分比，输出值=额定输出功率×百分比。
- 功率调节参数	BIN	2 Byte	01H：绝对值，数据分辨率：0.1 kW/位；偏移量：-1000.0 kW；数据范围：-1000.0kW ~ +1000.0kW（正表示充电，负是放电）
02H：百分比，数据分辨率：%1/位；偏移量：0%；数据范围：0%~100%

**充电桩配置信息**:
- 厂家编码	BIN	4 Byte	
- 充电控制器序列号	压缩BCD码	12 Byte	生产厂商应保证每块充电控制器的序列号唯一。
- 充电控制器软件版本	BIN	3 Byte	软件版本号组成分为：主版本号，次版本号和发行版本号。
示例：软件版本号为V1.00.00，主版本号标识为01H，次版本号标识为00H，发行版本号为00H，报文字节发送顺序为01H 00H 00H。
- 充电控制器软件日期	压缩BCD码	4 Byte	Data1 ～ Data2：年
Data3：月份
Data4：日期
示例：软件日期为2017年05月04日，表示为20H 17H 05H 04H，报文字节发送顺序为17H 20H 05H 04H。
- 充电机最高输出电压	BIN	2 Byte	数据分辨率：0.1 V/位；0V 偏移
- 充电机最低输出电压	BIN	2 Byte	数据分辨率：0.1 V/位；0V 偏移
- 充电机最大输出电流	BIN	2 Byte	数据分辨率：0.1 A/位；-400A 偏移数据范围：-400.0A ～ 0.0A
- 充电机最小输出电流	BIN	2 Byte	数据分辨率：0.1 A/位；-400A 偏移数据范围：-400.0A ～ 0.0A


### 充电状态帧数据，state_data
**充电启动完成**:
- 充电中失败原因  1BYTE
- 充电桩与BMS通信协议版本号	Data5	BIN	3 Byte
- BMS与充电桩通信协议版本号	Data8	BIN	3 Byte
- 充电桩与BMS 握手结果	Data11	BIN	1 Byte
- 电池类型	Data12	BIN	1 Byte
- 最高允许温度	Data13	BIN	1 Byte

- BMS 最高允许充电电压	Data14	BIN	2 Byte
- 单体最高允许充电电压	Data16	BIN	2 Byte
- 最高允许充电电流	Data18	BIN	2 Byte
- 整车动力蓄电池额定总电压	Data20	BIN	2 Byte
- 整车动力蓄电池当前电压	Data22	BIN	2 Byte
- 整车动力蓄电池额定容量	Data24	BIN	2 Byte
- 整车动力蓄电池标称能量	Data26	BIN	2 Byte
- 整车动力蓄电池荷电状态	Data28	BIN	2 Byte
- 充电机最高输出电压	Data30	BIN	2 Byte
- 充电机最低输出电压	Data32	BIN	2 Byte
- 充电机最大输出电流	Data34	BIN	2 Byte
- 充电机最小输出电流	Data36	BIN	2 Byte
- 车辆识别码	Data38	ASCII	17 Byte
- 电池组生产厂商	Data55	ASCII	4 Byte
- 电池组序列号	Data59	遵循 GB/T 27930-2015协议规定	4 Byte
- 电池组生产日期-年	Data63	BIN	1 Byte
- 电池组生产日期-月	Data64	BIN	1 Byte

- 电池组生产日期-日	Data65	BIN	1 Byte
- 电池组充电次数	Data66	BIN	3 Byte
- 电池组产权标识	Data69	BIN	1 Byte
- BMS软件版本号	Data77	BIN	8 Byte


**充电停止完成状态**:
- 停止原因 1 Byte
- 停机成功标识 1byte
- BMS中止充电原因	Data4	BIN	1 Byte
- BMS充电故障原因	Data5	BIN	2 Byte
- BMS 中止错误原因	Data7	BIN	1 Byte
- 中止荷电状态 SOC（%）	Data8	BIN	1 Byte
- 动力蓄电池单体最低电压（V）	Data9	BIN	2 Byte
- 动力蓄电池单体最高电压（V）	Data11	BIN	2 Byte
- 动力蓄电池最低温度（ºC）	Data13	BIN	1 Byte
- 动力蓄电池最高温度（ºC）	Data14	BIN	1 Byte

- 接收SPN2560=0x00 的充电机辨识报文超时	Data15	BIN	2 bit
- 接收SPN2560=0xAA 的充电机辨识报文超时	Data15	BIN	2 bit
- 接收充电机的时间同步和充电机最大输出能力报文超时	Data16	BIN	2 bit
- 接收充电机完成充电准备报文超时	Data16	BIN	2 bit
- 接收充电机充电状态报文超时	Data17	BIN	2 bit
- 接收充电机中止充电报文超时	Data17	BIN	2 bit
- 接收充电机充电统计报文超时	Data18	BIN	2 bit
- BMS 其他错误	Data18	BIN	6 bit
- 接收 BMS 和车辆的辨识报文超时	Data19	BIN	2 bit
- 接收电池充电参数报文超时	Data20	BIN	2 bit
- 接收 BMS 完成充 电准备报文超时	Data20	BIN	2 bit
- 接收电池充电总状 态报文超时	Data21	BIN	2 bit

- 接收电池充电要求报文超时	Data21	BIN	2 bit
- 接收 BMS 中止充电报文超时	Data21	BIN	2 bit
- 接收 BMS 充电统计报文超时	Data22	BIN	2 bit
- 充电机其他错误	Data22	BIN	6 bit

**充电桩状态信息**:
- 充电桩状态	BIN	1 Byte
车辆VIN信息	BIN	17 Byte
电池组充电次数	BIN	3 Byte
整车动力蓄电池荷电状态	BIN	2 Byte
整车动力蓄电池当前电池电压	BIN	2 Byte


**车辆识别信息**:
- 车辆VIN信息	BIN	17 Byte
- 电池组充电次数	BIN	3 Byte
- 整车动力蓄电池荷电状态	BIN	2 Byte
- 整车动力蓄电池当前电池电压	BIN	2 Byte
- vin鉴权结果标志


### 充电数据帧数据，state_data
**遥测帧20**:
- 充电输出电压	Data2	BIN	2 Byte
- 充电输出电流	Data4	BIN	2 Byte
- SOC	Data6	BIN	1 Byte
- 电池组最低温 度	Data7	BIN	1 Byte
- 电池组最高温 度	Data8	BIN	1 Byte
- 单体电池最高 电压	Data9	BIN	2 Byte
- 单体电池最低 电压	Data11	BIN	2 Byte
- 充电机环境温 度	Data13	BIN	1 Byte
- 充电导引电压	Data14	BIN	2 Byte
- BMS需求电压	Data16	BIN	2 Byte
- BMS需求电流	Data18	BIN	2 Byte
- 充电模式	Data20	BIN	1 Byte
- BMS充电电压测量值	Data21	BIN	2 Byte
- BMS充电电流测量值	Data23	BIN	2 Byte
- 估算剩余充电时间	Data25	BIN	2 Byte
- 充电接口温度探头1温度(充电接口1 DC+)	Data27	BIN	1 Byte
- 充电接口温度探头2温度(充电接口1 DC-)	Data28	BIN	1 Byte
- 充电接口温度探头3温度(充电接口2 DC+)	Data29	BIN	1 Byte
- 充电接口温度探头4温度(充电接口2 DC-)	Data30	BIN	1 Byte
- 最高单体动力蓄电池电压所在编号	Data31	BIN	1 Byte
- 最高温度检测点编号	Data32	BIN	1 Byte
- 最低动力蓄电池温度检测点编号	Data33	BIN	1 Byte
- 充电机进风口温度	Data34	BIN	1 Byte
- 充电机出风口温度	Data35	BIN	1 Byte
- 充电机环境湿度	Data36	BIN	1 Byte


**遥测帧21**:
- 当前充电电量	BIN	2 Byte	数据分辨率：0.1 kWh/位，0 kWh 偏移量；数据范围：0 kWh~1000 kWh;（待机过程中此数据项为 0）
- 累计充电时长	BIN	2 Byte	数据分辨率：1 min/位，0 min 偏移量；数据范围：0min~6000 min；（待机过程中此数据项为 0）

**遥信数据22**:
- 工作状态	Data2	BIN	2 bit（bit1～2）
- 总故障	Data2	BIN	1 bit（bit3）
- 总告警	Data2	BIN	1 bit（bit4）
- 急停按钮动作 故障	Data2	BIN	1 bit（bit5）
- 烟雾故障	Data2	BIN	1 bit（bit6）
- 交流输入断路器故障	Data2	BIN	1 bit（bit7）
- 直流母线输出接触器拒动/误动故障	Data2	BIN	1 bit（bit8）
- 直流母线输出熔断器故障	Data3	BIN	1 bit（bit1）
- 充电接口电子锁故障	Data3	BIN	1 bit（bit2）
- 充电桩风扇故障	Data3	BIN	1 bit（bit3）
- 避雷器故障	Data3	BIN	1 bit（bit4）
- 绝缘监测故障	Data3	BIN	1 bit（bit5）
- 电池极性反接故障	Data3	BIN	1 bit（bit6）
- 充电中车辆控制导引故障	Data3	BIN	1 bit（bit7）
- 充电桩过温故 障	Data3	BIN	1 bit（bit8）
- 充电接口过温故障	Data4	BIN	1 bit（bit1）
- 充电枪未归位告警	Data4	BIN	1 bit（bit2）
- BMS 通信故障	Data4	BIN	1 bit（bit3）
- 输入电压过压故障	Data4	BIN	1 bit（bit4）
- 输入电压欠压故障	Data4	BIN	1 bit（bit5）
- 直流母线输出过压故障	Data4	BIN	1 bit（bit6）
- 直流母线输出欠压故障	Data4	BIN	1 bit（bit7）
- 直流母线输出欠压故障	Data4	BIN	1 bit（bit7）
- 直流母线输出过流故障	Data4	BIN	1 bit（bit8）
- 充电模块故障	Data5	BIN	1 bit（bit1）
- 充电模块交流输入故障	Data5	BIN	1 bit（bit2）
- 充电模块交流输入过压故障	Data5	BIN	1 bit（bit3）
- 充电模块交流输入欠压故障	Data5	BIN	1 bit（bit4）
- 充电模块交流输入缺相故障	Data5	BIN	1 bit（bit5）
- 充电模块直流输出短路故障	Data5	BIN	1 bit（bit6）
- 充电模块直流输出过流故障	Data5	BIN	1 bit（bit7）
- 充电模块直流输出过压故障	Data5	BIN	1 bit（bit8）
- 充电模块直流输出欠压故障	Data6	BIN	1 bit（bit1）
- 充电模块直流输出欠压故障	Data6	BIN	1 bit（bit1）
- 充电模块过温故障	Data6	BIN	1 bit（bit2）
- 充电模块通信故障	Data6	BIN	1 bit（bit3）
- 充电模块风扇故障	Data6	BIN	1 bit（bit4）
- 车辆连接状态	Data6	BIN	1 bit（bit5）
- 充电桩充电枪座状态	Data6	BIN	1 bit（bit6）
- 充电接口电子锁状态	Data6	BIN	1 bit（bit7）
- 直流输出接触器状态	Data6	BIN	1 bit（bit8）
- 直流输出接触器状态	Data6	BIN	1 bit（bit8）
- 其它类型故障	Data7～8	BIN	2 Byte


**遥信数据23**:

- 充电桩门禁故障	Data2	BIN	1 bit（bit1）
- 直流输出接触器粘连故障	Data2	BIN	1 bit（bit2）
- 绝缘监测告警	Data2	BIN	1 bit（bit3）
- 泄放回路故障	Data2	BIN	1 bit（bit4）
- 充电桩过温告警	Data2	BIN	1 bit（bit5）
- 充电枪过温告警	Data2	BIN	1 bit（bit6）
- 交流输入接触器拒动/误动故障	Data2	BIN	1 bit（bit7）
- 交流输入接触器粘连故障	Data2	BIN	1 bit（bit8）
- 辅助电源故障	Data3	BIN	1 bit（bit1）
- 桥接接触器拒动/误动故障	Data3	BIN	1 bit（bit2）
- 桥接接触器粘连故障	Data3	BIN	1 bit（bit3）
- 桥接接触器状态	Data3	BIN	1 bit（bit4）
- 模块紧急停机状态	Data3	BIN	1 bit（bit5）
- 充电桩水浸故障	Data3	BIN	1 bit（bit6）
- 充电机柜门禁故障	Data3	BIN	1 bit（bit7）
- 充电机柜水浸故障	Data3	BIN	1 bit（bit8）
- 充电机柜烟雾故障	Data4	BIN	1 bit（bit1）
- 功率控制模块通信故障	Data4	BIN	1 bit（bit2）
- 功率控制模块通信告警	Data4	BIN	1 bit（bit3）
- 功率控制模块故障	Data4	BIN	1 bit（bit4）
- 功率控制模块告警	Data4	BIN	1 bit（bit5）
- 开关模块通信故障	Data4	BIN	1 bit（bit6）
- 开关模块通信告警	Data4	BIN	1 bit（bit7）
- 开关模块故障	Data4	BIN	1 bit（bit8）
- 开关模块告警	Data5	BIN	1 bit（bit1）
- 直流充电控制模块判断BMS故障	Data5	BIN	1 bit（bit2）
- BMS发送故障信息	Data5	BIN	1 bit（bit3）
- 充电模块泄放故障	Data5	BIN	1 bit（bit4）
- 充电机请求即插即充启动	Data5	BIN	1 bit（bit5）
- 交流输入接触器状态	Data5	BIN	1 bit（bit6）
- 交流接触器控制状态	Data5	BIN	1 bit（bit7）
- 充电端口直流接触器K1控制状态	Data5	BIN	1 bit（bit8）
- 充电端口直流接触器K2控制状态	Data6	BIN	1 bit（bit1）
- 充电端口辅助电源输出开关控制状态	Data6	BIN	1 bit（bit4）
- 充电端口辅助电源输出开关控制状态	Data6	BIN	1 bit（bit5）
- 充电机柜风机开关控制状态	Data6	BIN	1 bit（bit6）
- 充电端口电子锁控制状态	Data6	BIN	1 bit（bit7）
- PE断线故障	Data7	BIN	1 bit（bit1）
- 控制导引电压异常	Data7	BIN	1 bit（bit2）
- 充电模块启动失败	Data7	BIN	1 bit（bit3）
- 充电模块启动失败	Data7	BIN	1 bit（bit3）
- 充电模块关机失败	Data7	BIN	1 bit（bit4）
- 充电模块地址异常	Data7	BIN	1 bit（bit5）
- 散热系统故障	Data7	BIN	1 bit（bit6）
- 充电机柜过温告警	Data7	BIN	1 bit（bit7）
- 充电机柜过温故障	Data7	BIN	1 bit（bit8）
- 充电桩执行功率分配策略失败故障	Data8	BIN	1 bit（bit1）
- 充电桩执行功率分配策略失败故障	Data8	BIN	1 bit（bit1）
- 无空闲模块可用故障	Data8	BIN	1 bit（bit2）
- 预充阶段调压失败	Data8	BIN	1 bit（bit3）
- 功率控制模块间通信超时	Data8	BIN	1 bit（bit4）
- 充电机环境湿度告警	Data8	BIN	1 bit（bit5）


