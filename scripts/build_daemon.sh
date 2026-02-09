#!/bin/bash
# 交叉编译脚本
# BY ZF

echo "=== TCU Daemon Cross Compilation Script ==="

# 检查CROSS_COMPILE环境变量
if [ -z "$CROSS_COMPILE" ]; then
    echo "Setting default cross-compiler..."
    export CROSS_COMPILE=arm-linux-gnueabihf-
fi

export CC=${CROSS_COMPILE}gcc
export CXX=${CROSS_COMPILE}g++

echo "Cross-compilation environment:"
echo "  CROSS_COMPILE: $CROSS_COMPILE"
echo "  CC: $CC"
echo "  CXX: $CXX"

# 检查交叉编译器是否存在
if ! command -v $CXX &> /dev/null; then
    echo "Error: Cross-compiler $CXX not found!"
    echo "Please install arm-linux-gnueabihf-g++ or set CROSS_COMPILE environment variable"
    exit 1
fi

# 进入守护进程目录
cd core/daemon

echo ""
echo "=== Compiling Daemon Process ==="

# 使用交叉编译Makefile
make -f Makefile.cross clean
make -f Makefile.cross all

if [ $? -eq 0 ]; then
    echo "✅ Daemon compilation successful"
    
    # 检查生成的文件
    echo ""
    echo "Generated files:"
    ls -la tcu_daemon test_process
    
    # 检查文件类型
    echo ""
    echo "File types:"
    file tcu_daemon test_process
    
else
    echo "❌ Daemon compilation failed"
    exit 1
fi

echo ""
echo "=== Deployment Instructions ==="
echo "1. Copy files to target device:"
echo "   scp tcu_daemon root@<target_ip>:/usr/app/tcu/"
echo "   scp test_process root@<target_ip>:/usr/app/tcu/"
echo ""
echo "2. Copy config files:"
echo "   scp ../../config/project_a/daemon_target.ini root@<target_ip>:/usr/app/config/daemon.ini"
echo ""
echo "3. On target device, run:"
echo "   cd /usr/app/tcu"
echo "   ./tcu_daemon"
echo "   # 或者指定配置文件路径："
echo "   ./tcu_daemon /usr/app/config/daemon.ini"
echo ""
echo "=== Cross-compilation completed ==="
