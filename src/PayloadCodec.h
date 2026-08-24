#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

// Shared codec for the oversized JSON payloads that ride inside a sync
// object's `data`: paragraph segmentations, translations, deep-read
// analyses. Sync objects travel as JSON and a segmented paper runs to
// hundreds of KB of text, so the inner document is zlib-deflated and
// base64'd. `codec` names the scheme in the object so a payload written
// before compression existed still decodes.
namespace PayloadCodec {

inline QString codecName() { return QStringLiteral("zlib-b64"); }

inline QString encode(const QJsonObject &inner)
{
    const QByteArray raw = QJsonDocument(inner).toJson(QJsonDocument::Compact);
    return QString::fromLatin1(qCompress(raw, 9).toBase64());
}

// Reads the {payload, codec} pair out of a sync object's `data`.
// Returns an empty object when there is nothing to decode.
inline QJsonObject decode(const QJsonObject &data)
{
    const QString payload = data.value(QStringLiteral("payload")).toString();
    if (payload.isEmpty())
        return {};
    const QByteArray b64 = QByteArray::fromBase64(payload.toLatin1());
    const QByteArray raw =
        data.value(QStringLiteral("codec")).toString() == codecName()
            ? qUncompress(b64)
            : b64;
    if (raw.isEmpty())
        return {};
    return QJsonDocument::fromJson(raw).object();
}

// Same, for a payload string whose codec is known by the caller.
inline QJsonObject decodeString(const QString &payload, bool compressed = true)
{
    if (payload.isEmpty())
        return {};
    const QByteArray b64 = QByteArray::fromBase64(payload.toLatin1());
    const QByteArray raw = compressed ? qUncompress(b64) : b64;
    if (raw.isEmpty())
        return {};
    return QJsonDocument::fromJson(raw).object();
}

} // namespace PayloadCodec
