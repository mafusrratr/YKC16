# Logger 模块更新日志

## [1.0.0] - 2025-11-07

### 新增功能 ✨

- **计费模型加载到共享内存**
  - Logger 初始化时自动加载最新计费模型到共享内存
  - 新计费模型保存时实时更新共享内存
  - 其他进程可从共享内存直接读取计费模型

- **数据库管理**
  - `loadLatestFeeModelFromDb()` - 从数据库加载最新计费模型
  - `loadLatestFeeModelFromFile()` - 静态方法，从文件加载
  - `loadFeeModelToShm()` - 加载并写入共享内存

- **充电交易记录**
  - 支持完整的充电交易信息存储
  - 包含时段电量、费用详情
  - 支持实体卡号字段

- **计费模型管理**
  - 支持最多 96 个时段
  - 严格的数组长度验证
  - 数据库和共享内存双重存储

### 架构优化 🏗️

- **移除重复接口**
  - 删除 `core/logger/ilogger.h`（重复）
  - 统一使用 `core/interfaces/ilogger.h`

- **目录结构重组**
  - `obj/` - 对象文件目录
  - `release/` - 可执行文件目录
  - `doc/` - 文档目录
  - 编译产物与源代码分离

- **配置文件简化**
  - 移除不使用的配置项（log_file、rotation、backup等）
  - 保留核心配置（log_level、数据库路径）

### 文档完善 📚

- `README.md` - 快速入门和基本使用
- `BUILD.md` - 详细编译说明
- `PROJECT_STRUCTURE.md` - 项目结构说明
- `DATABASE_TABLES.md` - 数据库表结构
- `CHANGELOG.md` - 更新日志
- `.gitignore` - Git 忽略规则

### 数据验证增强 🔒

- **TradeRecord 验证**
  - `partElect.size() == timeNum`
  - `chargeFee.size() == timeNum`
  - `serviceFee.size() == timeNum`
  - `pointsElect.size() == crossPoints`

- **FeeModel 验证**
  - `timeSeg.size() == timeNum`
  - `chargeFee.size() == timeNum`
  - `serviceFee.size() == timeNum`
  - `segFlag.size() == timeNum`

- **验证失败处理**
  - 输出到 stderr
  - 记录 WARN 日志到 operation_logs
  - 拒绝入库

### 性能优化 ⚡

- **消息队列缓冲区**
  - 从 1024 字节扩大到 8192 字节
  - 支持大型 JSON 消息（96段计费模型）

- **时区处理**
  - 显式使用本地时间
  - 修复数据库时间戳与系统时间的8小时偏差

### Bug 修复 🐛

- 修复 `std::stoi/stoul: no conversion` 异常
- 修复 JSON 解析偏移错误
- 修复交叉编译时 `std::put_time` 不可用问题
- 修复数据库文件权限和目录创建问题
- 修复消息队列 E2BIG 错误

### 依赖库管理 📦

- **Makefile 支持**
  - 环境变量 `EXTRA_LIB_PATH` 指定外部库路径
  - 自动查找共享内存库（`SHM_LIB_PATH`）
  - 本地和交叉编译分离

- **共享内存库集成**
  - 链接 `libevsshm.so`
  - 支持读写共享内存中的计费模型

### 测试增强 🧪

- 增加计费模型测试用例（有效、无效、边界）
- 增加从共享内存读取的测试
- 测试输出更详细（显示所有字段）

---

## 技术栈

- **语言：** C++11 (交叉编译使用 C++0x)
- **数据库：** SQLite3
- **IPC：** System V 消息队列
- **共享内存：** POSIX 共享内存
- **编译工具链：** arm-linux-gnueabihf-g++ 4.9.1
- **目标设备：** iMX6UL (NUC980)

---

**维护者：** BY ZF  
**版本：** 1.0.0  
**发布日期：** 2025-11-07

