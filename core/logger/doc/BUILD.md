# Logger 模块编译说明

## 快速开始

```bash
# 交叉编译（用于目标设备）
make -f Makefile.cross clean
make -f Makefile.cross

# 本地编译（仅用于语法检查）
make clean
make
```

## 交叉编译配置

### 基本用法

```bash
# 设置交叉编译工具链前缀
export CROSS_COMPILE=arm-linux-gnueabihf-

# 编译
cd core/logger
make -f Makefile.cross clean
make -f Makefile.cross
```

### 环境变量配置

Makefile.cross 支持以下环境变量：

| 变量 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `CROSS_COMPILE` | 交叉编译工具链前缀 | 无（必须设置） | `arm-linux-gnueabihf-` |
| `EXTRA_LIB_PATH` | 外部库路径 | `../../extraLib` | `/path/to/extraLib` |
| `PLATFORM` | 平台名称（jzq/nuc980/imx6ul） | 空 | `jzq` |
| `TOOLCHAIN_LIB_PATH` | 工具链库路径 | 自动检测 | `/opt/toolchain/lib` |
| `STATIC_STDCPP` | 是否静态链接 libstdc++ | `0` | `1`（解决版本不匹配） |

### 解决运行时库问题

#### 问题1: 找不到 libevsshm.so

**错误信息：**
```
error while loading shared libraries: libevsshm.so: cannot open shared object file
```

**解决方案：**

1. **使用平台特定库路径**（推荐）：
```bash
export PLATFORM=jzq  # 或 nuc980, imx6ul
make -f Makefile.cross
```

2. **确保库文件在目标设备上**：
```bash
# 将库文件复制到目标设备
scp extraLib/jzq/*.so root@target:/usr/lib/
# 或复制到程序目录
scp extraLib/jzq/*.so root@target:/usr/app/tcu/
```

3. **设置 LD_LIBRARY_PATH**（临时方案）：
```bash
# 在目标设备上运行前设置
export LD_LIBRARY_PATH=/usr/app/tcu:/usr/lib:$LD_LIBRARY_PATH
./tcu_logger
```

#### 问题2: libstdc++ 版本不匹配

**错误信息：**
```
./tcu_logger: /usr/lib/libstdc++.so.6: version `GLIBCXX_3.4.18' not found
./tcu_logger: /usr/lib/libstdc++.so.6: version `GLIBCXX_3.4.19' not found
```

**解决方案：**

1. **静态链接 libstdc++**（推荐，但会增加文件大小）：
```bash
export STATIC_STDCPP=1
make -f Makefile.cross clean
make -f Makefile.cross
```

2. **使用工具链的 libstdc++**：
```bash
# 指定工具链库路径
export TOOLCHAIN_LIB_PATH=/opt/toolchain/arm-linux-gnueabihf/lib
make -f Makefile.cross
# 然后将工具链的 libstdc++.so 复制到目标设备
scp /opt/toolchain/arm-linux-gnueabihf/lib/libstdc++.so.* root@target:/usr/lib/
```

3. **在目标设备上升级 libstdc++**（需要 root 权限）：
```bash
# 从工具链复制到目标设备
scp /opt/toolchain/arm-linux-gnueabihf/lib/libstdc++.so.6.* root@target:/usr/lib/
# 在目标设备上创建软链接
ssh root@target "cd /usr/lib && ln -sf libstdc++.so.6.* libstdc++.so.6"
```

#### 问题3: 链接时缺少 dlopen/dlsym

**错误信息：**
```
undefined reference to `dlsym'
undefined reference to `dlopen'
```

**解决方案：**

已自动添加 `-ldl` 链接选项，无需手动配置。

### 完整编译示例

```bash
# 示例1: JZQ 平台，静态链接 libstdc++
export CROSS_COMPILE=arm-none-linux-gnueabi-
export PLATFORM=jzq
export STATIC_STDCPP=1
make -f Makefile.cross clean
make -f Makefile.cross

# 示例2: NUC980 平台，使用工具链库
export CROSS_COMPILE=arm-linux-gnueabihf-
export PLATFORM=nuc980
export TOOLCHAIN_LIB_PATH=/opt/gcc-linaro-4.9-2016.02-x86_64_arm-linux-gnueabihf/arm-linux-gnueabihf/lib
make -f Makefile.cross clean
make -f Makefile.cross
```

## 查看编译配置

```bash
make -f Makefile.cross info
```

输出示例：
```
Cross-compiler: arm-linux-gnueabihf-g++
Flags: -pipe -std=c++0x -O2 -D_REENTRANT -w
Includes: -I../base -I../interfaces -I../../libv2gshm/libcshm -Isql
Library directories: -L../../extraLib/jzq -L../../extraLib -L../../libv2gshm/cshm/bin/Release
Runtime paths (rpath): -Wl,-rpath,../../extraLib/jzq:../../extraLib:...
Libraries: -lpthread -lsqlite3 -levsshm -ldl -static-libstdc++
CROSS_COMPILE: arm-linux-gnueabihf-
EXTRA_LIB_PATH: /path/to/extraLib
PLATFORM: jzq
PLATFORM_LIB_PATH: /path/to/extraLib/jzq
SHM_LIB_PATH: ../../libv2gshm/cshm/bin/Release
TOOLCHAIN_LIB_PATH: /opt/toolchain/lib
STATIC_STDCPP: 1
```

## 依赖库说明

### 必需库

- **libpthread.so** - POSIX 线程库（系统自带）
- **libsqlite3.so** - SQLite 数据库（位于 `extraLib/` 或 `extraLib/{platform}/`）
- **libevsshm.so** - 共享内存库（位于 `libv2gshm/cshm/bin/Release/`）
- **libdl.so** - 动态链接库（系统自带，用于 sqlite3 的扩展加载）

### 库文件位置

```
extraLib/
├── libsqlite3.so          # 通用版本
├── libevsshm.so           # 通用版本
├── jzq/                   # JZQ 平台特定库
│   ├── libsqlite3.so
│   └── libevsshm.so
├── nuc980/                # NUC980 平台特定库
│   ├── libsqlite3.so
│   └── libevsshm.so
└── imx6ul/                # iMX6UL 平台特定库
    ├── libsqlite3.so
    └── libevsshm.so
```

## 部署到目标设备

### 方法1: 使用 make install（本地安装）

```bash
make -f Makefile.cross install
# 安装到 /usr/app/tcu/
```

### 方法2: 手动部署

```bash
# 1. 复制可执行文件
scp release/tcu_logger root@target:/usr/app/tcu/

# 2. 复制库文件（如果使用动态链接）
scp extraLib/jzq/*.so root@target:/usr/lib/
# 或
scp extraLib/jzq/*.so root@target:/usr/app/tcu/

# 3. 复制配置文件
scp ../../config/project_a/logger.ini root@target:/usr/app/config/

# 4. 设置执行权限
ssh root@target "chmod +x /usr/app/tcu/tcu_logger"
```

### 方法3: 使用部署脚本

```bash
# 创建部署脚本 deploy.sh
#!/bin/bash
TARGET_HOST=root@192.168.1.100
PLATFORM=jzq

# 复制程序
scp release/tcu_logger $TARGET_HOST:/usr/app/tcu/

# 复制库文件
scp extraLib/$PLATFORM/*.so $TARGET_HOST:/usr/lib/

# 复制配置文件
scp ../../config/project_a/logger.ini $TARGET_HOST:/usr/app/config/

echo "Deployment completed!"
```

## 常见问题

### Q: 编译时提示找不到头文件？

**A:** 检查 `INCLUDES` 路径是否正确，确保以下目录存在：
- `../base`
- `../interfaces`
- `../../libv2gshm/libcshm`
- `sql/`

### Q: 链接时提示找不到库文件？

**A:** 
1. 检查 `EXTRA_LIB_PATH` 和 `PLATFORM_LIB_PATH` 是否正确
2. 确认库文件存在于指定路径
3. 使用 `make -f Makefile.cross info` 查看实际使用的路径

### Q: 运行时提示找不到共享库？

**A:**
1. 检查 rpath 设置：`readelf -d release/tcu_logger | grep RPATH`
2. 确保库文件在 rpath 指定的路径中
3. 或设置 `LD_LIBRARY_PATH` 环境变量

### Q: 静态链接 libstdc++ 后文件太大？

**A:** 这是正常的，静态链接会将 C++ 标准库打包进可执行文件。如果空间有限，可以：
1. 使用动态链接，但需要确保目标设备有对应版本的 libstdc++
2. 使用工具链的 libstdc++，通过 rpath 指定路径

## 编译输出

编译成功后，输出文件位于：

```
core/logger/
├── obj/              # 对象文件（.o）
└── release/          # 可执行文件
    ├── tcu_logger    # 主程序
    ├── test_logger   # 测试程序
    └── test_all_logs # 完整测试
```

---

**作者:** BY ZF  
**最后更新:** 2025-01-XX
