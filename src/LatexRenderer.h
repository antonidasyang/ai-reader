#pragma once

#include <QHash>
#include <QImage>
#include <QString>

// Process-wide LaTeX → QImage renderer backed by MicroTeX. Initializes
// the MicroTeX context lazily on first use so apps that never touch math
// pay no startup cost. Thread-affinity: the underlying QPainter draws on
// a QImage owned by this class, so calls must originate from the GUI
// thread (which is where MarkdownRenderer::toHtml currently runs).
//
// Cached by (mode, sizePx, latex) since every chat re-render walks the
// same Markdown body and re-rendering the same formula is wasted work.
class LatexRenderer
{
public:
    enum Mode { Inline, Display };

    static LatexRenderer &instance();

    // Returns a transparent-background PNG of the rendered formula, or
    // a null QImage when MicroTeX failed to init or the source did not
    // parse. Caller is expected to fall back to plain styled text.
    QImage render(const QString &latex, Mode mode, int textSizePx = 16);

    // Convenience: returns `data:image/png;base64,…` ready to drop into
    // an HTML <img src="…"/> attribute. Empty string on failure.
    QString renderDataUrl(const QString &latex, Mode mode, int textSizePx = 16);

    bool isAvailable() const { return m_initialized; }

private:
    LatexRenderer();
    ~LatexRenderer();
    LatexRenderer(const LatexRenderer &) = delete;
    LatexRenderer &operator=(const LatexRenderer &) = delete;

    bool m_initialized = false;
    QHash<QString, QImage> m_cache;
};
