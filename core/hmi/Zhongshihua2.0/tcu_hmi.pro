QT += core gui

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11
# BY ZF: Qt4/qmake 交叉环境下仅写 CONFIG += c++11 不稳定，显式补编译参数。
QMAKE_CXXFLAGS += -std=gnu++11
QMAKE_CFLAGS += -std=gnu99
DEFINES += HAVE_CONFIG_H
TEMPLATE = app
TARGET = tcu_hmi
DESTDIR = release
OBJECTS_DIR = obj
MOC_DIR = obj
RCC_DIR = obj
UI_DIR = obj

isEmpty(QRPATH) {
    QRPATH = $$PWD/../Changzhou_liyang_hmi/include/qrencode-3.4.4
}

SOURCES += \
    ../../base/common/config_manager_lite.cpp \
    ../../base/mqtt/mqtt_client.cpp \
    customwidgets.cpp \
    main.cpp \
    previewwindow.cpp \
    runtime_window.cpp \
    uipages.cpp

exists($$QRPATH/qrencode.h) {
    INCLUDEPATH += $$QRPATH
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
}

HEADERS += \
    ../../base/common/config_manager_lite.h \
    ../../base/mqtt/mqtt_client.h \
    customwidgets.h \
    previewwindow.h \
    runtime_window.h \
    uipages.h

INCLUDEPATH += \
    ../../base \
    ../../base/mqtt/include \
    ../../base/cjson/include \
    ../../logger/sql

# BY ZF: 交叉环境优先使用仓库内 extraLib 的已编译库，避免目标机缺少开发链接名。
LIBS += -L$$PWD/../../../extraLib/imx6ul
LIBS += -L$$PWD/../../../extraLib
exists($$PWD/../../../extraLib/imx6ul/libmosquitto.so.1) {
    LIBS += $$PWD/../../../extraLib/imx6ul/libmosquitto.so.1
} else {
    LIBS += -lmosquitto
}
exists($$PWD/../../../extraLib/imx6ul/libcjson.so.1.7.17) {
    LIBS += $$PWD/../../../extraLib/imx6ul/libcjson.so.1.7.17
} else {
    LIBS += -lcjson
}
exists($$PWD/../../../extraLib/imx6ul/libsqlite3.so) {
    LIBS += $$PWD/../../../extraLib/imx6ul/libsqlite3.so
} else {
    LIBS += -lsqlite3
}
LIBS += -lpthread -lm

FORMS += \
    ui/a3about.ui \
    ui/b1idle.ui \
    ui/c6flushcad.ui \
    ui/e1chargeinfo.ui \
    ui/f7checkoutok.ui \
    ui/numinputdlg.ui \
    ui/numinputfdlg.ui

RESOURCES += tcu_hmi.qrc
