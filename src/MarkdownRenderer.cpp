#include "MarkdownRenderer.h"

#include <cmark-gfm.h>
#include <cmark-gfm-core-extensions.h>

namespace {

void attachExtension(cmark_parser *parser, const char *name)
{
    cmark_syntax_extension *ext = cmark_find_syntax_extension(name);
    if (ext) cmark_parser_attach_syntax_extension(parser, ext);
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

    const QByteArray utf8 = markdown.toUtf8();
    cmark_parser *parser = cmark_parser_new(CMARK_OPT_DEFAULT);
    if (!parser) return markdown;

    // GFM extensions — strictly the ones that ship with libcmark-gfm.
    for (const char *name : { "table", "strikethrough", "autolink",
                              "tasklist", "footnotes" }) {
        attachExtension(parser, name);
    }

    cmark_parser_feed(parser, utf8.constData(), utf8.size());
    cmark_node *doc = cmark_parser_finish(parser);
    cmark_llist *exts = cmark_parser_get_syntax_extensions(parser);

    char *html = cmark_render_html(doc, CMARK_OPT_DEFAULT, exts);
    QString out = html ? QString::fromUtf8(html) : QString();

    free(html);
    cmark_node_free(doc);
    cmark_parser_free(parser);
    return out;
}
