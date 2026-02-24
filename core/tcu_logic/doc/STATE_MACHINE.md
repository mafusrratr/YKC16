# tcu_logic 状态机（执行版）

## 1. 状态定义
- `IDLE`：空闲，等待启动命令
- `PREPARE`：预检查/准备（策略、枪状态、权限等）
- `STARTING`：已下发启动命令，等待启动应答/启动完成
- `CHARGING`：充电中
- `STOPPING`：已下发停止命令，等待停止应答/停止完成
- `STOPPED`：正常停止完成
- `ERROR`：故障停止/异常中断

## 2. 主要输入
- HMI/平台 cmd：`start_charge` / `stop_charge` / `reset_error`
- pile_controller event：`start_response` / `start_complete` / `stop_response` / `stop_complete` / `deviceErr_on` / `deviceErr_off`
- pile_controller data：`yx` / `yc`
- meter/data：计量数据

## 3. 主要输出
- 向 pile_controller 发 cmd：`start_charge` / `stop_charge`
- 向 HMI/平台发 event：`state_change` / `fault` / `charge_record`
- 计费信息：`feeData` 独立 topic 上报

## 4. 状态流转（核心）
- `IDLE` → `PREPARE`
  - 触发：遥信无故障 + 车辆连接
- `PREPARE` → `STARTING`
  - 触发：收到启动命令 + 鉴权通过
  - 动作：下发 start_charge，收到 start_response(成功) 后进入 STARTING
  - 说明：当前测试阶段，收到 HMI/平台 `start_charge` 默认鉴权通过
- `PREPARE` → `IDLE`
  - 触发：车辆连接状态断开
- `STARTING` → `CHARGING`
  - 触发：收到 `start_complete` 且成功
- `STARTING` → `STOPPING`
  - 触发：`start_complete` 失败 / `deviceErr` / `totalFault` / 收到 stop_cmd / 启动阶段超时 50s（未收到 start_complete）
  - 动作：下发 stop_charge，进入停止流程
- `CHARGING` → `STOPPING`
  - 触发：收到 stop_cmd / 金额停机 / 设备故障 / workStatus=00 / stop_complete
  - 动作：下发 stop_charge；当总金额接近预充值金额时发布 `tcu_stop_request` 并触发停机
- `STOPPING` → `STOPPED`
  - 触发：进入 STOPPING 后达到 15 秒结算窗口，或电表连续 3 次上送电量无变化
  - 说明：窗口内持续接收电表更新，避免末端计量漏失；stop_complete 仅用于停止重发 stop_cmd
- `STOPPED` → `IDLE`
  - 触发：车辆连接状态断开（vehicleConnectStatus=0）
- `ERROR` → `IDLE`
  - 触发：收到 `reset_error`

## 4.1 ERROR 进入条件约束
- 仅在 `IDLE` / `PREPARE` 状态遇到严重故障时进入 `ERROR`。
- 其他状态（STARTING/CHARGING/STOPPING）出现故障，统一走 `STOPPING` → `STOPPED`。

## 5. 事件上报建议
- 进入任何状态：发布 `logic/event`，`event=state_change`
- ERROR：发布 `event=fault` 并带故障详情
- STOPPED：发布 `event=charge_record`（包含计量与统计）

## 6. 状态机 UML
- 状态机图（PlantUML）：
  - `core/tcu_logic/doc/STATE_MACHINE_UML.puml`
- 渲染图（SVG）：
  - `core/tcu_logic/doc/STATE_MACHINE_UML.svg`
