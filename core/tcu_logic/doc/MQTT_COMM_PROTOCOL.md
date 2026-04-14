# tcu_logic 模块 MQTT 通信规范（执行版）

## 1. 目标与范围
- 目标：统一 tcu_logic 与 pile_controller、HMI、电表、平台的 MQTT 交互规范。
- 范围：命令下发、事件上报、状态与计量数据。

## 2. Topic 规范
统一前缀：`tcu/`

### 2.1 与 pile_controller 交互
- 下发命令（由 tcu_logic → pile_controller）
  - `tcu/pile/{gun}/cmd`
  - 支持命令：`start_charge` / `stop_charge` / `power_ctrl` / `vehicle_id_confirm` / `vehicle_auth`
- 上送命令/流程事件（由 pile_controller → tcu_logic）
  - `tcu/pile/{gun}/event`
  - 事件类型：`start_response/start_complete/stop_response/stop_complete/deviceErr_on/deviceErr_off/vehicle_id/vehicle_auth_ack`
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
  - 即插即充鉴权请求例外：由 tcu_logic 发布到 `tcu/logic/{gun}/event`，事件名为 `plug_and_charge_auth_request`

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
  "cmd": "vin_req",
  "data": { ... }
}
```
建议 cmd：
- `vin_req`
- `start_charge`
- `stop_charge`
- `reset_error`
- `set_policy`
- `power_ctrl`

说明：
- HMI 发起即插即充识别流程时，入口命令建议使用 `vin_req`
- `start_charge` 主要用于平台补充业务参数或兼容旧流程
- `tcu/plat/{gun}/cmd` 统一使用 `cmd` 字段表示操作类型

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
- 当枪处于即插即充启动阶段，且已完成 `vehicle_id` 确认后，`auth_result` 会被按即插即充鉴权结果处理，
  等价于 `plug_and_charge_auth_result`。

- `plug_and_charge_auth_result`：即插即充专用平台鉴权结果
```json
{
  "cmd": "plug_and_charge_auth_result",
  "data": {
    "vin": "LDC913L27A1234567",
    "successFlag": 0,
    "failReason": 0
  }
}
```

说明：
- `successFlag`：`0` 成功，`1` 失败
- `failReason`：`0` 成功，`1` 非法 VIN，`2` 平台鉴权失败，`3` 平台鉴权超时，`255` 其他
- `vin` 兼容字段名 `vinCode`
- 平台返回失败时，不应伪装成 `cmd=start_charge`，而应明确发布 `cmd=plug_and_charge_auth_result`

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

`vin_req.data`（HMI 发起即插即充请求）建议字段：
- `startTime`：请求时间戳（ms，可选）
- `plugAndChargeFlag`：即插即充标志，`0x01` 非即插即充，`0x02` 即插即充
- `mergeChargeFlag`：合并充电标志
- `loadControlSwitch`：负荷控制开关（可选）
- `auxPowerVoltage`：辅助电源电压（可选）
- `v2g`：V2G 标志（可选）

`start_charge.data`（平台下发业务参数）建议字段：
- `startTime`：启动时间戳
- `chargeUserNo`：充电用户号
- `orderNo`：充电订单号
- `preTradeNo`：平台交易流水
- `tradeNo`：本地交易流水（可选）
- `plugAndChargeFlag`：即插即充标志，`0x01` 非即插即充，`0x02` 即插即充
- `mergeChargeFlag`：合并充电标志
- `v2g`：充放电模式标志0充 1 放
- `chargeMode`：充电模式
- `prechargeAmount`：预充值金额
- `feeModelNo`：计费模型编号
- `feeModelId`：计费模型ID（可选）
- `timeNum`：时段数
- `timeSeg[]`：时段开始时间（`HHMM`）
- `chargeFee[]`：各时段电费（单位：元/度，浮点）
- `serviceFee[]`：各时段服务费（单位：元/度，浮点）




即插即充识别请求示例（HMI -> logic）：
Topic：`tcu/logic/{gun}/cmd`
```json
{
  "ts": 1736150000000,
  "seq": 10,
  "source": "hmi",
  "gun": 0,
  "cmd": "vin_req",
  "data": {
    "startTime": 1736150000000,
    "plugAndChargeFlag": 2,
    "mergeChargeFlag": 0,
    "auxPowerVoltage": 12
  }
}
```

即插即充业务参数示例（平台 -> logic）：
Topic：`tcu/plat/{gun}/cmd`
```json
{
  "ts": 1736150003000,
  "seq": 11,
  "source": "platform",
  "gun": 0,
  "cmd": "start_charge",
  "data": {
    "vin": "LJ21BABB8L1001955",
    "chargeUserNo": "U10001",
    "orderNo": "P202602120001",
    "preTradeNo": "P202602120001",
    "tradeNo": "P202602120001",
    "plugAndChargeFlag": 2,
    "mergeChargeFlag": 1,
    "chargeMode": 1,
    "prechargeAmount": 100.0,
    "feeModelNo": 1,
    "feeModelId": "MODEL001",
    "timeNum": 12,
    "timeSeg": ["0000", "1200", "1430", "1535", "1538", "1540", "1550", "1600", "1730", "1830", "1930", "2130"],
    "chargeFee": [70, 0.90, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2],
    "serviceFee": [15, 0.15, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2]
  }
}
```

## 4.1 即插即充新增 MQTT 消息
当 HMI 发起 `vin_req` 且 `data.plugAndChargeFlag == 0x02` 时，logic 与 pile/plat 增加以下消息交互。

### 4.1.1 pile 上送车辆识别数据
Topic：`tcu/pile/{gun}/event`
```json
{
  "ts": 1736150001000,
  "seq": 101,
  "source": "pile_controller",
  "gun": 0,
  "type": "vehicle_id",
  "data": {
    "vin": "LDC913L27A1234567",
    "batteryChargeCount": [0, 0, 12],
    "soc": 560,
    "currentBatteryVoltage": 3850
  }
}
```

说明：
- 对应 CCU -> logic 的 `0x17` 车辆识别数据帧
- `soc`、`currentBatteryVoltage` 当前按原始协议值透传

### 4.1.2 logic 下发车辆识别数据应答
Topic：`tcu/pile/{gun}/cmd`
```json
{
  "ts": 1736150001100,
  "seq": 102,
  "source": "tcu_logic",
  "gun": 0,
  "cmd": "vehicle_id_confirm",
  "data": {
    "successFlag": 0,
    "failReason": 0
  }
}
```

说明：
- 对应 logic -> CCU 的 `0x18` 车辆识别数据应答帧
- 失败场景 `failReason=1` 表示非法 VIN

### 4.1.3 logic 发布平台鉴权请求
Topic：`tcu/logic/{gun}/event`
```json
{
  "ts": 1736150001200,
  "seq": 103,
  "source": "tcu_logic",
  "gun": 0,
  "event": "plug_and_charge_auth_request",
  "data": {
    "vin": "LDC913L27A1234567",
    "vinCode": "LDC913L27A1234567",
    "plugAndChargeFlag": 2,
    "mergeChargeFlag": 1,
    "chargeStartTime": 20260212120000,
    "soc": 560,
    "currentBatteryVoltage": 3850,
    "batteryChargeCount": [0, 0, 12]
  }
}
```

说明：
- plat/tcu_comm 订阅 `tcu/logic/{gun}/event` 后，收到 `event=plug_and_charge_auth_request` 即可发起平台鉴权
- 该消息在完成 `0x17/0x18` 后由 logic 主动发布
- 该请求仅用于向外部发起鉴权，不携带订单号、用户号、交易流水等业务字段
### 4.1.4 平台返回即插即充启动参数
Topic：`tcu/plat/{gun}/cmd`
```json
// {
//   "ts": 1736150003200,
//   "seq": 104,
//   "source": "platform",
//   "gun": 0,
//   "cmd": "start_charge",
//   "data": {
//     "vin": "LDC913L27A1234567",
//     "chargeUserNo": "U10001",
//     "orderNo": "P202602120001",
//     "preTradeNo": "P202602120001",
//     "tradeNo": "P202602120001",
//     "plugAndChargeFlag": 2,
//     "mergeChargeFlag": 0,
//     "chargeMode": 1,
//     "prechargeAmount": 100.0,
//     "feeModelNo": 1,
//     "feeModelId": "MODEL001",
//     "timeNum": 12,
//     "timeSeg": ["0000", "1200", "1430", "1535", "1538", "1540", "1550", "1600", "1730", "1830", "1930", "2130"],
//     "chargeFee": [70, 0.90, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2],
//     "serviceFee": [15, 0.15, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2]
//   }
// }
```

### 4.1.5 logic 下发车辆识别鉴权帧
Topic：`tcu/pile/{gun}/cmd`
```json
{
  "ts": 1736150003300,
  "seq": 105,
  "source": "tcu_logic",
  "gun": 0,
  "cmd": "vehicle_auth",
  "data": {
    "vin": "LDC913L27A1234567",
    "successFlag": 0,
    "failReason": 0,
    "chargeUserNo": "U10001",
    "orderNo": "P202602120001",
    "preTradeNo": "P202602120001",
    "tradeNo": "P202602120001",
    "chargeStartTime": 20260212120000,
    "chargeMode": 1,
    "prechargeAmount": 100.0,
    "feeModelNo": 1,
    "feeModelId": "MODEL001",
    "plugAndChargeFlag": 2,
    "mergeChargeFlag": ,
    "timeNum": 12,
    "timeSeg": ["0000", "1200", "1430", "1535", "1538", "1540", "1550", "1600", "1730", "1830", "1930", "2130"],
    "chargeFee": [70, 0.90, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2],
    "serviceFee": [15, 0.15, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2]
  }
}
```

说明：
- 对应 logic -> CCU 的 `0x19` 车辆识别鉴权帧
- MQTT 层除了 `0x19` 所需的 `vin/successFlag/failReason` 外，还附带启动充电业务参数，便于 pile/联调侧复用同一条业务上下文
- 若平台 30s 内未返回鉴权结果，logic 会自动下发失败：
```json
{
  "cmd": "vehicle_auth",
  "data": {
    "vin": "LDC913L27A1234567",
    "successFlag": 1,
    "failReason": 3,
    "plugAndChargeFlag": 2,
    "mergeChargeFlag": 1
  }
}
```

### 4.1.6 pile 上送车辆识别鉴权应答
Topic：`tcu/pile/{gun}/event`
```json
{
  "ts": 1736150003400,
  "seq": 106,
  "source": "pile_controller",
  "gun": 0,
  "type": "vehicle_auth_ack",
  "data": {
    "successFlag": 0,
    "failReason": 0
  }
}
```

说明：
- 对应 CCU -> logic 的 `0x1A` 车辆识别鉴权应答帧
- `failReason=1` 表示 VIN 不一致

### 4.1.7 logic 侧联调事件
Topic：`tcu/logic/{gun}/event`

常用新增事件：
- `plug_and_charge_vehicle_id_confirmed`
- `plug_and_charge_vehicle_id_reject`
- `plug_and_charge_auth_result_forwarded`
- `plug_and_charge_auth_timeout`
- `plug_and_charge_auth_ack`

`plug_and_charge_auth_ack` 示例：
```json
{
  "ts": 1736150003400,
  "seq": 107,
  "source": "tcu_logic",
  "gun": 0,
  "event": "plug_and_charge_auth_ack",
  "data": {
    "successFlag": 0,
    "failReason": 0
  }
}
```

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
  "plugAndChargeFlag": 2,
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
