# Zhongshihua V2 HMI 基础流程框架说明

## 1. 本次落地范围

本次先完成基础充电业务流程框架，详情页仅预留入口与占位，不实现详情内容。

状态页面包括：

- IDLE
- PREPARE
- STARTING
- CHARGING
- STOPPING
- STOPPED
- DETAIL(预留)

## 2. 代码结构

- `src/main.cpp`：程序入口
- `src/app_shell.h/.cpp`：页面路由、MQTT 接入、状态驱动
- `src/page/state_page.h/.cpp`：基础状态页组件（统一布局）
- `src/page/detail_page.h/.cpp`：详情页占位
- `tcu_hmi.pro`：qmake 构建文件

## 3. MQTT 输入与状态驱动

`AppShell` 当前订阅：

- `tcu/logic/+/event`：接收 `state_change`，驱动状态页切换
- `tcu/logic/+/feeData`：接收金额、电量、充电时长
- `tcu/pile/+/data`：接收 YC 电压、电流并计算功率

当前基础框架按 `gun=0` 接入（单枪），后续可扩展多枪并行页面。

## 4. 页面行为

- 运行模式固定全屏（800x480）
- 刷新周期 500ms
- 右上角按钮：
  - 正常状态页：`详情(预留)`
  - 详情页：`返回`

## 5. 详情页预留说明

`DetailPage` 当前只保留占位文案，后续将承载：

1. 计费模型
2. 调试信息
3. 充电记录
4. 故障记录
5. 外置存储

## 6. 后续建议

下一阶段在不变更主流程页面的前提下，逐步填充 `DetailPage` 的 5 个子模块。
