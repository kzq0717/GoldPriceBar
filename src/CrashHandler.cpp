#include "CrashHandler.h"
#include "Logger.h"
#include "AppSettings.h"

#include <QDir>
#include <QDateTime>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#  pragma comment(lib, "dbghelp.lib")
#endif

QString CrashHandler::dumpDir()
{
    QString base = AppSettings::instance().resolvedDatabaseDir();
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return QDir(base).filePath(QStringLiteral("logs"));
}

void CrashHandler::install()
{
#ifdef Q_OS_WIN
    SetUnhandledExceptionFilter(unhandledFilter);
    Logger::info(QStringLiteral("CrashHandler installed (minidump -> %1)").arg(dumpDir()));
#else
    Logger::info(QStringLiteral("CrashHandler: minidump only supported on Windows"));
#endif
}

#ifdef Q_OS_WIN
long __stdcall CrashHandler::unhandledFilter(EXCEPTION_POINTERS* info)
{
    const QString dir = dumpDir();
    QDir().mkpath(dir);

    const QString dumpPath = QDir(dir).filePath(
        QStringLiteral("crash-%1.dmp")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-hhmmss"))));

    HANDLE hFile = CreateFileW(
        reinterpret_cast<LPCWSTR>(dumpPath.utf16()),
        GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = info;
        mei.ClientPointers = FALSE;

        const MINIDUMP_TYPE type = static_cast<MINIDUMP_TYPE>(
            MiniDumpNormal | MiniDumpWithDataSegs | MiniDumpWithHandleData |
            MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                          type, info ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(hFile);
    }

    Logger::error(QStringLiteral("CRASH: unhandled exception, dump=%1 code=0x%2")
                      .arg(dumpPath)
                      .arg(info && info->ExceptionRecord
                               ? QString::number(info->ExceptionRecord->ExceptionCode, 16)
                               : QStringLiteral("?")));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif
