# Zhongshihua2.0 UI 适配分析

## 已有 UI 的页面职责

- `B1 Idle`
  - 适合做双枪总览页
  - `cell0/cell1` 已经能承接每枪空闲/充电概要
- `C6 FlushCad`
  - 适合承接 `PREPARE/STARTING`
  - 已有二维码区、枪号、提示文案
- `E1 ChargeInfo`
  - 适合承接 `CHARGING/STOPPING`
  - 已有金额、电量、时长、电压、电流、SOC 展示位
- `F7 CheckoutOk`
  - 适合承接 `STOPPED`
  - 已有结束金额、电量、时长、停机原因展示位
- `A3 About`
  - 不属于核心充电流程页
  - 作为辅助信息/配置页保留

## 与当前 HMI 流程逻辑的对应关系

当前项目流程输入来自 MQTT：

- `tcu/logic/{gun}/event`
- `tcu/logic/{gun}/feeData`
- `tcu/pile/{gun}/data`
- `tcu/plat/{gun}/event`
- `tcu/plat/{gun}/setConfig`

运行态映射已经按下面方式接入：

- `IDLE` -> `B1`
- `PREPARE` / `STARTING` -> `C6`
- `CHARGING` / `STOPPING` -> `E1`
- `STOPPED` -> `F7`

## 当前这版适配的设计取舍

- 保留 `Zhongshihua2.0` 的原始视觉和 `.ui` 页面，不改 UI 文件结构。
- 新增运行态窗口，用适配层把 MQTT 数据填进现有控件。
- 默认启动运行态；如需继续看纯页面预览，可带 `--preview` 启动。

## 还存在的结构性差异

- 当前 UI 只有 `B1` 是天然双枪总览页，`C6/E1/F7` 都是单枪全屏页。
- 现有流程逻辑支持双枪同时存在业务状态，因此运行态做了“焦点枪”策略：
  - 优先显示最近进入业务态的枪
  - `B1` 继续显示双枪概览
- 如果后续要严格支持“双枪同时各自独立全屏流程”，需要再补一套左右分栏的 `C6/E1/F7` 组合界面。

## 本次适配已接通的数据

- 平台配置：`gunCount`、`cdzId`、`operatorId`、`macAddr`、`gunId`
- 逻辑状态：`state_change`
- 费用数据：`totalAmount`、`totalEnergy`、`chargedTime`
- 遥测数据：`outputVoltage`、`outputCurrent`
- 扩展字段兼容：`soc` / `vehicleSoc`、`reasonText` / `stopReasonText`

## 推荐后续增强

1. 增加 `ERROR` 专用页或故障弹层，而不是只在 `B1` 上做告警标识。
2. 把 `A3 About` 里真正需要的运维字段接到当前配置源。
3. 若项目确定长期双枪并发展示，补一版双枪业务页，不再依赖焦点枪切换。
