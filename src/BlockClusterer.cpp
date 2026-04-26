#include "BlockClusterer.h"

#include <QPdfDocument>
#include <QPdfSelection>
#include <QRegularExpression>
#include <QSet>

namespace {

const QSet<QString> &commonHeadings()
{
    static const QSet<QString> set = {
        QStringLiteral("Abstract"),
        QStringLiteral("Introduction"),
        QStringLiteral("Background"),
        QStringLiteral("Related Work"),
        QStringLiteral("Methods"),
        QStringLiteral("Method"),
        QStringLiteral("Methodology"),
        QStringLiteral("Approach"),
        QStringLiteral("Experiments"),
        QStringLiteral("Evaluation"),
        QStringLiteral("Results"),
        QStringLiteral("Discussion"),
        QStringLiteral("Conclusion"),
        QStringLiteral("Conclusions"),
        QStringLiteral("Limitations"),
        QStringLiteral("Future Work"),
        QStringLiteral("References"),
        QStringLiteral("Acknowledgments"),
        QStringLiteral("Acknowledgements"),
        QStringLiteral("Appendix"),
    };
    return set;
}

Block::Kind classify(const QString &text)
{
    static const QRegularExpression captionRx(
        QStringLiteral("^(Figure|Fig\\.|Table|Tab\\.)\\s*\\d"));
    static const QRegularExpression numberedHeading(
        QStringLiteral("^\\d+(\\.\\d+){0,3}\\s+[A-Z]"));
    static const QRegularExpression romanHeading(
        QStringLiteral("^(I|II|III|IV|V|VI|VII|VIII|IX|X|XI|XII)\\.\\s+[A-Z]"));

    if (captionRx.match(text).hasMatch())
        return Block::Caption;

    if (text.length() < 140) {
        if (numberedHeading.match(text).hasMatch())
            return Block::Heading;
        if (romanHeading.match(text).hasMatch())
            return Block::Heading;
        if (commonHeadings().contains(text.trimmed()))
            return Block::Heading;
    }

    return Block::Paragraph;
}

QString joinLines(const QStringList &lines)
{
    QString out;
    out.reserve(64 * lines.size());
    for (const QString &line : lines) {
        if (line.isEmpty())
            continue;
        if (!out.isEmpty()) {
            // Hyphenation: "neur-\nons" → "neurons"
            if (out.endsWith(QChar('-')) && !out.endsWith(QStringLiteral("--"))) {
                out.chop(1);
                out += line;
                continue;
            }
            out += QChar(' ');
        }
        out += line;
    }
    return out;
}

} // namespace

QVector<Block> BlockClusterer::extract(QPdfDocument &doc)
{
    QVector<Block> blocks;
    int nextId = 0;

    for (int p = 0; p < doc.pageCount(); ++p) {
        QPdfSelection sel = doc.getAllText(p);
        const QString pageText = sel.text();
        if (pageText.trimmed().isEmpty())
            continue;

        const QStringList rawLines = pageText.split(QChar('\n'), Qt::KeepEmptyParts);

        QStringList currentPara;
        auto flush = [&]() {
            if (currentPara.isEmpty())
                return;
            const QString text = joinLines(currentPara).trimmed();
            currentPara.clear();
            if (text.isEmpty())
                return;
            Block b;
            b.id = nextId;
            b.ord = nextId;
            b.page = p;
            b.text = text;
            b.kind = classify(text);
            blocks.append(b);
            ++nextId;
        };

        for (const QString &raw : rawLines) {
            const QString line = raw.trimmed();
            if (line.isEmpty())
                flush();
            else
                currentPara.append(line);
        }
        flush();
    }

    return blocks;
}
