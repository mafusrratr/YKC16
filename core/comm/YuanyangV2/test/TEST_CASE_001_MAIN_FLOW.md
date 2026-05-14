# TEST_CASE_001_MAIN_FLOW

## 目的

验证 YuanyangV2 首版主链路：TCP 握手、扩展三遥、启停控制、计费模型 2.0、交易记录确认。

## 前置条件

- 运行配置使用 `doc/yuanyang_v2_comm_config.example.ini` 中的字段。
- 平台模拟器支持 `0x81/0x82` RSA/AES 握手，后续业务尾部按 AES-128-ECB PKCS 填充加解密。
- 内部 MQTT broker 可用，topic 前缀为 `tcu`。

## 用例

- 启动 `tcu_comm` 后，平台收到 `0x81` 请求 RSA 公钥，并返回当前 RSA 公钥。
- 平台收到 `0x82` 设备认证，确认 AES 密钥后返回 COT `0x04`，`tcu_comm` 进入在线状态并发送心跳。
- 平台下发 `0x03` 总召唤，`tcu_comm` 回复 `0x25`、`0x27`、`0x11`、`0x12`。
- 平台下发 `0x51` 计费模型 2.0，`tcu_comm` 保存 `FeeModel` 并用 COT `0x04` 确认。
- 平台下发 `0x41` 启动控制，`tcu_comm` 发布 `tcu/plat/{gun}/cmd` 的 `start_charge`，收到 `start_complete.successFlag=0` 后回 COT `0x04`。
- logic 发布 `update_record` 后，`tcu_comm` 上送 `0x42`；平台 COT `0x04` 确认后，`tcu_comm` 发布 `record_cfm` 并调用本地记录确认。

## 验收标准

- 远端执行 `build imx6ul Makefile.cross` 成功生成 `release/tcu_comm`。
- TCP 调试日志中业务帧除心跳和握手公钥请求外均可解密。
- MQTT 中能看到 `start_charge`、`record_cfm` 和平台在线事件。
