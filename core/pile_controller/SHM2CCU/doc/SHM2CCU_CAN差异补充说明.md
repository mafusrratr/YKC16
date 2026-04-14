# SHM2CCU 相对 CAN2CCU 的补充处理说明

本文记录 `SHM2CCU` 相比普通 `CAN2CCU` 额外增加的共享内存侧处理，主要用于联调时核对 `feeData` 写入和停机状态通知。

## 1. feeData 写入共享内存

`CAN2CCU` 主要围绕 CAN 协议帧和 MQTT 上下行做转换，不需要把计费实时数据回写到桩侧共享内存。

`SHM2CCU` 增加了对 logic 计费快照的订阅：

```text
tcu/logic/<gun>/feeData
```

收到消息后读取：

```json
{
  "data": {
    "totalAmount": 0.0,
    "totalEnergy": 0.0,
    "chargedTime": 0
  }
}
```

并写入共享内存：

| 数据 | 左枪点位 | 右枪点位 | 分辨率 | 说明 |
| --- | --- | --- | --- | --- |
| 当前消费金额 | `YC167` | `YC423` | `0.0001` | 写入 `totalAmount * 10000` |
| 充电已充电量 | `YC136` | `YC392` | `0.0001kWh` | 非 V2G 模式写入 `totalEnergy * 10000` |
| 放电已放电量 | `YC151` | `YC407` | `0.0001kWh` | V2G 模式写入 `totalEnergy * 10000` |
| 高精度电量 | `DD2` | `DD66` | `0.0001kWh` | 充电和放电都写该点 |
| 累计充电时间 | `YC168` | `YC424` | `1s` | 写入 `chargedTime`，单位秒 |

枪口偏移规则：

```text
YC/YX 右枪偏移 = +256
DD 右枪偏移 = +64
```

充电还是放电只根据启动命令里的 `v2g` 标志判断，不使用共享内存里的充电阶段模式点位。

实现位置：

```text
SHMPileController::updateFeeData()
PileControllerProcess::handleFeeDataMessage()
```

## 2. 停机状态通知 YC210

`SHM2CCU` 还需要接收 logic 的状态转换事件：

```text
tcu/logic/<gun>/event
```

当事件满足：

```json
{
  "event": "state_change",
  "data": {
    "to": "STOPPED"
  }
}
```

写入共享内存即插即充鉴权过程点位：

| 数据 | 左枪点位 | 右枪点位 | 写入值 | 说明 |
| --- | --- | --- | --- | --- |
| 即插即充鉴权过程 | `YC210` | `YC466` | `11` | 停机结算，通知界面 |

实现位置：

```text
SHMPileController::setPlugAndChargeState(11)
PileControllerProcess::handleLogicEventMessage()
```

## 3. 当前保留边界

`SHM2CCU` 不复刻 CAN 协议中的纯帧级应答语义，以下内容仍按共享内存语义处理：

```text
outputVA_ctrl 不做
clear_fault 不做
confirmFlag 不做
CAN 特有版本/握手字段不做
```
