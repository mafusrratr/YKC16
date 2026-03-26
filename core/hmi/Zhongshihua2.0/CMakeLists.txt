cmake_minimum_required(VERSION 3.16)
project(tcu_hmi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets)

add_executable(tcu_hmi
    ../../base/common/config_manager_lite.cpp
    ../../base/common/config_manager_lite.h
    ../../base/common/fault_reason_mapper.cpp
    ../../base/common/fault_reason_mapper.h
    ../../base/common/message_queue.cpp
    ../../base/common/message_queue.h
    ../../base/mqtt/mqtt_client.cpp
    ../../base/mqtt/mqtt_client.h
    customwidgets.cpp
    customwidgets.h
    exportprogressdlg.cpp
    exportprogressdlg.h
    main.cpp
    previewwindow.cpp
    previewwindow.h
    runtime_window.cpp
    runtime_window.h
    tcu_hmi.qrc
    uipages.cpp
    uipages.h
    ui/a3about.ui
    ui/b1idle.ui
    ui/c6flushcad.ui
    ui/e1chargeinfo.ui
    ui/f7checkoutok.ui
    ui/numinputdlg.ui
    ui/numinputfdlg.ui
)

target_include_directories(tcu_hmi PRIVATE
    ../../base
    ../../base/cjson/include
)

target_link_libraries(tcu_hmi PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    mosquitto
    cjson
)
