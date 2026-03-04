# 充电计费单元软件概要设计方案

## 1. 文档目的
- 说明当前重构项目的软件架构、模块职责、模块间接口和关键信息流。
- 作为后续接入新平台协议、新硬件驱动时的统一设计基线。

## 2. 系统边界与总体架构
- 系统定位：TCU 侧充电业务中枢，完成“充电设备控制 + 计费 + 平台适配 + 日志留存”。
- 架构风格：多进程 + MQTT 总线解耦 + 协议适配插件化。
- 运行模式：各进程独立运行，通过 `tcu/...` 主题协作，不做进程间直接强耦合调用。

当前核心进程：
- `tcu_pile_controller`：桩侧协议与设备交互（CAN主控）。
- `tcu_logic`：业务状态机与计费核心。
- `tcu_meter`：电表采集（RS485 + DLT645）。
- `tcu_comm`（平台适配进程，按平台拆目录）：
  - `core/comm/ChangzhouLiyangSmartcityPlat`
  - `core/comm/Zhongshihua2.0`
- `logger` / `daemon`：日志与守护（基础设施）。

## 3. 模块组成与职责

### 3.1 `pile_controller`
- 位置：`core/pile_controller`
- 职责：
  - 对接 CAN 主控协议（CAN2CCU）。
  - 执行启动/停止/握手/心跳等桩侧交互。
  - 上送遥信（yx）、遥测（yc）、流程事件（start/stop complete 等）。
- 对外主题：
  - 订阅：`tcu/pile/{gun}/cmd`
  - 发布：`tcu/pile/{gun}/data`、`tcu/pile/{gun}/event`

### 3.2 `tcu_logic`
- 位置：`core/tcu_logic`
- 职责：
  - 维护状态机：`IDLE -> PREPARE -> STARTING -> CHARGING -> STOPPING -> STOPPED/ERROR`。
  - 聚合 pile/meter 数据，执行计费与停机策略。
  - 向 pile 下发启停命令，向上游发布状态/费用/记录事件。
- 对外主题：
  - 订阅：`tcu/logic/{gun}/cmd`、`tcu/plat/{gun}/cmd`、`tcu/pile/{gun}/event`、`tcu/pile/{gun}/data`、`tcu/meter/{gun}/data`
  - 发布：`tcu/pile/{gun}/cmd`、`tcu/logic/{gun}/event`、`tcu/logic/{gun}/feeData`

### 3.3 `tcu_meter`
- 位置：`core/meter`
- 职责：
  - 按枪配置轮询电表，解析 `totalEnergy/voltage/current`。
  - 输出在线/离线事件。
- 对外主题：
  - 发布：`tcu/meter/{gun}/data`、`tcu/meter/{gun}/event`

### 3.4 `tcu_comm`（平台适配层）
- 位置：`core/comm/*`
- 职责：
  - 维护平台 TCP 会话与登录状态机。
  - 平台帧 <-> 内部 MQTT 命令/事件映射。
  - 适配不同平台协议帧，不改动 `tcu_logic` 核心业务。
- 设计原则：
  - 每个平台一套独立进程目录，帧号与字段不做运行时动态切换。
  - 内部统一输出 `tcu/plat/{gun}/cmd|event`，供 `tcu_logic` / 其他模块消费。

### 3.5 `base` 与 `logger`
- `core/base`：`BaseProcess`、`MqttClient`、配置与公共工具。
- `core/logger`：交易记录落库与确认（含 `logTradeRecord/confirmTradeRecord` 链路）。

## 4. 模块通信接口（MQTT）

统一前缀：`tcu`

- `tcu/pile/{gun}/cmd`：逻辑下发到桩控制（QoS2）。
- `tcu/pile/{gun}/event`：桩上送流程事件（QoS2）。
- `tcu/pile/{gun}/data`：桩上送遥信/遥测（QoS0）。
- `tcu/meter/{gun}/data`：电表数据（QoS0）。
- `tcu/meter/{gun}/event`：电表在线状态（QoS1）。
- `tcu/logic/{gun}/cmd`：HMI/本地下发到逻辑（QoS1）。
- `tcu/logic/{gun}/event`：逻辑状态/业务事件上送（QoS1）。
- `tcu/logic/{gun}/feeData`：逻辑计费数据上送（QoS1）。
- `tcu/plat/{gun}/cmd`：平台适配层下发到逻辑（QoS1）。
- `tcu/plat/{gun}/event`：平台配置/事件通知（如 `type=setConfig`）。

## 5. 关键信息流

### 5.1 启动充电主流程
1. HMI 或平台发布 `start_charge` 到 `tcu/logic/{gun}/cmd` 或 `tcu/plat/{gun}/cmd`。
2. `tcu_logic` 校验状态与鉴权条件，进入 `STARTING`。
3. `tcu_logic` 向 `tcu/pile/{gun}/cmd` 下发启动命令。
4. `pile_controller` 上送 `start_response/start_complete` 到 `tcu/pile/{gun}/event`。
5. `tcu_logic` 收到成功后转 `CHARGING`，周期上报 `logic/event` 与 `feeData`。

### 5.2 停机与结束流程
1. 触发源：平台/HMI停机命令、故障停机、预充阈值停机、枪断开等。
2. `tcu_logic` 转 `STOPPING`，按策略重发停机命令并等待收敛。
3. 收到停机完成并满足电量收敛窗口后转 `STOPPED`。
4. 生成交易记录并上送 `update_record` 事件，记录进入日志系统。

### 5.3 计费信息流
1. `tcu_meter` 上送电量电压电流。
2. `tcu_logic` 结合费率模型做分时段累计，发布 `feeData`。
3. `tcu_comm` 订阅 `feeData` 和 pile 遥测，组平台实时监测帧（不同平台帧号不同）。

### 5.4 记录确认流
1. `tcu_logic` 发布 `update_record`。
2. `tcu_comm` 组帧上送平台交易记录。
3. 平台回确认后，`tcu_comm` 触发本地确认（并可发布 `record_cfm`）。

## 6. 配置与部署约束

固定配置路径（目标机）：
- `/usr/app/config/pile_controller.ini`
- `/usr/app/config/tcu_logic.ini`
- `/usr/app/config/tcu_meter.ini`
- `/usr/app/config/tcu_comm.ini`（按平台进程复用同名入口）

部署建议：
- MQTT broker 独立守护运行。
- 各进程由 daemon 监管并自动拉起。
- 跨模块参数（枪数、枪号映射）以 `pile_controller` 为主基准，其他模块读取并对齐。

## 7. 可靠性与一致性策略
- 命令链路高可靠：启停命令与关键事件使用 QoS2。
- 数据链路低延迟：遥测类数据使用 QoS0。
- 状态去耦：状态转换以 `tcu_logic` 为单一业务真源。
- 幂等与重试：
  - `tcu_logic` 对启动/停机命令有重发窗口控制。
  - `tcu_comm` 平台登录链路采用状态机与重连重试。

## 8. 扩展设计（平台与硬件）
- 平台扩展：新增目录 `core/comm/<PlatformName>`，实现本平台帧与统一 MQTT 的映射。
- 硬件扩展：
  - pile 扩展：新增控制器/协议实现，保持 `tcu/pile/*` 主题不变。
  - meter 扩展：新增协议驱动，保持 `tcu/meter/*` 主题不变。

## 9. 当前实现完成度（摘要）
- 已完成：
  - `pile_controller` CAN 主流程与 MQTT 上下行；
  - `tcu_logic` 状态机、计费、交易记录事件；
  - `tcu_meter` 645 轮询解析与数据上送；
  - 常州/中石化平台适配主链路（持续迭代中）。
- 进行中：
  - 平台侧帧细节补齐与联调；
  - 安全加解密（SM2/SM4）实装与验收。

## 10. 评审关注点
- 主题命名和 QoS 是否与现场联调一致。
- 平台适配是否严格“模块内封闭”，避免侵入 `tcu_logic`。
- 枪号映射、计费模型、交易记录确认链路是否端到端闭环。
- 故障与停机收敛条件是否满足现场安全策略。

## 11. 架构优势（MQTT 路由 + 多进程微服务）
- 解耦清晰：模块只依赖 Topic 协议，不依赖对方实现细节，平台/硬件替换成本低。
- 故障隔离：单进程异常不会直接拖垮全系统，守护进程可独立拉起恢复。
- 并行开发：`pile/logic/meter/comm` 可分团队并行迭代，通过消息契约对齐。
- 可扩展性：新增平台仅扩展 `core/comm/<Platform>`，不改核心业务状态机。
- 可观测性：所有关键动作天然消息化，便于抓包、回放、追因和联调。
- 可靠性可分级：命令通道用高 QoS，遥测通道用低延迟 QoS，实现可靠性与实时性的平衡。
- 部署灵活：可按设备能力裁剪模块，支持后续边缘网关化和云边协同演进。

## 12. UML 附图（PlantUML 源）
- 主要进程框架：`docs/image/MAIN_PROCESS_ARCHITECTURE.puml`
- 关键业务时序：`docs/image/KEY_SEQUENCE_FLOW.puml`
