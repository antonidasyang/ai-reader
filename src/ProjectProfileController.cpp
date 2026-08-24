#include "ProjectProfileController.h"

#include "AnalysisStore.h"

#include <QCryptographicHash>
#include <QJsonObject>
#include <QStringList>

namespace {

// Field key, and the line that introduces it to the model. Order is the
// order the dialog shows and the prompt block reads.
struct Field {
    const char *key;
    const char *promptLabel;
};

const Field kFields[] = {
    {"goal",       "Research goal"},
    {"questions",  "Core questions"},
    {"scenarios",  "Application setting"},
    {"hypotheses", "Current hypotheses"},
    {"scope",      "In scope"},
    {"outOfScope", "Explicitly out of scope"},
    {"dimensions", "Dimensions the reader cares about most"},
    {"stage",      "Where the reader is in the work"},
    {"background", "Reader's background"},
};

} // namespace

ProjectProfileController::ProjectProfileController(AnalysisStore *store,
                                                   QObject *parent)
    : QObject(parent)
    , m_store(store)
{
    connect(m_store, &AnalysisStore::changed, this,
            &ProjectProfileController::changed);
}

QStringList ProjectProfileController::fieldNames()
{
    QStringList out;
    for (const Field &f : kFields)
        out.append(QString::fromLatin1(f.key));
    return out;
}

QVariantMap ProjectProfileController::profile() const
{
    const QJsonObject o = m_store->profile();
    QVariantMap m;
    for (const Field &f : kFields) {
        m.insert(QString::fromLatin1(f.key),
                 o.value(QString::fromLatin1(f.key)).toString());
    }
    return m;
}

bool ProjectProfileController::hasProfile() const
{
    const QJsonObject o = m_store->profile();
    for (const Field &f : kFields) {
        if (!o.value(QString::fromLatin1(f.key)).toString().trimmed().isEmpty())
            return true;
    }
    return false;
}

bool ProjectProfileController::canEdit() const { return m_store->canWrite(); }

QString ProjectProfileController::summary() const
{
    const QJsonObject o = m_store->profile();
    QString s = o.value(QStringLiteral("goal")).toString().trimmed();
    if (s.isEmpty())
        s = o.value(QStringLiteral("questions")).toString().trimmed();
    s = s.section(QChar('\n'), 0, 0).trimmed();
    if (s.size() > 90)
        s = s.left(88) + QStringLiteral("…");
    return s;
}

QString ProjectProfileController::updatedAt() const
{
    return m_store->profile().value(QStringLiteral("updatedAt")).toString();
}

QString ProjectProfileController::updatedByEmail() const
{
    return m_store->profile().value(QStringLiteral("updatedByEmail")).toString();
}

bool ProjectProfileController::save(const QVariantMap &fields)
{
    QJsonObject o;
    for (const Field &f : kFields) {
        const QString key = QString::fromLatin1(f.key);
        o.insert(key, fields.value(key).toString().trimmed());
    }
    return m_store->putProfile(o);
}

bool ProjectProfileController::clearProfile()
{
    return m_store->putProfile(QJsonObject{});
}

QString ProjectProfileController::promptBlock() const
{
    const QJsonObject o = m_store->profile();
    QString out;
    for (const Field &f : kFields) {
        const QString v =
            o.value(QString::fromLatin1(f.key)).toString().trimmed();
        if (v.isEmpty())
            continue;
        out += QStringLiteral("- %1: %2\n")
                   .arg(QString::fromLatin1(f.promptLabel), v);
    }
    if (out.isEmpty())
        return {};
    return QStringLiteral("The reader's research project:\n") + out;
}

QString ProjectProfileController::hash() const
{
    const QString block = promptBlock();
    if (block.isEmpty())
        return QStringLiteral("none");
    return QString::fromLatin1(
               QCryptographicHash::hash(block.toUtf8(),
                                        QCryptographicHash::Sha1)
                   .toHex())
        .left(12);
}
