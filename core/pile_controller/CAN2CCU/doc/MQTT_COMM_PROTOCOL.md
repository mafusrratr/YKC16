# PileController 进程 MQTT 通信规范（草案）

## 1. 目标与范围
- 目标：定义 pile_controller 与其他进程（如 tcu_logic、meter 等）通过 MQTT 的消息规范。
- 范围：命令下发与状态/事件数据发布。

## 2. Broker 约定
- Broker：Mosquitto
- 默认地址：`localhost:1883`
- 认证：开发阶段可无认证，生产建议用户名/密码或证书。

## 3. Topic 规范

### 3.1 命令通道
- `tcu/pile/{gun}/cmd`

### 3.2 上送命令通道（由 pile_controller 主动发起）
- `tcu/pile/{gun}/event`

### 3.2 数据通道
- `tcu/pile/{gun}/data`

**说明**：
- `{gun}`：`0` / `1` / `all`
- `cmd`、`event` 与 `data` 必须区分，避免处理混淆。
- **上下行方向不固定**：根据业务角色决定发布/订阅方向（例如：tcu_logic 可发布 cmd，pile_controller 可发布 data；也可在某些场景反向）。

## 4. Payload 通用字段
所有消息建议包含以下字段：
```json
{
  "ts": 1736150000000,
  "seq": 1024,
  "source": "pile_controller",
  "gun": 0
}
```

字段说明：
- `ts`：毫秒时间戳
- `seq`：递增序号（便于去重/排序）
- `source`：来源进程标识
- `gun`：枪号（与 topic 一致）

## 5. 命令消息（cmd）
Topic：`tcu/pile/{gun}/cmd`

示例（start_charge）：
```json
p
```

示例（stop_charge）：
```json
{
  "ts": 1736150000000,
  "seq": 57,
  "source": "tcu_logic",
  "gun": 0,
  "cmd": "stop_charge",
  "data": {
    "stopReason": 1,
    "tcuStopCode": 0
  }
}
```

### 5.1 cmd 字段建议值
- `start_charge`
- `stop_charge`

### 5.2 start_charge 参数
- `loadControlSwitch`：负荷控制开关（01H=启用，02H=关闭）
- `plugAndChargeFlag`：即插即充标识（01H=非即插即充，02H=即插即充，05H/06H=双枪对一车模式）
- `auxPowerVoltage`：辅助电源电压（0CH=12V，18H=24V）

### 5.3 stop_charge 参数
- `stopReason`：停止原因（见规约定义）
- `tcuStopCode`：TCU 停止码（见规约定义）

## 6. 数据消息（data）
Topic：`tcu/pile/{gun}/data`

数据消息必须包含 `type` 字段，用于分类：
```json
{
  "ts": 1736150000000,
  "seq": 1024,
  "source": "pile_controller",
  "gun": 0,
  "type": "yx",
  "data": { ... }
}
```

### 6.1 type 分类建议
- `yc`：遥测
- `yx`：遥信
- `data` 通道不再承载 `deviceErr` 事件

## 6.2 上送命令消息（event）
Topic：`tcu/pile/{gun}/event`

用于上送命令/流程事件：
- `start_response`：启动应答
- `stop_response`：停止应答
- `start_complete`：启动完成
- `stop_complete`：停止完成
- `deviceErr_on`：故障总由 0->1（故障出现）
- `deviceErr_off`：故障总由 1->0（故障恢复）

说明：
- event 不做去重，按 CAN 接收次数直接上送。

## 7. QoS 与 Retain 建议
- 状态/数据类：QoS 0（低延迟）
- 命令下发（cmd）：QoS 2（可靠投递）
- 上送命令（event）：QoS 2（确保送达）
- `data` 不使用 retain
- `cmd` 不建议 retain

## 8. 断线与重连策略
- pile_controller 与 broker 断线时继续本地采集
- 重连后发布最新 `data`（建议带最新 `seq`）
