#include <QApplication>
#include <iostream>

#include "hmi_window.h"

int main(int argc, char* argv[])
{
    // BY ZF: Qt GUI 程序入口
    QApplication app(argc, argv);

    HmiWindow window;
    if (!window.initialize()) {
        std::cerr << "[HMI] initialize failed" << std::endl;
        return 1;
    }

    // BY ZF: initialize() 可能已切全屏显示，避免再次 show() 覆盖窗口模式。
    if (!window.isVisible()) {
        window.show();
    }
    return app.exec();
}
