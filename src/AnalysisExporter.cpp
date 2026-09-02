#include "AnalysisExporter.h"

#include "AnalysisStore.h"
#include "AnalysisTypes.h"
#include "LibraryAnalysisService.h"
#include "ProjectController.h"
#include "ProjectProfileController.h"

#include <QDateTime>
#include <QFile>
#include <QJsonValue>
#include <QTextStream>

#include <functional>

namespace {

QString indentOf(int n) { return QString(n * 2, QChar(' ')); }

QString sanitize(const QString &s)
{
    QString out = s;
    out.replace(QChar('\n'), QChar(' '));
    return out.trimmed();
}

} // namespace

AnalysisExporter::AnalysisExporter(AnalysisStore *store,
                                   ProjectController *projects,
                                   ProjectProfileController *profile,
                                   LibraryAnalysisService *research,
                                   QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_projects(projects)
    , m_profile(profile)
    , m_research(research)
{
}

QString AnalysisExporter::claimLine(const QJsonObject &claim, int indent) const
{
    const QString text = sanitize(claim.value(QStringLiteral("text")).toString());
    if (text.isEmpty())
        return {};
    QString line = indentOf(indent) + QStringLiteral("- ") + text;

    QStringList marks;
    const QString source = claim.value(QStringLiteral("source")).toString();
    if (!source.isEmpty())
        marks << Analysis::sourceLabel(source);
    if (claim.value(QStringLiteral("unsupported")).toBool())
        marks << tr("no evidence found");
    const QString type = claim.value(QStringLiteral("type")).toString();
    if (!type.isEmpty())
        marks << type;

    QStringList cites;
    for (const QJsonValue &v : claim.value(QStringLiteral("evidence")).toArray()) {
        const QJsonObject e = v.toObject();
        if (e.value(QStringLiteral("verified")).toBool()) {
            const QString section =
                sanitize(e.value(QStringLiteral("section")).toString());
            cites << (section.isEmpty()
                          ? tr("p%1").arg(e.value(QStringLiteral("page")).toInt())
                          : tr("%1, p%2")
                                .arg(section)
                                .arg(e.value(QStringLiteral("page")).toInt()));
        } else {
            // Exported as it is shown: a citation that did not check out is
            // not quietly dropped.
            cites << tr("unverified");
        }
    }
    if (!cites.isEmpty())
        marks << cites.join(QStringLiteral(", "));
    if (!marks.isEmpty())
        line += QStringLiteral("  _(%1)_").arg(marks.join(QStringLiteral(" · ")));
    return line + QChar('\n');
}

QString AnalysisExporter::claimList(const QJsonArray &claims, int indent) const
{
    QString out;
    for (const QJsonValue &v : claims)
        out += claimLine(v.toObject(), indent);
    return out;
}

QString AnalysisExporter::stringList(const QJsonArray &items, int indent) const
{
    QString out;
    for (const QJsonValue &v : items) {
        const QString s = sanitize(v.toString());
        if (!s.isEmpty())
            out += indentOf(indent) + QStringLiteral("- ") + s + QChar('\n');
    }
    return out;
}

QString AnalysisExporter::quickSection(const QJsonObject &d) const
{
    if (d.isEmpty())
        return {};
    QString out;
    const QString oneLiner = d.value(QStringLiteral("oneLiner")).toString();
    if (!oneLiner.isEmpty())
        out += QStringLiteral("> %1\n\n").arg(sanitize(oneLiner));

    const QJsonObject advice = d.value(QStringLiteral("advice")).toObject();
    const QJsonObject rel = d.value(QStringLiteral("relevance")).toObject();
    if (!advice.isEmpty() || !rel.isEmpty()) {
        out += QStringLiteral("**%1** %2 · %3\n\n")
                   .arg(tr("Verdict:"),
                        Analysis::adviceLabel(
                            advice.value(QStringLiteral("code")).toString()),
                        Analysis::relevanceLabel(
                            rel.value(QStringLiteral("level")).toString()));
        const QString why = sanitize(rel.value(QStringLiteral("reason")).toString());
        if (!why.isEmpty())
            out += why + QStringLiteral("\n\n");
    }

    struct Part { const char *heading; const char *key; bool array; };
    const Part parts[] = {
        {QT_TR_NOOP("The problem"), "problem", false},
        {QT_TR_NOOP("Why it matters"), "importance", false},
        {QT_TR_NOOP("What they did"), "method", false},
        {QT_TR_NOOP("Main results"), "results", true},
        {QT_TR_NOOP("Contributions"), "contributions", true},
        {QT_TR_NOOP("Limitations"), "limitations", true},
    };
    for (const Part &p : parts) {
        const QString key = QString::fromLatin1(p.key);
        QString body;
        if (p.array)
            body = claimList(d.value(key).toArray());
        else
            body = claimLine(d.value(key).toObject());
        if (body.isEmpty())
            continue;
        out += QStringLiteral("### %1\n\n").arg(tr(p.heading));
        out += body + QChar('\n');
    }

    const QJsonArray priority = d.value(QStringLiteral("priority")).toArray();
    if (!priority.isEmpty()) {
        out += QStringLiteral("### %1\n\n").arg(tr("Read first"));
        for (const QJsonValue &v : priority) {
            const QJsonObject p = v.toObject();
            out += QStringLiteral("- %1 — %2\n")
                       .arg(sanitize(p.value(QStringLiteral("what")).toString()),
                            sanitize(p.value(QStringLiteral("why")).toString()));
        }
        out += QChar('\n');
    }
    return out;
}

QString AnalysisExporter::deepSection(const QJsonObject &deep) const
{
    const QJsonObject modules = deep.value(QStringLiteral("modules")).toObject();
    if (modules.isEmpty())
        return {};
    QString out = QStringLiteral("## %1\n\n").arg(tr("Close reading"));
    for (const QString &id : Analysis::deepModules()) {
        const QJsonObject m = modules.value(id).toObject();
        if (m.isEmpty())
            continue;
        out += QStringLiteral("### %1\n\n").arg(Analysis::deepModuleTitle(id));
        const QString summary = sanitize(m.value(QStringLiteral("summary")).toString());
        if (!summary.isEmpty())
            out += summary + QStringLiteral("\n\n");

        for (const QJsonValue &sv : m.value(QStringLiteral("sections")).toArray()) {
            const QJsonObject sec = sv.toObject();
            const QString heading =
                sanitize(sec.value(QStringLiteral("heading")).toString());
            if (!heading.isEmpty())
                out += QStringLiteral("#### %1\n\n").arg(heading);
            out += claimList(sec.value(QStringLiteral("items")).toArray());
            out += QChar('\n');
        }

        // The per-module extras, in the order the schema defines them.
        const QJsonArray terms = m.value(QStringLiteral("terms")).toArray();
        for (const QJsonValue &tv : terms) {
            const QJsonObject t = tv.toObject();
            out += QStringLiteral("- **%1** — %2 _(%3)_\n")
                       .arg(sanitize(t.value(QStringLiteral("term")).toString()),
                            sanitize(t.value(QStringLiteral("plain")).toString()),
                            sanitize(t.value(QStringLiteral("roleInPaper"))
                                         .toString()));
        }
        if (!terms.isEmpty())
            out += QChar('\n');

        const QJsonArray steps = m.value(QStringLiteral("steps")).toArray();
        int n = 0;
        for (const QJsonValue &sv : steps) {
            const QJsonObject st = sv.toObject();
            out += QStringLiteral("%1. %2%3\n")
                       .arg(++n)
                       .arg(sanitize(st.value(QStringLiteral("step")).toString()),
                            st.value(QStringLiteral("novel")).toBool()
                                ? QStringLiteral(" _(%1)_").arg(tr("new here"))
                                : QStringLiteral(" _(%1)_").arg(tr("prior work")));
        }
        if (!steps.isEmpty())
            out += QChar('\n');

        const QJsonArray checklist =
            m.value(QStringLiteral("checklist")).toArray();
        for (const QJsonValue &cv : checklist) {
            const QJsonObject c = cv.toObject();
            out += QStringLiteral("- **%1**: %2 %3\n")
                       .arg(sanitize(c.value(QStringLiteral("what")).toString()),
                            sanitize(c.value(QStringLiteral("status")).toString()),
                            sanitize(c.value(QStringLiteral("detail")).toString()));
        }
        if (!checklist.isEmpty())
            out += QChar('\n');

        struct Extra { const char *heading; const char *key; bool claims; };
        const Extra extras[] = {
            {QT_TR_NOOP("Acknowledged by the authors"), "acknowledged", true},
            {QT_TR_NOOP("Not acknowledged"), "additional", true},
            {QT_TR_NOOP("Threats to validity"), "validityRisks", true},
            {QT_TR_NOOP("Practical constraints"), "practical", true},
            {QT_TR_NOOP("Still open"), "openQuestions", false},
            {QT_TR_NOOP("Questions for the authors or a supervisor"),
             "advisorQuestions", false},
            {QT_TR_NOOP("Directions"), "directions", false},
            {QT_TR_NOOP("Smallest experiments"), "minimalExperiments", false},
            {QT_TR_NOOP("Worth reading next"), "references", false},
            {QT_TR_NOOP("What would block a reproduction"), "blockers", false},
            {QT_TR_NOOP("What to learn first"), "prerequisites", false},
        };
        for (const Extra &e : extras) {
            const QJsonArray arr = m.value(QString::fromLatin1(e.key)).toArray();
            if (arr.isEmpty())
                continue;
            out += QStringLiteral("**%1**\n\n").arg(tr(e.heading));
            out += e.claims ? claimList(arr) : stringList(arr);
            out += QChar('\n');
        }
    }
    return out;
}

QString AnalysisExporter::paperMarkdown(const QString &paperId) const
{
    const AnalysisRecord quick =
        m_store->paperAnalysis(paperId, Analysis::KindQuick);
    const AnalysisRecord deep =
        m_store->paperAnalysis(paperId, Analysis::KindDeep);
    if (!quick.valid && !deep.valid)
        return {};

    QString title = quick.valid ? quick.title : deep.title;
    if (title.isEmpty())
        title = paperId;

    QString out = QStringLiteral("# %1\n\n").arg(title);
    out += QStringLiteral("_%1_\n\n")
               .arg(tr("Interpreted by AI Reader. Every statement carries where "
                       "it came from, and every page reference was checked "
                       "against the paper before it was written down."));
    const QString model = quick.valid ? quick.model : deep.model;
    const QString when = quick.valid ? quick.updatedAt : deep.updatedAt;
    const QString who = quick.valid ? quick.authorEmail : deep.authorEmail;
    QStringList meta;
    if (!model.isEmpty())
        meta << model;
    if (!who.isEmpty())
        meta << who;
    if (!when.isEmpty())
        meta << when;
    if (!meta.isEmpty())
        out += QStringLiteral("_%1_\n\n").arg(meta.join(QStringLiteral(" · ")));

    if (quick.valid) {
        out += QStringLiteral("## %1\n\n").arg(tr("Quick interpretation"));
        out += quickSection(quick.payload);
    }
    if (deep.valid)
        out += deepSection(deep.payload);

    const QJsonArray notes =
        m_store->note(paperId).value(QStringLiteral("notes")).toArray();
    if (!notes.isEmpty()) {
        out += QStringLiteral("## %1\n\n").arg(tr("My notes"));
        for (const QJsonValue &v : notes) {
            out += QStringLiteral("- %1\n")
                       .arg(sanitize(v.toObject()
                                         .value(QStringLiteral("text"))
                                         .toString()));
        }
        out += QChar('\n');
    }
    return out;
}

QString AnalysisExporter::librarySection(const QString &kind) const
{
    const AnalysisRecord rec = m_store->libraryAnalysis(kind);
    if (!rec.valid || rec.payload.isEmpty())
        return {};
    QString out = QStringLiteral("## %1\n\n").arg(Analysis::libraryKindTitle(kind));

    const QJsonObject p = rec.payload;
    // The disclaimers are part of the content, not decoration: a coverage
    // audit read without them says something it must not say.
    const QString disclaimer =
        sanitize(p.value(QStringLiteral("disclaimer")).toString());
    if (!disclaimer.isEmpty())
        out += QStringLiteral("> %1\n\n").arg(disclaimer);
    const QString caveat = sanitize(p.value(QStringLiteral("caveat")).toString());
    if (!caveat.isEmpty())
        out += QStringLiteral("> %1\n\n").arg(caveat);

    // Rendered generically: every library analysis is objects of strings,
    // string arrays, claim arrays and paper-id arrays, and spelling out
    // seven bespoke renderers would rot the first time a schema moved.
    std::function<QString(const QJsonValue &, int)> render =
        [&](const QJsonValue &v, int depth) -> QString {
        QString s;
        if (v.isString()) {
            const QString t = sanitize(v.toString());
            if (!t.isEmpty())
                s += indentOf(depth) + QStringLiteral("- ") + t + QChar('\n');
        } else if (v.isArray()) {
            for (const QJsonValue &e : v.toArray())
                s += render(e, depth);
        } else if (v.isObject()) {
            const QJsonObject o = v.toObject();
            if (o.contains(QStringLiteral("text"))
                && o.contains(QStringLiteral("source"))) {
                s += claimLine(o, depth);
                return s;
            }
            for (auto it = o.begin(); it != o.end(); ++it) {
                if (it.key() == QLatin1String("generatedAt"))
                    continue;
                if (it.value().isString()) {
                    const QString t = sanitize(it.value().toString());
                    if (t.isEmpty())
                        continue;
                    s += indentOf(depth)
                         + QStringLiteral("- **%1**: %2\n").arg(it.key(), t);
                } else if (it.key() == QLatin1String("paperIds")
                           || it.key() == QLatin1String("sourcePaperIds")
                           || it.key() == QLatin1String("representativePaperIds")
                           || it.key() == QLatin1String("opposingPaperIds")) {
                    QStringList titles;
                    for (const QJsonValue &idv : it.value().toArray())
                        titles << m_research->paperTitle(idv.toString());
                    if (!titles.isEmpty())
                        s += indentOf(depth)
                             + QStringLiteral("- **%1**: %2\n")
                                   .arg(it.key(),
                                        titles.join(QStringLiteral("; ")));
                } else {
                    const QString inner = render(it.value(), depth + 1);
                    if (inner.isEmpty())
                        continue;
                    s += indentOf(depth) + QStringLiteral("- **%1**\n").arg(it.key());
                    s += inner;
                }
            }
        }
        return s;
    };

    for (auto it = p.begin(); it != p.end(); ++it) {
        if (it.key() == QLatin1String("disclaimer")
            || it.key() == QLatin1String("caveat")
            || it.key() == QLatin1String("generatedAt"))
            continue;
        const QString body = render(it.value(), 0);
        if (body.isEmpty())
            continue;
        out += QStringLiteral("### %1\n\n").arg(it.key());
        out += body + QChar('\n');
    }
    if (!rec.authorEmail.isEmpty()) {
        out += QStringLiteral("_%1_\n\n")
                   .arg(tr("Generated by %1 over %2 papers, %3")
                            .arg(rec.authorEmail)
                            .arg(rec.paperCount)
                            .arg(rec.updatedAt));
    }
    return out;
}

QString AnalysisExporter::projectMarkdown() const
{
    QString out = QStringLiteral("# %1\n\n")
                      .arg(m_projects->currentName().isEmpty()
                               ? tr("Research report")
                               : m_projects->currentName());
    out += QStringLiteral("_%1_\n\n")
               .arg(tr("Generated by AI Reader from the interpretations of the "
                       "papers in this project. It describes this collection, "
                       "not the field: work that is not collected here is not "
                       "work that does not exist."));

    const QString profile = m_profile->promptBlock();
    if (!profile.isEmpty()) {
        out += QStringLiteral("## %1\n\n").arg(tr("The project"));
        out += profile + QChar('\n');
    }

    for (const QString &kind :
         {Analysis::KindTaxonomy, Analysis::KindMap, Analysis::KindConsensus,
          Analysis::KindEvolution, Analysis::KindCoverage,
          Analysis::KindOpportunities, Analysis::KindActions})
        out += librarySection(kind);

    const QList<AnalysisRecord> papers =
        m_store->paperAnalyses(Analysis::KindQuick);
    if (!papers.isEmpty()) {
        out += QStringLiteral("## %1\n\n").arg(tr("The papers"));
        for (const AnalysisRecord &r : papers) {
            const QJsonObject d = r.payload;
            out += QStringLiteral("- **%1** — %2 _(%3)_\n")
                       .arg(r.title.isEmpty() ? r.paperId : r.title,
                            sanitize(d.value(QStringLiteral("oneLiner")).toString()),
                            Analysis::relevanceLabel(
                                d.value(QStringLiteral("relevance"))
                                    .toObject()
                                    .value(QStringLiteral("level"))
                                    .toString()));
        }
        out += QChar('\n');
    }
    return out;
}

bool AnalysisExporter::save(const QString &markdown, const QUrl &dest) const
{
    if (markdown.isEmpty() || !dest.isValid())
        return false;
    const QString path = dest.isLocalFile() ? dest.toLocalFile() : dest.toString();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << markdown;
    return true;
}

QString AnalysisExporter::suggestedPaperName(const QString &paperId) const
{
    AnalysisRecord rec = m_store->paperAnalysis(paperId, Analysis::KindQuick);
    if (!rec.valid)
        rec = m_store->paperAnalysis(paperId, Analysis::KindDeep);
    QString name = rec.valid && !rec.title.isEmpty() ? rec.title : paperId;
    for (const QChar c : QStringLiteral("/\\:*?\"<>|"))
        name.remove(c);
    return name.left(80).trimmed() + QStringLiteral(".md");
}
