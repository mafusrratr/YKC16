# curl/libcurl 交叉编译说明

本目录按 `extraLib/mosquitto` 的方式组织：先准备 curl 源码包和源码目录，再通过顶层 `Makefile` 完成交叉编译、安装和运行时文件收集。

## 1. 准备源码包

放置到 `extraLib/curl/` 目录：

```text
curl-7.88.1.tar.xz
```

解压后源码目录为：

```text
curl-7.88.1/
```

如果需要重新下载：

```bash
cd /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/curl
curl -L https://curl.se/download/curl-7.88.1.tar.xz -o curl-7.88.1.tar.xz
tar xf curl-7.88.1.tar.xz
```

## 2. 前置条件

先准备 OpenSSL 交叉编译产物。默认使用：

```text
extraLib/openSSL/install-arm
```

如果实际使用 `jzq` 或 `nuc980`，编译时通过 `OPENSSL_PREFIX` 指定：

```text
extraLib/openSSL/install-jzq
extraLib/openSSL/install-nuc980
```

## 3. 执行编译

在远端交叉编译环境中执行。

推荐优先使用仓库远端构建命令，让构建脚本注入 `CROSS_COMPILE` 等临时环境变量：

```bash
cd /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/curl
build imx6ul Makefile.cross
```

切换目标平台：

```bash
build jzq Makefile.cross
build nuc980 Makefile.cross
```

查看实际使用的环境和路径：

```bash
make -f Makefile.cross info
```

### imx6ul 示例

如果不走 `build` 命令，也可以手动指定环境变量：

```bash
cd /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/curl

make \
  CROSS_COMPILE=arm-linux-gnueabihf- \
  PLATFORM=imx6ul
```

如果工具链是完整路径：

```bash
make \
  CROSS_COMPILE=/opt/gcc-linaro-4.9-2016.02-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf- \
  PLATFORM=imx6ul
```

### jzq 示例

```bash
make \
  CROSS_COMPILE=arm-buildroot-linux-gnueabihf- \
  PLATFORM=jzq
```

### nuc980 示例

```bash
make \
  CROSS_COMPILE=arm-linux-gnueabi- \
  PLATFORM=nuc980
```

## 4. 产物

编译完成后输出：

```text
extraLib/curl/install/curl/usr/bin/curl
extraLib/curl/install/curl/usr/include/curl/*.h
extraLib/curl/install/curl/usr/lib/libcurl.so*
```

运行时部署文件会收集到：

```text
extraLib/curl/install/runtime/
  curl/bin/curl
  curl/lib/libcurl.so*
  openSSL/lib/libssl.so*
  openSSL/lib/libcrypto.so*
```

注意：构建时使用 `--prefix=/usr` 并通过 `DESTDIR` 安装到本地 `install/curl/`，这样生成的 `curl` 目标板 RPATH 指向 `/usr/lib`，不会再写入编译机绝对路径。

## 5. 部署到目标机

把 `install/runtime/` 下的 `curl/` 和 `openSSL/` 拷贝到目标机：

```text
/usr/app/extraLib/curl/
/usr/app/extraLib/openSSL/
```

目标机验证：

```bash
LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib \
/usr/app/extraLib/curl/bin/curl --version
```

应看到：

```text
Protocols: ... http https ...
Features: ... SSL ...
```

测试上传：

```bash
LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib \
/usr/app/extraLib/curl/bin/curl -v \
  -H "Authorization: Bearer test-token-001" \
  -F "file=@/usr/app/upload/data_20260513153000.tar.gz" \
  -F "device_id=tcu001" \
  http://平台IP:8080/api/device/log/upload
```

## 6. 板端版本不匹配问题

如果目标机上执行 `curl --version` 出现类似结果：

```text
curl 7.88.1 (...) libcurl/7.57.0
WARNING: curl and libcurl versions do not match.
```

说明 `curl` 可执行文件已经是新版本，但运行时动态链接到了系统旧的 `libcurl.so.4`。这种情况下可能继续报：

```text
curl: symbol lookup error: curl: undefined symbol: curl_url
```

先确认目标机上实际库文件内容：

```sh
find /lib /usr/lib /usr/local/lib /usr/app -name "libcurl.so*" -ls 2>/dev/null
strings /usr/lib/libcurl.so.4.8.0 | grep "libcurl/"
```

临时验证方式：

```sh
LD_LIBRARY_PATH=/usr/lib /usr/bin/curl --version
```

如果这样能看到：

```text
curl 7.88.1 (...) libcurl/7.88.1 OpenSSL/1.1.1w
```

说明库本身可用，只是默认动态库搜索路径没有优先加载新库。目标系统如果没有 `ldconfig`，可以用 wrapper 固化环境变量：

```sh
mv /usr/bin/curl /usr/bin/curl.real

cat > /usr/bin/curl <<'EOF'
#!/bin/sh
export LD_LIBRARY_PATH=/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
exec /usr/bin/curl.real "$@"
EOF

chmod +x /usr/bin/curl
curl --version
```

如果 OpenSSL 部署在独立目录，例如 `/usr/app/extraLib/openSSL/lib`，则 wrapper 改为：

```sh
cat > /usr/bin/curl <<'EOF'
#!/bin/sh
export LD_LIBRARY_PATH=/usr/lib:/usr/app/extraLib/openSSL/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
exec /usr/bin/curl.real "$@"
EOF
```

不建议在目标机上无确认地删除或覆盖系统库。若必须替换 `/usr/lib/libcurl.so*`，需要确认软链接关系如下：

```text
libcurl.so -> libcurl.so.4.8.0
libcurl.so.4 -> libcurl.so.4.8.0
libcurl.so.4.8.0
```

## 7. 在进程中调用 libcurl

如果不想通过外部 `curl` 命令上传，也可以在业务进程中直接链接 `libcurl`。

编译时需要使用本次安装目录里的头文件和库：

```makefile
# BY ZF
CURL_PREFIX := /usr/app/extraLib/curl
OPENSSL_PREFIX := /usr/app/extraLib/openSSL

CFLAGS += -I$(CURL_PREFIX)/include
LDFLAGS += -L$(CURL_PREFIX)/lib -L$(OPENSSL_PREFIX)/lib
LDLIBS += -lcurl -lssl -lcrypto -lpthread
```

目标机运行时仍需要保证动态库能被找到：

```sh
export LD_LIBRARY_PATH=/usr/app/extraLib/curl/lib:/usr/app/extraLib/openSSL/lib:$LD_LIBRARY_PATH
```

在程序里用 multipart/form-data 上传文件的最小示例：

```c
// BY ZF
#include <stdio.h>
#include <curl/curl.h>

int upload_log_file(const char *url,
                    const char *token,
                    const char *device_id,
                    const char *file_path)
{
    CURL *curl = NULL;
    CURLcode ret;
    struct curl_slist *headers = NULL;
    curl_mime *form = NULL;
    curl_mimepart *part = NULL;
    char auth_header[160];
    int rc = -1;

    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", token);

    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }

    headers = curl_slist_append(headers, auth_header);

    form = curl_mime_init(curl);

    part = curl_mime_addpart(form);
    curl_mime_name(part, "file");
    curl_mime_filedata(part, file_path);

    part = curl_mime_addpart(form);
    curl_mime_name(part, "device_id");
    curl_mime_data(part, device_id, CURL_ZERO_TERMINATED);

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    ret = curl_easy_perform(curl);
    if (ret == CURLE_OK) {
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        rc = (http_code >= 200 && http_code < 300) ? 0 : -2;
    } else {
        fprintf(stderr, "curl upload failed: %s\n", curl_easy_strerror(ret));
        rc = -3;
    }

    curl_mime_free(form);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return rc;
}

int main(void)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);

    int rc = upload_log_file(
        "http://43.142.89.201:8080/api/device/log/upload",
        "test-token-001",
        "tcu001",
        "/usr/app/upload/data_20260514160000.tar.gz");

    curl_global_cleanup();
    return rc == 0 ? 0 : 1;
}
```

接入业务进程时建议：

- `curl_global_init()` 在进程启动时调用一次，退出前调用 `curl_global_cleanup()`。
- 每次上传创建自己的 `CURL *` 和 `curl_mime *`，上传完成后释放。
- 多线程上传时不要多个线程共用同一个 `CURL *`。
- 设置 `CURLOPT_NOSIGNAL=1L`，避免多线程或超时场景下信号行为影响进程。
- 设置连接超时和总超时，避免网络异常时卡住主流程。

## 8. 说明

- 当前构建启用 `file`、`ftp`、`ftps`、`http`、`https`。
- 已禁用 SMTP、MQTT、LDAP、HTTP2、brotli、zstd、libpsl 等非必要依赖。
- 不建议替换目标机 `/usr/bin/curl` 或 `/usr/lib/libcurl.so*`。
- 如果切换平台或工具链，先执行 `make distclean`，避免复用旧 ABI 的配置结果。
