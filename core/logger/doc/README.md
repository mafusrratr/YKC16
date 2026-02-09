# TCU Logger 模块

> 高性能日志系统，支持多进程异步日志、充电交易记录、计费模型管理及共享内存同步

## 快速开始

### 1. 编译

```bash
# 交叉编译（用于目标设备）
make -f Makefile.cross clean
make -f Makefile.cross

# 本地编译（仅用于语法检查）
make clean
make
```

📖 详细编译说明请查看 **[BUILD.md](BUILD.md)**

### 2. 运行

```bash
# 启动 logger 进程
cd /usr/app/tcu
./tcu_logger /usr/app/config/logger.ini &

# 运行测试
./test_logger
```

### 3. 基本使用

```cpp
#include "../base/logger/log_sender.h"

LogSender logger("my_module");

// 记录日志
logger.info("模块启动成功");
logger.error("通信失败", "详细错误信息");

// 保存充电交易记录
TradeRecord rec;
rec.tradeNo = "TNO_001";
rec.totalElect = 1000;
logger.logTradeRecord(rec);

// 保存计费模型（会自动更新共享内存）
FeeModel model;
model.feeModelId = "FM_001";
model.timeNum = 3;
logger.saveFeeModel(model);
```

## 核心特性

✅ **5级日志** - DEBUG/INFO/WARN/ERROR/FATAL  
✅ **异步传输** - 消息队列，不阻塞业务进程  
✅ **数据验证** - 严格的数组长度校验  
✅ **充电记录** - 完整的交易信息存储  
✅ **计费模型** - 数据库存储 + 共享内存同步  
✅ **实时更新** - 新计费模型立即同步到共享内存  
✅ **定期备份** - 自动备份数据库，可配置目录与间隔  

## 目录说明

```
core/logger/
├── obj/              # 对象文件（编译产物）
├── release/          # 可执行文件
├── doc/              # 📚 详细文档
├── sql/              # SQLite 头文件
├── *.cpp, *.h        # 源代码
├── Makefile          # 本地编译
├── Makefile.cross    # 交叉编译
├── BUILD.md          # 🔨 编译说明
└── README.md         # 本文件
```

## 📚 完整文档

所有详细文档位于 **[doc/](doc/)** 目录：

| 文档 | 说明 |
|------|------|
| [doc/README.md](doc/README.md) | 完整使用文档（API、数据结构、示例） |
| [doc/BUILD.md](doc/BUILD.md) | 详细编译说明（本地、交叉编译） |
| [doc/PROJECT_STRUCTURE.md](doc/PROJECT_STRUCTURE.md) | 项目架构和模块说明 |
| [doc/DATABASE_TABLES.md](doc/DATABASE_TABLES.md) | 数据库表结构和字段 |
| [doc/STRESS_TEST.md](doc/STRESS_TEST.md) | 压力测试说明文档 |
| [doc/CHANGELOG.md](doc/CHANGELOG.md) | 版本更新日志 |

## 数据库

| 数据库 | 位置 | 说明 |
|--------|------|------|
| tcu.db | /usr/app/data/ | 运行日志 |
| chargerecords.db | /usr/app/data/ | 充电交易记录 |
| feemodel.db | /usr/app/data/ | 计费模型 |

## 配置文件

`logger.ini` 最小配置：
```ini
[Logger]
log_level=1

[Database]
main_db_path=/usr/app/data/tcu.db
charge_db_path=/usr/app/data/chargerecords.db
fee_db_path=/usr/app/data/feemodel.db

[Backup]
backup_dir=/usr/app/data/backup
backup_interval_minutes=43200   # 30天
backup_check_minutes=60         # 每小时检查一次
```

## 依赖库

- `libsqlite3.so` - SQLite 数据库
- `libevsshm.so` - 共享内存库
- `libpthread.so` - 线程库

## Makefile 命令

```bash
make              # 编译所有程序
make clean        # 清理编译产物
make docs         # 整理文档到 doc/ 目录
make install      # 安装到 /usr/app/tcu/
make test         # 运行测试
make -f Makefile.cross info  # 查看交叉编译配置
```

---

**作者:** BY ZF  
**版本:** 1.0.0  
**最后更新:** 2025-11-07
