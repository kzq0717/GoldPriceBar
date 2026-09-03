#ifndef GLOBALHOTKEY_H
#define GLOBALHOTKEY_H

#include <QObject>
#include <QAbstractNativeEventFilter>

/**
 * Windows 全局热键：默认 Ctrl+Shift+G
 * 非 Windows 平台 register 返回 false（可后续扩展 X11）
 */
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    bool registerHotkey();
    void unregisterHotkey();
    bool isRegistered() const { return m_registered; }

signals:
    void activated();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    bool m_registered = false;
    int m_id = 1;
};

#endif // GLOBALHOTKEY_H
