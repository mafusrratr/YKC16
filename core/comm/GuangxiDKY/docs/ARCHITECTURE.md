# GuangxiDKY MODBUS 接入说明

## 1. 模块定位
- 进程名：`tcu_comm`
- 介质：RS485（MODBUS RTU）
- 协议来源：`docs/充电桩MODBUS协议-V2.0(1).docx`
- 角色：充电桩侧从站，响应上位机 `0x03` 读寄存器与 `0x06` 写寄存器。

## 2. 代码结构
- `main.cpp`：进程入口。
- `comm_process.h/.cpp`：
  - MQTT 数据订阅与运行态缓存
  - MODBUS RTU 帧解析/CRC/应答封装
  - 寄存器映射与写控制下发
- `Makefile.cross`：iMX6UL 交叉编译。

## 3. 协议映射（当前实现）
### 3.1 读寄存器（功能码 0x03）
- 起始地址：`0x0000`
- 寄存器数量：`13`

寄存器定义：
- `0x0000` 充电桩序号
- `0x0001` 枪序号
- `0x0002` 枪运行状态
- `0x0003` 枪类型
- `0x0004` SOC
- `0x0005` 连接状态（1连接/2未连接）
- `0x0006` 充电电压（0.1V/位）
- `0x0007` 充电电流（0.1A/位）
- `0x0008` 充电功率（0.1kW/位）
- `0x0009` 充电时间（1s/位）
- `0x000A` 充电电量（0.1kWh/位）
- `0x000B` 本次电费（0.1元/位）
- `0x000C` 当前电表读数（0.1kWh/位）

### 3.2 写单寄存器（功能码 0x06）
- 控制寄存器：`0x2000`
- 含义：充电功率控制（0.1kW/位）
- 动作：接收后转发 MQTT 控制命令 `cmd=power_ctrl`。

### 3.3 写单寄存器（功能码 0x06）
- 控制寄存器：`0x3000`
- 含义：充放电启停控制（2Byte）
  - 高字节：充放电标志位（`0x01` 充电，`0x02` 放电）
  - 低字节：启停标志位（`0x55` 停止，`0xAA` 启动）
- 动作：
  - `0x01AA` -> `cmd=start_charge`
  - `0x02AA` -> `cmd=start_charge` 且 `data.v2g=1`
  - `0x0155` / `0x0255` -> `cmd=stop_charge`

## 4. MQTT 接口
### 4.1 订阅（输入）
- `tcu/logic/{gun}/event`
- `tcu/logic/{gun}/feeData`
- `tcu/pile/{gun}/data`
- `tcu/pile/{gun}/event`
- `tcu/meter/{gun}/data`

### 4.2 发布（输出）
- `tcu/plat/{gun}/cmd`
  - 功率控制：`cmd=power_ctrl`
  - `data.maxChargePowerKw`
  - 启停控制：`cmd=start_charge` / `cmd=stop_charge`
  - 放电启动时附带 `data.v2g=1`

## 5. 关键配置（[Comm]）
- `cdz_no=1`（充电桩通讯地址，范围1~16）
- `modbus_export_gun=0`
- `debug=1`（统一调试开关）
- MODBUS 参数使用进程内默认值（不开放配置）：
  - 串口：`/dev/ttyS1, 9600, 8N1`
  - 站号：`1`
  - 读区：`0x0000` 起始、`13` 个寄存器
  - 写控寄存器：`0x2000`（功率） / `0x3000`（充放电启停）

## 6. 说明
- 当前按文档 V2.0 的核心读写流程落地。
- 若后续文档新增寄存器，直接扩展 `getRegisterValue()` 即可。
