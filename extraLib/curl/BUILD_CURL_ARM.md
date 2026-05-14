# curl/libcurl 交叉编译与移植说明

## 1. 目标

在 `extraLib/curl` 下交叉编译一套独立的 `curl + libcurl`，运行时不依赖目标设备系统自带的 `/usr/bin/curl` 和 `/usr/lib/libcurl.so`。

目标设备上建议部署为：

```text
/usr/app/extraLib/curl/bin/curl
/usr/app/extraLib/curl/lib/libcurl.so*
/usr/app/extraLib/openSSL/lib/libssl.so*
/usr/app/extraLib/openSSL/lib/libcrypto.so*
```

运行时固定这样调用：

```sh
LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib \
/usr/app/extraLib/curl/bin/curl --version
```

正常应看到：

```text
Protocols: ... http https ...
Features: ... SSL ...
```

## 2. 前置条件

先完成 OpenSSL 交叉编译。当前脚本默认查找：

```text
imx6ul -> extraLib/openSSL/install-arm
nuc980 -> extraLib/openSSL/install-nuc980
jzq    -> extraLib/openSSL/install-jzq
```

如果实际 OpenSSL 安装目录不同，构建时通过 `OPENSSL_DIR` 指定。

## 3. 推荐构建命令

以 `imx6ul` 为例：

```sh
cd /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/curl

export CROSS_COMPILE=/opt/gcc-linaro-4.9-2016.02-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-
export HOST=arm-linux-gnueabihf
export OPENSSL_DIR=/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/install-arm

chmod +x scripts/build_curl.sh scripts/package_runtime.sh
scripts/build_curl.sh imx6ul
scripts/package_runtime.sh imx6ul
```

如果工具链已经在 `PATH` 中，也可以只设置：

```sh
export CROSS_COMPILE=arm-linux-gnueabihf-
scripts/build_curl.sh imx6ul
```

## 4. 构建产物

编译安装目录：

```text
extraLib/curl/install-imx6ul/
  bin/curl
  include/curl/*.h
  lib/libcurl.so*
```

运行时打包目录：

```text
extraLib/curl/runtime-imx6ul/
  curl/bin/curl
  curl/lib/libcurl.so*
  openSSL/lib/libssl.so*
  openSSL/lib/libcrypto.so*
```

把 `runtime-imx6ul/` 下的 `curl/` 和 `openSSL/` 拷贝到目标机 `/usr/app/extraLib/`。

## 5. 目标机验证

```sh
LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib \
/usr/app/extraLib/curl/bin/curl --version
```

测试 HTTP：

```sh
LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib \
/usr/app/extraLib/curl/bin/curl -I http://平台IP:8080/health
```

测试上传：

```sh
LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib \
/usr/app/extraLib/curl/bin/curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/usr/app/upload/data_20260513153000.tar.gz" \
  -F "device_id=tcu001" \
  http://平台IP:8080/api/device/log/upload
```

## 6. HTTPS 说明

脚本使用 `--with-openssl` 编译，支持 HTTPS。测试阶段如果平台只开放 HTTP，可以先走 `http://平台IP:8080`。

正式使用 HTTPS 时建议部署 CA 证书，并通过 curl 指定：

```sh
--cacert /usr/app/certs/cacert.pem
```

临时调试可以使用 `-k` 跳过证书校验，但正式环境不建议使用。

## 7. 说明

- 默认 curl 版本为 `7.88.1`，可通过 `CURL_VERSION=版本号` 覆盖。
- 默认只保留 HTTP/HTTPS 需要的能力，禁用了 FTP、SMTP、MQTT、LDAP、HTTP2、brotli、zstd 等非必要依赖。
- 不建议替换目标机 `/usr/bin/curl` 或 `/usr/lib/libcurl.so*`，避免影响系统组件。
