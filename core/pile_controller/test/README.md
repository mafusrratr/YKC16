# PileController 测试

本目录为 `pile_controller` 模块的测试程序。

## 测试程序

| 程序 | 说明 |
|------|------|
| **test_protocol** | 协议层：初始化、CAN ID、编解码接口 |
| **test_can_controller** | 控制器：配置解析、设备打开、命令接口、多枪 |
| **test_can_communication** | CAN 收发：原始帧发送/接收，需 can0 |
| **test_business_frame_from_can** | 从 CAN 接收并解析业务帧（如 YX22） |
| **test_start_stop_charge** | 启停流程：配置(0x07/0x08/0x09/0x0A) → 插枪 → 启动 → 完成确认 → 充电 5 分钟 → 停止 |

## 编译与运行

```bash
# 本地
cd test && make
./release/test_start_stop_charge   # 示例：启停流程，需 CAN 设备

# 交叉编译
make -f Makefile.cross
```

**test_start_stop_charge** 可选配置参数，默认：
`device_path=can0,baudrate=250000,bcu_addr=0x8A,cdz_addr=0xE0,gun_no=0,packet_timeout=5,retry_count=3`

## 环境

- 需 CAN 设备的测试：配置 SocketCAN（如 `ip link set can0 type can bitrate 250000 && ip link set up can0`）
- 交叉编译目标：iMX6UL（Makefile.cross）
