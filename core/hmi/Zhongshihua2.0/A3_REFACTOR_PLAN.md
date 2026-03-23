# A3 运维中心重构方案

## 目标定位

`A3About` 不再作为单纯“关于页”，而是作为 HMI 的“运维中心 / 维护中心”。

保留现有 `QTabWidget` 多页签架构，重定义每个页签职责。

## 页签规划

### 1. 分时信息

保留原页签名“分时信息”。

#### 目标展示

- 横轴：时刻
- 纵轴：价格
- 以连续柱状图展示各时段价格
- 每段柱体由两层叠加组成：
  - 电费
  - 服务费
- 每段显示总价标签

#### 数据来源

- 开发机样例库：
  - `/Users/seear/embedded_codes/99_ForCodexs/2510_RefactorProject/core/hmi/Zhongshihua2.0/release/feemodel.db`
- 目标机实际路径：
  - `/mnt/nandflash/feemodel.db`

#### 已确认的数据库结构

表：`tbFeeModel`

字段：
- `feeModelId`
- `timeNum`
- `timeSeg`
- `segFlag`
- `chargeFee`
- `serviceFee`
- `timeStamp`

样例：

- `timeSeg = 0000;1200;1430;1535;...`
- `chargeFee = 2000;900;2000;...`
- `serviceFee = 1000;150;1000;...`
- `timeStamp = 2026-03-19 15:47:19`

#### 读取策略

- 每次进入或点击刷新时，从 `feemodel.db` 中读取 `timeStamp` 最新的一条计费模型
- 如果读到空数据库或无有效记录：
  - 不覆盖当前缓存
  - 保持上一次有效模型继续显示
- 如果读到新模型：
  - 更新缓存
  - 刷新图表显示

#### 待确认项

- `chargeFee/serviceFee` 的计价单位
  - 当前从样例看，很像“厘/千分之一元”或“分/万分之一元”这类整数存储
  - 这里需要业务口径确认后再决定图表显示保留几位小数

#### UI 实现建议

- 不建议继续使用大量 `QLabel` 静态排版
- 新增一个自绘控件，例如 `FeeModelChartWidget`
- 当前 `a3about.ui` 中原“分时信息”页保留为容器页，图表控件占主区域
- 辅助信息建议保留：
  - 当前模型编号
  - 更新时间
  - 当前时段
  - 图例：电费 / 服务费 / 总价

### 2. 设备信息

建议复用现有“版本/设备信息”类页签，但内容重组。

#### 信息区块

##### 软件版本信息

- `logic: xx.xx (日期)`
- `meter: xx.xx (日期)`
- `mon: xx.xx (日期)`
- 后续可扩展：
  - `comm`
  - `daemon`
  - `hmi`

##### 通信状态

- 平台通信状态：`0/1`
- 电表通信状态：`0/1`
- 主控通信状态：`0/1`

##### 设备身份

- `cdzNo`
- `cdzId`
- `macAddr`

##### 故障摘要

- 当前故障数量
- 当前活动故障名称
- 最近故障时间

#### 数据来源

- 主要来自 MQTT：
  - `/tcu/mon/0/event`
- 静态信息来自本地配置和启动缓存：
  - `cdzNo`
  - `cdzId`
  - `macAddr`

#### 运行态建议

- 在 `RuntimeWindow` 中新增独立的 `MonStatusData`
- 不建议把设备监控数据塞进 `GunUiData`

推荐字段：
- `logicVersion`
- `logicBuildDate`
- `meterVersion`
- `meterBuildDate`
- `monVersion`
- `monBuildDate`
- `platformComm`
- `meterComm`
- `controllerComm`
- `activeFaultCount`
- `latestFaultName`
- `latestFaultTime`
- `lastEventTime`

### 3. 故障信息

#### 目标展示

列表/表格方式展示故障记录。

列定义：
- 编号
- 枪号
- 开始时间
- 消除时间
- 故障名称

#### 数据来源

- `error.db`

#### 实现建议

- 不继续沿用旧页面中零散的故障 `QLabel`
- 重构为记录列表页
- 后续可加翻页或滚动

### 4. 充电记录

#### 目标展示

保留原设计风格，重点改数据来源。

功能：
- 筛选按钮：
  - A枪
  - B枪
  - 全部
- 记录按时间倒序显示

#### 数据来源

- `charge_record.db`

#### 实现建议

- 优先复用原有“充电记录汇总”页的主体布局
- 将旧静态字段替换为实际记录列表和筛选按钮逻辑

### 5. 外置存储

#### 目标

保持原页面设计和原来逻辑。

功能：
- 日志导出
- U 盘升级执行

#### 实现建议

- UI 基本不动
- 重点补执行逻辑与状态反馈

### 6. 参数配置

#### 目标

通过屏幕操作完成配置文件修改。

#### 当前策略

- 先保留页签占位
- 等配置项结构明确后再细化交互

## 实施顺序

### 第一阶段

1. 完成 `A3` 六个页签的职责映射
2. 重构“设备信息”页静态布局
3. 在“分时信息”页接入图表控件壳子和缓存机制

### 第二阶段

1. 接入 `/tcu/mon/0/event`
2. 完成设备信息页字段刷新
3. 完成 `feemodel.db` 最新模型读取与缓存更新

### 第三阶段

1. 接入 `error.db`
2. 接入 `charge_record.db`
3. 完成列表页和筛选逻辑

### 第四阶段

1. 补齐外置存储执行逻辑
2. 完成参数配置交互

## 需要用户后续补充的关键口径

- `/tcu/mon/0/event` 的字段定义
- `error.db` 的表结构
- `charge_record.db` 的表结构
- 参数配置页需要编辑的具体配置项

## 已确认的解析口径

### 分时信息金额口径

- `chargeFee/serviceFee` 中的整数值按 `元 * 1000` 存储
- 例如：
  - `2000 -> 2.000 元`
- 页面显示统一保留 3 位小数
- 转换规则：
  - `displayFee = rawValue / 1000.0`

### segFlag 理解

- `segFlag` 与 `timeSeg/chargeFee/serviceFee` 按序号一一对应
- 第 `i` 个 `segFlag` 对应第 `i` 段时段的计费数据
- 当前阶段先将其作为分段标识缓存
- 如果后续业务需要映射为“尖/峰/平/谷”，再补充字典映射

## 第一页设备信息草案

### 页面定位

用于展示设备当前软件构成、外部通信情况、设备身份和故障摘要。

### 页面布局

建议采用左右双栏布局。

#### 左栏：软件版本信息

布局形式：分组标题 + 多行条目。

建议字段：
- `logic`
- `meter`
- `mon`
- `comm`
- `daemon`
- `hmi`

每行格式建议：
- `logic    v1.2.3    2026-03-19`
- `meter    v1.0.8    2026-03-18`

建议控件结构：
- 分组标题：`软件版本信息`
- 6 行组件标签：
  - 组件名
  - 版本号
  - 日期

#### 右栏：通信状态信息

布局形式：分组标题 + 状态卡片/状态行。

建议字段：
- 平台通信状态：`0/1`
- 电表通信状态：`0/1`
- 主控通信状态：`0/1`

建议显示方式：
- 标签在左
- 状态值在右
- `1` 用绿色或高亮白色
- `0` 用红色或灰色

#### 底部附加信息

建议放在页面底部作为补充信息区：
- `cdzNo`
- `cdzId`
- `macAddr`
- 当前故障数
- 最近故障名称

### 视觉建议

- 左右两栏宽度接近 1:1
- 使用清晰的分组标题，不做过多装饰
- 不建议延续老 `A3` 页密集表格风格

### 建议数据结构

```cpp
struct ModuleVersionInfo {
    std::string name;
    std::string version;
    std::string buildDate;
};

struct MonStatusData {
    std::vector<ModuleVersionInfo> modules;
    int platformComm;
    int meterComm;
    int controllerComm;
    int activeFaultCount;
    std::string latestFaultName;
    std::string latestFaultTime;
    std::string lastEventTime;
};
```

### 建议控件命名

- `group_device_versions`
- `group_device_comm`
- `lblVersionLogic`
- `lblVersionMeter`
- `lblVersionMon`
- `lblVersionComm`
- `lblVersionDaemon`
- `lblVersionHmi`
- `lblCommPlatform`
- `lblCommMeter`
- `lblCommController`
- `lblDeviceCdzNo`
- `lblDeviceCdzId`
- `lblDeviceMac`
- `lblDeviceFaultCount`
- `lblDeviceLatestFault`

## 第二页分时信息草案

### 页面定位

用于展示当前最新计费模型的图形化分时价格。

### 页面布局

建议分为上下两层：

#### 顶部信息区

左侧显示模型基础信息：
- 计费模型 ID
- 模型更新时间

右侧显示当前时段信息：
- 当前时段起止
- 电费
- 服务费
- 总价

建议示例：
- `计费模型：MODEL001`
- `更新时间：2026-03-19 15:47:19`
- `当前时段：14:30 - 15:35`
- `电费：0.900 元`
- `服务费：0.150 元`
- `总价：1.050 元`

#### 下方图表区

用一个大图表区域展示整天分时价格。

图表规则：
- 横轴：各分段起始时间
- 纵轴：价格（元）
- 每段为叠加柱
  - 下层：电费
  - 上层：服务费
- 每段柱上方显示总价

图例建议放在图表右上：
- 蓝色：电费
- 绿色：服务费
- 文本：总价

### 当前时段识别规则

- 取当前系统时间，转换成 `HHmm`
- 在 `timeSeg` 数组中查找所属区间
- 区间规则建议：
  - `timeSeg[i] <= now < timeSeg[i+1]`
  - 最后一段为 `timeSeg[last] <= now < 2400`

### 数据读取与缓存建议

```cpp
struct FeeSegment {
    std::string segFlag;
    std::string startTime;   // 例如 1430
    std::string endTime;     // 例如 1535
    double chargeFee;        // 元
    double serviceFee;       // 元
};

struct FeeModelData {
    std::string feeModelId;
    std::string timeStamp;
    std::vector<FeeSegment> segments;
    int currentIndex;
    bool valid;
};
```

缓存规则：
- 启动时尝试读取本地缓存模型
- 点击刷新或进入页面时读取 `/mnt/nandflash/feemodel.db`
- 如果新读取结果为空或无有效段：
  - 保留旧缓存
- 如果读到有效新模型：
  - 替换缓存
  - 刷新图表

### 建议控件命名

- `lblFeeModelId`
- `lblFeeModelTime`
- `lblCurrentSeg`
- `lblCurrentChargeFee`
- `lblCurrentServiceFee`
- `lblCurrentTotalFee`
- `feeModelChart`

### 图表控件建议

新增自绘控件：

```cpp
class FeeModelChartWidget : public QWidget
```

职责：
- 接收 `FeeModelData`
- 计算最大纵轴值
- 绘制叠加柱
- 高亮当前时段
- 绘制总价标签和横轴刻度

## 建议的开发顺序

### 步骤 1

重构 `a3about.ui` 的前两个页签容器：
- 设备信息
- 分时信息

### 步骤 2

补充运行态数据结构：
- `MonStatusData`
- `FeeModelData`

### 步骤 3

实现 `FeeModelChartWidget`

### 步骤 4

实现：
- `/tcu/mon/0/event` 解析
- `/mnt/nandflash/feemodel.db` 最新模型读取
- 空库不覆盖缓存逻辑
