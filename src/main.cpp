#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "PriceBarWindow.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"
#include "Logger.h"
#include "CrashHandler.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QApplication::setApplicationName("GoldPriceBarLite");
    QApplication::setApplicationVersion("0.2.0");
    QApplication::setOrganizationName("GoldPriceBarLite");
    QApplication::setOrganizationDomain("local");

    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

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
