# Package 模块

负责TCU升级包的打包制作，生成标准的升级包（install.tar.gz）。

## 目录结构

```
package/
├── make_update_package.sh     # 交互式打包脚本
├── source/                    # 源文件目录（示例）
│   ├── app/                   # 应用程序
│   ├── lib/                   # 库文件
│   └── test/                  # 测试文件
├── install/                   # 打包输出目录（临时，可删除）
│   ├── manifest.xml           # 清单文件
│   ├── checksum.md5           # MD5校验和
│   └── files/                 # 待更新文件
├── install.tar.gz             # 生成的升级包
└── README.md                  # 本文件
```

## 功能特性

- ✅ 交互式配置升级包
- ✅ 自动扫描源文件目录（跳过 `.DS_Store` 和根目录直接文件）
- ✅ 按文件夹批量配置目标路径
- ✅ 生成XML格式清单文件（manifest.xml）
- ✅ 计算MD5校验和（checksum.md5）
- ✅ 生成标准升级包（install.tar.gz）
- ✅ 自动验证升级包完整性

## 使用方法

### 基本用法

```bash
cd package
./make_update_package.sh -s source
```

### 参数说明

```bash
./make_update_package.sh [选项]

选项:
  -s, --source DIR     源文件目录（默认：当前目录）
  -h, --help           显示帮助信息
```

### 交互式配置

运行脚本后，按提示输入：

1. **包基本信息**：
   - 包名称（可选，默认：TCU System Update）
   - 包描述（可选）

2. **文件目标路径**：
   - 按文件夹配置目标路径前缀
   - 输入绝对路径（以 `/` 开头）
   - 留空可跳过该文件夹

3. **更新命令**（可选）：
   - 预更新命令（更新前执行）
   - 后更新命令（更新后执行）
   - 回滚命令（回滚时执行）

## 升级包结构

```
install.tar.gz (解压后)
└── install/
    ├── manifest.xml       # XML格式清单文件
    ├── checksum.md5       # MD5校验和文件
    └── files/             # 待更新文件目录
        └── ...
```

### manifest.xml 格式

```xml
<?xml version="1.0" encoding="UTF-8"?>
<package packageVersion="1.0">
  <packageInfo>
    <name>包名称</name>
    <buildDate>构建日期</buildDate>
    <description>包描述</description>
  </packageInfo>
  <files>
    <file>
      <source>files/路径/文件名</source>
      <destination>/目标路径/文件名</destination>
      <permission>777</permission>
      <owner>root</owner>
      <backup>true</backup>
      <md5>MD5值</md5>
      <size>文件大小</size>
    </file>
  </files>
  <commands>
    <preUpdate>
      <command>预更新命令</command>
    </preUpdate>
    <postUpdate>
      <command>后更新命令</command>
    </postUpdate>
    <rollback>
      <command>回滚命令</command>
    </rollback>
  </commands>
</package>
```

### checksum.md5 格式

标准 `md5sum` 输出格式：
```
MD5值  文件路径
MD5值  manifest.xml
```

## 使用示例

### 示例1：打包应用程序和库文件

```bash
# 源文件结构
source/
├── app/
│   └── tcu_app
└── lib/
    └── libtcu.so

# 运行打包脚本
./make_update_package.sh -s source

# 配置目标路径
# [文件夹: app] -> /usr/bin
# [文件夹: lib] -> /usr/lib
```

生成的升级包将包含：
- `files/app/tcu_app` -> `/usr/bin/tcu_app`
- `files/lib/libtcu.so` -> `/usr/lib/libtcu.so`

### 示例2：验证升级包

```bash
# 解压升级包
tar -xzf install.tar.gz

# 查看清单文件
cat install/manifest.xml

# 验证MD5校验和
cd install
md5sum -c checksum.md5
```

## 注意事项

1. **源文件要求**：
   - 只处理子文件夹下的文件
   - 自动跳过 `.DS_Store` 文件
   - 跳过根目录下的直接文件

2. **目标路径要求**：
   - 必须是绝对路径（以 `/` 开头）
   - 按文件夹配置，该文件夹下所有文件使用相同的前缀

3. **固定配置**：
   - 权限：777
   - 所有者：root
   - 备份：true

4. **输出文件**：
   - 固定为 `install.tar.gz`（在当前目录生成）

## 故障排查

### 问题：找不到源文件

**解决**：检查源文件目录路径是否正确

### 问题：目标路径配置错误

**解决**：确保输入的是绝对路径（以 `/` 开头）

### 问题：打包失败

**检查**：
1. 是否有写入权限
2. 磁盘空间是否足够
3. 源文件是否可读

## 技术细节

### 文件扫描

- 使用 `find` 命令递归扫描
- 自动跳过 `.DS_Store` 文件
- 只处理子文件夹下的文件

### MD5计算

- 使用系统命令 `md5sum` 或 `md5`
- 兼容 macOS 和 Linux

### 打包

- 使用 `tar -czf` 命令打包
- 确保包内包含 `install/` 目录结构

## 与更新程序的配合

打包脚本生成的升级包可以直接被 `update_handle` 模块使用：

```bash
# 1. 制作升级包
cd package
./make_update_package.sh -s source

# 2. 使用更新程序安装
cd ../update_handle
./tcu_update -p ../package/install.tar.gz -qws
```
