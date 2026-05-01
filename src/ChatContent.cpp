#include "ChatContent.h"

#include "CodeHighlighter.h"
#include "LatexRenderer.h"
#include "MarkdownRenderer.h"

#include <algorithm>
#include <QRegularExpression>
#include <QVariantMap>
#include <QVector>

namespace {

struct Segment {
    enum Type { Text, Code, Math };
    Type type;
    QString content;
    QString lang; // code only
};

// Splits the Markdown source into top-level segments. Fenced code blocks
// (``` or ~~~ at start of a line) and display-math (`$$ … $$`) become
// their own segments; everything else is grouped into Text segments
// that are later piped through cmark for inline rendering. Math inside
// code fences is left alone (we never split a code block by `$$`).
QVector<Segment> tokenize(const QString &md)
{
    struct Range { int start; int end; Segment seg; };
    QVector<Range> ranges;

    static const QRegularExpression codeRe(
        QStringLiteral(R"((?:^|\n)(```|~~~)([^\n]*)\n([\s\S]*?)\n\1)"));
    static const QRegularExpression mathRe(
        QStringLiteral(R"(\$\$([\s\S]+?)\$\$)"));

    auto it = codeRe.globalMatch(md);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        Segment s;
        s.type = Segment::Code;
        s.lang = m.captured(2).trimmed();
        s.content = m.captured(3);
        // Skip the leading \n (if present) so the surrounding text
        // segment doesn't end with a stray blank line. Cast to int —
        // QRegularExpressionMatch::captured*() returns qsizetype, and
        // MSVC won't auto-narrow inside brace-init.
        int start = int(m.capturedStart(0));
        if (start < md.size() && md[start] == QLatin1Char('\n'))
            ++start;
        ranges.append({ start, int(m.capturedEnd(0)), s });
    }

    auto inCode = [&ranges](int pos) {
        for (const Range &r : ranges)
            if (r.seg.type == Segment::Code && pos >= r.start && pos < r.end)
                return true;
        return false;
    };

    auto mit = mathRe.globalMatch(md);
    while (mit.hasNext()) {
        const QRegularExpressionMatch m = mit.next();
        if (inCode(int(m.capturedStart(0)))) continue;
        Segment s;
        s.type = Segment::Math;
        s.content = m.captured(1).trimmed();
        ranges.append({ int(m.capturedStart(0)), int(m.capturedEnd(0)), s });
    }

    std::sort(ranges.begin(), ranges.end(),
              [](const Range &a, const Range &b) { return a.start < b.start; });

    QVector<Segment> out;
    int last = 0;
    for (const Range &r : ranges) {
        if (r.start > last) {
            Segment t;
            t.type = Segment::Text;
            t.content = md.mid(last, r.start - last);
            if (!t.content.trimmed().isEmpty())
                out.append(t);
        }
        out.append(r.seg);
        last = r.end;
    }
    if (last < md.size()) {
        Segment t;
        t.type = Segment::Text;
        t.content = md.mid(last);
        if (!t.content.trimmed().isEmpty())
            out.append(t);
    }
    return out;
}

} // namespace

ChatContent::ChatContent(MarkdownRenderer *markdown, QObject *parent)
    : QObject(parent)
    , m_markdown(markdown)
{
}

QVariantList ChatContent::render(const QString &markdown) const
{
    QVariantList out;
    if (markdown.isEmpty()) return out;

    const QVector<Segment> segs = tokenize(markdown);
    for (const Segment &s : segs) {
        QVariantMap item;
        if (s.type == Segment::Code) {
            item[QStringLiteral("type")] = QStringLiteral("code");
            item[QStringLiteral("source")] = s.content;
            item[QStringLiteral("language")] = s.lang;
            item[QStringLiteral("html")] =
                CodeHighlighter::highlight(s.content, s.lang);
        } else if (s.type == Segment::Math) {
            item[QStringLiteral("type")] = QStringLiteral("math");
            item[QStringLiteral("source")] = s.content;
            LatexRenderer &lr = LatexRenderer::instance();
            item[QStringLiteral("dataUrl")] = lr.isAvailable()
                ? lr.renderDataUrl(s.content, LatexRenderer::Display, 18)
                : QString();
        } else {
            item[QStringLiteral("type")] = QStringLiteral("text");
            // Hand the segment to MarkdownRenderer so inline `$math$`,
            // inline-code highlighting, and GFM extensions all stay in
            // one code path with the legacy RichText route.
            const QString html = m_markdown
                ? m_markdown->toHtml(s.content)
                : s.content;
            item[QStringLiteral("html")] = html;
        }
        out.append(item);
    }
    return out;
}
