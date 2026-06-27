#include "SearchService.h"
#include "ProjectController.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVariantMap>

namespace {
// Turn free user input into a safe FTS5 query: strip punctuation, AND the
// tokens, prefix-match the last one (live-as-you-type).
QString ftsQuery(const QString &raw)
{
    static const QRegularExpression nonWord(
        QStringLiteral("[^\\w\\s]"),
        QRegularExpression::UseUnicodePropertiesOption);
    QString cleaned = raw;
    cleaned.replace(nonWord, QStringLiteral(" "));
    const QStringList toks =
        cleaned.split(QRegularExpression(QStringLiteral("\\s+")),
                      Qt::SkipEmptyParts);
    if (toks.isEmpty())
        return {};
    QStringList parts;
    for (int i = 0; i < toks.size(); ++i)
        parts << (toks.at(i) + (i == toks.size() - 1 ? QStringLiteral("*")
                                                      : QString()));
    return parts.join(QChar(' '));
}
} // namespace

SearchService::SearchService(LibraryDb *db, ProjectController *projects,
                             QObject *parent)
    : QObject(parent)
    , m_db(db)
    , m_projects(projects)
{
}

bool SearchService::available() const
{
    return m_db->ftsAvailable();
}

QVariantList SearchService::search(const QString &query) const
{
    QVariantList out;
    const QString pid = m_projects->currentId();
    if (pid.isEmpty())
        return out;
    const QString q = ftsQuery(query);
    if (q.isEmpty())
        return out;

    const QList<SearchHit> hits = m_db->search(pid, q, 100);
    for (const SearchHit &h : hits) {
        QVariantMap m;
        m.insert(QStringLiteral("itemId"), h.objectId);
        m.insert(QStringLiteral("kind"), h.kind);
        m.insert(QStringLiteral("snippet"), h.snippet);
        SyncObjectRow row;
        if (m_db->getObject(pid, h.objectId, row)) {
            m.insert(QStringLiteral("title"),
                     row.data.value(QStringLiteral("title")).toString());
            m.insert(QStringLiteral("localPath"),
                     row.data.value(QStringLiteral("localPath")).toString());
        }
        out.append(m);
    }
    return out;
}
