# tcu_logic 计费逻辑说明

## 1. 目标
- 在 `CHARGING/STOPPING` 阶段，基于电表电量增量和计费模型计算费用。
- 按时段累计电量、电费、服务费，并发布 `feeData`。
- 当总金额接近预充值金额时，触发 `tcu_stop_request` 并停机。

## 2. 输入来源
### 2.1 启动命令（`start_charge.data`）
- `startTime`：启动时间戳（ms）
- `chargeUserNo`：充电用户号
- `orderNo`：充电订单号
- `chargeMode`：充电模式
- `prechargeAmount`：预充值金额（元）
- `feeModelNo`：计费模型编号
- `feeModelId`：计费模型ID（可选）
- `timeNum`：时段数
- `timeSeg[]`：时段起始时间（`HHMM`）
- `chargeFee[]`：电费单价（单位：`元/度`，浮点）
- `serviceFee[]`：服务费单价（单位：`元/度`，浮点）

### 2.2 电表数据（`tcu/meter/{gun}/data`）
- 电量：`totalEnergy` / `energy` / `total_kwh`（累计电量）
- 电压：`voltage` / `meterVoltage` / `outputVoltage`
- 电流：`current` / `meterCurrent` / `outputCurrent`

说明：电表通道不传金额，金额由本模块计算。

## 3. 计费模型解析
- `timeSeg` 由 `HHMM` 转为“日内分钟”。
- 分段按起始分钟升序排序。
- 价格使用：
  - `price(元/度) = feeValue`（直接使用）

## 4. 计费计算流程
1. 收到首个电量值时建立基线（不计费）。
2. 后续每次收到电量，计算增量：
   - `deltaKwh = currentTotalEnergy - lastTotalEnergy`
3. `deltaKwh > 0` 才计费：
   - 按当前时间映射到时段 `segIdx`
   - `deltaElectric = deltaKwh * chargePrice[segIdx]`
   - `deltaService = deltaKwh * servicePrice[segIdx]`
4. 累加：
   - 分段：`segEnergy/segElectric/segService`
   - 总计：`totalEnergy/electicAmount/serviceAmount/totalAmount`
5. 发布 `tcu/logic/{gun}/feeData`（QoS1）。

## 5. 停机阈值（预充值）
- 触发条件：
  - `totalAmount >= prechargeAmount - precharge_stop_margin`
- 触发动作：
  - 发布 `logic/event`：`tcu_stop_request`
  - 触发状态机停机流程（进入 `STOPPING`，下发 `stop_charge`）
- 单次充电周期只触发一次。

## 6. 费率时段输出格式
`feeData.data.segmentsAmount[]` 与计费模型时段一致：
- `startTs`：时段开始时间（`HHMM`）
- `endTs`：下一时段开始时间（`HHMM`，最后一段为 `2400`）
- `energyKwh`：该时段累计电量
- `electicAmount`：该时段累计电费
- `serviceAmount`：该时段累计服务费

## 7. 当前实现边界
- 时段按“当前本地时间分钟”选段计费，不回溯跨段拆分。
- 仅基于累计电量增量计费，若电量回退（异常）不扣减费用。
- 金额字段名保持现有对接格式：`electicAmount`（保持兼容，未更名）。

## 8. 交易记录落库（LogSender）
- 触发时机：`STOPPING -> STOPPED` 转换时。
- 调用接口：`LogSender::logTradeRecord(const TradeRecord& rec)`。
- 主要字段映射：
  - `gunNo` <- 枪号
  - `preTradeNo` <- 启动命令 `preTradeNo`（无则回退 `orderNo`）
  - `tradeNo` <- 设备交易号（优先基于订单号生成）
  - `vinCode` <- `start_complete.vin`
  - `timeDivType` <- `chargeMode`
  - `startType` <- 启动来源（HMI=1，Platform=2）
  - `chargeStartTime/chargeEndTime` <- 启停时间（YYYYMMDDHHMMSS）
  - `startSoc/endSoc` <- `start_complete.soc`(0.1% -> %) / `stop_complete.stopSoc`(%)，统一按 % 保存
  - `reason` <- `stop_complete.stopReason`
  - `feeModelId` <- 计费模型ID
  - `sumStart/sumEnd` <- 电表总示值（kWh，double，保留5位小数）
  - `totalElect` <- 总电量（kWh，double，保留5位小数）
  - `totalPowerCost/totalServCost/totalCost` <- 费用（元，double，保留5位小数）
  - `timeNum/partElect/chargeFee/serviceFee` <- 分段计费结果
