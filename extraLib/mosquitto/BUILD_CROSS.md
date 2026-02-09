# Mosquitto 交叉编译说明（无 TLS）

本目录提供顶层 Makefile，用于先编译 cJSON，再编译 mosquitto 动态库。
请在远端交叉编译环境中执行。

## 1. 准备源码包
放置到 `extraLib/mosquitto/` 目录：
- `mosquitto-2.1.1.tar.gz`
- `cJSON-1.7.17.tar.gz`

## 2. 执行编译
```bash
cd extraLib/mosquitto
make CROSS_COMPILE=arm-linux-gnueabihf-
```

## 3. 产物
编译完成后输出：
- `extraLib/mosquitto/install/cjson/`（cJSON 头文件与库）
- `extraLib/mosquitto/install/mosquitto/`（libmosquitto.so 与头文件）

## 4. 说明
- 已默认关闭 TLS（`WITH_TLS=no`）
- 默认关闭文档与 SRV/Websocket 支持
