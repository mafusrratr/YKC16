# tcu_meter 工作进展记录

## 1. 当前完成状态
- 已完成 `tcu_meter` 进程主链路：配置加载 -> 串口初始化 -> 645 轮询 -> 解析 -> MQTT 上送。
- 已完成多口多表模型：每枪独立配置 `gunN_meter_addr` 与 `gunN_serial_device`。
- 已完成 645 读表核心流程：
  - 请求组帧（读数据）
  - 前导 `FE` 处理
  - 抽帧、校验和校验、DI 校验
  - `-0x33` 解码 + BCD 转浮点
- 已完成 MQTT 输出：
  - `tcu/meter/{gun}/data`（`totalEnergy/voltage/current`）
  - `tcu/meter/{gun}/event`（`meter_online/meter_offline`）
- 已完成文档：
  - 架构文档：`core/meter/doc/ARCHITECTURE.md`
  - 测试用例：`core/meter/doc/TEST_CASES.md`
  - 数据流 UML：`core/meter/doc/DATA_FLOW_UML.puml`
  - 数据流图：`core/meter/doc/DATA_FLOW_UML.svg`

## 2. 已固定的实现口径
- 当前按“多口多表”约束：枪间 `serial_device` 不允许重复。
- 当前 DI 固定为：
  - 电能：`0x0001FF00`
  - 电压：`0x02100100`
  - 电流：`0x02110100`
- `gunN_serial_device` 为必填；其余枪级串口参数可选（缺省回退全局参数）。

## 3. 待现场实测确认项
- 报文口径确认：DI 对应测点是否与现场电表完全一致。
- 数据精度确认：电能/电压/电流的小数位与量纲是否符合实表定义。
- 异常工况确认：超时、断线、恢复时 `event` 的触发行为。

## 4. 当前结论
- 代码侧功能已达到联调可用状态。
- 进入下一模块开发前，仅需完成现场实测校对与精度验收。
