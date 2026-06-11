# virtualPlug Pile/Meter MQTT 仿真规格

## 1. 目标与边界

`virtualPlug` 的主定位是 **pile_controller 模拟器**，用于在没有真实主控、BMS、负载和电表的情况下，给 `tcu_logic` / `tcu_comm` 提供可控的桩侧 MQTT 输入。

核心目标：

- 模拟 `pile_controller`，订阅 `tcu/pile/{gun}/cmd`。
- 发布 `tcu/pile/{gun}/data` 和 `tcu/pile/{gun}/event`。
- 自动配合发布 `tcu/meter/{gun}/data` 和 `tcu/meter/{gun}/event`。
- 通过 WebUI 提供业务动作级交互，例如插枪、拔枪、启动完成、停机完成、故障触发、响应 VIN 请求。
- 所有 `yx`、`yc`、`start_complete`、`meter data` 从同一份枪/车辆/BMS 配置派生，避免字段之间互相矛盾。

不属于 `virtualPlug` 的职责：

- 不判断 VIN 是否合法。
- 不判断双枪 VIN 是否一致。
- 不替 `tcu_logic` 做业务状态机判断。
- 不模拟平台 TCP 私有协议；平台鉴权结果可先通过内部 MQTT 入口辅助下发。
- 不模拟真实 CAN/485 帧细节，只模拟 MQTT 语义层。

## 2. Topic 与通用 payload

`{gun}` 使用全局枪号：

```text
globalGun = localGun + bias_no
```

当多个 `virtualPlug` 分布式运行时，每个实例通过 `bias_no` 和 `gun_count` 占用一段不重叠的全局枪号。

主要 Topic：

| 方向 | Topic | 说明 |
|---|---|---|
| logic -> virtualPlug | `tcu/pile/{gun}/cmd` | pile 命令入口 |
| virtualPlug -> logic/comm/HMI | `tcu/pile/{gun}/data` | pile 遥信/遥测 |
| virtualPlug -> logic/comm | `tcu/pile/{gun}/event` | pile 流程事件 |
| virtualPlug -> logic/comm/HMI | `tcu/meter/{gun}/data` | 电表读数 |
| virtualPlug -> logic | `tcu/meter/{gun}/event` | 电表在线/离线 |

通用 payload：

```json
{
  "ts": 1736150000000,
  "seq": 1,
  "source": "pile_controller",
  "gun": 0,
  "type": "yx",
  "data": {}
}
```

字段说明：

- `ts`：毫秒时间戳。
- `seq`：`virtualPlug` 内部递增序号。
- `source`：模拟来源，pile 使用 `pile_controller`，meter 使用 `tcu_meter`。
- `gun`：全局枪号，应与 topic 一致。
- `cmd`：仅用于 `cmd` topic。
- `type`：用于 pile `data/event` 的消息类型，例如 `yx`、`yc`、`start_complete`。
- `event`：用于 meter `event`，例如 `meter_online`、`meter_offline`。
- `data`：业务字段。

## 3. virtualPlug 接收的 pile cmd

`virtualPlug` 订阅 `tcu/pile/{gun}/cmd`，按当前 `pile_controller` 代码口径处理以下命令。

| cmd | 关键字段 | virtualPlug 行为 |
|---|---|---|
| `start_charge` | `loadControlSwitch`、`plugAndChargeFlag`、`auxPowerVoltage`、`mergeChargeFlag`、`v2g` | 缓存启动上下文，更新枪模式，等待页面触发启动应答或启动完成 |
| `stop_charge` | `stopReason`、`tcuStopCode` | 缓存停机上下文，等待页面触发停机应答或停机完成 |
| `vehicle_id_confirm` | `successFlag`、`failReason`、`result` | 记录 logic 对 VIN 的确认结果，不自行判断 VIN |
| `vehicle_auth` | `successFlag`、`failReason`、`result`、`vin` / `vinCode` | 记录平台鉴权结果，可由页面或自动规则发布 `vehicle_auth_ack` |
| `power_ctrl` | `maxChargePowerKw` 或数字 payload | 调整当前枪功率上限，影响后续电流/电量模型 |
| `outputVA_ctrl` | `demandVoltage` / `outputVoltage` / `voltage`，`demandCurrent` / `outputCurrent` / `current` | 调整当前枪输出电压、电流目标 |
| `clear_fault` | 无固定字段 | 清除当前枪故障状态，发布故障恢复相关报文 |

`start_charge` 示例：

```json
{
  "ts": 1736150000000,
  "seq": 10,
  "source": "tcu_logic",
  "gun": 0,
  "cmd": "start_charge",
  "data": {
    "loadControlSwitch": 1,
    "plugAndChargeFlag": 1,
    "auxPowerVoltage": 12,
    "mergeChargeFlag": 0,
    "v2g": 0
  }
}
```

`v2g != 0` 表示放电/V2G 模式。`virtualPlug` 只记录并切换计量方向，业务是否合法由 `tcu_logic` 判断。

## 4. virtualPlug 发布的 pile data

### 4.1 遥信 yx

Topic：`tcu/pile/{gun}/data`

`type=yx` 表示桩侧遥信状态，主要驱动插枪、拔枪、故障、工作状态变化。

```json
{
  "ts": 1736150000000,
  "seq": 11,
  "source": "pile_controller",
  "gun": 0,
  "type": "yx",
  "data": {
    "workStatus": 0,
    "totalFault": 0,
    "totalAlarm": 0,
    "emergencyStopFault": 0,
    "vehicleConnectStatus": 1,
    "vinReq": 0,
    "gunSeatStatus": 1,
    "electronicLockStatus": 1,
    "dcContactorStatus": 0,
    "otherFault": 0
  }
}
```

关键字段：

| 字段 | 生成来源 | 对 logic 的意义 |
|---|---|---|
| `vehicleConnectStatus` | 页面插枪/拔枪动作 | 非 0 表示插枪，0 表示拔枪 |
| `workStatus` | 当前枪运行状态 | 充电中变为 0 会触发停机流程 |
| `totalFault` | 故障配置 | 非 0 触发故障事件 |
| `otherFault` | 故障点配置 | 非 0 可触发常态故障记录 |
| `vinReq` | VIN 请求状态，可选 | 用于展示或兼容，不替代 `vehicle_id` event |
| `dcContactorStatus` | 输出状态 | 启动后可置 1，停机后置 0 |
| `electronicLockStatus` | 枪锁状态 | 插枪后可置 1，拔枪后置 0 |

### 4.2 遥测 yc

Topic：`tcu/pile/{gun}/data`

`type=yc` 表示桩侧遥测数据。该报文应与车辆/BMS 配置、电气输出和 meter 数据同步生成。

```json
{
  "ts": 1736150001000,
  "seq": 12,
  "source": "pile_controller",
  "gun": 0,
  "type": "yc",
  "data": {
    "outputVoltage": 750.0,
    "outputCurrent": 80.0,
    "soc": 45,
    "batteryMinTemp": 25,
    "batteryMaxTemp": 30,
    "cellMaxVoltage": 3.65,
    "cellMinVoltage": 3.55,
    "pileEnvTemp": 28,
    "guideVoltage": 12.0,
    "bmsReqVoltage": 750.0,
    "bmsReqCurrent": 80.0,
    "chargeMode": 1,
    "bmsMeasuredVoltage": 748.5,
    "bmsMeasuredCurrent": 79.5,
    "estimatedRemainTime": 30,
    "interfaceTemp1": 28,
    "interfaceTemp2": 28,
    "interfaceTemp3": 28,
    "interfaceTemp4": 28,
    "maxVoltageCellNo": 1,
    "maxTempPointNo": 1,
    "minTempPointNo": 1,
    "inletTemp": 25,
    "outletTemp": 28,
    "envHumidity": 50
  }
}
```

`yc` 联动要求：

- `outputVoltage`、`outputCurrent` 来自当前枪输出配置或 `outputVA_ctrl`。
- `bmsReqVoltage`、`bmsReqCurrent` 来自车辆/BMS 需求配置。
- `bmsMeasuredVoltage`、`bmsMeasuredCurrent` 与实际输出值同步，可允许小幅偏差。
- `chargeMode` 跟随启动命令或枪配置。
- `soc` 随充电/放电方向周期变化。
- `estimatedRemainTime` 每次周期发布 `yc` 时重算。
- `batteryMinTemp`、`batteryMaxTemp`、`cellMaxVoltage`、`cellMinVoltage` 等来自车辆/BMS 配置，可随运行轻微变化。

## 5. virtualPlug 发布的 pile event

### 5.1 在线/离线

`pile_online`：

```json
{
  "ts": 1736150000000,
  "seq": 20,
  "source": "pile_controller",
  "gun": 0,
  "type": "pile_online",
  "data": {
    "reason": "heartbeat_ok",
    "heartbeatCommStatus": 0
  }
}
```

`pile_offline`：

```json
{
  "ts": 1736150000000,
  "seq": 21,
  "source": "pile_controller",
  "gun": 0,
  "type": "pile_offline",
  "data": {
    "reason": "heartbeat_timeout",
    "heartbeatCommStatus": 1
  }
}
```

### 5.2 启动应答 start_response

当前 `tcu_logic` 不依赖 `start_response` 决定进入充电，但该报文仍可用于联调完整时序。

```json
{
  "ts": 1736150001000,
  "seq": 22,
  "source": "pile_controller",
  "gun": 0,
  "type": "start_response",
  "data": {
    "confirmFlag": 0,
    "startFailReason": 0,
    "loadControlSwitch": 1,
    "plugAndChargeFlag": 1,
    "auxPowerVoltage": 12
  }
}
```

### 5.3 启动完成 start_complete

`start_complete` 是 `tcu_logic` 判断启动成功/失败的关键报文。

```json
{
  "ts": 1736150002000,
  "seq": 23,
  "source": "pile_controller",
  "gun": 0,
  "type": "start_complete",
  "data": {
    "successFlag": 0,
    "chargeFailReason": 0,
    "pileBmsVersion": [1, 0, 0],
    "bmsPileVersion": [1, 0, 0],
    "handshakeResult": 0,
    "batteryType": 1,
    "maxAllowTemp": 60,
    "bmsMaxChargeVoltage": 750,
    "cellMaxChargeVoltage": 4200,
    "maxAllowChargeCurrent": 250,
    "ratedTotalVoltage": 750,
    "currentTotalVoltage": 740,
    "ratedCapacity": 200,
    "nominalEnergy": 150,
    "soc": 45,
    "pileMaxOutputVoltage": 1000,
    "pileMinOutputVoltage": 200,
    "pileMaxOutputCurrent": 250,
    "pileMinOutputCurrent": 0,
    "vin": "LJ21BABB8L1001955",
    "batteryManufacturer": "TEST",
    "batterySerial": [0, 0, 0, 1],
    "batteryProdYear": 2026,
    "batteryProdMonth": 5,
    "batteryProdDay": 15,
    "batteryChargeCount": [0, 1, 2],
    "batteryPropertyFlag": 0,
    "bmsSoftwareVersion": [1, 0, 0, 0, 0, 0, 0, 0]
  }
}
```

生成规则：

- `successFlag=0` 表示启动成功；非 0 表示启动失败。
- 失败时必须带 `chargeFailReason`。
- `vin` 来自枪/车辆配置；普通充电和 VIN 充电都应填入。
- `soc` 使用启动完成时当前 SOC。
- `bmsMaxChargeVoltage`、`maxAllowChargeCurrent`、`currentTotalVoltage` 来自车辆/BMS 配置。
- `pileMaxOutputVoltage`、`pileMinOutputVoltage`、`pileMaxOutputCurrent`、`pileMinOutputCurrent` 来自桩能力配置。
- `batteryType`、`ratedTotalVoltage`、`ratedCapacity`、`nominalEnergy`、`batteryManufacturer`、`batterySerial`、`bmsSoftwareVersion` 来自车辆配置。
- `currentTotalVoltage` 应与启动时 SOC、车辆配置和 `yc.bmsMeasuredVoltage` 保持合理一致。

### 5.4 VIN 上送 vehicle_id

`vehicle_id` 只在 VIN/即插即充流程中作为 BMS 车辆识别响应发布。普通充电不需要单独发布该事件，但普通充电的 `start_complete` 仍应携带 VIN。

```json
{
  "ts": 1736150001500,
  "seq": 24,
  "source": "pile_controller",
  "gun": 0,
  "type": "vehicle_id",
  "data": {
    "vin": "LJ21BABB8L1001955",
    "batteryChargeCount": [0, 1, 2],
    "soc": 45,
    "currentBatteryVoltage": 740
  }
}
```

生成规则：

- `vin` 来自对应枪配置。
- 双枪 VIN 合并充时，A/B 枪分别按各自配置发布。
- `virtualPlug` 不判断 VIN 合法性和一致性。

### 5.5 鉴权确认 vehicle_auth_ack

```json
{
  "ts": 1736150002500,
  "seq": 25,
  "source": "pile_controller",
  "gun": 0,
  "type": "vehicle_auth_ack",
  "data": {
    "successFlag": 0,
    "failReason": 0
  }
}
```

`successFlag` 建议与收到的 `vehicle_auth` 结果保持一致。

### 5.6 停机应答 stop_response

```json
{
  "ts": 1736150008000,
  "seq": 26,
  "source": "pile_controller",
  "gun": 0,
  "type": "stop_response",
  "data": {
    "confirmFlag": 0
  }
}
```

当前 `tcu_logic` 基本忽略 `stop_response`，但保留用于完整时序。

### 5.7 停机完成 stop_complete

```json
{
  "ts": 1736150010000,
  "seq": 27,
  "source": "pile_controller",
  "gun": 0,
  "type": "stop_complete",
  "data": {
    "stopReason": 1,
    "stopSuccessFlag": 0,
    "bmsStopReason": 0,
    "bmsChargeFaultReason": 0,
    "bmsStopErrorReason": 0,
    "stopSoc": 80,
    "cellMinVoltage": 3.55,
    "cellMaxVoltage": 3.65,
    "batteryMinTemp": 25,
    "batteryMaxTemp": 32,
    "faults": []
  }
}
```

注意：

- `stop_complete` 会更新停机原因和结束 SOC。
- `tcu_logic` 从 `STOPPING` 收口到 `STOPPED` 还依赖电表连续稳定或超时，不只依赖 `stop_complete`。

### 5.8 故障出现/恢复

故障出现：

```json
{
  "ts": 1736150015000,
  "seq": 28,
  "source": "pile_controller",
  "gun": 0,
  "type": "deviceErr_on",
  "data": {
    "totalFault": 1,
    "otherFault": 1,
    "faults": ["emergencyStopFault"]
  }
}
```

故障恢复：

```json
{
  "ts": 1736150020000,
  "seq": 29,
  "source": "pile_controller",
  "gun": 0,
  "type": "deviceErr_off",
  "data": {
    "totalFault": 0,
    "otherFault": 0
  }
}
```

页面触发故障时，建议同时更新 `yx.totalFault/otherFault`，再发布 `deviceErr_on`，让状态量和事件一致。

## 6. meter 自动配合数据

meter 默认跟随 pile 自动生成，只保留在线/离线和少量参数的手动控制。

Topic：`tcu/meter/{gun}/data`

```json
{
  "ts": 1736150003000,
  "seq": 40,
  "source": "tcu_meter",
  "gun": 0,
  "data": {
    "totalEnergy": 12.345,
    "ReverseEnergy": 0.0,
    "voltage": 750.0,
    "current": 80.0
  }
}
```

联动规则：

- 充电时递增 `totalEnergy`。
- 放电时递增 `ReverseEnergy`。
- `voltage` 与 `pile.yc.outputVoltage` 保持一致。
- `current` 与 `pile.yc.outputCurrent` 保持一致。
- `tcu_logic` 在 `v2g=1` 时优先使用 `ReverseEnergy` 作为计量电量。

meter 事件：

```json
{
  "ts": 1736150000001,
  "seq": 41,
  "source": "tcu_meter",
  "gun": 0,
  "event": "meter_offline",
  "data": {
    "reason": "manual_inject"
  }
}
```

```json
{
  "ts": 1736150005001,
  "seq": 42,
  "source": "tcu_meter",
  "gun": 0,
  "event": "meter_online",
  "data": {
    "reason": "manual_recover"
  }
}
```

## 7. 枪/车辆/BMS 配置模型

每把枪至少需要维护以下配置。WebUI 可按“枪配置页”展示，业务动作只从配置中取值。

### 7.1 枪配置

| 字段 | 示例 | 用途 |
|---|---:|---|
| `localGun` | 0 | 本实例内枪号 |
| `globalGun` | 0 | MQTT topic 枪号 |
| `gunName` | A枪 | 页面显示 |
| `vin` | `LJ21BABB8L1001955` | `vehicle_id` 和 `start_complete` |
| `mode` | `charge` / `discharge` | 决定计量方向 |
| `outputVoltage` | 750.0 | `yc` 和 meter |
| `outputCurrent` | 80.0 | `yc` 和 meter |
| `initialSoc` | 45 | 初始 SOC |
| `targetSoc` | 80 | 充电目标 SOC |
| `dischargeTargetSoc` | 20 | 放电目标 SOC |
| `socRatePerMinute` | 1.0 | 剩余时长和 SOC 变化 |
| `initialTotalEnergy` | 12.345 | meter 正向电量初值 |
| `initialReverseEnergy` | 0.0 | meter 反向电量初值 |

### 7.2 桩能力配置

| 字段 | 示例 |
|---|---:|
| `pileMaxOutputVoltage` | 1000 |
| `pileMinOutputVoltage` | 200 |
| `pileMaxOutputCurrent` | 250 |
| `pileMinOutputCurrent` | 0 |

### 7.3 车辆/BMS 配置

| 字段 | 示例 | 用途 |
|---|---:|---|
| `batteryType` | 1 | `start_complete` |
| `ratedTotalVoltage` | 750 | `start_complete` |
| `currentTotalVoltage` | 740 | `start_complete` / `vehicle_id` |
| `ratedCapacity` | 200 | `start_complete` |
| `nominalEnergy` | 150 | `start_complete` |
| `bmsMaxChargeVoltage` | 750 | `start_complete` |
| `cellMaxChargeVoltage` | 4200 | `start_complete` |
| `maxAllowChargeCurrent` | 250 | `start_complete` |
| `maxAllowTemp` | 60 | `start_complete` |
| `batteryManufacturer` | `TEST` | `start_complete` |
| `batterySerial` | `[0,0,0,1]` | `start_complete` |
| `batteryChargeCount` | `[0,1,2]` | `start_complete` / `vehicle_id` |
| `bmsSoftwareVersion` | `[1,0,0,0,0,0,0,0]` | `start_complete` |
| `batteryMinTemp` | 25 | `yc` / `stop_complete` |
| `batteryMaxTemp` | 32 | `yc` / `stop_complete` |
| `cellMinVoltage` | 3.55 | `yc` / `stop_complete` |
| `cellMaxVoltage` | 3.65 | `yc` / `stop_complete` |
| `bmsReqVoltage` | 750.0 | `yc` |
| `bmsReqCurrent` | 80.0 | `yc` |

## 8. 页面业务动作到 MQTT 报文映射

| 页面动作 | MQTT 报文 |
|---|---|
| 插 A 枪 | `pile/A/data type=yx vehicleConnectStatus=1,totalFault=0` |
| 拔 A 枪 | `pile/A/data type=yx vehicleConnectStatus=0` |
| 主控上线 | `pile/A/event type=pile_online` |
| 主控离线 | `pile/A/event type=pile_offline` |
| 启动应答成功 | `pile/A/event type=start_response confirmFlag=0` |
| 启动完成成功 | `pile/A/event type=start_complete successFlag=0 + 当前枪 VIN/BMS 数据` |
| 启动完成失败 | `pile/A/event type=start_complete successFlag!=0,chargeFailReason=...` |
| 开始输出 | 周期发布 `pile/A/data type=yc` 和 `meter/A/data` |
| 停机应答成功 | `pile/A/event type=stop_response confirmFlag=0` |
| 停机完成 | `pile/A/event type=stop_complete` |
| 故障触发 | 更新 `pile/A/data type=yx totalFault=1,otherFault=...`，并发布 `deviceErr_on` |
| 故障恢复 | 更新 `pile/A/data type=yx totalFault=0,otherFault=0`，并发布 `deviceErr_off` |
| 响应 VIN 请求 | `pile/A/event type=vehicle_id vin=枪配置 VIN` |
| 鉴权确认应答 | `pile/A/event type=vehicle_auth_ack` |
| meter 离线 | `meter/A/event event=meter_offline` |
| meter 在线 | `meter/A/event event=meter_online` |

## 9. 充电、放电、VIN、合并充关键时序

### 9.1 单枪普通充电

```text
1. 页面点击插枪
2. virtualPlug 发布 yx(vehicleConnectStatus=1)
3. logic 进入 PREPARE
4. HMI/平台下发 start_charge 到 logic
5. logic 下发 tcu/pile/{gun}/cmd start_charge
6. 页面点击启动完成成功
7. virtualPlug 发布 start_complete(successFlag=0, vin=枪配置VIN)
8. logic 进入 CHARGING
9. virtualPlug 周期发布 yc + meter data
10. logic/HMI/平台下发 stop_charge
11. 页面点击停机完成
12. virtualPlug 发布 stop_complete，并继续发布稳定 meter data
13. logic 收口到 STOPPED
14. 页面点击拔枪
15. virtualPlug 发布 yx(vehicleConnectStatus=0)
```

### 9.2 单枪放电

与普通充电类似，但启动上下文中 `v2g=1`：

```text
meter.totalEnergy 默认不变
meter.ReverseEnergy 按功率递增
soc 按 socRatePerMinute 递减
estimatedRemainTime 按放电目标 SOC 重算
```

### 9.3 单枪 VIN 即插即充

```text
1. 插枪
2. HMI/平台触发 vin_req 或 plugAndChargeFlag=2 的 start_charge
3. logic 下发 start_charge 到 pile
4. 页面点击“响应 VIN 请求”
5. virtualPlug 发布 vehicle_id(vin=枪配置VIN)
6. logic 下发 vehicle_id_confirm
7. 平台鉴权结果进入 logic
8. logic 下发 vehicle_auth
9. virtualPlug 发布 vehicle_auth_ack
10. 页面点击启动完成
11. virtualPlug 发布 start_complete(vin=枪配置VIN)
```

### 9.4 合并充命令启动

```text
1. A/B 双枪插枪
2. logic 分别下发 A/B 枪 start_charge
3. 页面点击双枪启动完成
4. virtualPlug 分别发布 A/B 枪 start_complete
5. A/B 枪分别周期发布 yc + meter data
6. 停机时分别发布 stop_complete
```

### 9.5 VIN 合并充启动

```text
1. A/B 双枪插枪
2. logic 分别下发 A/B 枪 start_charge
3. 页面点击“响应双枪 VIN 请求”
4. virtualPlug 分别发布 A/B 枪 vehicle_id，VIN 均来自各自枪配置
5. logic 判断 VIN 合法性和一致性
6. 若 logic 继续下发 vehicle_auth，virtualPlug 分别发布 vehicle_auth_ack
7. 页面点击双枪启动完成
8. virtualPlug 分别发布 A/B 枪 start_complete
```

## 10. 字段生成与联动规则

### 10.1 电量计算

每个计量周期：

```text
power_kW = outputVoltage * outputCurrent / 1000
delta_kWh = power_kW * intervalSec / 3600
```

充电：

```text
totalEnergy += delta_kWh
ReverseEnergy 不变
soc += socRatePerMinute * intervalSec / 60
```

放电：

```text
ReverseEnergy += delta_kWh
totalEnergy 默认不变
soc -= socRatePerMinute * intervalSec / 60
```

SOC 需要限制在 `0..100` 范围内。

### 10.2 BMS 需求与实测同步

`yc` 中 BMS 相关字段按以下规则生成：

```text
bmsReqVoltage = vehicle.bmsReqVoltage
bmsReqCurrent = vehicle.bmsReqCurrent
bmsMeasuredVoltage = outputVoltage + measuredVoltageOffset
bmsMeasuredCurrent = outputCurrent + measuredCurrentOffset
currentTotalVoltage = vehicle.currentTotalVoltage，可随 SOC 小幅变化
```

默认情况下：

```text
measuredVoltageOffset = -1.5
measuredCurrentOffset = -0.5
```

偏差只用于仿真真实测量差异，不应造成明显不合理数据。

### 10.3 剩余时长 estimatedRemainTime

每次周期发布 `yc` 时重算。

充电：

```text
remain = (targetSoc - soc) / socRatePerMinute
```

放电：

```text
remain = (soc - dischargeTargetSoc) / socRatePerMinute
```

边界规则：

- `socRatePerMinute <= 0` 时，`estimatedRemainTime=0`。
- 计算结果小于 0 时置 0。
- 建议单位为分钟，取整数或保留 1 位小数均可；为了贴近当前 pile 上送字段，优先使用整数分钟。

### 10.4 start_complete 与运行态一致性

`start_complete` 生成时必须从当前运行态和配置取值：

- `vin` 使用当前枪配置 VIN。
- `soc` 使用当前运行态 SOC。
- `currentTotalVoltage` 使用当前车辆/BMS 总电压。
- `bmsMaxChargeVoltage`、`maxAllowChargeCurrent` 使用 BMS 能力配置。
- `pileMaxOutputVoltage`、`pileMinOutputVoltage`、`pileMaxOutputCurrent`、`pileMinOutputCurrent` 使用桩能力配置。
- `batteryChargeCount`、`batteryManufacturer`、`batterySerial`、`bmsSoftwareVersion` 使用车辆配置。
- 若启动失败，`successFlag` 非 0，并设置 `chargeFailReason`；其余字段仍可按当前配置填充，便于上层记录和排查。

### 10.5 停机完成与 meter 稳定

发布 `stop_complete` 后，不应立即停止 meter 数据。为了让 `tcu_logic` 完成 STOPPING 收口，`virtualPlug` 应继续发布若干次稳定 meter data：

```text
totalEnergy/ReverseEnergy 不再变化
voltage/current 可逐步降为 0 或保持最后值后归零
```

推荐策略：

- `stop_complete` 后至少发布 3 次相同电量的 meter data。
- 周期与运行期一致，例如 1s。
- 然后将 `yc.outputCurrent`、`meter.current` 置 0。

