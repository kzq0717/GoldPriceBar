#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QGuiApplication>

#include "PriceBarWindow.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "CrashHandler.h"
#include "SingleInstance.h"

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

static void earlyFail(const QString& title, const QString& text)
{
#ifdef Q_OS_WIN
    MessageBoxW(nullptr,
                reinterpret_cast<LPCWSTR>(text.utf16()),
                reinterpret_cast<LPCWSTR>(title.utf16()),
                MB_OK | MB_ICONERROR);
#else
    QMessageBox::critical(nullptr, title, text);
#endif
}

int main(int argc, char *argv[])
{
    // 便于从资源管理器双击：把 exe 同目录加入 DLL 搜索路径（部署后 Qt*.dll 在旁边）
#ifdef Q_OS_WIN
    {
        wchar_t buf[MAX_PATH];
        if (GetModuleFileNameW(nullptr, buf, MAX_PATH) > 0) {
            std::wstring path(buf);
            const size_t slash = path.find_last_of(L"\\/");
            if (slash != std::wstring::npos) {
                path.resize(slash);
                SetDllDirectoryW(path.c_str());
                SetCurrentDirectoryW(path.c_str());
            }
        }
    }
#endif

    QApplication app(argc, argv);

    QApplication::setApplicationName("GoldPriceBarLite");
    QApplication::setApplicationVersion("1.3.4");
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    QApplication::setOrganizationName("GoldPriceBarLite");
    QApplication::setOrganizationDomain("local");

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // 平台插件未部署时双击会“无反应”，尽量给出提示
    if (QGuiApplication::platformName().isEmpty()) {
        earlyFail(QStringLiteral("启动失败"),
                  QStringLiteral(
                      "未能加载 Qt 平台插件（platforms/qwindows.dll）。\n\n"
                      "请在本机执行：\n"
                      "  build.bat deploy\n"
                      "或：\n"
                      "  deploy.bat\n"
                      "或用 run_with_qt.bat 启动（会把 Qt bin 加入 PATH）。"));
        return 2;
    }

    // 单实例
    SingleInstance single(QStringLiteral("GoldPriceBarLite_single_instance"));
    if (!single.tryLock()) {
        earlyFail(QStringLiteral("已在运行"),
                  QStringLiteral(
                      "GoldPriceBarLite 已在运行。\n"
                      "请查看系统托盘（右下角）图标，右键可「显示/隐藏价格条」。\n"
                      "快捷键：Ctrl+Shift+G\n\n"
                      "若确认已退出仍提示，可删除临时目录下：\n"
                      "GoldPriceBarLite_single_instance.lock"));
        return 1;
    }

    AppSettings::instance().load();

    Logger::init();
    CrashHandler::install();
    Logger::info(QStringLiteral("Application start, version %1 platform=%2")
                     .arg(QApplication::applicationVersion(),
                          QGuiApplication::platformName()));

    if (!ExtremeDatabase::instance().open()) {
        Logger::warn(QStringLiteral("SQLite open failed, extremes will not persist"));
    } else {
        Logger::info(QStringLiteral("SQLite: %1")
                         .arg(ExtremeDatabase::instance().databasePath()));
    }

    Logger::info(QStringLiteral("Creating PriceBarWindow..."));
    PriceBarWindow window;
    Logger::info(QStringLiteral("PriceBarWindow created OK"));
    window.show();
    window.raise();
    window.activateWindow();
    AppSettings::instance().applyNetworkProxy();
    Logger::info(QStringLiteral("Proxy applied after window show"));
    Logger::info(QStringLiteral("Main window shown geo=%1,%2 %3x%4")
                     .arg(window.x()).arg(window.y())
                     .arg(window.width()).arg(window.height()));

    const int code = app.exec();
    Logger::info(QStringLiteral("Application exit, code=%1").arg(code));
    return code;
}
