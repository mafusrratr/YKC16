# tcu_hmi 模块概要设计（评审稿）

## 1. 模块定位
`tcu_hmi` 是本地人机显示进程，基于 Qt4.8.6 开发，负责：
- 根据枪数量（1/2）自适应布局。
- 根据充电状态实时切换显示内容。
- 聚合 `logic/pile/plat` 多来源 MQTT 数据并可视化。

本模块只做显示与交互，不参与状态机决策，不直接控制充电。

---

## 2. 目录规划（已创建）
- `core/hmi/`
- `core/hmi/include/`
- `core/hmi/src/`
- `core/hmi/doc/`

编译目标：`release/tcu_hmi`（后续在 `core/hmi/Makefile.cross` 落地）。

---

## 3. 输入/输出接口

## 3.1 MQTT 输入（订阅）

1) `tcu/logic/{gun}/event`（QoS2，retain 兼容）
- 关键事件：`state_change`
- 用于驱动页面状态：`IDLE/PREPARE/STARTING/CHARGING/STOPPING/STOPPED`

2) `tcu/logic/{gun}/feeData`（QoS1）
- 关键字段：
  - `data.totalEnergy`
  - `data.totalAmount`
  - `data.electicAmount`
  - `data.serviceAmount`
  - `data.chargedTime`（若存在）
- 用于显示实时金额、电量、充电时长。

3) `tcu/pile/{gun}/data`（QoS0）
- `type=yc` 时读取：
  - `outputVoltage`
  - `outputCurrent`
- HMI 侧计算 `power = voltage * current / 1000`（kW）。

4) `tcu/plat/{gun}/event`（QoS1，建议 retain）
- `type=setConfig` 时读取：
  - `data.cdzId`
  - `data.gunCount`
  - `data.guns[]`（gunId、gunType）
  - `data.operatorId`（如有）
  - `data.macAddr`（如有）
- 用于决定 1枪/2枪布局，以及 PREPARE 状态二维码内容生成。

## 3.2 本模块输出
- 不向业务模块下发控制命令。
- 可选输出：
  - `logger info`（例如：UI 初始化成功、收到 setConfig、状态切换）。

---

## 4. 核心数据模型（进程内）

1) 全局配置模型
- `gunCount`（仅支持 1 或 2）
- `cdzId`
- `guns[2].gunId`

2) 每枪运行态模型
- `state`（来自 logic event）
- `voltage/current/power`（来自 pile yc）
- `totalEnergy/totalAmount/electicAmount/serviceAmount/chargedTime`（来自 feeData）
- `qrPayload`（由 setConfig 生成）

3) 页面布局模型
- `layoutMode=single|dual`
- 单枪：独占全屏
- 双枪：左 0 / 右 1

---

## 5. 关键流程

## 5.1 启动流程
1. 启动 Qt 应用与主窗口。
2. 连接 MQTT 并订阅 4 类 topic。
3. 等待 `setConfig`：
   - 若已收到 `gunCount=2`，切换双枪布局。
   - 否则默认单枪布局。

## 5.2 状态驱动显示流程
收到 `logic/{gun}/event` 的 `state_change` 后刷新页面：
- `IDLE`：显示“请插枪”。
- `PREPARE`：显示二维码（基于 `cdzId+gunId` 规则生成）。
- `STARTING`：显示“充电启动中...”。
- `CHARGING`：显示电压/电流/功率/实时金额/充电电量。
- `STOPPING`：显示“停止中...”。
- `STOPPED`：显示“已完成充电，如需再次充电请重新插拔充电枪”，并保留本次电量/金额。

## 5.3 PREPARE 二维码生成流程
输入：`gunId` + `operatorId` + `macAddr` + 规则前缀。

建议默认规则（可配置）：
- `qr_payload = "hlht://" + gunId + "." + operatorId + "/" + macAddr`

说明：
- 当前阶段先统一二维码负载规则，后续如需与平台完全一致，可替换为平台指定格式。

---

## 6. 界面布局规则

屏幕分辨率约束：
- 固定分辨率：`800 x 480`。
- 布局与字号以 `800x480` 为基准进行适配。

1) `gunCount=1`
- 仅展示枪0卡片，宽高占满内容区。

2) `gunCount=2`
- 画面左右两栏。
- 左栏固定枪0，右栏固定枪1。

3) 刷新策略
- MQTT 到消息后更新内存模型。
- UI 线程定时（例如 100~200ms）合并刷新，避免闪烁。

---

## 7. 与现有模块边界
- `tcu_logic`：唯一状态源。
- `pile_controller`：实时电压电流源。
- `comm`：平台配置（setConfig）来源。
- `logger`：可选运行日志。

`tcu_hmi` 不修改上述模块业务逻辑，仅消费消息渲染。

---

## 8. 下一步落地内容（待你评审后实施）
1. 建立 `core/hmi` 可编译工程（Qt4.8.6 + MQTT）。
2. 实现 HMI 数据模型与 MQTT 适配层。
3. 实现 1/2 枪自适应主界面。
4. 实现状态页与实时数据渲染。
5. 实现二维码绘制与规则配置项。
6. 补充 `tcu_hmi.ini` 配置与联调用例。

---

## 9. 详细流程文档
- 加载与状态转换说明：`core/hmi/doc/STATE_FLOW.md`
- 流程 UML：`core/hmi/doc/STATE_FLOW_UML.puml`
