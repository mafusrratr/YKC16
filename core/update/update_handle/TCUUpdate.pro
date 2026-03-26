######################################################################
# TCU Update Handle - Qt 4.8.6 Project
######################################################################

QT += core gui xml

# CONFIG += c++11  # Qt 4.8.6 不支持 C++11

TARGET = tcu_update
TEMPLATE = app

# 设置输出目录
CONFIG(debug, debug|release) {
    DESTDIR = release/bin
    OBJECTS_DIR = release/obj
    MOC_DIR = release/obj
    RCC_DIR = release/obj
    UI_DIR = release/obj
} else {
    DESTDIR = release/bin
    OBJECTS_DIR = release/obj
    MOC_DIR = release/obj
    RCC_DIR = release/obj
    UI_DIR = release/obj
}

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    updateengine.cpp \
    scriptparser.cpp \
    backupmanager.cpp \
    updateexecutor.cpp \
    configmanager.cpp \
    logger.cpp \
    diskspacechecker.cpp \
    updatestatusmanager.cpp

HEADERS += \
    mainwindow.h \
    updateengine.h \
    scriptparser.h \
    backupmanager.h \
    updateexecutor.h \
    configmanager.h \
    logger.h \
    diskspacechecker.h \
    updatestatusmanager.h

FORMS += \
    mainwindow.ui

# Default rules for deployment.
# 注意：实际部署时，可执行文件在 release/bin/ 目录下
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

