# TCU 日志上传平台对接记录

本文档记录平台侧部署参数、设备侧 `curl` 请求格式，以及 2026-05-27 板端实测结果。

## 平台侧设置

部署目录建议：

```text
/opt/tcu-log-upload
```

服务由 `docker compose` 启动：

```yaml
services:
  log-upload:
    build: ./app
    container_name: log-upload
    ports:
      - "8080:8080"
    environment:
      UPLOAD_TOKEN: "test-token-001"
      UPLOAD_ROOT: "/data/uploads"
      MAX_UPLOAD_MB: "100"
    volumes:
      - ./data/uploads:/data/uploads
    restart: unless-stopped
```

平台服务端口：

```text
TCP 8080
```

云服务器安全组/防火墙需要允许设备访问 `TCP 8080` 入方向。

容器内保存目录：

```text
/data/uploads
```

宿主机 volume 目录：

```text
/opt/tcu-log-upload/data/uploads
```

文件落盘规则：

```text
data/uploads/<device_id>/<yyyymm>/<dd>/<原文件名>
```

示例：

```text
data/uploads/tcu001/202605/27/data_test.tar.gz
```

## 平台接口

健康检查：

```text
GET /health
```

上传接口：

```text
POST /api/device/log/upload
```

完整测试地址：

```text
http://43.142.89.201:8080/api/device/log/upload
```

鉴权方式：

```text
Authorization: Bearer test-token-001
```

请求类型：

```text
multipart/form-data
```

表单字段：

```text
file        必填，上传的 .tar.gz 文件
device_id   必填，设备编号，允许 A-Z/a-z/0-9/_/-
timestamp   可选，设备打包时间
checksum    可选，文件 sha256
```

平台限制：

```text
文件名必须匹配 *.tar.gz
默认最大 100MB
Token 错误返回 401
checksum 不匹配返回 4005
重复文件不会覆盖，返回 duplicated=true
```

## 设备侧 curl 状态

2026-05-27 板端已确认：

```text
curl 7.88.1 (arm-unknown-linux-gnueabihf) libcurl/7.88.1 OpenSSL/1.1.1w
Protocols: file ftp ftps http https
```

当前 `curl` 二进制已修正 RPATH：

```text
RPATH=/usr/lib
```

因此设备侧可直接调用：

```sh
curl --version
```

不需要再额外写 `LD_LIBRARY_PATH`。

如需排查实际动态库命中路径：

```sh
LD_DEBUG=libs curl --version 2>&1 | grep -E "libcurl|search|trying|calling"
```

## 平台连通测试

健康检查：

```sh
curl -v http://43.142.89.201:8080/health
```

预期返回：

```json
{"code":0,"message":"ok"}
```

## 上传测试命令

先创建测试包：

```sh
mkdir -p /usr/app/upload
tar czf /usr/app/upload/data_test.tar.gz /etc/hosts
```

上传：

```sh
curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/usr/app/upload/data_test.tar.gz" \
  -F "device_id=tcu001" \
  http://43.142.89.201:8080/api/device/log/upload
```

带 sha256 校验：

```sh
sha256sum /usr/app/upload/data_test.tar.gz

curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/usr/app/upload/data_test.tar.gz" \
  -F "device_id=tcu001" \
  -F "checksum=<sha256值>" \
  http://43.142.89.201:8080/api/device/log/upload
```

## 2026-05-27 板端实测结果

请求关键信息：

```text
POST /api/device/log/upload HTTP/1.1
Host: 43.142.89.201:8080
User-Agent: curl/7.88.1
Authorization: Bearer test-token-001
Content-Type: multipart/form-data
```

平台响应：

```json
{
  "code": 0,
  "message": "ok",
  "file_id": "tcu001/202605/27/data_test.tar.gz",
  "duplicated": false,
  "size": 145,
  "sha256": "dcbb584acad1bd4df91210960449751706de999f22e73d5353c077e6f4bee373",
  "timestamp": null
}
```

结论：

```text
设备 -> 平台 HTTP 上传链路已通过。
平台 Token 鉴权正常。
multipart/form-data 解析正常。
平台落盘路径正常。
```

## 后续正式使用建议

设备侧文件名建议使用：

```text
data_YYYYMMDDHHMMSS.tar.gz
```

正式命令示例：

```sh
curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/usr/app/upload/data_20260527143000.tar.gz" \
  -F "device_id=tcu001" \
  http://43.142.89.201:8080/api/device/log/upload
```

正式部署前建议修改：

```text
UPLOAD_TOKEN
device_id
平台域名或 IP
是否启用 HTTPS
是否上传 checksum
上传成功后本地包保留策略
```
