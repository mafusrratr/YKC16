# 平台侧上传文件保存结构

本文档记录 `tcu-log-upload` 平台服务在服务器上的部署目录、容器挂载关系和上传文件落盘规则。

## 部署根目录

服务器推荐部署路径：

```text
/opt/tcu-log-upload/
  docker-compose.yml
  app/
    Dockerfile
    requirements.txt
    main.py
  data/
    uploads/
```

Docker Compose 中的 volume 挂载关系：

```text
宿主机: /opt/tcu-log-upload/data/uploads
容器内: /data/uploads
```

服务配置：

```yaml
UPLOAD_ROOT: "/data/uploads"
```

因此所有上传文件最终保存在宿主机：

```text
/opt/tcu-log-upload/data/uploads/
```

## 接口

设备上传接口：

```text
POST http://<server-ip>:8080/api/device/log/upload
```

认证头：

```text
Authorization: Bearer <UPLOAD_TOKEN>
```

表单字段：

```text
file        必填，tar.gz 包
device_id   必填，设备编号
timestamp   可选，设备侧打包时间，格式 YYYYMMDDHHMMSS
checksum    可选，文件 sha256
```

## 落盘目录规则

平台按设备和服务器接收日期分目录保存：

```text
/opt/tcu-log-upload/data/uploads/<device_id>/<yyyymm>/<dd>/<filename>
```

示例：

```text
/opt/tcu-log-upload/data/uploads/46010003275590/202605/28/delta_46010003275590_20260528160000.tar.gz
```

注意：`yyyymm/dd` 使用平台服务器接收时的本地日期，不使用设备包内时间。

## 当前增量包命名

设备侧当前上传增量包：

```text
delta_<device_id>_<created_ts>.tar.gz
```

示例：

```text
delta_46010003275590_20260528160000.tar.gz
```

包内结构：

```text
delta.db
manifest.json
```

`delta.db` 保存本轮新增数据，表名和字段结构与设备侧源表一致。

`manifest.json` 保存本包的设备号、创建时间、表清单、水位范围和行数，用于平台后续导入或排查。

## 历史全量包命名

早期全量包可能仍存在：

```text
data_<device_id>.tar.gz
data_<device_id>_<timestamp>.tar.gz
data_<device_id>_<start_ts>_<end_ts>.tar.gz
```

这些属于历史测试或旧 uploader 版本产物。新版本以 `delta_` 前缀区分。

## 同名文件处理

平台收到文件后先尝试按原始文件名保存：

```text
<filename>
```

如果同目录下已经存在同名文件，平台不会覆盖旧文件，而是追加时间戳和序号：

```text
<stem>_<timestamp>.tar.gz
<stem>_<timestamp>_001.tar.gz
<stem>_<timestamp>_002.tar.gz
```

其中 `timestamp` 优先使用设备表单字段 `timestamp`，如果没有则使用平台接收时间。

## 常用检查命令

检查服务：

```sh
cd /opt/tcu-log-upload
docker ps
curl http://127.0.0.1:8080/health
docker logs --tail=50 log-upload
```

查看某台设备当天上传文件：

```sh
ls -ltr /opt/tcu-log-upload/data/uploads/<device_id>/<yyyymm>/<dd>/
```

查看包内容：

```sh
tar tzf /opt/tcu-log-upload/data/uploads/<device_id>/<yyyymm>/<dd>/<package>.tar.gz
```

解出并检查 manifest：

```sh
tar xzf /opt/tcu-log-upload/data/uploads/<device_id>/<yyyymm>/<dd>/<package>.tar.gz -C /tmp
cat /tmp/manifest.json
```

检查 delta.db：

```sh
sqlite3 /tmp/delta.db ".tables"
sqlite3 /tmp/delta.db "SELECT COUNT(*) FROM operation_logs;"
```

