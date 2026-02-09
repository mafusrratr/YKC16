# TCU充电桩计费单元项目文件
# BY ZF
# 低Qt耦合版本

TEMPLATE = app
TARGET = tcu_lite
VERSION = 1.0.0

CONFIG += console
CONFIG -= app_bundle

# C++11特性
CONFIG += c++11

# 输出目录
DESTDIR = bin
OBJECTS_DIR = obj

# 源文件目录
INCLUDEPATH += core
INCLUDEPATH += core/interfaces
INCLUDEPATH += core/base
INCLUDEPATH += core/base/common
INCLUDEPATH += core/base/logger
INCLUDEPATH += core/base/process
INCLUDEPATH += core/test
INCLUDEPATH += libv2gshm/cshm
INCLUDEPATH += $$PWD

# 头文件
HEADERS += \
    core/interfaces/iprocess.h \
    core/interfaces/icharge_logic.h \
    core/interfaces/icommunication.h \
    core/interfaces/imeter.h \
    core/interfaces/ilogger.h \
    core/interfaces/ipile_controller.h \
    core/base/common/message_queue.h \
    core/base/common/config_manager.h \
    core/base/process/base_process.h \
    core/base/daemon_process.h \
    core/test/test_process.h

# 源文件
SOURCES += \
    core/base/common/message_queue.cpp \
    core/base/common/config_manager.cpp \
    core/base/process/base_process.cpp \
    core/base/daemon_process.cpp \
    core/test/test_process.cpp \
    test_main.cpp

# 库文件
LIBS += -L$$PWD/libv2gshm/libcshm -levsshm

# 交叉编译配置
# NUC980交叉编译工具链
linux-arm-gnueabihf-g++*|linux-arm-gnueabihf-gcc* {
    QMAKE_CC = arm-linux-gnueabihf-gcc
    QMAKE_CXX = arm-linux-gnueabihf-g++
    QMAKE_LINK = arm-linux-gnueabihf-g++
    
    # 动态链接（用户偏好）
    QMAKE_LFLAGS += -Wl,-rpath,/usr/lib
}

# 安装配置
target.path = /usr/app/mon
INSTALLS += target

# 依赖共享内存库
PRE_TARGETDEPS += $$PWD/libv2gshm/libcshm/libevsshm.so

