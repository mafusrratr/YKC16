# V2G vs EVS 协议数据结构对比

## 概述

TCU系统同时支持两套充电桩通信协议：
- **V2G协议**：相对简单的协议，侧重于实时状态上报
- **EVS协议**：更完善的协议，支持更复杂的计费模型和交易信息

## 一、交易记录对比

### V2G交易记录 (`_sdk_event_executionStatus`)

**特点**：侧重于**实时状态和电能示值**

```cpp
typedef struct {
    unsigned char gunNo;                      // 充电枪编号
    char applySheetNo[41];                    // 订单编号
    char vehicleVIN[18];                      // VIN码
    unsigned char executionStatus;            // 执行状态
    // 直流电表数据（5个费率）
    unsigned int chargingEnergyValue[5];      // 充电电能示值
    unsigned int disChargingEnergyValue[5];   // 放电电能示值
    unsigned int cumulativeCharge[5];         // 累计充电电量
    unsigned int cumulativeDischarge[5];      // 累计放电电量
    // 交流电表数据（5个费率）
    unsigned int acChargingEnergyValue[5];    // 交流充电电能示值
    unsigned int acDisChargingEnergyValue[5]; // 交流放电电能示值
    unsigned int acCumulativeCharge[5];       // 交流累计充电电量
    unsigned int acCumulativeDischarge[5];    // 交流累计放电电量
    unsigned int cumulativeTime;              // 累计时间（秒）
    unsigned char socNumber;                  // 当前SOC（%）
    unsigned int eventDesc;                   // 事件描述
} _sdk_event_executionStatus;
```

**关键差异**：
- ✅ 包含**直流+交流**双电表数据（支持V2G双向充电）
- ✅ 有**5个费率**的电能示值数组
- ❌ **没有计费信息**（电费、服务费、总费用）
- ❌ 没有开始/结束时间戳
- ❌ 没有时段电量分布

### EVS交易记录 (`_evs_event_tradeInfo`)

**特点**：完整的**交易结算信息**，包含详细的计费数据

```cpp
typedef struct {
    unsigned char gunNo;                      // 充电枪编号
    char preTradeNo[41];                      // 平台交易流水号
    char tradeNo[41];                         // 设备交易流水号
    char vinCode[18];                         // VIN码
    unsigned char timeDivType;                // 计量计费类型
    unsigned char startType;                  // 启动方式
    unsigned int chargeStartTime;             // 开始充电时间（时间戳）
    unsigned int chargeEndTime;               // 结束充电时间（时间戳）
    unsigned char startSoc;                   // 启动时SOC（%）
    unsigned char endSoc;                     // 停止时SOC（%）
    unsigned int reason;                      // 停止充电原因
    char feeModelId[17];                      // 计费模型编号
    long long sumStart;                       // 电表总起示值
    long long sumEnd;                         // 电表总止示值
    unsigned int totalElect;                  // 总电量
    unsigned int totalPowerCost;              // 总电费（分）
    unsigned int totalServCost;               // 总服务费（分）
    unsigned int totalCost;                   // 总消费金额（分）
    unsigned char timeNum;                    // 时段数
    // 时段数据（最多96个时段）
    unsigned int partElect[96];               // 时段电量
    unsigned int chargeFee[96];               // 时段电费
    unsigned int serviceFee[96];              // 时段服务费
    unsigned char startPoint;                 // 起始点标识
    unsigned char crossPoints;                // 跨越点数
    unsigned int pointsElect[96];             // 跨越点电量
} _evs_event_tradeInfo;
```

**关键差异**：
- ✅ 包含**完整的计费信息**（电费、服务费、总费用）
- ✅ 有**开始/结束时间戳**
- ✅ 支持**时段计费**（最多96个时段）
- ✅ 包含**跨越点电量**（处理跨时段的情况）
- ✅ 有**启动/停止SOC**
- ❌ 只有**总电量**，没有分费率的电能示值

### 交易记录对比总结

| 特性 | V2G | EVS |
|------|-----|-----|
| 数据重点 | 实时状态、电能示值 | 交易结算、计费明细 |
| 电表数据 | 直流+交流，5费率 | 总电量 |
| 计费信息 | ❌ 无 | ✅ 完整（电费/服务费/总费用） |
| 时段数据 | ❌ 无 | ✅ 最多96个时段 |
| 时间信息 | 累计时间 | 开始/结束时间戳 |
| SOC信息 | 当前SOC | 启动SOC + 结束SOC |
| 适用场景 | 实时监控、状态上报 | 交易结算、账单生成 |

## 二、计费模型对比

### V2G计费模型 (`_sdk_service_issue_feeModel`)

**特点**：简单的时间段标志 + 固定费率

```cpp
typedef struct {
    unsigned char timeNum;                    // 时段数（1-48）
    char timeSeg[14][5];                      // 时段开始时间点（HHMM格式）
    unsigned int segFlag[14];                 // 时段标志
    unsigned int chargeFee[4];                  // 电费模型（固定4个值）
    unsigned int serviceFee[4];               // 服务费模型（固定4个值）
} _sdk_service_issue_feeModel;
```

**关键限制**：
- ⚠️ 时段数最多 **14个**（但timeNum可以是1-48，可能分段存储）
- ⚠️ 电费和服务费只有 **4个固定值**（可能对应：峰/平/谷/尖）
- ⚠️ **没有模型ID**，无法区分不同的计费方案

### EVS计费模型 (`_evs_service_issue_feeModel`)

**特点**：灵活的时段费率，每个时段独立定价

```cpp
typedef struct {
    char feeModelId[17];                      // 计费模型编号
    unsigned char timeNum;                    // 时段数（1-96）
    char timeSeg[96][5];                      // 时段开始时间点（HHMM格式）
    unsigned int chargeFee[96];               // 电费模型（每个时段一个）
    unsigned int serviceFee[96];              // 服务费模型（每个时段一个）
} _evs_service_issue_feeModel;
```

**关键优势**：
- ✅ 时段数最多 **96个**
- ✅ **每个时段独立的电费和服务费**
- ✅ 有**模型ID**，支持多套计费方案
- ✅ 更灵活，可以精确到每15分钟一个时段

### 计费模型对比总结

| 特性 | V2G | EVS |
|------|-----|-----|
| 时段数上限 | 14（可能分段） | 96 |
| 费率存储 | 4个固定值 | 每个时段独立 |
| 模型标识 | ❌ 无 | ✅ 有模型ID |
| 灵活性 | 低（固定费率模式） | 高（任意时段定价） |
| 适用场景 | 简单的峰谷平电价 | 复杂的分时电价、实时电价 |

## 三、远程启动对比

### V2G远程启动 (`_sdk_service_remoteStart`)

**特点**：简单的订单信息传递

```cpp
typedef struct {
    unsigned char gunNo;                      // 充电枪编号
    char applyNo[41];                         // 订单编号
    char userId[256];                         // 用户ID（最大255字符）
    char VIN[18];                             // VIN码
    unsigned char decisionType;               // 决策类型
    char decisionTime[16];                    // 决策时间（时间戳字符串）
} _sdk_service_remoteStart;
```

**关键特点**：
- ✅ 包含用户ID（支持长ID）
- ✅ 有决策类型和决策时间
- ❌ **没有充电参数**（电压、电流、电量等）
- ❌ **没有鉴权码**

### EVS远程启动 (`_evs_service_startCharge`)

**特点**：完整的充电控制参数

```cpp
typedef struct {
    unsigned char gunNo;                      // 充电枪编号
    char preTradeNo[41];                      // 平台交易流水号
    char tradeNo[41];                         // 设备交易流水号
    unsigned char startType;                  // 启动方式
    char authCode[256];                       // 鉴权码（最大255字符）
    unsigned char chargeMode;                 // 充电模式
    unsigned int limitData;                   // 限制值（电量/金额/时间）
    unsigned int stopCode;                    // 停机码
    unsigned char startMode;                  // 启动模式
} _evs_service_startCharge;
```

**关键特点**：
- ✅ 有**平台流水号 + 设备流水号**（双重标识）
- ✅ 包含**鉴权码**（支持离线鉴权）
- ✅ 有**充电模式**、**限制值**、**停机码**
- ✅ 更详细的启动控制参数
- ❌ 没有用户ID（通过鉴权码隐含）

### 远程启动对比总结

| 特性 | V2G | EVS |
|------|-----|-----|
| 订单标识 | 单一订单号 | 平台流水号+设备流水号 |
| 用户信息 | 直接包含用户ID | 通过鉴权码 |
| 鉴权支持 | ❌ 无 | ✅ 有鉴权码（支持离线） |
| 充电参数 | ❌ 无 | ✅ 有（模式、限制值、停机码） |
| 适用场景 | 在线订单启动 | 在线/离线订单启动 |

## 四、常量定义对比

### V2G常量

```cpp
#define V2G_MAX_TRADE_LEN (40 + 1)           // 订单号长度：41字符
#define V2G_MAX_USERID_LEN (255 + 1)         // 用户ID：256字符
#define V2G_MAX_CAR_VIN_LEN (17 + 1)         // VIN码：18字符
#define V2G_MAX_ENERGY_DATA_LEN (5)          // 电能数据：5个费率
#define V2G_MAX_MODEL_DEVSEG (14)            // 计费模型时段数：14
```

### EVS常量

```cpp
#define EVS_MAX_TRADE_LEN (40 + 1)           // 订单号长度：41字符（同V2G）
#define EVS_MAX_CAR_VIN_LEN (17 + 1)         // VIN码：18字符（同V2G）
#define EVS_MAX_MODEL_DEVSEG (96)            // 计费模型时段数：96（更多）
#define EVS_MAX_MODEL_ID_LEN (16 + 1)        // 计费模型ID：17字符
#define EVS_MAX_AUTHCODE_LEN (255 + 1)       // 鉴权码：256字符
```

## 五、使用场景建议

### 使用V2G协议的场景

1. **实时监控系统**
   - 需要实时查看充电状态、电能示值
   - 需要区分直流/交流电表数据
   - 需要查看多费率电能

2. **简单的电价体系**
   - 峰谷平电价（最多4个费率）
   - 不需要复杂的时段计费

3. **在线订单**
   - 订单通过平台直接下发
   - 不需要离线鉴权

### 使用EVS协议的场景

1. **完整的交易结算**
   - 需要生成详细的账单
   - 需要时段电量分布
   - 需要跨时段电量处理

2. **复杂的分时电价**
   - 需要精确到每15分钟一个时段
   - 每个时段独立定价
   - 需要支持多套计费模型

3. **离线充电支持**
   - 支持二维码离线鉴权
   - 支持本地鉴权码验证
   - 需要设备流水号管理

## 六、代码中的使用

### 共享内存中的存储

两套协议的数据在共享内存中**独立存储**：

```cpp
// V2G数据
_sdk_event_executionStatus* v2gTrade = shm.getTradeRecord(0);
_sdk_service_remoteStart* v2gStart = shm.getRemoteStart(0);
_sdk_service_issue_feeModel* v2gFee = shm.getFeeModel();

// EVS数据
_evs_event_tradeInfo* evsTrade = shm.evs_getTradeRecord(0);
_evs_service_startCharge* evsStart = shm.evs_getRemoteStart(0);
_evs_service_issue_feeModel* evsFee = shm.evs_getFeeModel();
```

### 选择建议

根据项目需求选择：
- **如果只需要实时状态和简单计费** → 使用V2G
- **如果需要完整的交易结算和复杂计费** → 使用EVS
- **可以同时支持两套协议**，根据现场配置选择

## 总结

| 维度 | V2G协议 | EVS协议 |
|------|---------|---------|
| **设计目标** | 实时监控、状态上报 | 交易结算、计费管理 |
| **数据完整性** | 侧重实时数据 | 侧重结算数据 |
| **计费复杂度** | 简单（固定费率） | 复杂（时段定价） |
| **时段支持** | 最多14个（固定费率） | 最多96个（独立定价） |
| **鉴权支持** | 仅在线 | 在线+离线 |
| **适用场景** | 监控系统 | 计费系统 |
| **推荐使用** | 状态监测场景 | 商业化运营场景 |

**建议**：如果项目需要商业化运营和详细的账单，优先使用EVS协议。如果只需要实时监控和简单计费，V2G协议更简单。

