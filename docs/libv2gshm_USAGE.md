# libv2gshm 共享内存库使用文档

## 概述

libv2gshm是经过生产验证的System V共享内存库，用于TCU各进程之间的实时数据共享。  
共享内存Key固定为 `85000`，自动创建或连接已存在的共享内存。

## 编译和链接

### 库文件位置
- 头文件：`libv2gshm/cshm/v2gshm.h` 或 `libv2gshm/libcshm/v2gshm.h`
- 库文件：`libv2gshm/libcshm/libevsshm.so`（动态库）

### 编译配置
```cpp
// 头文件路径
#include "v2gshm.h"

// 链接库（在.pro文件中）
LIBS += -L$$PWD/libv2gshm/libcshm -levsshm
```

## 基本使用

### 1. 创建和初始化

```cpp
#include "v2gshm.h"

// 构造函数会自动创建或连接共享内存
// 注意：构造函数中如果init失败会exit(1)，需要确保共享内存已创建
CShm shm;  // 自动初始化，失败会退出

// 或者检查初始化结果（如果重写了init）
if (!shm.init()) {
    // 初始化失败处理
}
```

### 2. 共享内存配置

当前配置（编译时宏定义）：
```cpp
#define YC_COUNT    612      // 遥测数据数量
#define YX_COUNT    612      // 遥信数据数量
#define DD_COUNT    128      // 数据字典数量
#define PORT_COUNT  2        // 充电枪数量（当前固定2枪）
```

共享内存Key：
```cpp
#define SHM_KEY     85000    // 固定Key，不可修改
```

## 数据结构访问方法

### 一、遥测遥信数据（YC/YX/DD）

```cpp
// 获取遥测数据（YC - 模拟量）
YC* yc = shm.getYc(ycno);  // ycno: 0 ~ YC_COUNT-1
yc->index = ycno;
yc->value = 12345;          // 原始值
yc->coef = 0.01f;           // 系数
float realValue = yc->value * yc->coef;  // 实际值

// 获取遥信数据（YX - 开关量）
YX* yx = shm.getYx(yxno);  // yxno: 0 ~ YX_COUNT-1
yx->value = true;           // 布尔值

// 获取数据字典（DD - 长整型）
DD* dd = shm.getDd(ddno);  // ddno: 0 ~ DD_COUNT-1
dd->value = 123456789LL;
```

### 二、充电端口状态

```cpp
// 获取充电端口信息（PortNo: 0或1，对应2枪）
_tagChargePort* port = shm.getChargePort(PortNo);
port->PortId = PortNo + 1;      // 充电接口标识（1或2）
port->WorkStatus = 3;            // 工作状态
strcpy(port->QrCode, "QRCODE123");  // 二维码

// 工作状态枚举（参考CHARGE_STATE，但这里用WorkStatus）
// STATE_IDLE = 0,      // 空闲
// STATE_PLUGIN,         // 插枪
// STATE_FLASHCARD,      // 刷卡
// STATE_WAITCHARGE,     // 等待充电
// STATE_CHARGE,         // 充电中
// STATE_FULL,           // 充满
// STATE_STOP,           // 停止
// STATE_EMESTOP,        // 急停
// STATE_UNPLUG          // 拔枪
```

### 三、设备信息

```cpp
// 获取设备信息（全局唯一）
_tagDeviceInfo* devInfo = shm.getDeviceInfo();

// 常用字段
strcpy(devInfo->pileSoftwareVer, "1.0.0");
devInfo->NumberOfChargPort = 2;
devInfo->nPowerRating = 120000;        // 额定功率（W）
devInfo->nChargerMaxOutVolt = 8000;    // 最高输出电压（0.1V）
devInfo->nChargerMaxOutCurr = 15000;   // 最高输出电流（0.1A）
```

### 四、交易记录（V2G协议）

```cpp
// 获取交易记录（PortNo: 0或1）
_sdk_event_executionStatus* trade = shm.getTradeRecord(PortNo);

trade->gunNo = PortNo + 1;
strcpy(trade->applySheetNo, "ORDER123456");
trade->executionStatus = 1;                    // 执行状态
trade->cumulativeTime = 3600;                  // 累计时间（秒）
trade->socNumber = 80;                         // SOC（%）

// 电量数据（5个费率）
trade->chargingEnergyValue[0] = 10000;         // 总电量
trade->cumulativeCharge[0] = 50000;            // 累计充电电量
```

### 五、远程启动（V2G协议）

```cpp
// 获取远程启动信息（PortNo: 0或1）
_sdk_service_remoteStart* remote = shm.getRemoteStart(PortNo);

remote->gunNo = PortNo + 1;
strcpy(remote->applyNo, "ORDER123");
strcpy(remote->userId, "USER001");
strcpy(remote->VIN, "VIN123456789");
```

### 六、计费模型（V2G协议）

```cpp
// 获取计费模型（全局唯一）
_sdk_service_issue_feeModel* feeModel = shm.getFeeModel();

feeModel->timeNum = 4;                         // 时段数
strcpy(feeModel->timeSeg[0], "0000");          // 时段开始时间HHMM
feeModel->chargeFee[0] = 100;                  // 电费（分/度）
feeModel->serviceFee[0] = 50;                  // 服务费（分/度）
```

### 七、EVS协议数据

```cpp
// EVS交易记录
_evs_event_tradeInfo* evsTrade = shm.evs_getTradeRecord(PortNo);
strcpy(evsTrade->tradeNo, "TRADE123");
evsTrade->totalElect = 100;                    // 总电量
evsTrade->totalCost = 15000;                   // 总费用（分）

// EVS远程启动
_evs_service_startCharge* evsStart = shm.evs_getRemoteStart(PortNo);
strcpy(evsStart->tradeNo, "TRADE123");
evsStart->startType = 1;                       // 启动方式

// EVS计费模型
_evs_service_issue_feeModel* evsFee = shm.evs_getFeeModel();
strcpy(evsFee->feeModelId, "MODEL001");
evsFee->timeNum = 96;                          // 最多96个时段
```

### 八、远程公共数据（HMI通信）

```cpp
// 获取远程公共数据（用于显示界面交互）
_remote_public* remote = shm.getRemotePublic(PortNo);

// 鉴权相关
remote->auth_request = 2;              // 0:null; 2:验证码; 3:账号
remote->auth_okflag = 1;               // 0:null; 1:ok; 2:fail

// 交易相关
remote->trade_request = 1;             // 0:null; 1:有卡交易; 2:验证码; 3:账号; 4:二维码
remote->debit_okflag = 1;              // 扣款结果

// 软件更新
remote->soft_update_request = 1;
remote->soft_update_ready = 1;
```

## 使用示例

### 示例1：读取充电状态

```cpp
#include "v2gshm.h"

CShm shm;

// 读取枪1的状态
_tagChargePort* port1 = shm.getChargePort(0);  // 枪1对应索引0
printf("枪1状态: %d\n", port1->WorkStatus);
printf("枪1二维码: %s\n", port1->QrCode);

// 读取枪2的状态
_tagChargePort* port2 = shm.getChargePort(1);  // 枪2对应索引1
printf("枪2状态: %d\n", port2->WorkStatus);
```

### 示例2：写入电表数据

```cpp
// 假设遥测索引0是枪1的电压
YC* voltage = shm.getYc(0);
voltage->value = 80000;      // 800.00V = 80000 * 0.01
voltage->coef = 0.01f;

// 假设遥测索引1是枪1的电流
YC* current = shm.getYc(1);
current->value = 5000;       // 50.00A = 5000 * 0.01
current->coef = 0.01f;
```

### 示例3：设置交易记录

```cpp
// 设置枪1的交易记录
_sdk_event_executionStatus* trade = shm.getTradeRecord(0);
trade->gunNo = 1;
strcpy(trade->applySheetNo, "ORDER20240101001");
trade->cumulativeTime = 3600;          // 1小时
trade->cumulativeCharge[0] = 50000;    // 50度
```

## 注意事项

### 1. 初始化顺序
- **第一个进程**：创建共享内存（CShm构造函数自动调用`CreateShm()`）
- **后续进程**：直接连接已存在的共享内存
- **关键**：确保第一个进程先启动，否则其他进程会因为共享内存不存在而失败

### 2. 线程安全
- **当前设计**：共享内存没有内置锁机制
- **建议**：
  - 读多写少的场景：一般不会冲突
  - 关键数据更新：考虑使用原子操作或外部锁
  - 多进程同时写同一地址：需要应用层同步

### 3. 数据一致性
- 共享内存是直接映射，修改立即生效
- 建议按进程划分数据区域：
  - 充电逻辑进程：负责写入`_tagChargePort`
  - 计量进程：负责写入电表数据（YC/DD）
  - 平台通信进程：读取交易记录，写入远程启动命令
  - 显示进程：读取所有数据用于显示

### 4. 指针有效性
- `get*()`方法返回的是指向共享内存的指针
- 指针在进程生命周期内有效
- 不要释放这些指针，因为指向的是共享内存

### 5. 共享内存清理
```cpp
// 仅在需要删除共享内存时调用（一般由守护进程负责）
shm.DelShm();  // 删除共享内存（需要权限）
```

### 6. 派生类的DataInit
```cpp
// 如果需要初始化共享内存数据，可以继承CShm并重写DataInit
class MyShm : public CShm {
public:
    virtual void DataInit() {
        // 自定义初始化逻辑
        _tagChargePort* port = getChargePort(0);
        port->WorkStatus = STATE_IDLE;
    }
};
```

## 各进程的使用场景

### 充电逻辑进程
- **读取**：设备信息、计费模型、远程启动命令
- **写入**：充电端口状态(`_tagChargePort`)、交易记录

### 计量计费进程
- **读取**：设备信息、计费模型
- **写入**：电表数据（YC/YX/DD）、交易记录中的电量部分

### 主控通信进程
- **读取**：充电端口状态（读取需求）
- **写入**：实时遥测数据（YC）- 电压、电流、功率等

### 平台通信进程
- **读取**：交易记录、设备信息、充电状态
- **写入**：远程启动命令、计费模型更新

### 显示界面进程
- **读取**：所有数据（只读）

### 日志进程
- **不使用共享内存**（只通过消息队列）

## 故障排查

### 共享内存创建失败
```
The Shm can not Create!!!!!
```
- 检查是否有足够的共享内存配额：`ipcs -l`
- 检查是否有权限：`ipcs -m`
- 检查是否已有进程占用了key 85000：`ipcs -m | grep 85000`

### 共享内存连接失败
- 确保共享内存已创建（第一个进程已启动）
- 检查进程权限

### 数据不一致
- 检查是否有多个进程同时写同一数据
- 考虑添加应用层的互斥机制

## 总结

libv2gshm提供了简单直观的接口，各进程通过统一的CShm类访问共享内存数据。使用时需要注意：
1. **初始化顺序**：第一个进程负责创建
2. **数据归属**：明确各进程的读写权限
3. **同步机制**：根据需要在应用层添加锁或原子操作
4. **指针管理**：不要释放get*()返回的指针

