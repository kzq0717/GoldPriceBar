#include "GlobalHotkey.h"
#include "Logger.h"

#include <QGuiApplication>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
{
    qApp->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterHotkey();
    qApp->removeNativeEventFilter(this);
}

bool GlobalHotkey::registerHotkey()
{
    unregisterHotkey();
#ifdef Q_OS_WIN
    // MOD_CONTROL | MOD_SHIFT, 'G'
    const UINT mods = MOD_CONTROL | MOD_SHIFT;
    const UINT vk = 0x47; // 'G'
    if (RegisterHotKey(nullptr, m_id, mods, vk)) {
        m_registered = true;
        Logger::info(QStringLiteral("Global hotkey registered: Ctrl+Shift+G"));
        return true;
    }
    Logger::warn(QStringLiteral("RegisterHotKey failed, err=%1").arg(GetLastError()));
    return false;
#else
    Logger::warn(QStringLiteral("Global hotkey only supported on Windows"));
    return false;
#endif
}

void GlobalHotkey::unregisterHotkey()
{
#ifdef Q_OS_WIN
    if (m_registered) {
        UnregisterHotKey(nullptr, m_id);
        m_registered = false;
    }
#else
    m_registered = false;
#endif
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const MSG* msg = static_cast<MSG*>(message);
        if (msg && msg->message == WM_HOTKEY && static_cast<int>(msg->wParam) == m_id) {
            emit activated();
            return true;
        }
    }
#else
    Q_UNUSED(eventType);
    Q_UNUSED(message);
#endif
    return false;
}
