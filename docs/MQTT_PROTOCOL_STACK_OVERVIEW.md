# MQTT 协议栈总览

## 1. 文档目的

本文档用于统一梳理当前项目内各进程实际使用的 MQTT Topic、发布/订阅关系、QoS/retain 策略，以及少量典型示例。

说明：
- 本文档基于当前仓库代码整理，优先以实际实现为准。
- 个别历史兼容 Topic 仍保留订阅，但不一定是当前主链路。
- 不同平台项目（中石化 2.0、常州、广西等）共享同一条 TCU 内部 MQTT 总线，平台侧消息通过 `tcu/plat/{gun}/...` 抽象。

## 2. 总体命名规则

当前项目内部 MQTT Topic 统一采用如下命名：

- `tcu/logic/{gun}/cmd`
- `tcu/logic/{gun}/event`
- `tcu/logic/{gun}/feeData`
- `tcu/pile/{gun}/cmd`
- `tcu/pile/{gun}/data`
- `tcu/pile/{gun}/event`
- `tcu/meter/{gun}/data`
- `tcu/meter/{gun}/event`
- `tcu/plat/{gun}/cmd`
- `tcu/plat/{gun}/event`
- `tcu/card/0/cmd`
- `tcu/card/0/event`
- `tcu/save/{gun}/event`
- `tcu/logger/{gun}/event`

其中：
- `{gun}` 为枪序号，当前项目内部统一采用 **0-based**（如 `0`、`1`）。
- `cmd`：命令下发。
- `data`：连续业务数据。
- `event`：事件、状态变化、应答、故障等。

## 3. 公共消息字段约定

大多数 JSON 消息采用如下公共字段：

```json
{
  "ts": 1774406540047,
  "seq": 91,
  "source": "tcu_logic",
  "gun": 1,
  "event": "update_record",
  "data": {}
}
```

或：

```json
{
  "ts": 1774406540047,
  "seq": 91,
  "source": "tcu_logic",
  "gun": 1,
  "cmd": "start_charge",
  "data": {}
}
```

字段说明：
- `ts`：毫秒时间戳。
- `seq`：发送方内部消息序号。
- `source`：发送进程名，例如 `tcu_logic`、`pile_controller`、`tcu_meter`、`tcu_comm`。
- `gun`：枪序号，0-based。
- `cmd`：命令字，仅 `cmd` 类 Topic 使用。
- `event`：事件字，仅 `event` 类 Topic 使用。
- `type`：部分模块的事件/数据子类型字段，例如 `type=yx`、`type=yc`、`type=Error`、`type=setConfig`。
- `data`：消息体。

## 4. MQTT 总线 Topic 一览表

| Topic | 发布方 | 订阅方 | QoS | retain | 用途 |
|---|---|---|---:|---:|---|
| `tcu/logic/{gun}/cmd` | HMI | logic | 1/2（视发送方） | 否 | 本地启动/停止/V2G 等命令 |
| `tcu/logic/{gun}/event` | logic | HMI、comm | 2 | 是 | 状态变化、交易更新等 |
| `tcu/logic/{gun}/feeData` | logic | HMI、comm | 1 | 否 | 计费分段实时数据 |
| `tcu/pile/{gun}/cmd` | logic、HMI | pile_controller | 1/2 | 否 | 启停、清故障、功率控制等 |
| `tcu/pile/{gun}/data` | pile_controller | logic、comm、HMI | 0 | 否 | 主控遥测/遥信数据 |
| `tcu/pile/{gun}/event` | pile_controller | logic、comm | 2（主事件）/1（在线状态） | 部分是 | 主控启动/停止完成、在线离线、故障事件 |
| `tcu/meter/{gun}/data` | meter | logic、comm、HMI | 0 | 否 | 电表读数 |
| `tcu/meter/{gun}/event` | meter | logic | 1 | 是 | 电表在线/离线 |
| `tcu/plat/{gun}/cmd` | comm | logic、HMI（部分平台/HMI兼容使用） | 1 | 否 | 平台下发的业务命令抽象 |
| `tcu/plat/{gun}/event` | comm | logic、HMI | 1 | 是 | 平台在线/离线、平台配置下发 |
| `tcu/card/0/cmd` | logic | card | 1 | 否 | 读卡器射频开关、锁卡、写余额、解锁 |
| `tcu/card/0/event` | card | logic | 1 | 否 | 读卡信息、锁卡结果、写卡结果、解锁结果 |
| `tcu/save/{gun}/event` | logic | logger | 2 | 是 | 本地持久化事件，目前主要是故障记录 |
| `tcu/logger/{gun}/event` | logger | logic | 2 | 否 | 未确认交易记录回传等 |

## 5. 各模块 MQTT 消息梳理

### 5.1 tcu_logic

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/tcu_logic/charge_logic_process.cpp`

#### 订阅
- `tcu/logic/{gun}/cmd`
- `tcu/plat/{gun}/cmd`
- `tcu/plat/{gun}/event`
- `tcu/pile/{gun}/event`
- `tcu/pile/{gun}/data`
- `tcu/meter/{gun}/data`
- `tcu/meter/{gun}/event`
- `tcu/logger/{gun}/event`
- `tcu/card/0/event`

#### 发布

1) `tcu/pile/{gun}/cmd`
- `cmd=start_charge`
- `cmd=stop_charge`
- `cmd=power_ctrl`

2) `tcu/logic/{gun}/event`
- `event=state_change`
- `event=update_record`
- 其他过程性事件（如停机请求、状态驱动事件等）

3) `tcu/logic/{gun}/feeData`
- 充电分段计费实时数据

4) `tcu/save/{gun}/event`
- `type=Error`
- 用于本地故障持久化

5) `tcu/card/0/cmd`
- `op=open_rf`
- `op=close_rf`
- `op=card_lock`
- `op=card_unlock`
- `op=card_write`

#### 发布策略
- `tcu/pile/{gun}/cmd`：QoS2，retain=false
- `tcu/logic/{gun}/event`：QoS2，retain=true
- `tcu/logic/{gun}/feeData`：QoS1，retain=false
- `tcu/save/{gun}/event`：QoS2，retain=true
- `tcu/card/0/cmd`：QoS1，retain=false

### 5.2 tcu_pile_controller / tcu_cdzctrl

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/pile_controller/CAN2CCU/pile_controller_process.cpp`

#### 订阅
- `tcu/pile/{gun}/cmd`

#### 支持命令
- `cmd=start_charge`
- `cmd=stop_charge`
- `cmd=clear_fault`
- `cmd=power_ctrl`
- `cmd=outputVA_ctrl`

#### 发布

1) `tcu/pile/{gun}/data`
- `type=yc`：遥测数据
- `type=yx`：遥信数据

2) `tcu/pile/{gun}/event`
- `event=start_response`
- `event=stop_response`
- `type=start_complete`
- `type=stop_complete`
- `type=deviceErr_on`
- `type=deviceErr_off`
- `event=pile_online`
- `event=pile_offline`

#### 发布策略
- `tcu/pile/{gun}/data`：QoS0，retain=false
- 主控业务事件：QoS2，retain=false
- 主控在线/离线：QoS2/1（当前实现重连补发时为 retain=true）

### 5.3 tcu_meter

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/meter/meter_process.cpp`

#### 发布

1) `tcu/meter/{gun}/data`
- 电表总电量、电压、电流、反向有功电量等

2) `tcu/meter/{gun}/event`
- `event=meter_online`
- `event=meter_offline`

#### 发布策略
- `tcu/meter/{gun}/data`：QoS0，retain=false
- `tcu/meter/{gun}/event`：QoS1，retain=true

### 5.4 tcu_logger

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/logger/logger_process.cpp`

#### 订阅
- `tcu/save/+/event`

当前主要处理：
- `type=Error`

#### 发布
- `tcu/logger/{gun}/event`
  - `event=unconfirmed_record`

#### 发布策略
- 订阅 `tcu/save/+/event`：QoS2
- 发布 `tcu/logger/{gun}/event`：QoS2，retain=false

### 5.5 comm（中石化 2.0）

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/comm/Zhongshihua2.0/comm_process.cpp`

#### 订阅
- `tcu/logic/+/event`
- `tcu/logic/+/feeData`
- `tcu/pile/+/data`
- `tcu/pile/+/event`
- `tcu/meter/+/data`

#### 发布

1) `tcu/plat/{gun}/cmd`
- `cmd=start_charge`
- `cmd=stop_charge`
- `cmd=record_cfm`

2) `tcu/plat/{gun}/event`
- `type=setConfig`
- `event=platform_online`
- `event=platform_offline`

#### 发布策略
- `tcu/plat/{gun}/cmd`：QoS1，retain=false
- `tcu/plat/{gun}/event`：QoS1，retain=true
- `setConfig`：QoS1，retain=true

### 5.6 comm（常州溧阳）

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/comm/ChangzhouLiyangSmartcityPlat/comm_process.cpp`

#### 订阅
- `tcu/logic/+/event`
- `tcu/logic/+/feeData`
- `tcu/pile/+/data`
- `tcu/pile/+/event`

#### 发布
- `tcu/plat/{gun}/cmd`
- `tcu/plat/{gun}/event`
  - 包含 `type=setConfig`

#### 发布策略
- `tcu/plat/{gun}/cmd`：QoS1，retain=false
- `tcu/plat/{gun}/event`：QoS1，retain=true

### 5.7 comm（广西 DKY）

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/comm/GuangxiDKY/comm_process.cpp`

#### 订阅
当前实现为总订阅：
- `tcu/+/+/+`

实际使用的主题包括：
- `tcu/logic/{gun}/event`
- `tcu/logic/{gun}/feeData`
- `tcu/pile/{gun}/data`
- `tcu/pile/{gun}/event`
- `tcu/meter/{gun}/data`

#### 发布
- `tcu/plat/{gun}/cmd`
  - `cmd=power_ctrl`
  - `cmd=start_charge`
  - `cmd=stop_charge`
- 放电启动时，`data.v2g=1`

#### 发布策略
- `tcu/plat/{gun}/cmd`：QoS1，retain=false

### 5.8 HMI（中石化 2.0）

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/hmi/Zhongshihua2.0/runtime_window.cpp`

#### 订阅
- `tcu/logic/+/event`
- `tcu/logic/+/feeData`
- `tcu/pile/+/data`
- `tcu/plat/+/event`
- `tcu/plat/+/setConfig`（历史兼容）
- `tcu/mon/0/event`（监控兼容预留）

#### 说明
- 当前 HMI 实际使用 `tcu/plat/{gun}/event` 中的 `type=setConfig` 来更新配置和二维码。
- `tcu/plat/{gun}/setConfig` 仍保留兼容处理。

### 5.9 HMI（常州溧阳）

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/hmi/Changzhou_liyang_hmi/hmi_window.cpp`

#### 订阅
- `tcu/logic/+/event`
- `tcu/logic/+/feeData`
- `tcu/pile/+/data`
- `tcu/plat/+/event`
- `tcu/plat/+/setConfig`

### 5.10 HMI（广西 DKY）

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/hmi/GuangxiDKY_hmi/hmi_window.cpp`

#### 订阅
- `tcu/logic/+/event`
- `tcu/logic/+/feeData`
- `tcu/pile/+/cmd`
- `tcu/pile/+/data`
- `tcu/meter/+/data`

#### 发布
- `tcu/logic/{gun}/cmd`
  - `cmd=start_charge`
  - `cmd=stop_charge`
  - 放电场景附带 `data.v2g=1`

### 5.11 tcu_card

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/card/card_process.cpp`

#### 订阅
- `tcu/card/0/cmd`

#### 支持命令
- `op=open_rf`
- `op=close_rf`
- `op=card_lock`
- `op=card_unlock`
- `op=card_write`

#### 发布
- `event=rf_opened`
- `event=rf_closed`
- `event=card_info`
- `event=card_locked`
- `event=card_unlocked`
- `event=card_written`
- `event=op_failed`
- `event=device_online`
- `event=device_offline`

#### 发布策略
- `tcu/card/0/event`：QoS1，retain=false

## 6. 关键消息类型归纳

### 6.1 logic 命令

Topic：`tcu/logic/{gun}/cmd`

常见 `cmd`：
- `start_charge`
- `stop_charge`
- `request_card_start`

示例：

```json
{
  "ts": 1774406500000,
  "seq": 12,
  "source": "tcu_hmi",
  "gun": 0,
  "cmd": "start_charge",
  "data": {
    "loadControlSwitch": 2,
    "plugAndChargeFlag": 1,
    "auxPowerVoltage": 12,
    "mergeChargeFlag": 0,
    "v2g": 0
  }
}
```

离线刷卡启动示例：

```json
{
  "ts": 1775450000000,
  "seq": 13,
  "source": "tcu_hmi",
  "gun": 0,
  "cmd": "request_card_start",
  "data": {
    "mode": "offline",
    "chargeMode": 2,
    "feeModelNo": 12,
    "feeModelId": "MODEL014",
    "timeNum": 12,
    "timeSeg": ["0000", "1200", "1430", "1535", "1538", "1540", "1550", "1600", "1730", "1830", "1930", "2130"],
    "chargeFee": [0.70, 0.90, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2],
    "serviceFee": [0.15, 0.15, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2]
  }
}
```

说明：
- HMI 按目标枪发布到 `tcu/logic/{gun}/cmd`
- 当前 `data.mode` 支持：
  - `offline`：离线刷卡启动，已实现
  - `online`：预留，logic 当前会返回 `cmd_reject`
- HMI 可在 `data` 中一并携带充电模式、计费模型等启动参数
- HMI 不需要传 `orderNo`、`prechargeAmount`
- `orderNo` 由 logic 本地生成，`prechargeAmount` 由实际刷卡读到的卡余额决定
- logic 会使用 HMI 传递的计费模型；若未携带，则回退使用当前枪已同步的本地计费模型
- logic 会用实际刷到的卡号和余额覆盖 `chargeUserNo/cardNumber/prechargeAmount`
- 若读到的卡已处于锁卡状态（`locked=true`），logic 会返回 `event=card_auth_reject`，`reason=card_locked`
- 示例：
  - 0 号枪：`tcu/logic/0/cmd`
  - 1 号枪：`tcu/logic/1/cmd`

### 6.2 pile 业务事件

Topic：`tcu/pile/{gun}/event`

典型事件：
- `start_complete`
- `stop_complete`
- `pile_online`
- `pile_offline`

示例：

```json
{
  "ts": 1773990603812,
  "seq": 44659,
  "source": "pile_controller",
  "gun": 0,
  "type": "stop_complete",
  "data": {
    "stopReason": 1,
    "stopSuccessFlag": 0,
    "bmsStopReason": 0,
    "bmsChargeFaultReason": 0,
    "bmsStopErrorReason": 0,
    "stopSoc": 20,
    "cellMinVoltage": 0,
    "cellMaxVoltage": 39,
    "batteryMinTemp": -50,
    "batteryMaxTemp": -50,
    "faults": []
  }
}
```

### 6.3 pile 遥测/遥信数据

Topic：`tcu/pile/{gun}/data`

`type=yc` 示例：

```json
{
  "ts": 1774408725992,
  "seq": 321,
  "source": "pile_controller",
  "gun": 0,
  "type": "yc",
  "data": {
    "outputVoltage": 0,
    "outputCurrent": 0,
    "soc": 0,
    "batteryMinTemp": -50,
    "batteryMaxTemp": -50,
    "cellMaxVoltage": 0,
    "cellMinVoltage": 0
  }
}
```

`type=yx` 示例：

```json
{
  "ts": 1774408726002,
  "seq": 322,
  "source": "pile_controller",
  "gun": 0,
  "type": "yx",
  "data": {
    "workStatus": 0,
    "totalFault": 1,
    "totalAlarm": 0,
    "emergencyStopFault": 1,
    "vehicleConnectStatus": 1,
    "otherFault": 34
  }
}
```

### 6.4 meter 事件

Topic：`tcu/meter/{gun}/event`

示例：

```json
{
  "ts": 1774430970533,
  "seq": 527,
  "source": "tcu_meter",
  "gun": 0,
  "event": "meter_offline",
  "data": {
    "reason": "serial_recv_timeout"
  }
}
```

### 6.5 平台在线/离线事件

Topic：`tcu/plat/{gun}/event`

示例：

```json
{
  "ts": 1774430000000,
  "seq": 12,
  "source": "tcu_comm",
  "gun": 0,
  "event": "platform_online",
  "data": {
    "reason": "login_ok"
  }
}
```

或：

```json
{
  "ts": 1774430000000,
  "seq": 13,
  "source": "tcu_comm",
  "gun": 0,
  "event": "platform_offline",
  "data": {
    "reason": "tcp_closed"
  }
}
```

### 6.6 save 故障持久化事件

Topic：`tcu/save/{gun}/event`

说明：
- 当前主要由 logic 发布，用于 logger 做本地持久化。
- 目前典型类型为 `type=Error`。

示例：

```json
{
  "ts": 1774337524532,
  "seq": 185,
  "source": "tcu_logic",
  "gun": 0,
  "type": "Error",
  "data": {
    "occurTime": "20260324153204",
    "faultType": 1,
    "pointKey": "standby_0001",
    "faultSource": "pile_yx",
    "faultMessage": "交流接触器故障",
    "rawValue": 1
  }
}
```

### 6.7 logger 回传未确认记录

Topic：`tcu/logger/{gun}/event`

示例结构：

```json
{
  "ts": 1774400000000,
  "seq": 21,
  "source": "tcu_logger",
  "gun": 0,
  "event": "unconfirmed_record",
  "data": {
    "count": 3,
    "records": []
  }
}
```

### 6.8 card 事件

Topic：`tcu/card/0/event`

刷卡成功示例：

```json
{
  "ts": 1775450001234,
  "seq": 45,
  "source": "tcu_card",
  "event": "card_info",
  "data": {
    "uidHex": "A1B2C3D4",
    "cardNoHex": "11223344556677889900AABBCCDDEEFF",
    "cardBalance": 12345,
    "locked": false
  }
}
```

说明：
- `cardBalance` 当前按分为单位，`100` 表示 `1.00元`
- logic 当前按 HMI 下发的 `data.mode` 处理刷卡模式，已实现 `offline`
- `tcu/card/0/cmd` / `tcu/card/0/event` 为固定共享读卡器 topic，不再由 logic 配置项决定

## 7. 典型链路

### 7.1 启动充电链路
- HMI 发布 `tcu/logic/{gun}/cmd`，`cmd=start_charge`
- logic 状态机校验后发布 `tcu/pile/{gun}/cmd`
- pile_controller 执行并回发 `tcu/pile/{gun}/event`：`start_response` / `start_complete`
- logic 根据事件切换状态，并向 `tcu/logic/{gun}/event` 发布 `state_change`
- comm 订阅 logic/pile/meter 数据，决定是否向平台上送对应报文

### 7.2 充电停止与记录链路
- logic 发 `tcu/pile/{gun}/cmd`：`stop_charge`
- pile_controller 回发 `stop_complete`
- logic 汇总 feeData 与 stopReason，发布 `update_record`
- logic 同时调用 logger 消息队列落充电记录
- logger 如有未确认交易，可通过 `tcu/logger/{gun}/event` 回传

### 7.3 常态故障持久化链路
- pile/meter/plat 侧事件触发 logic 内部故障判定
- logic 生成统一 `pointKey`
- logic 发布 `tcu/save/{gun}/event`，`type=Error`
- logger 订阅该消息并落本地故障记录

### 7.4 离线刷卡启动链路
- HMI 发布 `tcu/logic/{gun}/cmd`，`cmd=request_card_start`
- HMI 在 `data.mode=offline` 时请求离线刷卡启动
- logic 使用固定共享读卡器 topic 下发 `tcu/card/0/cmd`：`op=open_rf`
- card 模块读取到卡后发布 `tcu/card/0/event`：`event=card_info`
- logic 判断余额大于 `100` 后下发 `op=card_lock`，锁卡成功后启动充电
- 共享读卡器在任一离线刷卡会话存在时保持可用，允许另一把枪后续再次发起刷卡启动
- 充电中再次检测到同卡 `card_info` 时，logic 触发停机并记录“刷卡停止”
- 停止完成后，logic 下发 `op=card_write` 回写余额，再下发 `op=card_unlock`
- 若无其他离线刷卡充电会话，读卡器回传解锁成功后，logic 最后下发 `op=close_rf`

## 8. 当前兼容 / 历史 / 预留 Topic

### 8.1 `tcu/plat/{gun}/setConfig`
- HMI 仍保留订阅和解析。
- 当前主链路推荐使用：
  - `tcu/plat/{gun}/event`
  - `type=setConfig`

### 8.2 `tcu/mon/0/event`
- 中石化 HMI 当前仍订阅。
- 该 Topic 用于显示版本信息、通信状态、故障摘要等监控信息。
- 但当前仓库中的 `daemon` 模块未发现 MQTT 发布实现，因此此 Topic 目前应视为：
  - **兼容预留 Topic**
  - 非当前稳定主链路

## 9. 使用建议

- 新增内部业务链路时，优先沿用现有 Topic 树，不再创建平行命名空间。
- 状态类消息优先使用 `event`，并明确 `event`/`type` 字段语义。
- 连续业务数据优先使用 `data`。
- 命令统一使用 `cmd`，避免再混入 `type` 充当命令字。
- 需要跨重启保留当前状态的消息，使用 `retain=true`，如：
  - `platform_online/offline`
  - `pile_online/offline`
  - `meter_online/offline`
  - `save Error`
  - `logic state_change`
- 高频实时数据（`pile data`、`meter data`、`feeData`）不建议 retain。
