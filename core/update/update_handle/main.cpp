#include "mainwindow.h"
#include <QtGui/QApplication>
#include <QtGui/QMessageBox>
#include <QtCore/QTextStream>
#include <QtCore/QTextCodec>
#include <unistd.h>
#include <locale.h>

// 检查root权限
bool checkRootPermission() {
    return (getuid() == 0);
}

// 显示帮助信息
void showHelp(const QString &programName) {
    QTextStream out(stdout);
    out << "用法: " << programName << " [选项]\n\n";
    out << "选项:\n";
    out << "  -p, --package PATH    升级包路径（执行更新）\n";
    out << "  -r, --rollback        执行回滚操作\n";
    out << "  -h, --help            显示帮助信息\n\n";
    out << "示例:\n";
    out << "  " << programName << " -p /path/to/install.tar.gz\n";
    out << "  " << programName << " -r\n";
}

int main(int argc, char *argv[])
{
    // 设置系统locale为UTF-8（如果支持）
    setlocale(LC_ALL, "C.UTF-8");
    setlocale(LC_CTYPE, "C.UTF-8");
    
    QApplication app(argc, argv);
    
    // 设置Qt应用程序默认编码为UTF-8
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForTr(QTextCodec::codecForName("UTF-8"));
    QTextCodec::setCodecForCStrings(QTextCodec::codecForName("UTF-8"));
    
    app.setApplicationName("TCU Update");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("TCU");
    
    // 解析命令行参数
    QString packagePath;
    bool rollback = false;
    bool showHelpFlag = false;
    
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "-p" || args[i] == "--package") {
            if (i + 1 < args.size()) {
                packagePath = args[++i];
            } else {
                QTextStream err(stderr);
                err << "错误: -p 参数需要指定升级包路径\n";
                showHelp(app.applicationName());
                return 1;
            }
        } else if (args[i] == "-r" || args[i] == "--rollback") {
            rollback = true;
        } else if (args[i] == "-h" || args[i] == "--help") {
            showHelpFlag = true;
        } else {
            QTextStream err(stderr);
            err << "错误: 未知参数 " << args[i] << "\n";
            showHelp(app.applicationName());
            return 1;
        }
    }
    
    // 显示帮助
    if (showHelpFlag) {
        showHelp(app.applicationName());
        return 0;
    }
    
    // 检查参数
    if (packagePath.isEmpty() && !rollback) {
        QTextStream err(stderr);
        err << "错误: 必须指定 -p 或 -r 参数\n";
        showHelp(app.applicationName());
        return 1;
    }
    
    if (!packagePath.isEmpty() && rollback) {
        QTextStream err(stderr);
        err << "错误: -p 和 -r 参数不能同时使用\n";
        return 1;
    }
    
    // 检查root权限
    if (!checkRootPermission()) {
        QMessageBox::critical(0, "权限错误", 
            "此程序需要root权限才能运行。\n\n"
            "请使用以下方式运行：\n"
            "  sudo " + app.applicationName() + " [选项]");
        return 1;
    }
    
    // 创建主窗口
    MainWindow window;
    
    // 设置操作模式
    if (rollback) {
        window.setRollbackMode();
    } else if (!packagePath.isEmpty()) {
        window.setPackagePath(packagePath);
    }
    
    window.show();
    
    return app.exec();
}

