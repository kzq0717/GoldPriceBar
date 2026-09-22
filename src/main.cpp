#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>
#include <QFile>
#include <QQuickWindow>
#include <QQmlApplicationEngine>
#include <QQmlContext>

#include "PriceService.h"
#include "QmlBridge.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "CrashHandler.h"
#include "SingleInstance.h"
#include "GlobalHotkey.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static void earlyFail(const QString &title, const QString &text) {
#ifdef Q_OS_WIN
    MessageBoxW(nullptr, reinterpret_cast<LPCWSTR>(text.utf16()), reinterpret_cast<LPCWSTR>(title.utf16()),
        MB_OK | MB_ICONERROR);
#else
    QMessageBox::critical(nullptr, title, text);
#endif
}

int main(int argc, char *argv[]) {
    qputenv("QT_QUICK_CONTROLS_STYLE", "Basic");

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
    QApplication::setApplicationVersion("1.4.0");
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/app.png")));
    QApplication::setOrganizationName("GoldPriceBarLite");
    QApplication::setOrganizationDomain("local");
    QApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
    app.setQuitOnLastWindowClosed(false);

    // 平台插件未部署时双击会“无反应”，尽量给出提示
    if (QGuiApplication::platformName().isEmpty()) {
        earlyFail(QStringLiteral("启动失败"), QStringLiteral("未能加载 Qt 平台插件（platforms/qwindows.dll）。\n\n"
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
        earlyFail(
            QStringLiteral("已在运行"), QStringLiteral("GoldPriceBarLite 已在运行。\n"
                                                       "请查看系统托盘（右下角）图标，右键可「显示/隐藏价格条」。\n"
                                                       "快捷键：Ctrl+Shift+G\n\n"
                                                       "若确认已退出仍提示，可删除临时目录下：\n"
                                                       "GoldPriceBarLite_single_instance.lock"));
        return 1;
    }

    AppSettings::instance().load();

    Logger::init();
    CrashHandler::install();

    Logger::info(QStringLiteral("=== GoldPriceBarLite %1 QML-UI BUILD ===")
            .arg(QApplication::applicationVersion()));

    Logger::info(QStringLiteral("Application start, version %1 platform=%2")
            .arg(QApplication::applicationVersion(), QGuiApplication::platformName()));

    if (!ExtremeDatabase::instance().open()) {
        Logger::warn(QStringLiteral("SQLite open failed, extremes will not persist"));
    } else {
        Logger::info(QStringLiteral("SQLite: %1").arg(ExtremeDatabase::instance().databasePath()));
    }

    AppSettings::instance().applyNetworkProxy();

    PriceService priceService;
    QmlBridge bridge(&priceService);

    Logger::info(QStringLiteral("Resource check: PriceBar.qml exists=%1, SentimentWindow.qml exists=%2, SettingsWindow.qml exists=%3")
                     .arg(QFile::exists(QStringLiteral(":/qml/PriceBar.qml")))
                     .arg(QFile::exists(QStringLiteral(":/qml/SentimentWindow.qml")))
                     .arg(QFile::exists(QStringLiteral(":/qml/SettingsWindow.qml"))));

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated, [](QObject *obj, const QUrl &objUrl) {
        if (!obj) {
            Logger::error(QStringLiteral("QML load failed for: %1").arg(objUrl.toString()));
        } else {
            Logger::info(QStringLiteral("QML loaded OK: %1").arg(objUrl.toString()));
        }
    });

    engine.rootContext()->setContextProperty(QStringLiteral("bridge"), &bridge);

    engine.load(QUrl(QStringLiteral("qrc:/qml/PriceBar.qml")));
    engine.load(QUrl(QStringLiteral("qrc:/qml/SentimentWindow.qml")));
    engine.load(QUrl(QStringLiteral("qrc:/qml/SettingsWindow.qml")));

    const auto rootObjs = engine.rootObjects();
    if (rootObjs.isEmpty()) {
        Logger::error(QStringLiteral("QQmlApplicationEngine has 0 root objects!"));
        earlyFail(QStringLiteral("QML 加载失败"), QStringLiteral("未能加载 QML 主界面组件，程序即将退出。"));
        return -1;
    }

    for (QObject *obj : rootObjs) {
        auto *win = qobject_cast<QQuickWindow*>(obj);
        if (!win) continue;
        const QString name = win->objectName();
        const QString title = win->title();
        if (name == QLatin1String("priceBarWindow") || (name.isEmpty() && title == QStringLiteral("GoldPriceBar"))) {
            bridge.setPriceBarWindow(win);
            Logger::info(QStringLiteral("Bound PriceBar QQuickWindow (title: %1)").arg(title));
        } else if (name == QLatin1String("sentimentWindow") || (name.isEmpty() && title.contains(QStringLiteral("舆情")))) {
            bridge.setSentimentWindow(win);
            Logger::info(QStringLiteral("Bound SentimentWindow QQuickWindow (title: %1)").arg(title));
        } else if (name == QLatin1String("settingsWindow") || (name.isEmpty() && title.contains(QStringLiteral("设置")))) {
            bridge.setSettingsWindow(win);
            Logger::info(QStringLiteral("Bound SettingsWindow QQuickWindow (title: %1)").arg(title));
        }
    }

    priceService.start();

    GlobalHotkey hotkey(&app);
    auto applyHotkey = [&hotkey]() {
        const bool want = AppSettings::instance().hotkeyEnabled();
        if (want && !hotkey.isRegistered()) {
            hotkey.registerHotkey();
        } else if (!want && hotkey.isRegistered()) {
            hotkey.unregisterHotkey();
        }
    };
    QObject::connect(&hotkey, &GlobalHotkey::activated, &bridge, &QmlBridge::requestTogglePriceBar);
    QObject::connect(&AppSettings::instance(), &AppSettings::settingsChanged, &app, applyHotkey);
    applyHotkey();

    if (AppSettings::instance().sentimentEnabled()) {
        bridge.refreshSentiment();
    }

    Logger::info(QStringLiteral("QML UI launched successfully"));
    const int code = app.exec();
    Logger::info(QStringLiteral("Application exit, code=%1").arg(code));
    return code;
}
