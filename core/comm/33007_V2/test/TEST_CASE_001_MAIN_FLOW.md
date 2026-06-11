# TEST_CASE_001_MAIN_FLOW

## 目的

验证 33007 V2 主链路：TCP 建连、心跳、总召/扩展三遥、启停控制、交易记录确认。

## 前置条件

- 运行配置参考 `doc/33007_v2_comm_config.example.ini`，部署到 `/usr/app/config/tcu_comm.ini`。
- 平台模拟器按 NBT33007-2013 执行细则收发明文 ASDU。
- 内部 MQTT broker 可用，topic 前缀为 `tcu`。

## 用例

- 启动 `tcu_comm` 并完成 TCP 建连后，进入在线状态并按配置周期发送心跳 `0x43` 控制域报文。
- 平台下发 `0x03` 总召唤，`tcu_comm` 回复连续 VSQ 的 `0x27`、`0x25`、`0x10`、`0x11`、`0x12` 明文帧。
- 平台下发标准 `0x41` 启动控制，命令体到 16 位卡号结束；`tcu_comm` 发布 `tcu/plat/{gun}/cmd` 的 `start_charge`，收到 `start_complete.successFlag=0` 后按原命令体回 COT `0x04`。
- logic 发布 `update_record` 后，`tcu_comm` 上送 `0x42`；平台 COT `0x04` 确认后，`tcu_comm` 发布 `record_cfm` 并调用本地记录确认。

## 验收标准

- 远端执行 `build imx6ul Makefile.cross` 成功生成 `release/tcu_comm`。
- TCP 调试日志中业务 ASDU 均为明文。
- MQTT 中能看到 `start_charge`、`record_cfm` 和平台在线事件。
