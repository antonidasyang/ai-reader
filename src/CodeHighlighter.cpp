#include "CodeHighlighter.h"

#include <QSet>
#include <QString>

namespace CodeHighlighter {

QString htmlEscape(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (QChar c : s) {
        const ushort u = c.unicode();
        if (u == '<')      out += QStringLiteral("&lt;");
        else if (u == '>') out += QStringLiteral("&gt;");
        else if (u == '&') out += QStringLiteral("&amp;");
        else if (u == '"') out += QStringLiteral("&quot;");
        else               out += c;
    }
    return out;
}

namespace {

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
        s.keywords.clear();
    }
    return s;
}

QString wrap(const QString &color, const QString &content)
{
    return QStringLiteral("<span style=\"color:%1\">").arg(color)
         + content + QStringLiteral("</span>");
}

QString highlightImpl(const QString &src, const LangSpec &spec)
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

} // namespace

QString highlight(const QString &source, const QString &language)
{
    return highlightImpl(source, specForLang(language));
}

} // namespace CodeHighlighter
