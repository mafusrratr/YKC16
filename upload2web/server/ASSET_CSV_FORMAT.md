# 平台资产管理 CSV 格式

## 粒度约定

资产管理 CSV 使用桩级别粒度：

```text
一行 = 一个充电桩 / 一个 pile_code
```

`pile_code` 需要和设备上传接口中的 `device_id` 保持一致。资产模板不按枪口拆分行，不在资产 CSV 中维护 `gun_no`、`gun_name`、`gun_type`、`gun_id`、`connector_id` 这类枪级资产字段。

枪数量由平台根据后续上送数据推断，例如：

- `delta.db` 中 `charge_trade_info.gun_no`
- `delta.db` 中 `meter_minute_points.gun_no`
- `delta.db` 中 `bms_minute_points.gun_no`
- 运行日志、平台配置消息等包含的枪相关信息

## 推荐字段

```csv
station_code,station_name,station_address,pile_code,pile_name,pile_model,enabled
```

字段说明：

| 字段 | 必填 | 说明 |
| --- | --- | --- |
| `station_code` | 是 | 站点编号。 |
| `station_name` | 是 | 站点名称。 |
| `station_address` | 否 | 站点地址。 |
| `pile_code` | 是 | 桩编号，需要和设备上传使用的 `device_id` 一致。 |
| `pile_name` | 否 | 桩显示名称。 |
| `pile_model` | 否 | 设备型号。 |
| `enabled` | 是 | 是否启用，`1` 表示启用，`0` 表示停用。 |

## 示例

```csv
station_code,station_name,station_address,pile_code,pile_name,pile_model,enabled
ST001,测试充电站,国电南瑞科技园,46010003275590,1号桩,DC-120KW,1
```

## 不推荐字段

资产 CSV 不建议包含：

```text
gun_no
gun_id
connector_id
gun_count
gun_name
gun_type
```

原因：

- 当前上传链路是桩级别，文件名为 `delta_<device_id>_<created_ts>.tar.gz`。
- 枪级信息会自然出现在业务明细表中，平台从明细推断更可靠。
- 枪数量可能受配置、协议、现场改造影响，资产 CSV 中静态维护容易和运行数据不一致。

如平台必须展示枪数量，应将其作为平台侧根据数据计算出的派生字段，而不是 CSV 导入字段。
