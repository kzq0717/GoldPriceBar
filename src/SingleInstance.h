#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QLockFile>
#include <QString>

/**
 * @brief 单实例锁（基于 QLockFile，跨平台）
 */
class SingleInstance
{
public:
    explicit SingleInstance(const QString& key);
    ~SingleInstance();

    bool tryLock();
    QString errorString() const { return m_error; }

private:
    QLockFile m_lock;
    QString m_error;
};

#endif // SINGLEINSTANCE_H
