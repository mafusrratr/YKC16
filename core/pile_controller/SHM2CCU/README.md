# SHM2CCU 模块说明

`SHM2CCU` 是 `pile_controller` 在共享内存接桩场景下的实现，整体目标仍然是对齐 `CAN2CCU` 的 MQTT 交互口径，但由于 SHM 模式没有 CAN 帧层语义，也没有 HMI/CAN 直连链路，所以模块内部额外承担了一部分共享内存读写、状态补偿和联调打印职责。

本文只记录当前已经落地的 `SHM2CCU` 特殊处理，供联调和后续维护时快速核对。

## 1. feeData 回写共享内存

`SHM2CCU` 订阅：

```text
tcu/logic/<gun>/feeData
```

收到计费快照后，将 logic 数据直接回写到桩侧共享内存。

| 数据 | 左枪点位 | 右枪点位 | 分辨率 | 说明 |
| --- | --- | --- | --- | --- |
| 当前消费金额 | `YC167` | `YC423` | `0.0001` | 写入 `totalAmount * 10000` |
| 充电已充电量 | `YC136` | `YC392` | `0.0001kWh` | 非 V2G 模式写入 `totalEnergy * 10000` |
| 放电已放电量 | `YC151` | `YC407` | `0.0001kWh` | V2G 模式写入 `totalEnergy * 10000` |
| 高精度电量 | `DD2` | `DD66` | `0.0001kWh` | 充电和放电都写该点 |
| 累计充电时间 | `YC168` | `YC424` | `1s` | 写入 `chargedTime` |

枪口偏移规则：

```text
YC/YX 右枪偏移 = +256
DD 右枪偏移 = +64
```

实现位置：

```text
PileControllerProcess::handleFeeDataMessage()
SHMPileController::updateFeeData()
```

## 2. logic 停机状态补写 YC210/YC466

`SHM2CCU` 订阅：

```text
tcu/logic/<gun>/event
```

当收到：

```json
{
  "event": "state_change",
  "data": {
    "to": "STOPPED"
  }
}
```

会将即插即充鉴权过程点位写为停机结算状态：

| 数据 | 左枪点位 | 右枪点位 | 写入值 | 说明 |
| --- | --- | --- | --- | --- |
| 即插即充鉴权过程 | `YC210` | `YC466` | `11` | 通知 CCU/HMI 进入停机结算界面 |

实现位置：

```text
PileControllerProcess::handleLogicEventMessage()
SHMPileController::setPlugAndChargeState()
```

## 3. SHM 本地即插即充流程勘误

SHM 模式下本地即插即充链路按共享内存点位驱动，`pile_controller` 当前承担的是“消费桩侧请求、立即拉起即插即充启动、再把 VIN 和鉴权结果往上层/CCU 双向搬运”的职责，而不是“等 logic 鉴权通过后再启动充电”。

### 3.1 启动触发点

启动入口读取：

| 枪口 | 点位 |
| --- | --- |
| A 枪 | `YX222` |
| B 枪 | `YX222 + 256 = YX478` |

该点在代码中使用常量：

```text
YX_HMI_LOCAL_PNC_START
```

实现位置：

```text
SHMPileController::getYX23Data()
```

### 3.2 消费 `YX222` 并立即发 `YX175`

当检测到 `vinReq` 从 `0 -> 1` 时，`SHM2CCU` 会立即执行两件事：

1. 清掉本地请求位
2. 按即插即充模式直接写启动点 `YX175/YX431`

其中：

| 动作 | 点位 | 说明 |
| --- | --- | --- |
| 清本地请求 | `YX222/YX478` | 读取后立即清 `value=0`，避免重复触发 |
| 发启动命令 | `YX175/YX431` | `plugAndChargeFlag=0x02`，即插即充方式启动 |

实现位置：

```text
SHMPileController::clearLocalPncStartRequest()
SHMPileController::setStartChargeData()
SHMPileController::startCharge()
PileControllerProcess::updateStatusFromController()
```

### 3.3 代发 `vin_req`

同时，`SHM2CCU` 仍会向：

```text
tcu/logic/<gun>/cmd
```

发布命令：

```text
cmd = vin_req
```

当前固定补齐的 `data` 字段：

| 字段 | 值 |
| --- | --- |
| `startTime` | 当前毫秒时间戳 |
| `plugAndChargeFlag` | `0x02` |
| `mergeChargeFlag` | `0x00` |
| `loadControlSwitch` | `0x02` |
| `auxPowerVoltage` | `0x0C` |
| `v2g` | `0x00` |

实现位置：

```text
PileControllerProcess::updateStatusFromController()
PileControllerProcess::publishLogicCmd()
PileControllerProcess::buildCmdPayload()
```

### 3.4 VIN 读取来源

`SHM2CCU` 在等待 VIN 时当前只读取：

1. `YC130.desname`

附加约束：

1. 只有 `YC130.value == 1` 时，才读取 `YC130.desname`

VIN 合法性不在 pile_controller 侧判断，统一交给 logic 处理。

同时还会补齐这些上送字段：

| 字段 | 来源 |
| --- | --- |
| `vin` | `YC130.desname` |
| `batteryChargeCount` | `YC_BATTERY_CHARGE_COUNT` |
| `soc` | `YC_START_COMPLETE_SOC` |
| `currentBatteryVoltage` | `YC_CURRENT_TOTAL_VOLTAGE` |

实现位置：

```text
SHMPileController::getVehicleIdData()
```

### 3.5 代发 `vehicle_id`

当 VIN 到位后，`SHM2CCU` 会向：

```text
tcu/pile/<gun>/event
```

发布事件：

```text
event = vehicle_id
```

实现位置：

```text
PileControllerProcess::updateStatusFromController()
```

### 3.6 处理 logic 鉴权结果

`SHM2CCU` 额外处理以下 logic 消息：

| 消息 | 处理 |
| --- | --- |
| `vehicle_id_confirm` | 记录确认结果并打印日志 |
| `vehicle_auth` | 将鉴权结果写入 `YX195/YX451`，随后回发 `vehicle_auth_ack` |

`YX195/YX451` 当前按现场约定写法如下：

| 字段 | 含义 |
| --- | --- |
| `desname[0]` | `result`，`0=成功`，`1=失败` |
| `desname[1]` | `reason` |

也就是说，`vehicle_auth` 不再触发二次 `startCharge()`；真正的共享内存启动动作已经在 `YX222` 被消费时完成。

实现位置：

```text
PileControllerProcess::onMqttMessage()
SHMPileController::setPlugAndChargeAuthResult()
```

## 4. 平台在线状态同步到 YC32

`SHM2CCU` 订阅：

```text
tcu/plat/<gun>/event
```

当收到平台链路事件时，同步写共享内存 `YC32`：

| 事件 | 写入值 | 说明 |
| --- | --- | --- |
| `platform_offline` | `0` | 平台离线 |
| `platform_online` | `1` | 平台在线 |

当前源码里保留了一个固定宏：

```cpp
#define SHM2CCU_PLATFORM_ONLINE_PNC 1
```

含义如下：

| 宏值 | `platform_online` 时写入 |
| --- | --- |
| `0` | `YC32 = 1` |
| `1` | `YC32 = 2` |

也就是如果后续需要“在线且可即插即充”口径，可以直接把源码里的宏改成 `1`。

实现位置：

```text
PileControllerProcess::handlePlatEventMessage()
SHMPileController::setSystemOnlineState()
```

## 5. pile 启停命令调试打印

为了排查 SHM 模式下“平台已下发命令，但桩侧没有执行”的问题，`SHM2CCU` 额外增加了 pile 命令入口日志。

当前会打印：

| 场景 | 日志目的 |
| --- | --- |
| 收到原始 `pile cmd` 报文 | 确认消息是否真的进了 `SHM2CCU` |
| JSON 解析失败 | 区分协议问题和业务问题 |
| 缺少 `cmd` 字段 | 排查平台字段格式不一致 |
| `cmd` 不支持 | 识别命令名不匹配 |
| `start_charge` / `stop_charge` 收包 | 确认启停命令是否进入本模块 |

实现位置：

```text
PileControllerProcess::onMqttMessage()
```

## 6. 当前仍然按 SHM 语义保留的边界

`SHM2CCU` 不复刻 `CAN2CCU` 的纯帧级应答行为，以下内容当前仍按共享内存语义处理：

```text
outputVA_ctrl 不做
clear_fault 不做
confirmFlag 不做
CAN 特有版本/握手字段不做
```

## 7. 相关文件

当前 SHM 特殊处理主要集中在以下文件：

```text
core/pile_controller/SHM2CCU/pile_controller_process.cpp
core/pile_controller/SHM2CCU/pile_controller_process.h
core/pile_controller/SHM2CCU/shm_pile_controller.cpp
core/pile_controller/SHM2CCU/shm_pile_controller.h
core/pile_controller/SHM2CCU/shm_point_map.h
```

原有补充说明仍保留在：

```text
core/pile_controller/SHM2CCU/doc/SHM2CCU_CAN差异补充说明.md
```
