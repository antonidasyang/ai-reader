#include "MetadataService.h"
#include "LibraryModel.h"
#include "PaperController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

namespace {

struct Identifier {
    QString type; // "doi" | "arxiv"
    QString value;
};

QString trimDoi(QString doi)
{
    static const QRegularExpression tail(QStringLiteral("[\\.,;:)\\]>]+$"));
    return doi.remove(tail);
}

Identifier extractIdentifier(const QString &text)
{
    static const QRegularExpression doiRe(
        QStringLiteral("10\\.\\d{4,}/[^\\s\"'<>]+"));
    const auto d = doiRe.match(text);
    if (d.hasMatch())
        return {QStringLiteral("doi"), trimDoi(d.captured(0))};

    static const QRegularExpression arxRe(
        QStringLiteral("(?:arXiv:)?(\\d{4}\\.\\d{4,5})(?:v\\d+)?"),
        QRegularExpression::CaseInsensitiveOption);
    const auto a = arxRe.match(text);
    if (a.hasMatch())
        return {QStringLiteral("arxiv"), a.captured(1)};

    return {};
}

QString cslItemType(const QString &t)
{
    if (t == QLatin1String("paper-conference"))
        return QStringLiteral("conferencePaper");
    if (t == QLatin1String("book"))
        return QStringLiteral("book");
    if (t == QLatin1String("chapter"))
        return QStringLiteral("bookSection");
    if (t == QLatin1String("thesis"))
        return QStringLiteral("thesis");
    if (t == QLatin1String("report"))
        return QStringLiteral("report");
    if (t == QLatin1String("article") || t == QLatin1String("preprint"))
        return QStringLiteral("preprint");
    return QStringLiteral("journalArticle");
}

QVariantMap cslToFields(const QJsonObject &csl)
{
    QVariantMap f;
    f.insert(QStringLiteral("title"), csl.value(QStringLiteral("title")).toString());

    QStringList authors;
    for (const QJsonValue &v : csl.value(QStringLiteral("author")).toArray()) {
        const QJsonObject a = v.toObject();
        const QString literal = a.value(QStringLiteral("literal")).toString();
        if (!literal.isEmpty()) {
            authors << literal;
            continue;
        }
        const QString given = a.value(QStringLiteral("given")).toString();
        const QString family = a.value(QStringLiteral("family")).toString();
        const QString name = (given + QChar(' ') + family).trimmed();
        if (!name.isEmpty())
            authors << name;
    }
    f.insert(QStringLiteral("creators"), authors);

    const QJsonArray dp = csl.value(QStringLiteral("issued"))
                              .toObject()
                              .value(QStringLiteral("date-parts"))
                              .toArray();
    if (!dp.isEmpty()) {
        const QJsonArray first = dp.first().toArray();
        if (!first.isEmpty())
            f.insert(QStringLiteral("year"), first.first().toInt());
    }

    const QString container =
        csl.value(QStringLiteral("container-title")).toString();
    if (!container.isEmpty())
        f.insert(QStringLiteral("publication"), container);
    const QString doi = csl.value(QStringLiteral("DOI")).toString();
    if (!doi.isEmpty())
        f.insert(QStringLiteral("doi"), doi);
    const QString abstract = csl.value(QStringLiteral("abstract")).toString();
    if (!abstract.isEmpty())
        f.insert(QStringLiteral("abstract"), abstract);
    f.insert(QStringLiteral("itemType"),
             cslItemType(csl.value(QStringLiteral("type")).toString()));
    f.insert(QStringLiteral("csljson"), csl.toVariantMap());
    return f;
}

} // namespace

MetadataService::MetadataService(LibraryModel *lib, PaperController *paper,
                                 QObject *parent)
    : QObject(parent)
    , m_lib(lib)
    , m_paper(paper)
{
}

void MetadataService::autoFill(const QString &itemId)
{
    const Identifier id = extractIdentifier(m_paper->headText());
    if (id.type.isEmpty()) {
        setStatus(tr("No DOI/arXiv id found; fill manually."));
        emit resolved(itemId, false);
        return;
    }
    if (id.type == QLatin1String("doi"))
        resolveDoi(itemId, id.value);
    else
        resolveArxiv(itemId, id.value);
}

void MetadataService::resolveIdentifier(const QString &itemId,
                                        const QString &identifier)
{
    const QString s = identifier.trimmed();
    const Identifier id = extractIdentifier(s);
    if (id.type.isEmpty()) {
        setStatus(tr("Unrecognised identifier."));
        emit resolved(itemId, false);
        return;
    }
    if (id.type == QLatin1String("doi"))
        resolveDoi(itemId, id.value);
    else
        resolveArxiv(itemId, id.value);
}

void MetadataService::resolveDoi(const QString &itemId, const QString &doi)
{
    setBusy(true);
    setStatus(tr("Looking up DOI…"));
    QNetworkRequest req{QUrl(QStringLiteral("https://doi.org/") + doi)};
    req.setRawHeader("Accept", "application/vnd.citationstyles.csl+json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, itemId, reply] {
        const QByteArray bytes = reply->readAll();
        const int status =
            reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (status < 200 || status >= 300) {
            fail(itemId, tr("DOI lookup failed (HTTP %1)").arg(status));
            return;
        }
        const QJsonObject csl = QJsonDocument::fromJson(bytes).object();
        if (csl.isEmpty()) {
            fail(itemId, tr("DOI returned no metadata."));
            return;
        }
        applyFields(itemId, cslToFields(csl));
    });
}

void MetadataService::resolveArxiv(const QString &itemId, const QString &arxivId)
{
    setBusy(true);
    setStatus(tr("Looking up arXiv…"));
    QNetworkRequest req{QUrl(
        QStringLiteral("https://export.arxiv.org/api/query?id_list=") + arxivId)};
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, itemId, arxivId, reply] {
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();

        QVariantMap f;
        QStringList authors;
        QString title, summary, published;
        QXmlStreamReader xml(bytes);
        bool inEntry = false;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const QStringView n = xml.name();
                if (n == u"entry")
                    inEntry = true;
                else if (inEntry && n == u"title")
                    title = xml.readElementText().simplified();
                else if (inEntry && n == u"summary")
                    summary = xml.readElementText().simplified();
                else if (inEntry && n == u"published")
                    published = xml.readElementText();
                else if (inEntry && n == u"name")
                    authors << xml.readElementText().simplified();
            } else if (xml.isEndElement() && xml.name() == u"entry") {
                break;
            }
        }
        if (title.isEmpty()) {
            fail(itemId, tr("arXiv returned no metadata."));
            return;
        }
        f.insert(QStringLiteral("title"), title);
        f.insert(QStringLiteral("creators"), authors);
        if (published.size() >= 4)
            f.insert(QStringLiteral("year"), published.left(4).toInt());
        if (!summary.isEmpty())
            f.insert(QStringLiteral("abstract"), summary);
        f.insert(QStringLiteral("publication"), QStringLiteral("arXiv"));
        f.insert(QStringLiteral("arxivId"), arxivId);
        f.insert(QStringLiteral("itemType"), QStringLiteral("preprint"));
        applyFields(itemId, f);
    });
}

void MetadataService::applyFields(const QString &itemId,
                                  const QVariantMap &fields)
{
    m_lib->updateItem(itemId, fields);
    setBusy(false);
    setStatus(tr("Metadata filled."));
    emit resolved(itemId, true);
}

void MetadataService::fail(const QString &itemId, const QString &message)
{
    setBusy(false);
    setStatus(message);
    emit resolved(itemId, false);
}

void MetadataService::setBusy(bool v)
{
    if (v == m_busy)
        return;
    m_busy = v;
    emit busyChanged();
}

void MetadataService::setStatus(const QString &s)
{
    if (s == m_status)
        return;
    m_status = s;
    emit statusChanged();
}
