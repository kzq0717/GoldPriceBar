#include "SingleInstance.h"

#include <QStandardPaths>
#include <QDir>

SingleInstance::SingleInstance(const QString& key)
    : m_lock(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                 .filePath(key + QStringLiteral(".lock")))
{
    m_lock.setStaleLockTime(30 * 1000);
}

SingleInstance::~SingleInstance()
{
    if (m_lock.isLocked())
        m_lock.unlock();
}

bool SingleInstance::tryLock()
{
    if (m_lock.tryLock(100))
        return true;
    m_error = QStringLiteral("Another instance is running (lock: %1)").arg(m_lock.fileName());
    return false;
}
