# tcu_meter 架构与评审要点

## 1. 目标
- 通过 RS485 读取直流 645 电表数据。
- 按枪上送 MQTT 数据：`totalEnergy`、`voltage`、`current`。
- 通过 MQTT 事件上送在线/离线状态。

## 2. 进程与文件
- 进程名：`tcu_meter`
- 入口：`core/meter/main.cpp`
- 核心流程：`core/meter/meter_process.*`
- 645 驱动：`core/meter/dlt645_meter.*`
- 构建：`core/meter/Makefile.cross`

## 3. 配置（/usr/app/config/tcu_meter.ini）
- `gun_count`
- `gunN_meter_addr`
- `gunN_serial_device`（必填）
- `gunN_serial_baudrate`（可选）
- `gunN_serial_data_bits`（可选）
- `gunN_serial_stop_bits`（可选）
- `gunN_serial_parity`（可选）
- `gunN_read_timeout_ms`（可选）
- `serial_baudrate`
- `serial_data_bits`
- `serial_stop_bits`
- `serial_parity`
- `mqtt_host`
- `mqtt_port`
- `mqtt_keepalive`
- `mqtt_client_id`
- `mqtt_topic_prefix`
- `poll_interval_ms`
- `read_timeout_ms`
- `retry_count`

## 4. MQTT Topic
- 数据上送：`tcu/meter/{gun}/data`（QoS0）
- 事件上送：`tcu/meter/{gun}/event`（QoS1）

数据示例：
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

事件示例：
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

## 5. 轮询与异常策略
- 每枪按 `poll_interval_ms` 轮询。
- 单次读取超时 `read_timeout_ms`。
- 单次轮询失败后按 `retry_count` 重试。
- 首次失败或在线转离线时发布 `meter_offline`。
- 离线恢复后发布 `meter_online`。
- 当前版本按“多口多表”约束：每枪串口设备号必须唯一。

## 6. 当前实现边界（评审重点）
- DI 采用代码内固定常量（未放配置）：
  - 总电量：`0x0001FF00`
  - 电压：`0x02100100`
  - 电流：`0x02110100`
- 若现场表型 DI 不一致，需要在 `dlt645_meter.cpp` 调整。
- 地址按 12 位十六进制解析，支持输入中含分隔符（非十六进制字符会被过滤）。

## 7. 每枪属性与端口绑定逻辑（多口多表）
- 每枪配置对象：`gunN_meter_addr + gunN_serial_*`。
- `gunN_serial_device` 为必填；其余枪级串口参数缺省时回退到全局 `serial_*`。
- 初始化时按每枪串口参数创建独立串口实例。
- 若检测到枪间 `serial_device` 重复，初始化失败并报错。
- 轮询时以“枪”为单位调度，读表通过枪绑定的 `portIndex` 执行。

## 8. 配置示例（多口多表）
```ini
gun_count=2
serial_baudrate=9600
serial_data_bits=8
serial_stop_bits=1
serial_parity=E
gun1_meter_addr=000000000001
gun1_serial_device=/dev/ttyS1
gun2_meter_addr=000000000002
gun2_serial_device=/dev/ttyS2
```

## 9. 数据流 UML
- 数据读取-解析-MQTT上送流程图（PlantUML）：
  - `core/meter/doc/DATA_FLOW_UML.puml`

## 10. 项目结构 UML
- 模块结构图（PlantUML）：
  - `core/meter/doc/PROJECT_STRUCTURE_UML.puml`
- 渲染图（SVG）：
  - `core/meter/doc/PROJECT_STRUCTURE_UML.svg`

## 11. 进展记录
- 当前模块进展与待验收项：
  - `core/meter/doc/PROGRESS.md`
