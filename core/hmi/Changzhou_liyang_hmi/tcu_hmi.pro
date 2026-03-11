# tcu_hmi qmake project (Qt4.8.6)
# BY ZF

TEMPLATE = app
TARGET = tcu_hmi
CONFIG += qt thread
CONFIG -= app_bundle

QT += core gui

# BY ZF: Qt4 下显式开启 C++11（仅写 CONFIG += c++11 不稳定）。
QMAKE_CXXFLAGS += -std=gnu++11
QMAKE_CFLAGS += -std=gnu99

# BY ZF: 让 qrencode 源码启用其自带 config.h（定义 __STATIC、VERSION 等）。
DEFINES += HAVE_CONFIG_H

# BY ZF: qrencode 库版本目录（可在 qmake 命令行覆盖）
isEmpty(QRPATH) {
    QRPATH = $$PWD/include/qrencode-3.4.4
}

INCLUDEPATH += $$PWD
INCLUDEPATH += $$PWD/..
INCLUDEPATH += $$PWD/../base
INCLUDEPATH += $$PWD/../base/common
INCLUDEPATH += $$PWD/../base/mqtt
INCLUDEPATH += $$PWD/../base/mqtt/include
INCLUDEPATH += $$PWD/../base/cjson/include

# BY ZF: qrencode 路径兼容（优先源码目录，找不到时允许外部 QRPATH 覆盖）。
exists($$QRPATH/qrencode.h) {
    INCLUDEPATH += $$QRPATH
    # BY ZF: 直接编译 qrencode-3.4.4 源码，避免目标机缺少 libqrencode。
    SOURCES += \
        $$QRPATH/bitstream.c \
        $$QRPATH/mask.c \
        $$QRPATH/mmask.c \
        $$QRPATH/mqrspec.c \
        $$QRPATH/qrencode.c \
        $$QRPATH/qrinput.c \
        $$QRPATH/qrspec.c \
        $$QRPATH/rscode.c \
        $$QRPATH/split.c
} else {
    message([tcu_hmi] qrencode source not found at $$QRPATH)
}

SOURCES += \
    main.cpp \
    hmi_window.cpp \
    ../base/common/config_manager_lite.cpp \
    ../base/mqtt/mqtt_client.cpp

HEADERS += \
    hmi_window.h \
    ../base/common/config_manager_lite.h \
    ../base/mqtt/mqtt_client.h

LIBS += -lpthread -lmosquitto -lcjson -lm
LIBS += -L$$PWD/../../extraLib

DESTDIR = release
OBJECTS_DIR = obj
MOC_DIR = obj/moc
RCC_DIR = obj/rcc
UI_DIR = obj/ui
