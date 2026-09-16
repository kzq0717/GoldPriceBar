#include "GlobalHotkey.h"
#include "Logger.h"

#include <QGuiApplication>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  ifndef MOD_NOREPEAT
#    define MOD_NOREPEAT 0x4000
#  endif
#endif

GlobalHotkey::GlobalHotkey(QObject* parent)
    : QObject(parent)
{
    // 不在构造时安装 native filter，避免启动阶段异常
}

GlobalHotkey::~GlobalHotkey()
{
    unregisterHotkey();
    if (m_filterInstalled) {
        qApp->removeNativeEventFilter(this);
        m_filterInstalled = false;
    }
}

bool GlobalHotkey::registerHotkey()
{
    unregisterHotkey();
#ifdef Q_OS_WIN
    // 稳定性：默认不注册系统热键（多用户反馈 RegisterHotKey 后进程退出）。
    // 设置中仍保留开关；真正注册需环境变量 GPB_ENABLE_HOTKEY=1
    const QByteArray allow = qgetenv("GPB_ENABLE_HOTKEY");
    if (allow != "1") {
        Logger::info(QStringLiteral(
            "Global hotkey skipped (set GPB_ENABLE_HOTKEY=1 to enable Ctrl+Shift+G)"));
        m_registered = false;
        return false;
    }
    if (!m_filterInstalled) {
        qApp->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
    const UINT mods = MOD_CONTROL | MOD_SHIFT | MOD_NOREPEAT;
    const UINT vk = 0x47;
    m_id = static_cast<int>(0x47504201); // 'GPB\x01' unique id
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
