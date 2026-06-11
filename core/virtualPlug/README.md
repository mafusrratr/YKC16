# virtualPlug 联调

## 架构

`virtual_plug_backend` 是 C++ 核，负责直接连接目标设备 MQTT broker：

```text
192.168.10.126:1883
```

Web 页面只连接本机 C++ 后端控制口：

```text
http://127.0.0.1:18080
```

浏览器不直接连接 MQTT broker，也不需要目标设备开启 WebSocket。

## 本机运行

```sh
make -C core/virtualPlug
core/virtualPlug/release/virtual_plug_backend core/virtualPlug/virtual_plug.ini
```

后台运行：

```sh
cd core/virtualPlug
./release/virtual_plug_backend -d virtual_plug.ini
```

然后打开：

```text
core/virtualPlug/web/index.html
```

页面中的后端地址保持 `127.0.0.1:18080`。

## 远端构建

按仓库约定进入远端环境后执行：

```sh
build imx6ul Makefile.cross
```

或在当前目录使用对应构建脚本构建 `Makefile.cross`。

## API 验证

```sh
curl 'http://127.0.0.1:18080/api/status'
curl 'http://127.0.0.1:18080/api/action?gun=0&action=plug'
curl 'http://127.0.0.1:18080/api/action?gun=0&action=startComplete'
curl 'http://127.0.0.1:18080/api/action?gun=0&action=stopComplete'
```
