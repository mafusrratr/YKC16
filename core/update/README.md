# TCU 升级程序项目

嵌入式Linux TCU（Telematics Control Unit）升级程序，支持升级包制作、安装和回滚功能。

## 项目结构

```
202601_tcuUpdate/
├── package/              # 打包模块
│   ├── make_update_package.sh  # 交互式打包脚本
│   ├── source/           # 源文件目录（示例）
│   ├── install/          # 打包输出目录（临时）
│   ├── install.tar.gz    # 生成的升级包
│   └── README.md         # 打包模块说明
├── update_handle/        # 更新处理模块
│   ├── TCUUpdate.pro     # Qt项目文件
│   ├── main.cpp          # 主程序入口
│   ├── mainwindow.*      # 主窗口（GUI）
│   ├── updateengine.*    # 更新引擎
│   ├── scriptparser.*    # XML清单解析器
│   ├── backupmanager.*   # 备份管理器
│   ├── updateexecutor.*  # 更新执行器
│   ├── configmanager.*   # 配置管理器
│   ├── logger.*          # 日志管理器
│   ├── diskspacechecker.* # 磁盘空间检查器
│   ├── config/           # 配置文件
│   │   ├── tcu_update.conf      # 配置文件示例
│   │   ├── README.md             # 配置说明
│   │   └── 配置部署脚本.sh       # 配置部署脚本
│   ├── release/          # 编译输出
│   │   ├── bin/          # 可执行文件
│   │   └── obj/          # 目标文件
│   └── README.md         # 更新模块说明
└── README.md             # 本文件
```

## 功能特性

### 1. 打包模块（package/）

**功能：**
- 交互式配置升级包
- 自动扫描源文件目录
- 生成XML格式清单文件（manifest.xml）
- 计算MD5校验和（checksum.md5）
- 生成标准升级包（install.tar.gz）
- 自动跳过 `.DS_Store` 等系统文件

**使用方法：**
```bash
cd package
./make_update_package.sh -s source
```

详细说明：参见 [package/README.md](package/README.md)

### 2. 更新处理模块（update_handle/）

**功能：**
- 解析升级包（install.tar.gz）
- 验证MD5校验和
- 检查磁盘空间（百分比控制）
- 创建备份（自动生成回滚包）
- 执行文件更新
- 支持预更新/后更新命令
- 图形界面显示进度和状态
- 支持手动回滚和命令行回滚

**命令行参数：**
```bash
./tcu_update -p /path/to/install.tar.gz  # 执行更新
./tcu_update -r                            # 执行回滚
./tcu_update -h                            # 显示帮助
```

**界面特性：**
- 分辨率：800x400 像素
- 实时显示更新进度
- 日志输出（时间戳）
- 状态信息显示
- 回滚按钮（更新失败时可用）

详细说明：参见 [update_handle/README.md](update_handle/README.md)

## 技术栈

- **打包脚本**：Bash（兼容 Bash 3.x+）
- **更新程序**：Qt 4.8.6 + C++
- **清单格式**：XML（QDomDocument）
- **校验算法**：MD5
- **界面框架**：Qt GUI（Qt 4.8.6）

## 快速开始

### 1. 制作升级包

```bash
cd package
./make_update_package.sh -s source
```

生成的升级包：`install.tar.gz`

### 2. 部署更新程序

```bash
cd update_handle
# 在远端服务器上编译（需要Qt 4.8.6交叉编译环境）
qmake TCUUpdate.pro
make

# 部署配置文件
cd config
./配置部署脚本.sh
```

### 3. 执行更新

```bash
# 在目标设备上运行
./tcu_update -p /path/to/install.tar.gz -qws
```

### 4. 执行回滚

```bash
# 方式1：通过GUI界面点击回滚按钮
# 方式2：命令行回滚
./tcu_update -r -qws
```

## 升级包结构

```
install.tar.gz (解压后)
└── install/
    ├── manifest.xml       # XML格式清单文件
    ├── checksum.md5       # MD5校验和文件
    └── files/             # 待更新文件目录
        └── ...
```

## 备份和回滚机制

### 备份

- **位置**：`/mnt/nandflash/ota_back/backup_YYYYMMDD_HHMMSS/`
- **保留数量**：最近3次（可配置）
- **格式**：回滚包（rollback.tar.gz），结构与升级包一致

### 回滚

- **方式1**：GUI界面点击"执行回滚"按钮
- **方式2**：命令行 `./tcu_update -r`
- **原理**：使用回滚包（rollback.tar.gz）复用更新流程

## 配置文件

配置文件位置：`/usr/app/config/tcu_update.conf`

主要配置项：
- `backup_dir`：备份目录路径
- `temp_dir`：临时解压目录
- `log_file`：日志文件路径
- `backup_keep_count`：备份保留数量
- `disk_space_margin_percent`：磁盘空间安全余量百分比

详细说明：参见 [update_handle/config/README.md](update_handle/config/README.md)

## 项目特点

1. **模块化设计**：打包和更新分离，职责清晰
2. **代码复用**：回滚复用更新流程，使用"逆安装包"方案
3. **可靠性**：MD5校验、磁盘空间检查、备份机制
4. **用户友好**：图形界面、实时进度、详细日志
5. **Qt 4.8.6兼容**：不使用C++11特性，兼容旧版本Qt

## 注意事项

1. **Qt版本**：必须使用 Qt 4.8.6
2. **权限要求**：更新程序需要root权限
3. **屏幕分辨率**：GUI适配 800x400 像素
4. **文件编码**：所有文件使用UTF-8编码
5. **备份空间**：确保备份目录有足够空间

## 开发环境

- **Qt版本**：4.8.6
- **编译器**：交叉编译工具链（arm-linux-gnueabihf）
- **目标平台**：嵌入式Linux（nuc980）
- **打包环境**：支持 macOS/Linux

## 许可证

项目开发中...
