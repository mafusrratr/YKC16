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

#### 发布策略
- `tcu/pile/{gun}/cmd`：QoS2，retain=false
- `tcu/logic/{gun}/event`：QoS2，retain=true
- `tcu/logic/{gun}/feeData`：QoS1，retain=false
- `tcu/save/{gun}/event`：QoS2，retain=true

### 5.2 tcu_pile_controller / tcu_cdzctrl

代码位置：
- `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/pile_controller/pile_controller_process.cpp`

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

## 6. 关键消息类型归纳

### 6.1 logic 命令

Topic：`tcu/logic/{gun}/cmd`

常见 `cmd`：
- `start_charge`
- `stop_charge`

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

