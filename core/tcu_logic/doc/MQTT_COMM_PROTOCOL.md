# tcu_logic 模块 MQTT 通信规范（执行版）

## 1. 目标与范围
- 目标：统一 tcu_logic 与 pile_controller、HMI、电表、平台的 MQTT 交互规范。
- 范围：命令下发、事件上报、状态与计量数据。

## 2. Topic 规范
统一前缀：`tcu/`

### 2.1 与 pile_controller 交互
- 下发命令（由 tcu_logic → pile_controller）
  - `tcu/pile/{gun}/cmd`
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

平台鉴权类 cmd（plat/cmd）：
- `auth_ok`：鉴权通过
- `auth_fail`：鉴权失败
- `auth_result`：通用结果（`data.result == 0` 通过，非 0 失败）
```json
{
  "cmd": "auth_result",
  "data": { "result": 0 }
}
```

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
- `tcu/logic/{gun}/event`：QoS 1
- `tcu/logic/{gun}/feeData`：QoS 1
- `tcu/meter/{gun}/data`：QoS 0
- `tcu/meter/{gun}/cmd`：QoS 1
- `tcu/plat/{gun}/cmd`：QoS 1
- `tcu/plat/{gun}/event`：QoS 1

## 8. 方向约定
- `cmd` 主题：发起控制的一方发布，执行方订阅。
- `event/data` 主题：产生数据的一方发布，使用方订阅。
