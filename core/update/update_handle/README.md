# Update Handle 模块

基于Qt 4.8.6开发的嵌入式Linux系统升级程序，负责升级包的解析、安装和回滚。

## 功能概述

- ✅ 解析升级包（install.tar.gz）
- ✅ 验证MD5校验和
- ✅ 检查磁盘空间（百分比控制，检查所有相关分区）
- ✅ 创建备份（自动生成回滚包）
- ✅ 执行文件更新
- ✅ 支持预更新/后更新命令
- ✅ 图形界面显示更新进度和状态
- ✅ 日志记录（错误和警告）
- ✅ 支持手动回滚和命令行回滚

## 命令行参数

```bash
./tcu_update [选项]

选项:
  -p, --package PATH    升级包路径（执行更新）
  -r, --rollback        执行回滚操作
  -h, --help            显示帮助信息
  -qws                   以Qt窗口服务器模式运行（嵌入式设备）
```

## 使用示例

```bash
# 执行更新
./tcu_update -p /path/to/install.tar.gz -qws

# 执行回滚
./tcu_update -r -qws

# 显示帮助
./tcu_update -h
```

## 项目结构

```
update_handle/
├── TCUUpdate.pro          # Qt项目文件
├── main.cpp               # 主程序入口
├── mainwindow.h/cpp       # 主窗口类（GUI）
├── mainwindow.ui          # UI界面文件
├── updateengine.h/cpp     # 更新引擎（核心逻辑）
├── scriptparser.h/cpp     # XML清单解析器
├── backupmanager.h/cpp    # 备份管理器（生成回滚包）
├── updateexecutor.h/cpp   # 更新执行器
├── configmanager.h/cpp    # 配置管理器
├── logger.h/cpp           # 日志管理器
├── diskspacechecker.h/cpp # 磁盘空间检查器
├── config/                # 配置文件目录
│   ├── tcu_update.conf    # 配置文件示例
│   ├── README.md          # 配置说明
│   └── 配置部署脚本.sh    # 配置部署脚本
└── release/               # 编译输出目录
    ├── bin/               # 可执行文件
    └── obj/               # 目标文件
```

## 编译说明

### 环境要求

- Qt 4.8.6
- 交叉编译工具链（arm-linux-gnueabihf）
- qmake

### 编译步骤

```bash
cd update_handle
qmake TCUUpdate.pro
make
```

编译输出：
- 可执行文件：`release/bin/tcu_update`
- 目标文件：`release/obj/*.o`

## 配置说明

配置文件位置：`/usr/app/config/tcu_update.conf`

主要配置项：
- `backup_dir`：备份目录路径（默认：`/mnt/nandflash/ota_back`）
- `temp_dir`：临时解压目录（默认：`/tmp/tcu_update_extract`）
- `log_file`：日志文件路径（默认：`/var/log/tcu_update.log`）
- `backup_keep_count`：备份保留数量（默认：3）
- `disk_space_margin_percent`：磁盘空间安全余量百分比（默认：10）

详细说明：参见 [config/README.md](config/README.md)

## 工作流程

### 更新流程

1. 验证升级包（文件存在性检查）
2. 解压升级包到临时目录
3. 验证MD5校验和（使用 `md5sum -c`）
4. 解析manifest.xml
5. 检查磁盘空间
6. 创建备份（生成回滚包）
7. 执行预更新命令
8. 执行文件更新
9. 执行后更新命令
10. 清理临时文件

### 回滚流程

1. 查找最新备份目录
2. 定位回滚包（rollback.tar.gz）
3. 复用更新流程（使用回滚包作为升级包）

## 备份和回滚机制

### 备份

- **位置**：`/mnt/nandflash/ota_back/backup_YYYYMMDD_HHMMSS/`
- **格式**：回滚包（rollback.tar.gz），结构与升级包一致
  ```
  backup_YYYYMMDD_HHMMSS/
  └── rollback.tar.gz
      └── install/
          ├── manifest.xml
          ├── checksum.md5
          └── files/
              └── ...
  ```
- **保留数量**：最近3次（可配置）

### 回滚

- **方式1**：GUI界面点击"执行回滚"按钮（更新失败时自动启用）
- **方式2**：命令行 `./tcu_update -r`
- **原理**：使用回滚包复用更新流程，确保回滚和更新行为一致

## 界面说明

- **分辨率**：800x400 像素（固定）
- **状态标签**：显示当前操作状态
- **进度条**：显示更新进度（0-100%）
- **日志区域**：显示详细日志（带时间戳）
- **回滚按钮**：更新失败时可用
- **关闭按钮**：操作完成后可用

## 错误处理

1. **部分文件更新失败**：显示失败文件列表和原因，启用回滚按钮
2. **预更新命令失败**：记录警告，继续执行
3. **后更新命令失败**：记录错误，需要回滚
4. **磁盘空间不足**：显示详细空间信息，终止更新
5. **MD5校验失败**：显示期望值和实际值，终止更新

## 日志记录

- **位置**：`/var/log/tcu_update.log`（可配置）
- **内容**：错误和警告信息
- **格式**：`[YYYY-MM-DD HH:MM:SS] [LEVEL] 消息`

## 技术细节

### Qt 4.8.6 兼容性

- 不使用C++11特性
- 使用Qt4风格的信号槽（`SIGNAL()`/`SLOT()`）
- 使用 `QDirIterator` 遍历目录
- 使用 `QProcess` 执行系统命令
- 使用 `QDomDocument` 解析XML

### 文件复制

- 使用系统命令 `cp` 复制文件（确保二进制文件正确复制）
- 复制后验证文件大小一致性

### 字符编码

- 所有文件使用UTF-8编码
- 使用 `QTextCodec` 设置编码
- 使用 `setlocale(LC_ALL, "")` 设置本地化

## 注意事项

1. **权限要求**：程序需要root权限运行
2. **屏幕分辨率**：GUI固定为800x400，不支持其他分辨率
3. **备份空间**：确保备份目录有足够空间
4. **临时目录**：确保临时目录有足够空间
5. **配置文件**：首次运行前需要部署配置文件

## 故障排查

### 问题：找不到配置文件

**解决**：运行配置部署脚本
```bash
cd config
./配置部署脚本.sh
```

### 问题：MD5校验失败

**检查**：
1. 升级包是否完整
2. checksum.md5文件是否正确
3. 文件路径是否正确

### 问题：磁盘空间不足

**检查**：
1. 备份目录空间
2. 临时目录空间
3. 目标分区空间

### 问题：回滚失败

**检查**：
1. 备份目录是否存在
2. rollback.tar.gz是否存在
3. 回滚包是否完整

## 开发历史

- ✅ 项目基础搭建
- ✅ 核心模块实现
- ✅ UI界面开发
- ✅ 更新引擎实现
- ✅ 备份和回滚机制（回滚包方案）
- ✅ 测试和优化
