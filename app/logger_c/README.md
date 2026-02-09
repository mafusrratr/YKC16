// BY ZF: 该目录提供给纯C业务的日志封装说明
# Logger C 封装使用说明

本目录包含一个针对日志系统的轻量级 C 接口包装，已经打包了所需的 C++ 依赖，方便纯 C 模块直接集成。
通过提供的 Makefile，可以快速生成动态库或静态库：`lib/liblogger_c.so`、`lib/liblogger_c.a`。根据部署需求选择其一即可。

## 目录结构

- `log_sender_c.h/.cpp` — C 接口声明与实现
- `cpp/` — C++ 原始实现，仅供内部引用
  - `logger_types.h` — 日志、交易、计费结构定义
  - `message_queue.h/.cpp` — 消息队列封装
  - `log_sender.h/.cpp` — 原始日志发送器实现
- `README.md` — 本说明文件

## 集成步骤

1. **加入源码**  
   将本目录复制到业务工程中，执行 `make`（或指定交叉编译器，如 `make CXX=arm-linux-gnueabihf-g++`），即可生成 `lib/liblogger_c.so` 与 `lib/liblogger_c.a`。  
   - 若希望最小化部署文件，仅把 `log_sender_c.h` 和 `lib/liblogger_c.a` 编译进业务程序即可。  
   - 若希望动态更新日志模块，可部署 `liblogger_c.so` 并在业务程序中按需链接。

2. **编译方式**  
   默认使用本地 `g++`；若在目标设备交叉编译，请覆盖 `CXX`。构建完成后得到的 `.so` 或 `.a` 可按需选择，业务模块无需关心内部 C++ 实现。

3. **C 调用示例**

```c
// BY ZF: C 侧调用示例
#include \"log_sender_c.h\"

int main(void) {
    LogSenderHandle sender = log_sender_create(\"charger_ctrl\");
    if (!sender) {
        return -1;
    }

    log_sender_info(sender, \"业务启动\", \"模块就绪\");
    log_sender_fatal(sender, \"通信终止\", \"err=EPIPE\");

    log_sender_destroy(sender);
    return 0;
}
```

4. **运行环境**  
   确保目标设备上已有常驻的 `tcu_logger` 进程，并共享同一份消息队列配置；否则日志无法被消费。

## 注意事项

- C 接口函数均在内部做空指针防护，但业务仍应保证在 `log_sender_destroy` 后不再使用旧句柄。
- 若需扩展为 `TradeRecord` 或 `FeeModel` 封装，可参考现有实现模式，在包装层补充对应函数。
- 如果选择动态库方式，请将 `liblogger_c.so` 放入系统可搜索到的位置，或在运行前导出 `LD_LIBRARY_PATH`。  
- 如果选择静态库方式，请在业务侧链接 `liblogger_c.a`，就不需要在目标设备额外部署 `.so`。


