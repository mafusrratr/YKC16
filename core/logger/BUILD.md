# Logger 模块编译说明

详细的编译步骤、依赖说明以及交叉编译注意事项请参阅 `doc/BUILD.md`。

```bash
# 查看完整文档
less doc/BUILD.md
```

常用命令：

```bash
make                 # 本地语法检查编译（输出至 obj/、release/）
make -f Makefile.cross  # 交叉编译（用于目标设备）
make docs            # 整理文档到 doc/ 目录
make install         # 安装到 /usr/app/tcu/
```

