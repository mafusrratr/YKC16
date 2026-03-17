# tcu_logic 模块 MQTT 通信规范（执行版）

## 1. 目标与范围
- 目标：统一 tcu_logic 与 pile_controller、HMI、电表、平台的 MQTT 交互规范。
- 范围：命令下发、事件上报、状态与计量数据。

## 2. Topic 规范
统一前缀：`tcu/`

### 2.1 与 pile_controller 交互
- 下发命令（由 tcu_logic → pile_controller）
  - `tcu/pile/{gun}/cmd`
  - 支持命令：`start_charge` / `stop_charge` / `power_ctrl`
- 上送命令/流程事件（由 pile_controller → tcu_logic）
  - `tcu/pile/{gun}/event`
  - 事件类型：`start_response/start_complete/stop_response/stop_complete/deviceErr_on/deviceErr_off`
- 数据通道（由 pile_controller → 订阅方）
  - `tcu/pile/{gun}/data`

### 2.2 与 HMI 交互
- HMI 下发到 tcu_logic
  - `tcu/logic/{gun}/cmd`
- tcu_logic 上报给 HMI
  - `tcu/logic/{gun}/event`
  - `tcu/logic/{gun}/feeData`

### 2.3 与电表模块交互
- 电表上送数据
  - `tcu/meter/{gun}/data`
- 电表控制命令
  - `tcu/meter/{gun}/cmd`

电表上送 `data` 常用字段：
- 电量：`totalEnergy` / `energy` / `total_kwh`
- 电压：`voltage` / `meterVoltage` / `outputVoltage`
- 电流：`current` / `meterCurrent` / `outputCurrent`

说明：电表通道不传金额；金额由计费逻辑生成（`feeData`）。

### 2.4 与平台交互
- 平台下发到 tcu_logic
  - `tcu/plat/{gun}/cmd`
- tcu_logic 上报平台
  - `tcu/plat/{gun}/event`
  - 其中配置类消息统一使用 `type=setConfig`，并由 `tcu_comm` 以 retain 方式发布

### 2.5 与 logger 交互（未确认记录回放）
- logger 上送到 tcu_logic
  - `tcu/logger/{gun}/event`
  - 事件类型：`unconfirmed_record`
  - 用途：logger 从 `chargerecords.db` 读取 `platform_confirm_flag=0` 的记录并回放给 logic 缓冲

**说明**：`{gun}` 为 `0/1/...`（与 pile_controller 约定一致）。

## 3. Payload 通用字段
```json
{
  "ts": 1736150000000,
  "seq": 1024,
  "source": "tcu_logic",
  "gun": 0
}
```
- `ts`：毫秒时间戳
- `seq`：递增序号
- `source`：来源模块
- `gun`：枪号

## 4. cmd 规范（logic/cmd, plat/cmd）
```json
{
  "ts": 1736150000000,
  "seq": 10,
  "source": "hmi",
  "gun": 0,
  "cmd": "start_charge",
  "data": { ... }
}
```
建议 cmd：
- `start_charge`
- `stop_charge`
- `reset_error`
- `set_policy`
- `power_ctrl`

说明：
- `tcu/plat/{gun}/cmd` 统一使用 `cmd` 字段表示操作类型（与 `start_charge` 入口一致）。

平台鉴权类 cmd（plat/cmd）：
- `auth_result`：通用结果（`data.result == 1` 通过，其他失败）
```json
{
  "cmd": "auth_result",
  "data": {
    "result": 1,
    "loadControlSwitch": 1,
    "plugAndChargeFlag": 1,
    "auxPowerVoltage": 12,
    "mergeChargeFlag": 0
  }
}
```

说明：
- 当 `auth_result.data.result == 1` 时，`data` 中的启动参数会覆盖 `pendingStartData`，
  作为最终下发给 `tcu/pile/{gun}/cmd` 的 `start_charge.data`。

平台停止命令（plat/cmd）：
- `stop_charge`：平台要求停止充电（与 HMI 停机语义一致）
```json
{
  "ts": 1736150005000,
  "seq": 21,
  "source": "platform",
  "gun": 0,
  "cmd": "stop_charge",
  "data": {
    "stopReason": 1,
    "tcuStopCode": 0
  }
}
```

平台功率控制命令（plat/cmd）：
- `power_ctrl`：平台下发最大充电功率（kW），logic 透传到 pile
```json
{
  "ts": 1736150005500,
  "seq": 22,
  "source": "platform",
  "gun": 0,
  "cmd": "power_ctrl",
  "data": {
    "maxChargePowerKw": 30.0
  }
}
```

处理规则：
- logic 仅识别 `cmd=power_ctrl`（不再兼容 `type` 作为命令字段）
- `data` 支持对象或数字：
  - 对象优先读取 `maxChargePowerKw`（兼容 `powerLimitKw` / `powerKw` / `kw`）
  - 数字：视为 `maxChargePowerKw`
- 解析成功后发布到 `tcu/pile/{gun}/cmd`：
```json
{
  "cmd": "power_ctrl",
  "data": {
    "maxChargePowerKw": 30.0
  }
}
```
- pile_controller 处理规则：
  - 仅按“绝对值功率控制”处理
  - `maxChargePowerKw` 转 CAN 业务数据：
    - `adjustType = 0x01`
    - `adjustParam = round((maxChargePowerKw + 1000.0) * 10.0)`
  - 使用 `0x10/0x0F` 下发功率调节
  - 在收到 `0x10` 应答前，按 250ms 周期重发
  - 5s 未收到应答则停止重发，并打印超时日志
- 解析失败时，发布 `tcu/logic/{gun}/event`：
  - `event=cmd_reject`
  - `data.cmd=power_ctrl`
  - `data.reason=missing_max_charge_power_kw`

平台记录确认命令（plat/cmd）：
- `record_cfm`：平台确认某条充电记录已入平台账单
```json
{
  "ts": 1736150006000,
  "seq": 22,
  "source": "tcu_comm",
  "gun": 0,
  "cmd": "record_cfm",
  "data": {
    "tradeNo": "DP202602120001",
    "confirmFlag": 1
  }
}
```

平台配置下发（plat/event，retain）：
```json
{
  "ts": 1772200000000,
  "seq": 12,
  "source": "tcu_comm",
  "gun": 0,
  "type": "setConfig",
  "data": {
    "gunId": 4294967295,
    "gunNo": 1,
    "cdzNo": "46010003275590",
    "factoryCreditCode": "FFFFFFFFF",
    "macAddr": "251134003202671000000000",
    "qrCode": "xxx"
  }
}
```

说明：
- `tcu_comm` 在 MQTT 重连成功后会主动发布一次每枪 `setConfig`（retain）。
- 中石化平台 `0x5A` 二维码下发解析成功后，`tcu_comm` 会：
  1) 更新 `/usr/app/config/tcu_comm.ini` 中 `gun{N}_qrcode`；
  2) 发布对应枪 `tcu/plat/{gun}/event` 的 `type=setConfig`（retain）；
  3) 向平台回 `0x5B` 设置结果。

`start_charge.data`（鉴权基础信息）建议字段：
- `startTime`：启动时间戳（ms）
- `chargeUserNo`：充电用户号
- `orderNo`：充电订单号
- `chargeMode`：充电模式
- `prechargeAmount`：预充值金额
- `feeModelNo`：计费模型编号
- `feeModelId`：计费模型ID（可选）
- `timeNum`：时段数
- `timeSeg[]`：时段开始时间（`HHMM`）
- `chargeFee[]`：各时段电费（单位：元/度，浮点）
- `serviceFee[]`：各时段服务费（单位：元/度，浮点）

## 5. event 规范（logic/event, plat/event）
```json
{
  "ts": 1736150000000,
  "seq": 11,
  "source": "tcu_logic",
  "gun": 0,
  "event": "state_change",
  "data": { ... }
}
```
建议 event 与 data 结构：
- `state_change`
```json
{
  "from": "PREPARE",
  "to": "STARTING",
  "reason": "start_response_ok"
}
```
- `fault`
```json
{
  "code": "deviceErr",
  "faults": ["emergencyStopFault", "smokeFault"],
  "otherFault": 34
} 
```
- `charge_record`
```json
{
  "startTs": 1736150000000,
  "endTs": 1736153600000,
  "durationSec": 3600,
  "energyKwh": 12.34,
  "stopReason": "user_stop"
}
```
- `update_record`
```json
{
  "gunNo": 0,
  "preTradeNo": "P202602120001",
  "tradeNo": "DP202602120001",
  "chargeStartTime": 20260212120001,
  "chargeEndTime": 20260212153010,
  "startSoc": 20.0,
  "endSoc": 80.0,
  "reason": 1,
  "feeModelId": "MODEL002",
  "sumStart": 112.53000,
  "sumEnd": 120.61000,
  "totalElect": 8.08000,
  "totalPowerCost": 15.23000,
  "totalServCost": 2.54000,
  "totalCost": 17.77000
}
```
- `auth_basis`
```json
{
  "source": "platform",
  "startTime": 1736150000000,
  "chargeUserNo": "U10001",
  "orderNo": "P202602120001",
  "chargeMode": 1,
  "prechargeAmount": 100.0,
  "feeModelNo": 1
}
```
- `tcu_stop_request`
```json
{
  "reason": "precharge_near_limit",
  "totalAmount": 99.2,
  "prechargeAmount": 100.0,
  "margin": 1.0,
  "orderNo": "P202602120001"
}
```
- `unconfirmed_record`（`tcu/logger/{gun}/event`）
```json
{
  "ts": 1736150007000,
  "seq": 23,
  "source": "tcu_logger",
  "gun": 0,
  "event": "unconfirmed_record",
  "data": {
    "gunNo": 0,
    "preTradeNo": "26403504517320119890",
    "tradeNo": "26403504517320119890",
    "chargeStartTime": 20260304170119,
    "chargeEndTime": 20260304174710,
    "startSoc": 20.0,
    "endSoc": 35.0,
    "reason": 8,
    "feeModelId": "PLAT_26403504517320119",
    "sumStart": 113.4,
    "sumEnd": 114.475,
    "totalElect": 1.075,
    "totalPowerCost": 0.645,
    "totalServCost": 0.63425,
    "totalCost": 1.27925,
    "timeNum": 1,
    "startPoint": 0,
    "crossPoints": 0,
    "cardNumber": "00000252<<",
    "partElect": [1.075],
    "chargeFee": [0.645],
    "serviceFee": [0.63425],
    "pointsElect": []
  }
}
```

## 6. feeData 规范（logic/feeData）
用于计费/价格展示，独立 topic。时段输出与计费模型一致（`startTs/endTs` 使用 `HHMM`）。
```json
{
  "ts": 1736150000000,
  "seq": 12,
  "source": "tcu_logic",
  "gun": 0,
  "data": {
    "totalEnergy": 12.34,
    "totalAmount": 20.92,
    "electicAmount": 18.52,
    "serviceAmount": 2.40,
    "feeModelNo": 1,
    "feeModelId": "MODEL001",
    "sgemtentNum": 1,
    "segmentsAmount": [
      {
        "startTs": "0000",
        "endTs": "2400",
        "energyKwh": 4.10,
        "electicAmount": 6.15,
        "serviceAmount": 2.40
      }
    ]
  }
}
```

## 7. QoS 建议
- `tcu/pile/{gun}/cmd`：QoS 2
- `tcu/pile/{gun}/event`：QoS 2
- `tcu/pile/{gun}/data`：QoS 0
- `tcu/logic/{gun}/cmd`：QoS 1
- `tcu/logic/{gun}/event`：QoS 2（retain=true）
- `tcu/logic/{gun}/feeData`：QoS 1
- `tcu/meter/{gun}/data`：QoS 0
- `tcu/meter/{gun}/cmd`：QoS 1
- `tcu/plat/{gun}/cmd`：QoS 1
- `tcu/plat/{gun}/event`：QoS 1
- `tcu/logger/{gun}/event`：QoS 2

## 8. 方向约定
- `cmd` 主题：发起控制的一方发布，执行方订阅。
- `event/data` 主题：产生数据的一方发布，使用方订阅。
