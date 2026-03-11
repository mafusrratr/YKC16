# tcu_hmi qmake project for Zhongshihua V2
# BY ZF

TEMPLATE = app
TARGET = tcu_hmi
CONFIG += qt thread
CONFIG -= app_bundle

QT += core gui

QMAKE_CXXFLAGS += -std=gnu++11
QMAKE_CFLAGS += -std=gnu99

INCLUDEPATH += $$PWD/src
INCLUDEPATH += $$PWD/..
INCLUDEPATH += $$PWD/../../base
INCLUDEPATH += $$PWD/../../base/common
INCLUDEPATH += $$PWD/../../base/mqtt
INCLUDEPATH += $$PWD/../../base/mqtt/include
INCLUDEPATH += $$PWD/../../base/cjson/include

SOURCES += \
    src/main.cpp \
    src/app_shell.cpp \
    src/page/state_page.cpp \
    src/page/detail_page.cpp \
    ../../base/common/config_manager_lite.cpp \
    ../../base/mqtt/mqtt_client.cpp

HEADERS += \
    src/app_shell.h \
    src/page/state_page.h \
    src/page/detail_page.h \
    ../../base/common/config_manager_lite.h \
    ../../base/mqtt/mqtt_client.h

LIBS += -L$$PWD/../../../extraLib/imx6ul
LIBS += -L$$PWD/../../../extraLib
LIBS += -lpthread -lmosquitto -lcjson -lm

DESTDIR = release
OBJECTS_DIR = obj
MOC_DIR = obj/moc
RCC_DIR = obj/rcc
UI_DIR = obj/ui
