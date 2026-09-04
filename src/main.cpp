#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QStandardPaths>

#include "PriceBarWindow.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "CrashHandler.h"
#include "SingleInstance.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName("GoldPriceBarLite");
    QApplication::setApplicationVersion("0.6.4");
    QApplication::setOrganizationName("GoldPriceBarLite");
    QApplication::setOrganizationDomain("local");

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // 单实例
    SingleInstance single(QStringLiteral("GoldPriceBarLite_single_instance"));
    if (!single.tryLock()) {
        QMessageBox::warning(
            nullptr,
            QObject::tr("已在运行"),
            QObject::tr("GoldPriceBarLite 已在运行，请勿重复打开。\n"
                        "可在系统托盘中找到图标并显示价格条。"));
        return 1;
    }

    AppSettings::instance().load();

    Logger::init();
    CrashHandler::install();
    Logger::info(QStringLiteral("Application start, version %1")
                     .arg(QApplication::applicationVersion()));

    if (!ExtremeDatabase::instance().open()) {
        Logger::warn(QStringLiteral("SQLite open failed, extremes will not persist"));
    } else {
        Logger::info(QStringLiteral("SQLite: %1")
                         .arg(ExtremeDatabase::instance().databasePath()));
    }

    PriceBarWindow window;
    Logger::info(QStringLiteral("Main window shown"));
    window.show();

    const int code = app.exec();
    Logger::info(QStringLiteral("Application exit, code=%1").arg(code));
    return code;
}
