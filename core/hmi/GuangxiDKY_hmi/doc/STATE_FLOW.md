# tcu_hmi 加载与状态转换说明

## 1. 模块加载流程

## 1.1 进程启动
1. 进入 `main()`，创建 `QApplication`。
2. 创建 `HmiWindow` 对象。
3. 调用 `HmiWindow::initialize()` 完成初始化。

## 1.2 initialize() 初始化步骤
1. `loadConfig()`
   - 固定读取 `/usr/app/config/tcu_hmi.ini`。
   - 读取屏幕分辨率、MQTT 地址和基础参数。
   - 全屏模式固定开启（不依赖配置开关）。
2. 固定窗口尺寸为 `800x480`（或配置覆盖值）。
3. `initMqtt()`
   - 建立 MQTT 连接。
   - 启动 mosquitto loop 线程。
4. 启动 UI 刷新定时器（100ms）。
5. 强制 `showFullScreen()`。

## 1.3 MQTT 连接后订阅
连接回调 `onMqttConnected()` 中订阅：
- `tcu/logic/+/event`（状态事件）
- `tcu/logic/+/feeData`（计费与时长）
- `tcu/pile/+/data`（YC 电压电流）
- `tcu/plat/+/event`（setConfig）
- `tcu/plat/+/setConfig`（兼容历史 topic）

---

## 2. 运行时数据加载

## 2.1 平台配置加载（setConfig）
输入：`tcu/plat/{gun}/event` 或 `tcu/plat/{gun}/setConfig`

识别条件：
- `type == "setConfig"` 或 `cmd == "setConfig"`

加载字段：
- `data.gunCount`（限制 1~2）
- `data.cdzId`
- `data.guns[]` 中的 `gun`、`gunId`

加载结果：
- 更新布局模式：1枪全屏 / 2枪左右分栏（左0右1）
- 生成每枪二维码载荷：`hlht://{gunId}.{operatorId}/{macAddr}`

## 2.2 业务数据加载
1) `logic/event`（`event=state_change`）
- 从 `data.to` 更新枪状态。

2) `logic/feeData`
- 更新：`totalEnergy`、`totalAmount`、`chargedTime`。

3) `pile/data`（`type=yc`）
- 更新：`outputVoltage`、`outputCurrent`。
- 本地计算：`power = voltage * current / 1000`（kW）。

---

## 3. 状态转换与界面映射

状态来源：`tcu/logic/{gun}/event` 的 `state_change`。

映射关系：
- `IDLE` -> 显示“请插枪”
- `PREPARE` -> 显示二维码
- `STARTING` -> 显示“充电启动中...”
- `CHARGING` -> 显示电压/电流/功率/实时金额/充电电量/充电时长
- `STOPPING` -> 显示“停止中...”
- `STOPPED` -> 显示“已完成充电，如需再次充电请重新插拔充电枪”，并显示最终电量与金额

说明：
- HMI 不自行推进状态，只消费 `logic` 上送状态。
- UI 刷新由 100ms 定时器统一触发，避免消息突发导致闪烁。

---

## 4. 线程与同步
- MQTT 回调线程更新内存态。
- Qt UI 线程负责绘制。
- 共享数据通过 `QMutex` 保护，避免并发读写冲突。

---

## 5. 退出流程
1. 停止 MQTT loop。
2. 断开 MQTT。
3. 释放窗口和 Qt 应用。
