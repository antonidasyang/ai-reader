// Asset Store Kit backend for the QKeychain shim in keychain.h.
#include "keychain.h"

#include <QMetaObject>

#include <asset/asset_api.h>

namespace QKeychain {

namespace {

Asset_Attr blobAttr(uint32_t tag, const QByteArray &bytes)
{
    Asset_Attr a{};
    a.tag = tag;
    a.value.blob.size = static_cast<uint32_t>(bytes.size());
    a.value.blob.data = reinterpret_cast<uint8_t *>(const_cast<char *>(bytes.constData()));
    return a;
}

Asset_Attr u32Attr(uint32_t tag, uint32_t v)
{
    Asset_Attr a{};
    a.tag = tag;
    a.value.u32 = v;
    return a;
}

Error mapError(int32_t rc)
{
    switch (rc) {
    case ASSET_SUCCESS:           return NoError;
    case ASSET_NOT_FOUND:         return EntryNotFound;
    case ASSET_PERMISSION_DENIED:
    case ASSET_ACCESS_DENIED:     return AccessDenied;
    case ASSET_SERVICE_UNAVAILABLE: return NoBackendAvailable;
    case ASSET_UNSUPPORTED:       return NotImplemented;
    default:                      return OtherError;
    }
}

QString describe(int32_t rc)
{
    switch (rc) {
    case ASSET_NOT_FOUND:           return QStringLiteral("Entry not found");
    case ASSET_PERMISSION_DENIED:   return QStringLiteral("Permission denied");
    case ASSET_ACCESS_DENIED:       return QStringLiteral("Access denied (device locked?)");
    case ASSET_SERVICE_UNAVAILABLE: return QStringLiteral("Asset Store service unavailable");
    default: return QStringLiteral("Asset Store error %1").arg(rc);
    }
}

} // namespace

Job::Job(const QString &service, QObject *parent)
    : QObject(parent), m_service(service)
{
}

Job::~Job() = default;

void Job::start()
{
    // qtkeychain never emits finished() from inside start(); callers connect
    // after construction but some code paths read state right after start().
    QMetaObject::invokeMethod(this, &Job::runAndFinish, Qt::QueuedConnection);
}

void Job::runAndFinish()
{
    run();
    emit finished(this);
    if (m_autoDelete)
        deleteLater();
}

void Job::setError(Error error, const QString &message)
{
    m_error = error;
    m_errorString = message;
}

QByteArray Job::alias() const
{
    // One flat namespace per app; qtkeychain keys are unique per service.
    return (m_service + QLatin1Char('/') + m_key).toUtf8();
}

ReadPasswordJob::ReadPasswordJob(const QString &service, QObject *parent)
    : Job(service, parent)
{
}

void ReadPasswordJob::run()
{
    const QByteArray a = alias();
    Asset_Attr query[] = {
        blobAttr(ASSET_TAG_ALIAS, a),
        u32Attr(ASSET_TAG_RETURN_TYPE, ASSET_RETURN_ALL),
    };
    Asset_ResultSet rs{};
    const int32_t rc = OH_Asset_Query(query, sizeof(query) / sizeof(query[0]), &rs);
    if (rc != ASSET_SUCCESS) {
        setError(mapError(rc), describe(rc));
        return;
    }
    if (rs.count > 0) {
        if (Asset_Attr *secret = OH_Asset_ParseAttr(&rs.results[0], ASSET_TAG_SECRET)) {
            m_data = QByteArray(reinterpret_cast<const char *>(secret->value.blob.data),
                                static_cast<qsizetype>(secret->value.blob.size));
        }
    }
    OH_Asset_FreeResultSet(&rs);
    if (m_data.isNull())
        setError(EntryNotFound, describe(ASSET_NOT_FOUND));
    else if (m_data == QByteArray(1, '\0'))
        m_data = QByteArray("");    // stored placeholder for "" (see write)
}

WritePasswordJob::WritePasswordJob(const QString &service, QObject *parent)
    : Job(service, parent)
{
}

void WritePasswordJob::run()
{
    const QByteArray a = alias();
    // The Asset Store rejects empty secrets; qtkeychain treats writing an
    // empty value as storing "", and the app only ever deletes instead.
    const QByteArray secret = m_data.isEmpty() ? QByteArray(1, '\0') : m_data;
    Asset_Attr add[] = {
        blobAttr(ASSET_TAG_ALIAS, a),
        blobAttr(ASSET_TAG_SECRET, secret),
        // Readable after the first unlock since boot — background sync and
        // auto-login run without the user staring at the screen.
        u32Attr(ASSET_TAG_ACCESSIBILITY, ASSET_ACCESSIBILITY_DEVICE_FIRST_UNLOCKED),
    };
    int32_t rc = OH_Asset_Add(add, sizeof(add) / sizeof(add[0]));
    if (rc == ASSET_DUPLICATED) {
        Asset_Attr query[] = { blobAttr(ASSET_TAG_ALIAS, a) };
        Asset_Attr update[] = { blobAttr(ASSET_TAG_SECRET, secret) };
        rc = OH_Asset_Update(query, 1, update, 1);
    }
    if (rc != ASSET_SUCCESS)
        setError(mapError(rc), describe(rc));
}

DeletePasswordJob::DeletePasswordJob(const QString &service, QObject *parent)
    : Job(service, parent)
{
}

void DeletePasswordJob::run()
{
    const QByteArray a = alias();
    Asset_Attr query[] = { blobAttr(ASSET_TAG_ALIAS, a) };
    const int32_t rc = OH_Asset_Remove(query, 1);
    if (rc != ASSET_SUCCESS)
        setError(mapError(rc), describe(rc));
}

} // namespace QKeychain
