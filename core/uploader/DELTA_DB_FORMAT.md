# tcu_uploader 增量包数据结构

## 上传包结构

增量上送文件名：

```text
delta_<device_id>_<created_ts>.tar.gz
```

包内固定包含：

```text
delta.db
manifest.json
```

上送包维度为桩级别，不按枪拆分文件。枪号只作为明细数据字段存在，例如 `gun_no`。平台侧如需判断枪数量，应根据 `delta.db` 中交易记录、遥测点、运行日志等数据综合推断，而不是依赖文件名或目录层级。

`delta.db` 是标准 SQLite3 数据库文件，不定义私有二进制页格式。读取方应使用 SQLite API 打开，不直接解析物理页。

## manifest.json

典型结构：

```json
{
  "mode": "incremental",
  "device_id": "46010003275590",
  "created_at": "20260528160000",
  "tables": [
    {
      "db": "tcu.db",
      "table": "operation_logs",
      "watermark_type": "time_seq",
      "time_column": "timestamp",
      "seq_column": "id",
      "from_time": "2026-05-28 15:58:00",
      "from_seq": "100",
      "to_time": "2026-05-28 16:00:00",
      "to_seq": "135",
      "rows": 35
    }
  ],
  "missing_files": []
}
```

字段说明：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `mode` | string | 固定为 `incremental`。 |
| `device_id` | string | 设备/桩编号。 |
| `created_at` | string | 增量包生成时间，格式 `YYYYMMDDHHMMSS`。 |
| `tables` | array | 本包实际导出的表。没有新增行的表不会出现。 |
| `missing_files` | array | 缺失但允许跳过的源 DB 文件，例如 `telemetry.db`。 |

`tables[]` 字段说明：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `db` | string | 源数据库文件名。 |
| `table` | string | 源表名，也是 `delta.db` 中的表名。 |
| `watermark_type` | string | 当前固定为 `time_seq`。 |
| `time_column` | string | 主水位时间列。 |
| `seq_column` | string | 同一时间内的辅助排序列。 |
| `from_time` / `from_seq` | string | 本次导出起始水位，不包含该点。 |
| `to_time` / `to_seq` | string | 本次导出结束水位，包含该点。 |
| `rows` | number | 本表导出行数。 |

## delta.db 物理头和页

`delta.db` 是标准 SQLite3 文件：

| 项 | 说明 |
| --- | --- |
| 文件头 | SQLite 标准 100 字节数据库头，魔数为 `SQLite format 3\000`。 |
| 页大小 | 由 SQLite 创建时决定，通常为 4096 字节；读取方不要假设固定页大小。 |
| 页类型 | SQLite 标准页，包括 schema 表页、表 B-tree 页、溢出页等。 |
| 自定义页 | 无。`tcu_uploader` 不写自定义页、不写私有二进制块。 |

平台侧或排查工具应通过：

```sh
sqlite3 delta.db ".tables"
sqlite3 delta.db ".schema"
```

查看逻辑表结构，而不是直接解析 SQLite 页。

## delta.db 逻辑表

`delta.db` 只包含本轮有新增数据的表，表名和字段与源表保持一致。

### tcu.db / operation_logs

水位：`timestamp + id`

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | 运行日志自增 ID。 |
| `timestamp` | DATETIME | 日志时间。 |
| `level` | INTEGER | 日志级别。 |
| `module` | TEXT | 模块名。 |
| `message` | TEXT | 日志消息。 |
| `details` | TEXT | 详情。 |
| `created_at` | DATETIME | 入库时间。 |

### tcu.db / performance_logs

水位：`timestamp + id`

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | 性能日志自增 ID。 |
| `timestamp` | DATETIME | 采样时间。 |
| `cpu_usage` | REAL | CPU 使用率。 |
| `memory_usage` | REAL | 内存使用率。 |
| `disk_usage` | REAL | 磁盘使用率。 |
| `network_usage` | REAL | 网络使用率。 |

### error.db / fault_records

水位：`occur_time + id`

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | 故障记录自增 ID。 |
| `gun` | INTEGER NOT NULL | 枪号。 |
| `type` | TEXT NOT NULL | 事件类型，例如 `Error`。 |
| `occur_time` | TEXT | 故障发生时间。 |
| `point_key` | TEXT | 故障点位或故障码。 |
| `fault_message` | TEXT | 故障描述。 |
| `raw_value` | INTEGER DEFAULT 0 | 原始值。 |

### chargerecords.db / charge_trade_info

水位：`created_at + id`

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | 交易记录自增 ID。 |
| `gun_no` | INTEGER | 枪号。 |
| `pre_trade_no` | TEXT | 平台交易流水号。 |
| `trade_no` | TEXT | 设备交易流水号。 |
| `vin_code` | TEXT | VIN。 |
| `time_div_type` | INTEGER | 计费时段类型。 |
| `start_type` | INTEGER | 启动方式。 |
| `charge_start_time` | INTEGER | 充电开始时间。 |
| `charge_end_time` | INTEGER | 充电结束时间。 |
| `start_soc` | REAL | 开始 SOC。 |
| `end_soc` | REAL | 结束 SOC。 |
| `reason` | INTEGER | 停止原因。 |
| `fee_model_id` | TEXT | 计费模型编号。 |
| `sum_start` | REAL | 起始总电量。 |
| `sum_end` | REAL | 结束总电量。 |
| `total_elect` | REAL | 总电量。 |
| `total_power_cost` | REAL | 总电费。 |
| `total_serv_cost` | REAL | 总服务费。 |
| `total_cost` | REAL | 总金额。 |
| `time_num` | INTEGER | 时段数。 |
| `part_elect_text` | TEXT | 分时电量，逗号分隔。 |
| `charge_fee_text` | TEXT | 分时电费，逗号分隔。 |
| `service_fee_text` | TEXT | 分时服务费，逗号分隔。 |
| `start_point` | INTEGER | 起始时段点。 |
| `cross_points` | INTEGER | 跨越点数。 |
| `points_elect_text` | TEXT | 跨越点电量，逗号分隔。 |
| `card_number` | TEXT | 卡号。 |
| `platform_confirm_flag` | INTEGER | 平台确认标志。 |
| `created_at` | DATETIME | 入库时间。 |

### feemodel.db / tbFeeModel

水位：`timeStamp + feeModelId`

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | 计费模型自增 ID。 |
| `feeModelId` | TEXT UNIQUE NOT NULL | 计费模型编号。 |
| `timeNum` | INTEGER NOT NULL | 时段数量。 |
| `timeSeg` | TEXT NOT NULL | 时段起点，分号分隔。 |
| `segFlag` | TEXT | 时段标志，分号分隔。 |
| `chargeFee` | TEXT NOT NULL | 电费数组，分号分隔。 |
| `serviceFee` | TEXT NOT NULL | 服务费数组，分号分隔。 |
| `timeStamp` | DATETIME NOT NULL | 模型更新时间。 |

### telemetry.db / meter_minute_points

水位：`created_at + id`

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | 电表分钟点自增 ID。 |
| `gun_no` | INTEGER NOT NULL | 枪号。 |
| `total_energy` | REAL | 正向总电量。 |
| `reverse_energy` | REAL | 反向总电量。 |
| `voltage` | REAL | 电压。 |
| `current` | REAL | 电流。 |
| `created_at` | TEXT NOT NULL | 分钟点时间。 |

### telemetry.db / bms_minute_points

水位：`created_at + id`

| 列 | 类型 | 说明 |
| --- | --- | --- |
| `id` | INTEGER PRIMARY KEY AUTOINCREMENT | BMS 分钟点自增 ID。 |
| `gun_no` | INTEGER NOT NULL | 枪号。 |
| `bms_req_voltage` | REAL | BMS 请求电压。 |
| `bms_req_current` | REAL | BMS 请求电流。 |
| `bms_measured_voltage` | REAL | BMS 测量电压。 |
| `bms_measured_current` | REAL | BMS 测量电流。 |
| `output_voltage` | REAL | 模块输出电压。 |
| `output_current` | REAL | 模块输出电流。 |
| `created_at` | TEXT NOT NULL | 分钟点时间。 |

## 兼容和注意事项

- 首次没有 `upload_state.json` 时，从空水位开始导出已有历史数据。
- 上传失败不会推进水位；包留在 `pending/`，下一轮优先重传。
- 上传成功后，从包内 `manifest.json` 推进水位。
- 如果源 DB 缺失且允许跳过，会记录到 `missing_files`。
- 如果源 DB 存在但查询失败，本轮打包失败，不推进水位。
