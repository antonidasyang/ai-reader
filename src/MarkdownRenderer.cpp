#include "MarkdownRenderer.h"

#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QVector>

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

namespace {

void attachExtension(cmark_parser *parser, const char *name)
{
    cmark_syntax_extension *ext = cmark_find_syntax_extension(name);
    if (ext) cmark_parser_attach_syntax_extension(parser, ext);
}

QString htmlEscape(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        ushort u = c.unicode();
        if (u == '<')      out += QStringLiteral("&lt;");
        else if (u == '>') out += QStringLiteral("&gt;");
        else if (u == '&') out += QStringLiteral("&amp;");
        else if (u == '"') out += QStringLiteral("&quot;");
        else               out += c;
    }
    return out;
}

// ── Tiny token-state-machine syntax highlighter ─────────────────────────
// Covers the languages most commonly seen in chat replies. Not as good as
// KSyntaxHighlighting but ships with no extra deps. Output is the same
// HTML cmark would emit (`<pre><code class="language-X">…</code></pre>`)
// with inline color spans inside the <code> body.

struct LangSpec {
    QSet<QString> keywords;
    bool lineCommentSlash = false; // //
    bool lineCommentHash  = false; // #
    bool blockComment     = false; // /* */
    bool stringDouble     = true;
    bool stringSingle     = false;
    bool stringBacktick   = false;
};

LangSpec specForLang(QString lang)
{
    lang = lang.toLower();
    LangSpec s;
    if (lang == "cpp" || lang == "c" || lang == "c++" || lang == "cxx"
        || lang == "objc" || lang == "objcpp") {
        s.keywords = { "alignas","alignof","and","asm","auto","bool","break",
            "case","catch","char","class","const","constexpr","continue",
            "default","delete","do","double","else","enum","explicit",
            "extern","false","float","for","friend","goto","if","inline",
            "int","long","mutable","namespace","new","noexcept","not",
            "nullptr","operator","or","private","protected","public",
            "register","return","short","signed","sizeof","static",
            "struct","switch","template","this","throw","true","try",
            "typedef","typeid","typename","union","unsigned","using",
            "virtual","void","volatile","while","override","final" };
        s.lineCommentSlash = true;
        s.blockComment = true;
        s.stringSingle = true;
    } else if (lang == "python" || lang == "py") {
        s.keywords = { "and","as","assert","async","await","break","class",
            "continue","def","del","elif","else","except","False","finally",
            "for","from","global","if","import","in","is","lambda","None",
            "nonlocal","not","or","pass","raise","return","True","try",
            "while","with","yield","self","cls","match","case" };
        s.lineCommentHash = true;
        s.stringSingle = true;
    } else if (lang == "javascript" || lang == "js" || lang == "typescript"
               || lang == "ts" || lang == "tsx" || lang == "jsx") {
        s.keywords = { "async","await","break","case","catch","class","const",
            "continue","debugger","default","delete","do","else","export",
            "extends","false","finally","for","from","function","if","import",
            "in","instanceof","let","new","null","of","return","static",
            "super","switch","this","throw","true","try","typeof","undefined",
            "var","void","while","with","yield","interface","type","enum",
            "implements","public","private","protected","readonly","as",
            "namespace","module","declare","abstract" };
        s.lineCommentSlash = true;
        s.blockComment = true;
        s.stringSingle = true;
        s.stringBacktick = true;
    } else if (lang == "json") {
        s.keywords = { "true","false","null" };
    } else if (lang == "bash" || lang == "sh" || lang == "shell" || lang == "zsh") {
        s.keywords = { "if","then","else","elif","fi","for","do","done",
            "while","case","esac","function","return","in","exit","local",
            "export","readonly","declare","unset","break","continue","echo" };
        s.lineCommentHash = true;
        s.stringSingle = true;
    } else {
        // Unknown language — caller falls back to plain escape.
        s.keywords.clear();
    }
    return s;
}

QString wrap(const QString &color, const QString &content)
{
    return QStringLiteral("<span style=\"color:%1\">").arg(color)
         + content + QStringLiteral("</span>");
}

QString highlightCode(const QString &src, const LangSpec &spec)
{
    if (spec.keywords.isEmpty() && !spec.lineCommentSlash
        && !spec.lineCommentHash && !spec.blockComment)
        return htmlEscape(src);

    static const QString kCommentColor = QStringLiteral("#7a7a7a");
    static const QString kStringColor  = QStringLiteral("#22863a");
    static const QString kKeywordColor = QStringLiteral("#d73a49");
    static const QString kNumberColor  = QStringLiteral("#005cc5");

    enum State { Normal, InString, InLineComment, InBlockComment };
    State state = Normal;
    QChar stringQuote;
    QString buf;
    QString out;
    out.reserve(src.size() * 2);

    auto flush = [&](const QString &color) {
        if (!buf.isEmpty()) { out += wrap(color, buf); buf.clear(); }
    };

    int i = 0;
    while (i < src.size()) {
        const QChar c = src[i];
        if (state == Normal) {
            if (spec.lineCommentSlash && c == QLatin1Char('/')
                && i + 1 < src.size() && src[i + 1] == QLatin1Char('/')) {
                state = InLineComment;
                buf = QStringLiteral("//");
                i += 2;
                continue;
            }
            if (spec.blockComment && c == QLatin1Char('/')
                && i + 1 < src.size() && src[i + 1] == QLatin1Char('*')) {
                state = InBlockComment;
                buf = QStringLiteral("/*");
                i += 2;
                continue;
            }
            if (spec.lineCommentHash && c == QLatin1Char('#')) {
                state = InLineComment;
                buf = QStringLiteral("#");
                i += 1;
                continue;
            }
            if ((spec.stringDouble   && c == QLatin1Char('"'))
                || (spec.stringSingle && c == QLatin1Char('\''))
                || (spec.stringBacktick && c == QLatin1Char('`'))) {
                state = InString;
                stringQuote = c;
                buf = htmlEscape(QString(c));
                i += 1;
                continue;
            }
            if (c.isLetter() || c == QLatin1Char('_')) {
                int j = i;
                while (j < src.size()
                       && (src[j].isLetterOrNumber() || src[j] == QLatin1Char('_')))
                    ++j;
                const QString word = src.mid(i, j - i);
                if (spec.keywords.contains(word))
                    out += wrap(kKeywordColor, htmlEscape(word));
                else
                    out += htmlEscape(word);
                i = j;
                continue;
            }
            if (c.isDigit()) {
                int j = i;
                while (j < src.size()
                       && (src[j].isLetterOrNumber()
                           || src[j] == QLatin1Char('.')
                           || src[j] == QLatin1Char('_')))
                    ++j;
                out += wrap(kNumberColor, htmlEscape(src.mid(i, j - i)));
                i = j;
                continue;
            }
            out += htmlEscape(QString(c));
            ++i;
        } else if (state == InLineComment) {
            buf += htmlEscape(QString(c));
            ++i;
            if (c == QLatin1Char('\n')) {
                flush(kCommentColor);
                state = Normal;
            }
        } else if (state == InBlockComment) {
            if (c == QLatin1Char('*') && i + 1 < src.size()
                && src[i + 1] == QLatin1Char('/')) {
                buf += QStringLiteral("*/");
                i += 2;
                flush(kCommentColor);
                state = Normal;
            } else {
                buf += htmlEscape(QString(c));
                ++i;
            }
        } else { // InString
            if (c == QLatin1Char('\\') && i + 1 < src.size()) {
                buf += htmlEscape(QString(c));
                buf += htmlEscape(QString(src[i + 1]));
                i += 2;
                continue;
            }
            buf += htmlEscape(QString(c));
            ++i;
            if (c == stringQuote || c == QLatin1Char('\n')) {
                flush(kStringColor);
                state = Normal;
            }
        }
    }
    if (state == InLineComment || state == InBlockComment)
        flush(kCommentColor);
    else if (state == InString)
        flush(kStringColor);

    return out;
}

void highlightCodeBlocks(cmark_node *root)
{
    QVector<cmark_node *> targets;
    cmark_iter *iter = cmark_iter_new(root);
    cmark_event_type ev;
    while ((ev = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        if (ev != CMARK_EVENT_ENTER) continue;
        cmark_node *n = cmark_iter_get_node(iter);
        if (cmark_node_get_type(n) == CMARK_NODE_CODE_BLOCK)
            targets.append(n);
    }
    cmark_iter_free(iter);

    for (cmark_node *n : targets) {
        const char *fence = cmark_node_get_fence_info(n);
        const char *literal = cmark_node_get_literal(n);
        const QString lang = QString::fromUtf8(fence ? fence : "").trimmed();
        const QString src  = QString::fromUtf8(literal ? literal : "");
        const LangSpec spec = specForLang(lang);
        const QString colored = highlightCode(src, spec);

        QString html = QStringLiteral(
            "<pre style=\"background:#f6f8fa;"
            "padding:8px;border-radius:4px;font-family:monospace;"
            "white-space:pre-wrap;\">");
        if (!lang.isEmpty())
            html += QStringLiteral("<code class=\"language-%1\">").arg(htmlEscape(lang));
        else
            html += QStringLiteral("<code>");
        html += colored;
        html += QStringLiteral("</code></pre>\n");

        cmark_node *htmlNode = cmark_node_new(CMARK_NODE_HTML_BLOCK);
        cmark_node_set_literal(htmlNode, html.toUtf8().constData());
        cmark_node_replace(n, htmlNode);
        cmark_node_free(n);
    }
}

// ── Math styling pre-pass ───────────────────────────────────────────────
// Real math rendering needs MicroTeX or KaTeX. As a placeholder, wrap
// inline `$…$` and display `$$…$$` segments in HTML <code>/<pre> tags
// with a distinct yellow background so they stand out from prose. Inner
// content is emitted as HTML numeric character references so cmark's
// inline-content parser doesn't mangle backslashes / asterisks / etc.

QString entityEncodeForMath(const QString &s)
{
    QString out;
    out.reserve(s.size() * 6);
    for (QChar c : s) {
        const ushort u = c.unicode();
        // Encode any char Markdown might interpret. Letters/digits pass
        // through to keep the output compact.
        if (c.isLetterOrNumber() || c == QLatin1Char(' ')
            || c == QLatin1Char('-') || c == QLatin1Char('+')
            || c == QLatin1Char('=') || c == QLatin1Char('/')
            || c == QLatin1Char(',') || c == QLatin1Char('.')
            || c == QLatin1Char(':') || c == QLatin1Char(';')
            || c == QLatin1Char('?') || c == QLatin1Char('!')
            || c == QLatin1Char('\'') || c == QLatin1Char('"')) {
            out += c;
        } else {
            out += QStringLiteral("&#%1;").arg(u);
        }
    }
    return out;
}

QString styleMath(const QString &md)
{
    static const QRegularExpression dispRe(
        QStringLiteral(R"(\$\$([\s\S]+?)\$\$)"));
    static const QRegularExpression inlineRe(
        QStringLiteral(R"(\$([^\$\n]+?)\$)"));

    QString r = md;
    {
        // Display math first so inline doesn't eat the inner $.
        QString out;
        int last = 0;
        QRegularExpressionMatchIterator it = dispRe.globalMatch(r);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            out += r.mid(last, m.capturedStart() - last);
            const QString inner = entityEncodeForMath(m.captured(1));
            out += QStringLiteral(
                "<pre style=\"background:#fff8d6;color:#5a3e00;"
                "padding:6px 8px;border-radius:4px;"
                "font-family:monospace;white-space:pre-wrap;\">$$")
                + inner
                + QStringLiteral("$$</pre>");
            last = m.capturedEnd();
        }
        out += r.mid(last);
        r = out;
    }
    {
        QString out;
        int last = 0;
        QRegularExpressionMatchIterator it = inlineRe.globalMatch(r);
        while (it.hasNext()) {
            QRegularExpressionMatch m = it.next();
            // Skip if the captured content is empty / pure whitespace; that
            // catches $$ pairs that survived the display pass and stray $.
            const QString raw = m.captured(1);
            if (raw.trimmed().isEmpty()) continue;
            out += r.mid(last, m.capturedStart() - last);
            out += QStringLiteral(
                "<code style=\"background:#fff8d6;color:#5a3e00;"
                "padding:0 3px;border-radius:3px;"
                "font-family:monospace;\">$")
                + entityEncodeForMath(raw)
                + QStringLiteral("$</code>");
            last = m.capturedEnd();
        }
        out += r.mid(last);
        r = out;
    }
    return r;
}

} // namespace

MarkdownRenderer::MarkdownRenderer(QObject *parent)
    : QObject(parent)
{
    cmark_gfm_core_extensions_ensure_registered();
}

QString MarkdownRenderer::toHtml(const QString &markdown) const
{
    if (markdown.isEmpty()) return {};

    const QString prepared = styleMath(markdown);
    const QByteArray utf8 = prepared.toUtf8();
    cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE);
    if (!parser) return markdown;

    // GFM extensions — strictly the ones that ship with libcmark-gfm.
    for (const char *name : { "table", "strikethrough", "autolink",
                              "tasklist", "footnotes" }) {
        attachExtension(parser, name);
    }

    cmark_parser_feed(parser, utf8.constData(), utf8.size());
    cmark_node *doc = cmark_parser_finish(parser);

    highlightCodeBlocks(doc);

    cmark_llist *exts = cmark_parser_get_syntax_extensions(parser);
    char *html = cmark_render_html(doc, CMARK_OPT_DEFAULT | CMARK_OPT_UNSAFE, exts);
    QString out = html ? QString::fromUtf8(html) : QString();

    free(html);
    cmark_node_free(doc);
    cmark_parser_free(parser);
    return out;
}
