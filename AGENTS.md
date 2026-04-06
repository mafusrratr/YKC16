# Repository Guidelines

## Project Structure & Module Organization
- `core/`：核心代码，包含进程与基础设施
  - `base/`：通用基础设施（配置、消息队列、日志、进程管理）
  - `pile_controller/`：主控通信进程（CAN/485/共享内存等）
  - `daemon/`、`logger/`：已完成的守护与日志进程
  - `meter/`、`charge_logic/`、`communication/`：待重构/开发模块
- `libv2gshm/`：共享内存库
- `config/`：配置文件（`config/project_a/` 为示例）
- `docs/`：架构与重构文档
- `core/pile_controller/CAN2CCU/test/`：当前 CAN2CCU 子项目测试程序

## Build, Test, and Development Commands
- `qmake cui.pro`：生成 Makefile（Qt 4.8.6）
- `make -C core/pile_controller/CAN2CCU`：当前 CAN2CCU 子项目本地构建
- 远端推荐构建命令：`build <目标设备类型> <构建文件>`，例如 `build imx6ul Makefile.cross`
- 目标设备类型可为 `imx6ul`，后续也可能为 `nuc980`、`jzq` 等环境
- 交叉编译示例：  
  `export CROSS_COMPILE=/opt/gcc-linaro-4.9-2016.02-x86_64_arm-linux-gnueabihf/bin/arm-linux-gnueabihf-`
- CAN2CCU 测试（示例）：  
  `make -C core/pile_controller/CAN2CCU/test`（或使用对应 `Makefile.cross`）

## Coding Style & Naming Conventions
- 语言：C/C++（Qt4 环境），配置读写使用 `QSettings`。
- 编码：C/C++ 源文件使用 **GBK**。
- 新增代码需添加注释 `// BY ZF`。
- 接口：仅在“多实现”场景使用接口，且放在进程内部目录。

## Testing Guidelines
- 当前测试为模块级可执行程序（`core/pile_controller/CAN2CCU/test/`）。
- 命名示例：`test_*`（如 `test_protocol.cpp`、`test_start_stop_charge.cpp`）。
- 建议先在无硬件环境下跑框架测试，再接入真实 CAN/设备联调。

## Commit & Pull Request Guidelines
- 仓库中未发现明确的提交格式约定（如有团队规范请补充）。
- PR 建议包含：改动范围、配置影响、测试方式（含命令与结果）。

## Configuration & Runtime Tips
- 运行环境默认目录：`/usr/app`，监控路径：`/usr/app/mon`。
- 主要配置入口：`config/project_a/config.ini`。

## Agent-Specific Instructions
- 所有回复需使用中文。
- 编译环境需通过远端 SSH：`ssh -p 2222 root@localhost`（密码：embedded），进入后切换到对应工作目录。
- 远端构建优先使用 `build <目标设备类型> <构建文件>`，例如 `build imx6ul Makefile.cross`，不要默认直接手写 `make`。
