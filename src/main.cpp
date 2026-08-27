#include <QApplication>
#include <QDir>
#include <QStandardPaths>

#include "PriceBarWindow.h"
#include "AppSettings.h"
#include "ExtremeDatabase.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    // 应用基本信息
    QApplication::setApplicationName("GoldPriceBarLite");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("GoldPriceBarLite");
    QApplication::setOrganizationDomain("local");

    // 高 DPI 支持（Qt 6 默认已较好，显式声明更稳妥）
    QApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

    // 加载配置
    AppSettings::instance().load();
    ExtremeDatabase::instance().open();

    // 主价格条窗口
    PriceBarWindow window;
    window.show();

    return app.exec();
}
