#include <QApplication>
#include <QFile>

#include "previewwindow.h"
#include "runtime_window.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("tcu_hmi");
    app.setOrganizationName("codex");

    QFile qssFile(":/styles/cui.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        app.setStyleSheet(QString::fromUtf8(qssFile.readAll()));
    }

    const QStringList args = app.arguments();
    if (args.contains("--preview")) {
        PreviewWindow window;
        window.showFullScreen();
        return app.exec();
    }

    RuntimeWindow window;
    if (!window.initialize()) {
        return 1;
    }
    return app.exec();
}
