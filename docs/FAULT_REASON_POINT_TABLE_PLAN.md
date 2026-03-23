# 故障点表与停机原因复用模块技术方案

## 1. 目标

本方案用于统一以下三类能力，并提供给 `tcu_logic`、`tcu_comm`、`HMI` 等多个项目复用：

- 故障点位判定：某个协议点位/故障码是否按“故障”处理
- 停机原因归一化：将不同来源的原始原因值映射为统一 `reason`
- 中文文案查询：根据统一 `reason` 查询对应中文描述，供 HMI、日志、平台对接使用

同时，`tcu_logic` 在故障判定完成后，需要通过 MQTT 发布：

- `tcu/save/{gun}/event`

用于通知其他本地持久化进程保存故障事件。

约束补充：
- `tcu/save/{gun}/event` 当前仅用于记录“常态故障”，即 `pile yx.otherFault`、电表通信中断、平台通信中断、logic 状态超时这类非交易内故障
- 启动中的失败和充电中的异常停机，不通过该 Error 事件单独落故障记录，而是用于生成本次充电记录的统一 `reason`

## 2. 设计原则

### 2.1 不使用 ini 配置
故障点表不是运行参数，而是业务策略，直接固化在程序中。

优点：
- 避免现场配置漂移
- 版本可追踪，变更可审计
- HMI/comm/logic 可共享同一套规则，不会出现解释不一致

### 2.2 单独建立公共头文件和 cpp
不能把故障点表逻辑散落在 `tcu_logic` 内部。应抽成公共模块，供多个项目直接链接使用。

建议放置位置：

- `core/base/common/fault_reason_mapper.h`
- `core/base/common/fault_reason_mapper.cpp`

该模块属于“公共业务映射层”，不是某个进程私有实现。

### 2.3 统一 reason，避免多套口径
`充电记录 reason` 与 `HMI 结算页 stop_reason` 使用同一个统一字段：

- `reason`

不再维护两套不同编号体系。

## 3. 故障类型定义

故障类型不是“来源”，而是业务阶段分类。统一定义为：

### 2.4 Error 事件与充电记录 reason 分工
故障判定后分两条处理链：

- 常态故障：发布 `tcu/save/{gun}/event`，`type = Error`，用于本地故障持久化
- 启动失败故障、充电中失败：不单独走 Error 持久化事件，直接写入本次充电记录 `reason`

这样可以避免：
- 空闲态设备故障和交易故障混在一起
- 启动失败/停机失败被重复记录两次

### 3.1 常态故障
场景：
- `IDLE`
- `PREPARE`

来源包括：
- pile 的 `yx` 故障（仅使用 MQTT `yx` 中 `otherFault` 字段）
- 电表通信故障
- 平台通信故障
- 其他设备常态异常

特点：
- 尚未进入本次充电交易
- 用于设备状态告警/本地持久化
- 不一定产生交易停机记录

### 3.2 启动失败故障
场景：
- 已进入 `STARTING`

包括：
- 启动超时：进入 `STARTING` 后未收到 `start_complete`
- `start_complete` 明确返回启动失败，并携带失败原因

特点：
- 交易已尝试启动，但未真正进入 `CHARGING`
- `reason` 按启动失败故障记录

### 3.3 充电中失败
场景：
- 已进入 `CHARGING`

包括：
- 因故障停机
- `stop_complete.stopReason` 对应的充电中停机原因
- `stop_complete.bmsStopReason` / `bmsChargeFaultReason` / `bmsStopErrorReason` 对应的 BMS 中止原因

特点：
- 已进入充电过程
- 交易记录与结算页均使用统一 `reason`

## 4. 公共模块设计

## 4.1 建议的数据结构

```cpp
// BY ZF: 故障业务阶段分类
struct FaultTypeDef {
    enum Type {
        NONE = 0,
        STANDBY = 1,       // 常态故障
        START_FAIL = 2,    // 启动失败
        CHARGING_FAIL = 3  // 充电中失败
    };
};

// BY ZF: 点表规则项
struct FaultPointRule {
    bool treatAsFault;         // 是否按故障处理；false 表示按正常停机/仅记录
    int faultType;             // FaultTypeDef::Type
    int reason;                // 统一停机原因编码
    const char* message;       // 中文描述
};

// BY ZF: 判定输出结果
struct FaultJudgeResult {
    bool valid;                // 是否命中有效规则
    bool treatAsFault;         // 是否按故障处理
    int faultType;             // 故障类型
    int reason;                // 统一停机原因
    std::string source;        // 来源：pile_yx / pile_yc / start_complete / stop_complete / meter / platform / logic
    std::string pointKey;      // 点位键名
    std::string message;       // 中文描述

    FaultJudgeResult()
        : valid(false)
        , treatAsFault(false)
        , faultType(FaultTypeDef::NONE)
        , reason(0)
    {}
};
```

## 4.2 统一原因码建议

统一 `reason` 采用程序内部编码，不直接等同协议原始值。

示例：

```cpp
enum UnifiedStopReason {
    REASON_NORMAL_STOP = 0,

    REASON_START_TIMEOUT = 1001,
    REASON_START_COMPLETE_FAIL = 1002,

    REASON_STOP_COMPLETE_ABNORMAL = 2001,
    REASON_BMS_STOP = 2002,
    REASON_BMS_CHARGE_FAULT = 2003,
    REASON_BMS_STOP_ERROR = 2004,

    REASON_PILE_RUNTIME_FAULT = 3001,
    REASON_METER_COMM_FAULT = 3002,
    REASON_PLATFORM_COMM_FAULT = 3003,
    REASON_LOGIC_TIMEOUT_FAULT = 3004
};
```

说明：
- 1000 段：启动失败
- 2000 段：充电中失败
- 3000 段：常态/控制类故障

## 4.3 对外接口建议

建议在 `fault_reason_mapper.h` 暴露如下接口：

```cpp
// BY ZF: 查找启动失败点表
FaultJudgeResult JudgeStartFailPoint(const std::string& pointKey);

// BY ZF: 查找充电中失败点表
FaultJudgeResult JudgeChargingFailPoint(const std::string& pointKey);

// BY ZF: 查找常态故障点表
FaultJudgeResult JudgeStandbyFaultPoint(const std::string& pointKey);

// BY ZF: 根据统一 reason 查询中文描述
const char* QueryReasonText(int reason);

// BY ZF: 判断是否属于故障停机
bool IsFaultReason(int reason);
```

说明：
- `Judge*` 负责从“点位键”映射到统一结果
- `QueryReasonText(reason)` 供 `HMI` 和 `comm` 直接查中文
- `IsFaultReason(reason)` 供结算/记录逻辑快速判断

## 5. 点位键设计

所有故障点统一转换为 `pointKey` 后再查表，避免业务代码里到处写 `switch/case`。

统一命名规则：
- `standby_%04X`
- `start_%04X`
- `charging_%04X`

说明：
- 前缀表示业务阶段，不表示具体来源
- 后 4 位为大写十六进制编码
- `pointKey` 只做统一检索键，不直接承载中文语义

### 5.1 常态故障类
常态故障只来源于 `pile` 的 `yx`，不包含 `yc`。

来源：
- `pile yx.otherFault`
- `meter_comm_break`
- `platform_comm_break`
- `logic_state_timeout`

编码建议：
- `yx.otherFault` 按 16bit 位图拆位，bit 编号直接落到低位编码
- 电表、平台、logic 通信类故障直接分配固定编码

示例：
- `standby_0003`
- `standby_0101`：电表通信中断
- `standby_0102`：平台通信中断
- `standby_0103`：logic 状态超时

### 5.2 启动失败类
启动完成帧 `start_complete` 当前 MQTT 中：
- `successFlag`：十进制数值
- `chargeFailReason`：十进制数值

其中 `chargeFailReason` 的原始协议语义按十六进制定义，来源文件：
- `core/tcu_logic/doc/start_complete_failureReason.md`

处理规则：
- 先读取十进制数值 `chargeFailReason`
- 再格式化为 4 位十六进制编码
- 启动超时使用固定编码

示例：
- MQTT `chargeFailReason = 14` -> `start_000E`
- 启动超时 -> `start_F001`

### 5.3 充电中失败类
停止完成帧 `stop_complete` 当前 MQTT 中：
- `stopReason`：十进制数值
- `bmsStopReason`：十进制数值
- `bmsChargeFaultReason`：十进制数值
- `bmsStopErrorReason`：十进制数值

其中 `stopReason` 的原始协议语义按十六进制定义，来源文件：
- `core/tcu_logic/doc/charging_failureReason.md`

为避免不同来源编码冲突，统一按分段编码映射到 `charging_%04X`：
- `stopReason`：`0x0000 ~ 0x00FF`
- `bmsStopReason`：`0x1000 ~ 0x1FFF`
- `bmsChargeFaultReason`：`0x2000 ~ 0x20FF`
- `bmsStopErrorReason`：`0x3000 ~ 0x30FF`

示例：
- MQTT `stopReason = 1` -> `charging_0001`
- MQTT `bmsStopReason = 1` -> `charging_1001`
- MQTT `bmsChargeFaultReason = 3` -> `charging_2003`
- MQTT `bmsStopErrorReason = 2` -> `charging_3002`

命名要求：
- 必须唯一
- 必须稳定
- 不依赖中文描述

## 5.4 三类故障来源汇总

### 常态故障来源
- pile `yx.otherFault` 位图故障
- 电表通信中断
- 平台通信中断
- logic 自身状态超时

### 启动失败故障来源
- `STARTING` 阶段超时未收到 `start_complete`
- `start_complete.successFlag != 0`
- `start_complete.chargeFailReason`（十进制转十六进制编码后生成 `start_%04X`）

### 充电中失败故障来源
- `CHARGING` 阶段收到 `stop_complete`
- `stop_complete.stopReason`（映射到 `charging_%04X`）
- `stop_complete.bmsStopReason`（映射到 `charging_%04X`）
- `stop_complete.bmsChargeFaultReason`（映射到 `charging_%04X`）
- `stop_complete.bmsStopErrorReason`（映射到 `charging_%04X`）

说明：
- `pile_controller` MQTT 上送保持原始数值，不改为十六进制字符串
- `logic` 根据业务阶段和原始值生成 `standby_%04X` / `start_%04X` / `charging_%04X`
- 点表模块只接收标准化 `pointKey`，不直接依赖原始 MQTT 字段

## 5.5 logic 侧处理分工

### 常态故障处理
命中以下来源时：
- `pile yx.otherFault`
- `meter_comm_break`
- `platform_comm_break`
- `logic_state_timeout`

`logic` 需要：
- 生成统一 `pointKey`
- 通过点表模块得到统一 `reason` 和中文文案
- 发布 `tcu/save/{gun}/event`，`type = Error`

建议 MQTT 消息格式示例：

```json
{
  "ts": 1774000000000,
  "seq": 1,
  "source": "tcu_logic",
  "gun": 0,
  "type": "Error",
  "data": {
    "occurTime": "20260322153045",
    "faultType": 1,
    "pointKey": "standby_0003",
    "faultSource": "pile_yx",
    "faultMessage": "急停按钮动作故障",
    "rawValue": 8
  }
}
```

说明：
- `faultType=1` 表示常态故障
- `pointKey` 为点表检索键
- `rawValue` 保留原始值，便于排查

### 启动失败处理
命中以下来源时：
- `start_%04X`

`logic` 需要：
- 生成统一 `pointKey`
- 通过点表模块得到统一 `reason`
- 将该 `reason` 写入本次充电记录
- 不发布 `tcu/save/{gun}/event` Error 故障记录

### 充电中失败处理
命中以下来源时：
- `charging_%04X`

`logic` 需要：
- 生成统一 `pointKey`
- 通过点表模块得到统一 `reason`
- 将该 `reason` 写入本次充电记录
- 不发布 `tcu/save/{gun}/event` Error 故障记录

- `logic` 根据业务阶段和原始值生成 `standby_%04X` / `start_%04X` / `charging_%04X`
- 点表模块只接收标准化 `pointKey`，不直接依赖原始 MQTT 字段

## 6. 点表实现方式

## 6.1 使用 std::map
由于后续故障点位可能接近或超过 100 条，不建议继续使用数组遍历。

建议在 `fault_reason_mapper.cpp` 内使用：

```cpp
static const std::map<std::string, FaultPointRule> kStartFailRuleMap;
static const std::map<std::string, FaultPointRule> kChargingFailRuleMap;
static const std::map<std::string, FaultPointRule> kStandbyFaultRuleMap;
```

原因：
- 数量较多，按 key 查找更直接
- 维护成本低
- 适合老 GCC / Qt4.8.6 交叉编译环境
- 便于 HMI/comm/logic 共享

不建议优先使用 `unordered_map`：
- 在老工具链环境中稳定性与兼容性不如 `std::map`
- 当前性能需求不需要哈希表

## 6.2 构造方式

不建议把几百行初始化列表直接写在头文件里。推荐在 cpp 中用构造函数填表：

```cpp
static FaultPointRule MakeRule(bool treatAsFault, int faultType, int reason, const char* message)
{
    FaultPointRule r;
    r.treatAsFault = treatAsFault;
    r.faultType = faultType;
    r.reason = reason;
    r.message = message;
    return r;
}

static std::map<std::string, FaultPointRule> BuildStartFailRuleMap()
{
    std::map<std::string, FaultPointRule> m;
    m["start_timeout"] = MakeRule(true, FaultTypeDef::START_FAIL, REASON_START_TIMEOUT, "启动超时");
    m["start_complete_fail_01"] = MakeRule(true, FaultTypeDef::START_FAIL, REASON_START_COMPLETE_FAIL, "启动失败-原因01");
    return m;
}
```

优点：
- 易分组维护
- diff 清晰
- 后续新增规则不影响对外接口

## 7. tcu_logic 接入方案

## 7.1 触发入口
`tcu_logic` 中需要接入以下入口：

### 常态故障
- `deviceErr_on`
- 电表通信超时
- 平台通信超时
- 其他空闲/插枪阶段异常

### 启动失败故障
- `STARTING` 超时未收到 `start_complete`
- `start_complete.successFlag != 0`

### 充电中失败
- `CHARGING` 阶段收到 `stop_complete`
- 且其停止原因经点表判定为故障停机

## 7.2 处理流程

```mermaid
flowchart TD
    A[接收到事件或状态变化] --> B[转换为 pointKey]
    B --> C[按阶段选择点表]
    C --> D[调用 FaultReasonMapper 查询]
    D --> E{treatAsFault?}
    E -- 是 --> F[生成 Error 事件]
    E -- 是 --> G[写统一 reason]
    E -- 否 --> H[按正常停机处理]
    F --> I[发布 tcu/save/{gun}/event]
    G --> J[交易记录与结算界面共用]
```

## 7.3 save 事件发布格式

`tcu_logic` 新增发布：

- Topic：`tcu/save/{gun}/event`

Payload：

```json
{
  "ts": 1774000000000,
  "seq": 123,
  "source": "tcu_logic",
  "gun": 0,
  "type": "Error",
  "data": {
    "occurTime": "20260322153045",
    "faultType": 2,
    "reason": 1002,
    "faultSource": "start_complete",
    "pointKey": "start_complete_fail_01",
    "faultMessage": "启动失败-原因01"
  }
}
```

说明：
- `faultType`：1/2/3
- `reason`：统一停机原因
- `pointKey`：便于后续检索、统计、回放
- `faultMessage`：中文文案，供本地持久化和展示

## 8. HMI 接入方案

HMI 不再自己维护一套故障码翻译逻辑。

HMI 的处理方式：
- 根据 `reason` 调用公共模块 `QueryReasonText(reason)`
- 在结算界面直接显示中文原因
- 在故障详情页可使用 `pointKey + message` 展示更细信息

这样可以保证：
- HMI 展示文本
- logic 写入交易记录的 `reason`
- comm 回传或平台解释

三者口径一致。

## 9. tcu_comm 接入方案

`tcu_comm` 后续如需：
- 根据 `reason` 回平台中文文本
- 根据 `reason` 决定某些协议字段
- 输出日志/调试信息

同样直接调用公共模块：
- `QueryReasonText(reason)`
- `IsFaultReason(reason)`

避免重复维护一套“协议原因值 -> 中文”的私有逻辑。

## 10. 目录与文件建议

建议新增以下公共文件：

- `core/base/common/fault_reason_mapper.h`
- `core/base/common/fault_reason_mapper.cpp`

建议内容职责：

### `fault_reason_mapper.h`
- 公共结构体定义
- 统一原因码定义
- 公共查询函数声明

### `fault_reason_mapper.cpp`
- 三张故障点表构建
- `reason -> 中文` 映射表
- 查询与判断实现

## 11. 第一阶段落地范围

第一版先覆盖最关键路径：

### 启动失败
- `start_timeout`
- `start_complete_fail_xx`

### 充电中失败
- `stop_complete_reason_xx`
- `bms_stop_reason_xx`
- `bms_charge_fault_reason_xx`
- `bms_stop_error_reason_xx`

### 常态故障
- `deviceErr_on` 里高优先级点位
- 电表通信中断
- 平台通信中断

## 12. 第二阶段扩展

后续继续补齐：
- 全部 pile `yx/yc` 点位
- 全部 stop_complete 扩展 bit 位
- 全部平台/电表/logic 内部控制类故障
- 故障统计与本地数据库查询接口

## 13. 预期收益

该方案落地后，可以实现：

- logic / HMI / comm 三个项目共用同一套故障规则
- 停机原因统一，不再多口径
- 中文描述统一，不再多项目重复维护
- 后续新增故障点只需要改公共 cpp 点表，不改业务主流程

## 14. 实施顺序建议

1. 新增 `fault_reason_mapper.h/.cpp`
2. 先实现统一 `reason -> 中文` 查询
3. 接入 `tcu_logic` 的启动失败/停止完成/常态故障判断
4. 发布 `tcu/save/{gun}/event type=Error`
5. HMI 与 comm 改为直接用公共模块查中文

