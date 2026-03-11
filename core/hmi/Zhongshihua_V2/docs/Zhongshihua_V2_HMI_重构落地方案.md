# 中石化 V2 HMI 重构落地方案（UI 划分/页面划分）

## 1. 文档目的

本文用于指导 `core/hmi/Zhongshihua_V2` 的界面重构落地，目标是：

- 复用旧项目 `docs/zsh_3018_jifeimoxing` 的视觉资源和成熟 UI 布局；
- 剥离旧业务耦合（全局变量、旧通信线程、旧协议解析）；
- 对接当前重构框架（MQTT/共享数据/新进程架构）；
- 统一编码为 UTF-8，消除 GBK 运行时依赖。

---

## 2. 现状问题与改造必要性

旧项目存在以下典型问题：

1. **UI 与业务强耦合**  
   页面直接访问 `g_hmi_info/g_bcu_info`，且包含协议处理和状态机逻辑。

2. **页面职责过大**  
   `a3about.cpp` 超大文件，混合了计费模型、记录查询、升级、调试等功能。

3. **编码体系老旧**  
   `main.cpp` 强制 `QTextCodec` 为 GBK，中文字面量与多处 `fromLocal8Bit` 混用，迁移风险高。

4. **刷新策略不可控**  
   大量 `timerEvent` 轮询，页面更新粒度粗，业务事件与 UI 更新边界不清晰。

---

## 3. 重构目标（本阶段）

### 3.1 功能目标

- 保留并重构旧 `about` 页能力，形成**详情页**，包含以下子模块：  
  - 计费模型
  - 调试信息
  - 充电记录
  - 故障记录
  - 外置存储（日志导出/文件浏览）
- 页面仅做展示与操作入口，不在页面内做协议解析。

### 3.2 架构目标

- UI 层、数据适配层、数据模型层分离；
- 页面只接受规范化数据结构；
- 业务数据由 adapter 从 MQTT/共享区读取并转换。

### 3.3 编码目标

- 新模块源码全部 UTF-8；
- 不再依赖 `QTextCodec::setCodecForLocale("GBK")`；
- 旧 GBK 数据仅在 adapter 单点做兼容转换。

---

## 4. 重构后目录与模块划分

建议新建（或按现有工程落位）：

```text
core/hmi/Zhongshihua_V2/
  ├── src/
  │   ├── main.cpp
  │   ├── app_shell.h/.cpp
  │   ├── model/
  │   │   ├── fee_model_data.h
  │   │   ├── charge_record_data.h
  │   │   ├── fault_record_data.h
  │   │   └── debug_info_data.h
  │   ├── adapter/
  │   │   ├── fee_model_adapter.h/.cpp
  │   │   ├── charge_record_adapter.h/.cpp
  │   │   ├── fault_record_adapter.h/.cpp
  │   │   └── debug_info_adapter.h/.cpp
  │   ├── page/
  │   │   ├── page_idle.h/.cpp
  │   │   ├── page_charge.h/.cpp
  │   │   ├── page_finish.h/.cpp
  │   │   └── page_detail.h/.cpp
  │   └── widget/
  │       ├── fee_segment_list.h/.cpp
  │       ├── record_list.h/.cpp
  │       └── storage_browser.h/.cpp
  ├── ui/
  │   ├── page_idle.ui
  │   ├── page_charge.ui
  │   ├── page_finish.ui
  │   └── page_detail.ui
  ├── resources/
  │   └── (复用旧 Resources 图片)
  └── docs/
      └── 本文档
```

---

## 5. UI 划分（层次划分）

## 5.1 Shell 层（AppShell）

- 职责：主窗口、全屏、页面切换、全局样式加载；
- 不处理业务数据；
- 仅根据页面状态机切换子页面。

## 5.2 Page 层（页面层）

每个页面只做“显示 + 用户交互事件抛出”：

- `PageIdle`：待机展示（桩号、枪号、连接提示）；
- `PageCharge`：充电中展示（电压、电流、功率、电量、金额）；
- `PageFinish`：结束展示（总电量、总金额、提示文案）；
- `PageDetail`：详情页（计费模型/调试信息/充电记录/故障记录/外置存储）。

## 5.3 Adapter 层（数据适配层）

- 从 MQTT 或共享内存接收原始数据；
- 做字段映射、单位换算、有效性校验；
- 生成页面可直接消费的 ViewData；
- 向页面发出“数据变更事件”。

## 5.4 Model 层（数据模型层）

- 定义纯数据结构，不依赖 Qt UI；
- 例如：
  - `FeeModelViewData`
  - `ChargeRecordViewData`
  - `FaultRecordViewData`
  - `DebugInfoViewData`

---

## 6. 页面划分（业务页面与路由）

建议页面路由（单枪模式）：

1. `IDLE` -> `PageIdle`
2. `PREPARE/STARTING` -> `PageIdle`（可加“启动中”状态提示）
3. `CHARGING` -> `PageCharge`
4. `STOPPING/STOPPED` -> `PageFinish`
5. “设置/维护入口” -> `PageDetail`（管理员或调试入口）

> 说明：详情页从主充电流程解耦，不占用主流程，仅作为维护入口。

---

## 7. 详情页（PageDetail）详细设计

## 7.1 页面结构（Tab 划分）

`PageDetail` 对齐旧 `about` 功能，划分 5 个 Tab：

1. **计费模型**：模型ID、费率价格、48槽压缩时段；
2. **调试信息**：通讯状态、关键配置、运行状态；
3. **充电记录**：分页查询、基础汇总、导出入口；
4. **故障记录**：故障码、时间、分页浏览；
5. **外置存储**：存储介质扫描、文件浏览、日志导出。

## 7.2 数据输入模型

建议由 adapter 输出到 `PageDetail` 的聚合数据：

- `FeeModelViewData`
- `DebugInfoViewData`
- `QVector<ChargeRecordViewData>`
- `QVector<FaultRecordViewData>`
- `StorageViewData`（挂载点、文件列表、可导出状态）

## 7.3 计费模型子模块规则

- 输入：48 个槽位，每槽 30 分钟；
- 当费率变化时切段；
- 生成连续段列表：`startHHmm-endHHmm + rateNo`；
- 全量异常（空、越界）时显示“计费模型数据异常”。

## 7.4 记录类子模块规则

- 充电记录/故障记录统一分页组件；
- 页码切换只触发数据请求，不在 UI 侧做全量缓存拼装；
- 导出操作仅发请求事件，具体文件生成由后台模块处理。

---

## 8. 与旧工程的映射关系

| 旧实现 | 新实现 |
|---|---|
| `a3about.ui` 多 tab 混合 | 重构为独立 `page_detail.ui`（5 个子 tab） |
| `a3about.cpp` 全局变量直读 | 由多个 adapter 输出规范化 ViewData |
| `QTextCodec(GBK)` | UTF-8 源码 + 单点兼容转码 |
| `timerEvent` 轮询刷新 | 事件驱动刷新（数据变更即更新） |
| 页面内协议细节/文件操作细节 | 细节下沉到 comm/logic/logger，UI 仅消费结果与触发请求 |

---

## 9. 编码迁移策略（GBK -> UTF-8）

1. 新模块文件统一 UTF-8 创建；
2. 不在新 `main.cpp` 中设置 GBK `QTextCodec`；
3. 旧数据源若仍为本地编码，adapter 中统一转换；
4. UI 文本全部使用 UTF-8 字面量；
5. 联调阶段重点验证：中文标签、金额单位、时间文本不乱码。

---

## 10. 落地计划与交付件

## 10.1 Phase-1（详情页骨架）

- 交付：
  - `page_detail.ui`
  - `page_detail.h/.cpp`
  - 5 个子 tab 的静态示例数据

## 10.2 Phase-2（数据接入）

- 交付：
  - `*_data.h` 与 `*_adapter.h/.cpp`（计费模型/调试/记录/存储）
  - 页面动态刷新（各 tab 数据实时反映）

## 10.3 Phase-3（主流程整合）

- 交付：
  - `app_shell` 页面路由
  - 与主充电流程页面联动
  - 编码与显示回归验证报告

---

## 11. 验收标准

- 页面可在 UTF-8 环境下稳定显示中文；
- 详情页 5 个子模块均可独立刷新；
- 计费模型显示与当前模型一致；
- 页面不依赖 `g_*` 全局变量；
- UI 无协议解析、无旧通信线程调用；
- 记录与存储操作有明确成功/失败提示。
