# tcu_uploader

`tcu_uploader` 是 TCU 运行数据上送进程，定期将 data 目录内的运行数据库增量导出为 `delta.db`，打包后通过 HTTP multipart/form-data 上传到平台。

## 行为

- 设备编号来自平台 `setConfig` MQTT 消息，优先级为 `cdzNo > cdzId > fallback_device_id`。
- 订阅 topic：
  - `tcu/plat/+/event`
  - `tcu/plat/+/setConfig`
- 只处理 payload 中 `type=setConfig` 或 `cmd=setConfig` 的消息。
- 打包文件名为 `delta_<device_id>_<created_ts>.tar.gz`。
- 上传成功后删除本地包，并推进 `/mnt/nandflash/data/upload/upload_state.json` 水位。
- 上传失败保留在 `work_dir/pending/`。
- `pending/` 超过 `max_pending_mb` 后删除旧包，优先保留新包。

## 默认打包内容

```text
delta.db
manifest.json
```

`delta.db` 中保留源表名和字段结构。水位只在平台返回 `code=0` 后推进；失败包会留在 `pending/`，下轮优先重传。
详细数据结构见 [DELTA_DB_FORMAT.md](DELTA_DB_FORMAT.md)。

默认增量表与水位：

```text
tcu.db: operation_logs(timestamp,id), performance_logs(timestamp,id)
error.db: fault_records(occur_time,id)
chargerecords.db: charge_trade_info(created_at,id)
feemodel.db: tbFeeModel(timeStamp,feeModelId)
telemetry.db: meter_minute_points(created_at,id), bms_minute_points(created_at,id)
```

水位使用时间列 + 辅助键的复合条件，避免数据库清理或轮转后自增 id 重置导致漏传。

## 构建

```sh
ssh -p 2222 root@localhost
cd /对应工作目录/core/uploader
build imx6ul Makefile.cross
```

`Makefile.cross` 直接链接 `libcurl`，默认使用：

```text
../../extraLib/curl/install/curl/usr/include
../../extraLib/curl/install/curl/usr/lib
../../extraLib/openSSL/install-arm
../../extraLib/imx6ul
```

目标板运行时相关动态库放在 `/usr/lib`。

## 单次运行

```sh
/usr/app/tcu/tcu_uploader /usr/app/config/tcu_uploader.ini --once
```

## 配置

默认配置文件：

```text
/usr/app/config/tcu_uploader.ini
```

核心字段：

```ini
[Upload]
upload_url=http://43.142.89.201:8080/api/device/log/upload
token=test-token-001
interval_minutes=1440
startup_delay_seconds=120

[Paths]
data_dir=/mnt/nandflash/data
work_dir=/mnt/nandflash/data/upload
include_files=tcu.db,error.db,feemodel.db,chargerecords.db,telemetry.db
```
