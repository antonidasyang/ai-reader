#pragma once

#include <QColor>
#include <QObject>
#include <QString>

// Stateless Markdown→HTML renderer backed by cmark-gfm. GFM extensions
// (tables, strikethrough, autolinks, task lists, footnotes) are enabled.
// Exposed to QML as the `markdown` context property; chat replies route
// their final Markdown through `toHtml()` and are shown via TextEdit's
// RichText format.
class MarkdownRenderer : public QObject
{
    Q_OBJECT
public:
    explicit MarkdownRenderer(QObject *parent = nullptr);

    Q_INVOKABLE QString toHtml(const QString &markdown) const;

    // A translation is plain text, not Markdown — but the model rebuilds
    // the paper's flattened formulas as $…$ / $$…$$ LaTeX. This renders
    // exactly those spans as images (drawn in `ink`, sized to `fontPx`)
    // and HTML-escapes everything else. Returns an empty string when no
    // span rendered, so the caller can stay on the cheaper PlainText
    // path; a span that fails to parse stays as its literal $…$ text
    // rather than a styled fallback — this is a reading surface.
    Q_INVOKABLE QString plainTextWithMath(const QString &text, int fontPx,
                                          const QColor &ink) const;
};
