# TCU Log Upload Server

这是一个平台侧日志上传测试服务，用于接收设备通过 HTTP multipart 上传的 `delta_*.tar.gz` 增量日志包。

## 目录结构

```text
upload2web/server/
  docker-compose.yml
  app/
    Dockerfile
    requirements.txt
    main.py
  data/
    uploads/
```

## 启动

```sh
cd upload2web/server
docker compose up -d --build
```

检查服务：

```sh
curl http://127.0.0.1:8080/health
docker logs -f log-upload
```

## 上传接口

```text
POST /api/device/log/upload
Authorization: Bearer test-token-001
Content-Type: multipart/form-data
```

字段：

```text
file        必填，.tar.gz 文件
device_id   必填，设备编号
timestamp   可选，设备打包时间
checksum    可选，文件 sha256
```

## 本机模拟测试

```sh
tar czf /tmp/data_test.tar.gz /etc/hosts

curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/tmp/data_test.tar.gz" \
  -F "device_id=tcu-test" \
  http://127.0.0.1:8080/api/device/log/upload
```

## 设备侧测试

```sh
curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/usr/app/upload/data_20260513153000.tar.gz" \
  -F "device_id=tcu001" \
  http://平台IP:8080/api/device/log/upload
```

带 sha256 校验：

```sh
sha256sum /usr/app/upload/data_20260513153000.tar.gz

curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/usr/app/upload/data_20260513153000.tar.gz" \
  -F "device_id=tcu001" \
  -F "checksum=<sha256值>" \
  http://平台IP:8080/api/device/log/upload
```

## 配置

在 `docker-compose.yml` 中修改：

```yaml
UPLOAD_TOKEN: "test-token-001"
UPLOAD_ROOT: "/data/uploads"
MAX_UPLOAD_MB: "100"
```

上传文件保存到：

```text
data/uploads/<device_id>/<yyyymm>/<dd>/
```

服务器落盘结构详见：

```text
SERVER_STORAGE_STRUCTURE.md
```

## 资产导入模板

资产 CSV 模板使用桩级粒度，一行一个桩，不配置枪级字段：

```text
assets_template.csv
```
