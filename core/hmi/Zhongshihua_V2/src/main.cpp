#include <QApplication>
#include <iostream>

#include "app_shell.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    AppShell shell;
    if (!shell.initialize()) {
        std::cerr << "[ZSHV2_HMI] initialize failed" << std::endl;
        return 1;
    }

    if (!shell.isVisible()) {
        shell.show();
    }

    return app.exec();
}
