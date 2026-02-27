# tcu_logic 最小测试用例（MQTT 模拟）

## 1. HMI 启动流程（start_charge + auth_result）→ 进入充电
1) 先保证遥信进入 PREPARE
- 发布 topic：`tcu/pile/0/data`
```json
{
  "ts": 1736150000000,
  "seq": 1,
  "source": "pile_controller",
  "gun": 0,
  "type": "yx",
  "data": {
    "vehicleConnectStatus": 1,
    "totalFault": 0,
    "workStatus": 0
  }
}
```
2) HMI 下发启动命令（仅在 PREPARE 有效）
- 发布 topic：`tcu/logic/0/cmd`
```json
{
  "ts": 1736150000100,
  "seq": 2,
  "source": "hmi",
  "gun": 0,
  "cmd": "start_charge",
  "data": {
    "startTime": 1736150000100,
    "chargeUserNo": "U10001",
    "orderNo": "H202602120001",
    "chargeMode": 1,
    "prechargeAmount": 100.0,
    "feeModelNo": 1,
    "feeModelId": "MODEL001",
    "timeNum": 3,
    "timeSeg": ["0000", "0800", "1800"],
    "chargeFee": [0.50, 0.80, 0.60],
    "serviceFee": [0.20, 0.20, 0.20]
  }
}
```
3) 平台返回鉴权结果（含最终启动参数）
- 发布 topic：`tcu/plat/0/cmd`
```json
{
  "ts": 1736150000200,
  "seq": 3,
  "source": "platform",
  "gun": 0,
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
4) 期望：
- `logic/event` 上报 `auth_basis`
- tcu_logic 下发 `tcu/pile/0/cmd` `start_charge`（参数以 `auth_result.data` 为准）
- `logic/event` 上报 `state_change` 到 STARTING
- 充电中收到电量增量后，`tcu/logic/0/feeData` 开始上送分段费用

5) pile_controller 上送启动完成
- 发布 topic：`tcu/pile/0/event`
```json
{
  "ts": 1736150000300,
  "seq": 4,
  "source": "pile_controller",
  "gun": 0,
  "type": "start_complete",
  "data": {
    "successFlag": 0
  }
}
```
期望：状态进入 CHARGING

## 2. 平台启动流程（start_charge + auth_result）→ 进入充电
- 发布 topic：`tcu/plat/0/cmd`
```json
{
  "ts": 1736150001000,
  "seq": 5,
  "source": "platform",
  "gun": 0,
  "cmd": "start_charge",
  "data": {
    "startTime": 1736150001000,
    "chargeUserNo": "U20001",
    "orderNo": "P202602120001",
    "chargeMode": 2,
    "prechargeAmount": 80.0,
    "feeModelNo": 2,
    "feeModelId": "MODEL002",
    "timeNum": 2,
    "timeSeg": ["0000", "1200"],
    "chargeFee": [0.70, 0.90],
    "serviceFee": [0.15, 0.15]
  }
}
```
然后发布 topic：`tcu/plat/0/cmd`
```json
{
  "ts": 1736150001100,
  "seq": 6,
  "source": "platform",
  "gun": 0,
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
期望：先上报 `auth_basis`，再下发 `tcu/pile/0/cmd` `start_charge`，状态到 STARTING

## 3. 停止流程 → STOPPING → STOPPED
1) 充电中下发停止
- 发布 topic：`tcu/logic/0/cmd`
```json
{
  "ts": 1736150002000,
  "seq": 7,
  "source": "hmi",
  "gun": 0,
  "cmd": "stop_charge",
  "data": {
    "stopReason": 1,
    "tcuStopCode": 0
  }
}
```
期望：进入 STOPPING，并下发 `tcu/pile/0/cmd` `stop_charge`

平台停止命令等价用例（与 HMI 停机相同）：
- 发布 topic：`tcu/plat/0/cmd`
```json
{
  "ts": 1736150002050,
  "seq": 701,
  "source": "platform",
  "gun": 0,
  "cmd": "stop_charge",
  "data": {
    "stopReason": 1,
    "tcuStopCode": 0
  }
}
```
期望：同样进入 STOPPING，并下发 `tcu/pile/0/cmd` `stop_charge`

2) pile_controller 上送停止完成
- 发布 topic：`tcu/pile/0/event`
```json
{
  "ts": 1736150002100,
  "seq": 8,
  "source": "pile_controller",
  "gun": 0,
  "type": "stop_complete",
  "data": {
    "stopReason": 1
  }
}
```
期望：保持 STOPPING，进入结算窗口（15s 或电表连续 3 次无变化）

3) 15s 结算窗口内持续上送电表（示例）
- 结算窗口内继续发送 `tcu/meter/0/data`：
  - topic：`tcu/meter/0/data`
```json
{
  "ts": 1736150002200,
  "seq": 9,
  "source": "meter",
  "gun": 0,
  "data": {
    "totalEnergy": 12.34,
    "voltage": 750.2,
    "current": 80.5
  }
}
```
期望：约 15s 后进入 STOPPED；若电表连续 3 次无变化可提前进入 STOPPED

## 4. 充电中触发 TCU 要求停机（预充值阈值）
前提：已进入 CHARGING，且启动命令中包含 `prechargeAmount`。

说明：
- 当前电表通道不携带金额，不可直接通过 `tcu/meter/{gun}/data` 触发预充值停机。
- 该用例依赖计费链路提供总金额（后续对接 `feeData`/计费模块后联调）。

## 5. STOPPED → IDLE
- 发布 topic：`tcu/pile/0/data`
```json
{
  "ts": 1736150003000,
  "seq": 10,
  "source": "pile_controller",
  "gun": 0,
  "type": "yx",
  "data": {
    "vehicleConnectStatus": 0,
    "totalFault": 0,
    "workStatus": 0
  }
}
```
期望：进入 IDLE

## 6. STARTING 超时与重发
1) 先进入 PREPARE（复用用例1步骤1），然后下发启动命令（复用用例1步骤2）
2) 发布鉴权通过（复用用例1步骤3）
3) 不发送 `tcu/pile/0/event` `start_complete`
4) 等待约 30 秒
期望：
- 可观察到 `tcu/pile/0/cmd` 追加重发一次 `start_charge`
5) 继续等待到约 60 秒
期望：
- `logic/event` 出现 `state_change`：`STARTING -> STOPPING`
- reason 为 `starting_timeout_60s`
- 同时可观察到 `tcu/pile/0/cmd` 周期下发 `stop_charge`（2s 间隔）

## 7. STOPPED 不响应 reset_error，仅车辆断开才回 IDLE
1) 先让流程进入 `STOPPED`（复用用例3）
2) 发布 topic：`tcu/logic/0/cmd`
```json
{
  "ts": 1736150004000,
  "seq": 11,
  "source": "hmi",
  "gun": 0,
  "cmd": "reset_error"
}
```
期望：
- 状态保持 `STOPPED`，不发生 `STOPPED -> IDLE`
3) 再发布车辆断开遥信（复用用例5的 `vehicleConnectStatus=0`）
期望：
- 触发 `STOPPED -> IDLE`
