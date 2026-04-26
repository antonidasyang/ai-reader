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

bool isHyphenLike(QChar c)
{
    const ushort u = c.unicode();
    return u == 0x002D    // HYPHEN-MINUS
        || u == 0x2010    // HYPHEN
        || u == 0x2011;   // NON-BREAKING HYPHEN
}

// QChar::isPrint() is false for Cc (control), Cf (format — incl. soft hyphen,
// zero-width joiners, BOM), Cs (surrogate), Co (private use), Cn (unassigned).
// Drop those entirely; normalize any whitespace to a plain space.
QString sanitize(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        if (c.isSpace()) {
            out.append(QChar(' '));
            continue;
        }
        if (!c.isPrint())
            continue;
        out.append(c);
    }
    return out;
}

struct ProcessedLine {
    QString text;
    bool hyphenated = false;
    bool blank = false;
};

// PDFium often emits a private-use codepoint at end-of-line where a word is
// hyphenated across lines (the PDF's embedded font defined a hyphen glyph
// there; no system font can render it). Treat ANY non-printable trailing
// char the same as a real hyphen: chop it and mark the line as hyphenated.
ProcessedLine preprocessLine(QString raw)
{
    ProcessedLine pl;
    raw = raw.trimmed();
    if (raw.isEmpty()) {
        pl.blank = true;
        return pl;
    }

    QChar last = raw.at(raw.size() - 1);
    if (isHyphenLike(last)) {
        raw.chop(1);
        pl.hyphenated = true;
    } else if (!last.isPrint() && !last.isSpace()) {
        raw.chop(1);
        pl.hyphenated = true;
    }

    pl.text = sanitize(raw).trimmed();
    if (pl.text.isEmpty())
        pl.blank = true;
    return pl;
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

        QVector<ProcessedLine> lines;
        const QStringList rawLines = pageText.split(QChar('\n'), Qt::KeepEmptyParts);
        lines.reserve(rawLines.size());
        for (const QString &raw : rawLines)
            lines.append(preprocessLine(raw));

        QString currentPara;
        bool prevHyphenated = false;
        auto flush = [&]() {
            if (currentPara.isEmpty())
                return;
            Block b;
            b.id = nextId;
            b.ord = nextId;
            b.page = p;
            b.text = currentPara.trimmed();
            b.kind = classify(b.text);
            blocks.append(b);
            ++nextId;
            currentPara.clear();
        };

        for (const ProcessedLine &line : lines) {
            if (line.blank) {
                flush();
                prevHyphenated = false;
                continue;
            }
            if (currentPara.isEmpty()) {
                currentPara = line.text;
            } else if (prevHyphenated) {
                currentPara += line.text;
            } else {
                currentPara += QChar(' ');
                currentPara += line.text;
            }
            prevHyphenated = line.hyphenated;
        }
        flush();
    }

    return blocks;
}
