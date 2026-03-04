# tcu_comm 架构说明（最小可运行版）

## 1. 定位
- `tcu_comm` 是平台协议适配与路由模块。
- 负责接收平台 TCP/HTTP/私有协议来包，转换后发布到内部 MQTT。
- 负责订阅 `tcu_logic` 与 `pile_controller` 上报，封装为平台协议并上送平台。
- 当前链路按智能城市规约采用 TCP 二进制帧（大端字段）。

## 2. MQTT 路由范围

### 2.1 入站（平台协议 -> 内部 MQTT）
- 发布：`tcu/plat/{gun}/cmd`
- 典型命令：
  - `start_charge`
  - `stop_charge`
  - `auth_result`
  - `set_fee_model`
  - `set_power_limit`

### 2.2 出站（内部 MQTT -> 平台协议）
- 订阅：`tcu/logic/{gun}/event`
- 订阅：`tcu/logic/{gun}/feeData`
- 订阅：`tcu/pile/{gun}/data`
- 订阅：`tcu/pile/{gun}/event`
- 上报：由 `tcu_comm` 封装后通过平台链路发送（非 MQTT）。

## 3. 模块职责边界
- `tcu_comm`：协议桥接、字段适配、平台会话与重连、入站/出站路由。
- `tcu_logic`：状态机、鉴权决策、计费、停机策略。
- `pile_controller`：桩侧协议与设备交互。
- `meter`：电表采集。

## 4. 配置
- 固定配置路径：`/usr/app/config/tcu_comm.ini`
- 配置节：`[Comm]`
- 关键项：
  - `gun_count`
  - `master_host`
  - `master_port`
  - `cdz_no`
  - `mac_addr`（24位十六进制字符串）
  - `factory_credit_code`
  - `tcp_reconnect_sec`
  - `tcp_heartbeat_sec`
  - `login_retry_sec`
  - `mqtt_host`
  - `mqtt_port`
  - `mqtt_keepalive`
  - `mqtt_client_id`
  - `mqtt_topic_prefix`
  - `mqtt_username` / `mqtt_password`

## 5. 平台登录状态机（模板）
- `LOGIN_REQ_SET_CONFIG`：请求配置（0x00）
- `LOGIN_REQ_LOGIN`：请求登录（0x20）
- `LOGIN_DONE`：登录完成，进入周期心跳

状态迁移：
- TCP 连接成功 -> `LOGIN_IDLE`
- `LOGIN_IDLE` -> `LOGIN_REQ_SET_CONFIG`
- 收到 `0x10` 配置确认 -> 发布 `setConfig`，然后进入 `LOGIN_REQ_LOGIN`
- 收到 `0x30` 登录应答 -> `LOGIN_DONE`
- 任一步骤发送失败或链路断开 -> 关闭 socket，按 `tcp_reconnect_sec` 重连

## 6. 帧格式（当前实现）
- 前导字：`F5 E2 F3 06`
- 版本：`0x04`
- 时间戳：8字节 BCD（`YYYYMMDDHHmmssFF`）
- 长度：2字节大端（序号+命令字+信息体+CRC）
- 序号：1字节
- 命令字：1字节
- 信息体：N字节
- CRC：2字节（CRC16-Modbus）

## 6. 当前实现状态
- 已实现：最小可运行内部 MQTT 订阅与路由骨架。
- 已实现：TCP 链路模板（连接维护、重连、登录状态机、心跳模板）。
- 已实现：平台 `0x14` 远程启动命令解析，发布 `tcu/plat/{gun}/cmd` 的 `start_charge`。
- 已实现：平台 `0x15` 远程停止命令解析，发布 `tcu/plat/{gun}/cmd` 的 `stop_charge`（`stopReason=0x01`）。
- 已实现：平台 `0x16` 交易记录确认解析，发布 `tcu/plat/{gun}/cmd` 的 `record_cfm`（含 `tradeNo`）。
- 已实现：`0x14` 携带的时段费率转换为 `FeeModel` 并调用 `LogSender::saveFeeModel(...)` 落库链路。
- 未实现：
  - 其余平台命令（停止、功率调节、更多应答）解析与下发
  - 平台协议报文签名/校验
  - 请求去重与幂等表
  - 登录应答与会话态闭环管理
  - 平台错误码映射

## 7. `0x14` 远程启动到内部命令映射
- 平台来包：`cmd=0x14`，信息体格式：
  - `gunId(4) + orderId(10,BCD) + userId(5,BCD) + userStatus(1)`
  - `energyBlock(4) + billingFlag(1) + periodCount(1) + period[N]*6`
  - 每个时段：`startHour(1,BCD) + endHour(1,BCD) + chargeFee(2,BE,分) + serviceFee(2,BE,分)`
- 内部发布：`tcu/plat/{gun}/cmd`，`cmd=start_charge`
- `data` 字段包含：
  - `startTime`（本地毫秒时间戳）
  - `chargeUserNo`（BCD 用户号转字符串）
  - `orderNo`（BCD 订单号转字符串）
  - `prechargeAmount`（`energyBlock/100.0`，元）
  - `timeNum/timeSeg/chargeFee/serviceFee`
  - `feeModelId`（当前规则：`PLAT_`+订单号前缀）
- 计费模型落库：
  - `FeeModel.timeSeg` 使用 `"HHMM"`（当前按小时点映射为 `"HH00"`）
  - `FeeModel.chargeFee/serviceFee` 单位为 `0.001元`（由协议 `分` 转换为 `*10`）

## 8. MQTT 示例
### 8.1 setConfig（由 0x10 配置确认触发）
- Topic：`tcu/plat/0/setConfig`
```json
{
  "ts": 1772200000000,
  "seq": 12,
  "source": "tcu_comm",
  "gun": 0,
  "cmd": "setConfig",
  "data": {
    "cdzId": 58050010904022,
    "feeModel": {
      "timeNum": 2,
      "timeSeg": ["0000", "1200"],
      "chargeFee": [0.7, 0.9],
      "serviceFee": [0.15, 0.15]
    },
    "gunCount": 2,
    "guns": [
      {"gun": 0, "gunId": 4294967295, "gunType": 1},
      {"gun": 1, "gunId": 4294967295, "gunType": 1}
    ]
  }
}
```
