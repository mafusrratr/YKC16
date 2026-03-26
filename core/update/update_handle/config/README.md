# 配置文件说明

## 配置文件位置

### 开发环境
- 配置文件示例：`update_handle/config/tcu_update.conf.example`
- 用于参考和测试

### 目标设备
- 配置文件路径：`/usr/app/config/tcu_update.conf`
- 程序启动时会自动读取此配置文件
- 如果配置文件不存在，程序将使用默认值

## 配置文件格式

配置文件采用 INI 格式（.ini），使用 `[Section]` 和 `key=value` 的格式。

### 示例

```ini
[General]
backup_dir=/mnt/nandflash/ota_back
temp_dir=/tmp/tcu_update_extract
log_file=/var/log/tcu_update.log
backup_keep_count=3
disk_space_margin_percent=10
status_file=/usr/app/data/update_status.flag
```

## 配置项说明

### backup_dir
- **类型**: 字符串（绝对路径）
- **默认值**: `/mnt/nandflash/ota_back`
- **说明**: 备份文件保存目录
- **要求**: 
  - 必须是绝对路径
  - 目录必须存在或可创建
  - 需要有写入权限

### temp_dir
- **类型**: 字符串（绝对路径）
- **默认值**: `/tmp/tcu_update_extract`
- **说明**: 升级包解压临时目录
- **要求**:
  - 必须是绝对路径
  - 需要有写入权限
  - 建议使用 `/tmp` 下的目录

### log_file
- **类型**: 字符串（绝对路径）
- **默认值**: `/var/log/tcu_update.log`
- **说明**: 日志文件路径
- **要求**:
  - 必须是绝对路径
  - 日志目录必须存在或可创建
  - 需要有写入权限

### backup_keep_count
- **类型**: 整数
- **默认值**: `3`
- **范围**: 1-10（建议值）
- **说明**: 保留的备份数量
- **注意**: 
  - 设置为 0 表示不自动删除备份（不推荐）
  - 超过此数量的旧备份会被自动删除

### disk_space_margin_percent
- **类型**: 整数
- **默认值**: `10`
- **范围**: 5-30（建议值）
- **说明**: 磁盘空间安全余量百分比
- **示例**: 
  - `10` 表示保留 10% 的额外空间
  - 如果计算需要 100MB，实际会检查是否有 110MB 可用空间

### status_file
- **类型**: 字符串（绝对路径）
- **默认值**: `/usr/app/data/update_status.flag`
- **说明**: 更新状态标志文件路径
- **格式**: 简单INI格式文本文件
- **内容**:
  - `STATUS`: 当前状态（UPDATING/UPDATED/ERROR，回滚复用更新状态标志）
  - `PACKAGE_PATH`: 升级包路径（更新时记录）
  - `TIMESTAMP`: Unix时间戳
- **要求**:
  - 必须是绝对路径
  - 目录必须存在或可创建
  - 需要有写入权限
- **状态说明**:
  - `UPDATING`: 更新进行中（包括回滚操作，回滚就是逆安装操作）
  - `UPDATED`: 更新成功完成（包括回滚成功）
  - `ERROR`: 更新或回滚过程中出错

## 配置文件部署

### 方法1：手动创建

```bash
# 在目标设备上创建配置文件
sudo mkdir -p /usr/app/config
sudo cp tcu_update.conf.example /usr/app/config/tcu_update.conf
sudo chmod 644 /usr/app/config/tcu_update.conf
```

### 方法2：通过升级包部署

将配置文件包含在升级包中，通过更新程序部署到目标位置。

### 方法3：使用默认值

如果配置文件不存在，程序会使用硬编码的默认值，功能正常但无法自定义配置。

## 配置文件验证

程序启动时会自动验证配置文件：
- 如果配置文件不存在：使用默认值，记录警告日志
- 如果配置文件格式错误：使用默认值，记录错误日志
- 如果配置值无效：使用默认值，记录警告日志

## 配置修改

### 修改配置文件

```bash
# 编辑配置文件
sudo vi /usr/app/config/tcu_update.conf

# 修改后重启程序
sudo systemctl restart tcu_update  # 如果使用 systemd
# 或
sudo killall tcu_update && sudo /path/to/tcu_update -p ...
```

### 配置生效

- 配置文件在程序启动时读取
- 修改配置后需要重启程序才能生效
- 运行中的程序不会自动重新加载配置

## 故障排查

### 配置文件不存在

**现象**: 程序使用默认值运行

**解决**: 
1. 检查配置文件路径是否正确
2. 创建配置文件并设置正确的权限

### 配置文件权限问题

**现象**: 无法读取配置文件

**解决**:
```bash
sudo chmod 644 /usr/app/config/tcu_update.conf
sudo chown root:root /usr/app/config/tcu_update.conf
```

### 配置值无效

**现象**: 程序使用默认值，日志中有警告

**解决**:
1. 检查配置文件格式是否正确
2. 检查配置值是否在有效范围内
3. 查看日志文件了解具体错误

## 最佳实践

1. **备份目录**: 使用独立的存储分区，避免影响系统分区
2. **临时目录**: 使用 `/tmp` 下的目录，系统会自动清理
3. **日志文件**: 定期清理或轮转，避免占用过多空间
4. **备份数量**: 根据存储空间合理设置，建议 3-5 个
5. **空间余量**: 根据实际使用情况调整，建议 10-15%

## 示例配置

### 最小配置（使用所有默认值）

```ini
[General]
# 所有配置项都使用默认值
```

### 完整配置（自定义所有项）

```ini
[General]
backup_dir=/mnt/nandflash/ota_back
temp_dir=/tmp/tcu_update_extract
log_file=/var/log/tcu_update.log
backup_keep_count=5
disk_space_margin_percent=15
status_file=/usr/app/data/update_status.flag
```

### 高安全配置（更多备份和空间余量）

```ini
[General]
backup_dir=/mnt/nandflash/ota_back
temp_dir=/tmp/tcu_update_extract
log_file=/var/log/tcu_update.log
backup_keep_count=5
disk_space_margin_percent=20
status_file=/usr/app/data/update_status.flag
```

