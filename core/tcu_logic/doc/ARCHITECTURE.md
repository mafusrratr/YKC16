# tcu_logic 架构设计

## 1. 目标
- 统一充电业务状态机与对外事件上报。
- 通过 MQTT 与 pile_controller/HMI/电表/平台解耦。
- 启动流程受鉴权控制，避免直接启动。

## 2. 进程边界
- 进程名：`tcu_logic`
- 入口：`core/tcu_logic/main.cpp`
- 核心类：`ChargeLogicProcess`（`core/tcu_logic/charge_logic_process.*`）
- 依赖组件：`BaseProcess`、`MqttClient`、`ConfigManagerLite`、`cJSON`

## 3. 配置
- 运行配置：`/usr/app/config/tcu_logic.ini`
- 关键配置：
  - `mqtt_host/mqtt_port/mqtt_keepalive/mqtt_client_id/mqtt_topic_prefix`
  - `gun_count`
  - `pile_config_path`（可从 pile_controller 配置复用枪数量）

## 4. MQTT 通道
- 与 pile_controller：
  - `tcu/pile/{gun}/cmd`（QoS2，下发命令）
  - `tcu/pile/{gun}/event`（QoS2，上送命令/流程）
  - `tcu/pile/{gun}/data`（QoS0，状态/遥测）
- 与 HMI：
  - `tcu/logic/{gun}/cmd`（QoS1）
  - `tcu/logic/{gun}/event`（QoS1）
  - `tcu/logic/{gun}/feeData`（QoS1）
- 与电表：
  - `tcu/meter/{gun}/data`（QoS0）
- 与平台：
  - `tcu/plat/{gun}/cmd`（QoS1）
  - `tcu/plat/{gun}/event`（QoS1）

## 5. 状态机与流程约束
- 状态：`IDLE/PREPARE/STARTING/CHARGING/STOPPING/STOPPED/ERROR`
- 关键约束：
  - `start_cmd` 仅在 `PREPARE` 状态响应。
  - `PREPARE → STARTING` 仅在鉴权通过（`auth_result.result == 1`）后触发。
  - `STARTING` 超过 30s 未收到 `start_complete` 重发一次启动命令；超过 60s 转 `STOPPING`。
  - `STOPPING → STOPPED` 使用固定 15s 结算窗口，或电表连续 3 次无变化提前结束。
  - `STOPPED → IDLE` 车辆断开（`vehicleConnectStatus=0`）。
- 详细流转见：`core/tcu_logic/doc/STATE_MACHINE.md`
- UML 图见：`core/tcu_logic/doc/STATE_MACHINE_UML.puml` / `core/tcu_logic/doc/STATE_MACHINE_UML.svg`

## 6. 事件与计费
- `logic/event`：
  - `state_change`：状态变更统一上报。
  - `fault`：故障/设备异常。
  - `charge_record`：交易结束摘要。
- `feeData`：计费展示/分段统计（见 MQTT 规范）。

## 7. 线程模型
- 单主循环线程：处理 MQTT 消息、状态机推进、停止重发（2s）。
- 通过 `MqttClient` 回调进入解析，不直接跨线程共享复杂状态。

## 8. 进度与联调
- 当前完成情况与联调验收清单见：`core/tcu_logic/doc/CURRENT_STATUS.md`
