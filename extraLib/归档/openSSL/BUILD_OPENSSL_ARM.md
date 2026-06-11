# OpenSSL 下载与 ARM 交叉编译（i.MX6UL 示例）

## 1. 目录约定

```bash
cd /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib
mkdir -p openSSL/install-arm
```

- 源码目录：`extraLib/openSSL/openssl-1.1.1w`
- 安装目录：`extraLib/openSSL/install-arm`

## 2. 下载源码（推荐 1.1.1w，老工具链兼容更稳）

```bash
cd /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL
wget https://www.openssl.org/source/old/1.1.1/openssl-1.1.1w.tar.gz
tar xf openssl-1.1.1w.tar.gz
```

## 3. 配置交叉编译环境

```bash
export CROSS_COMPILE=arm-linux-gnueabihf-
export CC=${CROSS_COMPILE}gcc
export AR=${CROSS_COMPILE}ar
export RANLIB=${CROSS_COMPILE}ranlib
export LD=${CROSS_COMPILE}ld
export STRIP=${CROSS_COMPILE}strip
```

> 如果你实际工具链前缀不同（例如 `/opt/toolchains/imx6ul/.../bin/arm-linux-gnueabihf-`），把 `CROSS_COMPILE` 改成你的真实前缀。

建议先确认工具链可用：

```bash
which ${CC}
${CC} -dumpmachine
```

期望输出包含 `arm-linux-gnueabihf`。

## 4. 配置 + 编译 + 安装（动态库）

```bash
cd /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/openssl-1.1.1w
make clean >/dev/null 2>&1 || true

./Configure linux-armv4 \
    --cross-compile-prefix=arm-linux-gnueabihf- \
    shared \
    no-tests \
    no-async \
    --prefix=/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/install-arm \
    --openssldir=/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/install-arm/ssl

make clean >/dev/null 2>&1 || true
make -j8 CC=${CC} AR=${AR} RANLIB=${RANLIB} LD=${LD}
make install_sw
```

如果遇到 `unsupported ARM architecture`，基本是用了主机 `gcc`（x86）而不是交叉编译器；请检查：

```bash
grep '^CC=' Makefile
```

若不是 ARM 交叉编译器，删掉旧配置后重来：

```bash
make distclean >/dev/null 2>&1 || true
./Configure linux-armv4 \
    --cross-compile-prefix=arm-linux-gnueabihf- \
    shared no-tests no-async \
    --prefix=/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/install-arm \
    --openssldir=/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/install-arm/ssl
make -j8 CC=${CC} AR=${AR} RANLIB=${RANLIB} LD=${LD}
```

## 5. 结果检查

```bash
file /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/install-arm/lib/libcrypto.so.1.1
file /Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/extraLib/openSSL/install-arm/lib/libssl.so.1.1
```

应看到 `ARM` 目标架构信息。

## 6. 部署到目标机

把以下内容拷贝到目标机（例如 `/usr/app/lib`）：

- `install-arm/lib/libcrypto.so.1.1`
- `install-arm/lib/libssl.so.1.1`
- 对应软链接 `libcrypto.so` / `libssl.so`（建议一并带上）

运行前设置：

```bash
export LD_LIBRARY_PATH=/usr/app/lib:$LD_LIBRARY_PATH
```

## 7. 在项目中链接

- 头文件：`-I.../extraLib/openSSL/install-arm/include`
- 库路径：`-L.../extraLib/openSSL/install-arm/lib -lssl -lcrypto`

## 8. 说明

- 本步骤先解决“动态库移植可用”。
- 国密接口（SM2/SM4）与 RSA 后续通过 `crypto_adapter` 统一接入到 `Zhongshihua2.0`。
