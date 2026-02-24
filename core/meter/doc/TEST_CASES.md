# tcu_meter 最小联调用例

## 1. 启动与基础连通
- 启动进程：`./tcu_meter`
- 期望：
  - 成功打开串口。
  - 成功连接 MQTT。
  - 周期发布 `tcu/meter/{gun}/data`。

## 2. 正常数据上送
- 订阅：`tcu/meter/+/data`
- 期望字段：
  - `data.totalEnergy`
  - `data.voltage`
  - `data.current`

示例：
```json
{
  "ts": 1736150000000,
  "seq": 1,
  "source": "tcu_meter",
  "gun": 0,
  "data": {
    "totalEnergy": 12.34,
    "voltage": 750.2,
    "current": 80.5
  }
}
```

## 3. 离线/恢复事件
- 订阅：`tcu/meter/+/event`
- 人工断开电表总线（或改错地址）后观察：
  - 上报 `meter_offline`
- 恢复通信后观察：
  - 上报 `meter_online`

示例（离线）：
```json
{
  "ts": 1736150000001,
  "seq": 2,
  "source": "tcu_meter",
  "gun": 0,
  "event": "meter_offline",
  "data": {
    "reason": "serial_recv_timeout"
  }
}
```
