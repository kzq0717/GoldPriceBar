#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

#include <QString>

class CrashHandler
{
public:
    static void install();
    static QString dumpDir();

private:
#ifdef Q_OS_WIN
    static long __stdcall unhandledFilter(struct _EXCEPTION_POINTERS* info);
#endif
};

#endif // CRASHHANDLER_H
