# tcu_hmi

这个子目录是从原项目中提取出来的纯 UI 工程，用于在不依赖原始底层协议和设备库的前提下，单独保留和预览 HMI 界面。

当前保留并用于预览的，是充电流程所需的业务界面：

- `B1 Idle`
- `C6 Authorize`
- `E1 Charging`
- `F7 Checkout Ok`
- `A3 About`

其中 `A3 About` 页面中存在多个 `WNumEdit` 输入控件，因此对应的输入键盘界面也必须保留：

- `NumInputFDlg`
- `NumInputDlg`

## 当前确认的图片绑定关系

按当前保留页面，图片资源确认如下。

保留：

- `B1 Idle`
  - `zq.png`
  - `qt_zq.png`
  - `yq.png`
  - `qt_yq.png`
  - `gth.png`
  - `duihao.png`
  - `about.png`
- `C6 Authorize`
  - `hb.png`
  - `vin.png`
  - `card.png`
  - `ewm.png`
  - `ewm_2.png`
  - `qcq.png`
  - `about.png`
- `E1 Charging`
  - `charge_dl.png`
  - `charge_je.png`
  - `charge_ts.png`
  - `ts.png`
  - `back.png`
- `F7 Checkout Ok`
  - `f7_ok.png`
  - `charge_ts.png`
  - `ts.png`
  - `back.png`
- `A3 About`
  - `login.png`
  - `input.png`
  - `back.png`
- `NumInputFDlg`
  - `jianpan.png`
  - `jianpan_2.png`

不保留：

- `234.png`
- `stopred.png`
- `newstop.png`
- `btnok.png`
- `btnback.png`

## 当前已完成的工作

- 提取并清理了原工程中的主要 UI 页面到 `ui/`
- 提取并整理了页面依赖的图片资源到 `Resources/`
- 提取并复用了原工程样式表 `styles/cui.qss`
- 新建了独立入口程序，运行后可在全屏预览窗口中切换主要业务页面
- 已按当前保留页面梳理图片依赖，并剔除了不再需要的资源引用
- 去除了 `zk/`、`nbt/`、`libtcu`、线程、硬件控制、数据库、通信协议等业务依赖
- 用轻量兼容控件替代了原工程中的 `BaseDialog`、`WNumEdit`、`QRWidget`、`CCellWidget`
- 增加了 `uic` 静态页面工厂，避免依赖 `QtUiTools/QUiLoader`
- 增加了兼容头文件，适配原 `.ui` 中的自定义控件声明
- 调整了 qmake 输出目录：
  - 编译中间产物输出到 `obj/`
  - 最终 target 输出到 `release/`

## 当前工程的定位

这套工程现在是“纯 UI 预览壳”，目标是最大程度保留原始界面视觉和控件结构，而不是复现原项目完整业务。

当前保留的内容：

- 页面布局
- 图片资源
- qss 样式
- 基础控件层级
- 演示文本和假数据填充
- 充电流程相关业务界面
- `A3 About` 所依赖的输入键盘界面

当前未接入的内容：

- 充电流程状态机
- 枪口状态、鉴权结果、计费结果
- CAN/串口/网络通信
- 数据库读写
- 语音、背光、升级、设备控制
- 原项目中的全局结构体和业务线程

## 构建方式

推荐使用 qmake 构建，当前目录结构已按 qmake 输出做了整理：

```bash
cd tcu_hmi
qmake tcu_hmi.pro
make
./release/tcu_hmi
```

如果使用 CMake：

```bash
cd tcu_hmi
cmake -S . -B build
cmake --build build
./build/tcu_hmi
```

运行后会以 `800x480` 全屏方式预览页面，并通过覆盖层按钮切换当前业务界面。

## 目录说明

- `main.cpp`
  - 程序入口，加载 qss 并启动预览窗口
- `previewwindow.*`
  - 全屏预览、页面切换和演示数据填充逻辑
- `customwidgets.*`
  - 原项目自定义控件的轻量替身
- `uipages.*`
  - 各页面的静态包装类和页面工厂
- `ui/`
  - 当前仅保留仍然使用的 `.ui`
- `Resources/`
  - 页面依赖图片资源
- `styles/cui.qss`
  - 样式表
- `obj/`
  - qmake 编译中间产物目录
- `release/`
  - qmake 生成目标目录

## 后续如何接入真实业务

如果后续需要把这套 UI 接回真实业务，建议按下面方式做，而不是把原工程整包直接拷回：

1. 建立独立的数据适配层

- 新增一个 `adapter/` 或 `data/` 目录
- 在这一层里把原项目的全局状态、协议数据、数据库结果转换成页面需要的简单数据结构
- 不要让页面类直接依赖 `g_hmi_info`、`g_bcu_info` 这类全局对象

2. 给页面定义明确的输入接口

- 例如给页面补充 `setState(...)`、`setChargeInfo(...)`、`setAuthorizeResult(...)` 之类的方法
- 让页面只负责展示，不负责拉取数据

3. 用信号槽或控制器驱动页面切换

- 现在 `previewwindow.cpp` 只是预览容器
- 真正接入时可以新增 `controller/`，由控制器决定显示哪个页面、何时刷新页面数据

4. 分阶段替换演示数据

- 先替换 `applyDemoData(...)` 中的假数据
- 再把页面切换逻辑接到真实状态流
- 最后再恢复必要的业务交互按钮和动作

5. 保持 UI 层与底层解耦

- 业务层负责“产生状态”
- UI 层负责“显示状态”
- 避免再次把设备访问、线程控制、system 调用直接写回页面类

## 推荐接入顺序

建议优先接入当前保留的业务界面：

1. 待机页 `B1`
2. 鉴权页 `C6`
3. 充电中页 `E1`
4. 结算页 `F7`
5. 配置/关于页 `A3`

补充：

- `A3` 接入时需要同时保留 `NumInputFDlg/NumInputDlg`
- 这两个页面目前已保留，用于承接输入控件的依赖关系

## 说明

当前工程的重点是“UI 已独立可编译、可预览、资源闭环完整”。  
如果后续需要，我可以继续做两类工作：

- 把预览壳改成接近原项目流程的页面跳转 Demo
- 继续抽象一层真实数据接口，开始逐步接回业务状态
