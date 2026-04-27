#pragma once

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
};
