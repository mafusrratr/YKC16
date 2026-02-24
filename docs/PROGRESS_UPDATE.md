# 项目进度更新（2026-02-10）

## 已完成
### pile_controller（CAN 侧）
- 协议与控制流程闭环：心跳、版本校验、下发参数、启停命令、状态解析。
- MQTT 接入：cmd/event/data 三通道，QoS 与 topic 规范更新。
- 事件上送：start/stop response & complete 全量上送，无去重。
- 重发机制：启动/停止命令 250ms 重发，5s 超时；完成应答单次发送。
- 通信异常处理：心跳超时重置参数流程、停止重发、清空长帧上下文。
- 线程安全：协议层访问加锁。
- 长帧超时：2 秒超时重置上下文。
- 文档与 UML 更新完成，UML 文件迁移到 `core/pile_controller/doc/`。
- mqtt/cjson 迁移到 `core/base/` 作为通用依赖。

### MQTT 规范
- `core/pile_controller/doc/MQTT_COMM_PROTOCOL.md`：cmd QoS2、event QoS2，data QoS0。
- 统一 topic 规划：`tcu/pile/{gun}/cmd|event|data`。

### 目录与命名规范调整
- `core/charge_logic` → `core/tcu_logic`，进程名 `tcu_logic`。
- 新增 `config/tcu_logic.ini` 示例配置。
- daemon 配置中 `tcu_charge_logic` → `tcu_logic`。
- 枚举名统一：`PROC_CHARGE_LOGIC` → `PROC_TCU_LOGIC`。

## 进行中
### tcu_logic（骨架已搭建）
- 进程入口：`core/tcu_logic/main.cpp`。
- 进程类：`core/tcu_logic/charge_logic_process.h/.cpp`。
- Makefile：本地与交叉编译已生成。
- MQTT 订阅/发布已接入：logic/cmd、plat/cmd、pile/event、pile/data、meter/data。
- 状态机草案完成（IDLE/PREPARE/STARTING/CHARGING/STOPPING/STOPPED/ERROR）。

## 待完成（下一步）
### tcu_logic 逻辑实现
- 具体状态流转与业务规则细化（错误处理、策略、计费）。
- feeData 独立 topic 产出逻辑。
- 与 HMI/平台/电表的消息结构补齐。

### 配置与运行
- 在目标设备的 `/usr/app/config/` 下放置 `tcu_logic.ini`。
- 若需同步枪数，配置 `pile_config_path=/usr/app/config/pile_controller.ini`。
