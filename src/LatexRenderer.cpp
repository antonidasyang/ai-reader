#include "LatexRenderer.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QFileInfo>
#include <QPainter>
#include <QString>

// MicroTeX public headers. The library is pulled in via FetchContent
// and exposes `src/` on the include path.
#include <latex.h>
#include <platform/qt/graphic_qt.h>

namespace {

QString cacheKey(const QString &latex, LatexRenderer::Mode mode, int sizePx)
{
    // Tab separator avoids ambiguity with anything LaTeX bodies might
    // legitimately contain.
    return QString::number(static_cast<int>(mode))
         + QChar('\t') + QString::number(sizePx)
         + QChar('\t') + latex;
}

} // namespace

LatexRenderer &LatexRenderer::instance()
{
    static LatexRenderer r;
    return r;
}

LatexRenderer::LatexRenderer()
{
    // MicroTeX needs a directory of font/glyph definitions at startup.
    // Two valid sources:
    //
    //   1. <exe-dir>/microtex_res — what packaged installs ship; the
    //      Windows POST_BUILD step in CMakeLists.txt mirrors the
    //      upstream res/ tree there, and AiReader.iss bundles it.
    //   2. AIREADER_MICROTEX_RES_DIR — the absolute build-machine
    //      path baked in by CMake. Only valid when running from the
    //      developer's own checkout, but it's the only thing
    //      available in early dev before D8 packaging exists.
    //
    // We pick the first one that contains the expected layout
    // (TeXFonts/) so a stale build-machine path on a packaged install
    // never overrides the relocatable copy.
    const QString relocatable =
        QCoreApplication::applicationDirPath() + QStringLiteral("/microtex_res");
    const QString builtIn =
        QString::fromUtf8(AIREADER_MICROTEX_RES_DIR);

    QString resDir;
    if (QFileInfo(relocatable + QStringLiteral("/TeXFonts")).isDir())
        resDir = relocatable;
    else if (QFileInfo(builtIn + QStringLiteral("/TeXFonts")).isDir())
        resDir = builtIn;

    if (resDir.isEmpty()) {
        qWarning("LatexRenderer: no MicroTeX res dir found "
                 "(tried %s and %s); math will fall back to raw LaTeX.",
                 qUtf8Printable(relocatable),
                 qUtf8Printable(builtIn));
        return;
    }

    try {
        tex::LaTeX::init(resDir.toLocal8Bit().constData());
        m_initialized = true;
        qInfo("LatexRenderer: MicroTeX initialised from %s",
              qUtf8Printable(resDir));
    } catch (const std::exception &e) {
        qWarning("LatexRenderer: MicroTeX init failed: %s", e.what());
    } catch (...) {
        qWarning("LatexRenderer: MicroTeX init failed (unknown exception)");
    }
}

LatexRenderer::~LatexRenderer()
{
    if (m_initialized)
        tex::LaTeX::release();
}

QImage LatexRenderer::render(const QString &latex, Mode mode, int textSizePx)
{
    if (!m_initialized)
        return {};
    const QString trimmed = latex.trimmed();
    if (trimmed.isEmpty())
        return {};

    const QString key = cacheKey(trimmed, mode, textSizePx);
    auto cached = m_cache.constFind(key);
    if (cached != m_cache.constEnd())
        return *cached;

    // Display math gets ~3× more horizontal slack so wide formulas
    // (matrices, long sums) don't immediately wrap.
    const int width = (mode == Display) ? 1200 : 400;
    const float lineSpace = textSizePx / 3.0f;

    tex::TeXRender *r = nullptr;
    try {
        r = tex::LaTeX::parse(trimmed.toStdWString(),
                              width,
                              static_cast<float>(textSizePx),
                              lineSpace,
                              0xff1d1d1d);
    } catch (const std::exception &e) {
        qWarning("LatexRenderer: parse failed for '%s': %s",
                 qUtf8Printable(trimmed), e.what());
        m_cache.insert(key, {});
        return {};
    } catch (...) {
        qWarning("LatexRenderer: parse failed for '%s'",
                 qUtf8Printable(trimmed));
        m_cache.insert(key, {});
        return {};
    }
    if (!r) {
        m_cache.insert(key, {});
        return {};
    }

    const int w = r->getWidth();
    const int h = r->getHeight();
    if (w <= 0 || h <= 0) {
        delete r;
        m_cache.insert(key, {});
        return {};
    }

    QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter painter(&img);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        tex::Graphics2D_qt g2(&painter);
        r->draw(g2, 0, 0);
    }
    delete r;

    m_cache.insert(key, img);
    return img;
}

QString LatexRenderer::renderDataUrl(const QString &latex, Mode mode, int textSizePx)
{
    const QImage img = render(latex, mode, textSizePx);
    if (img.isNull())
        return {};

    QByteArray buf;
    {
        QBuffer io(&buf);
        if (!io.open(QIODevice::WriteOnly))
            return {};
        if (!img.save(&io, "PNG"))
            return {};
    }
    return QStringLiteral("data:image/png;base64,")
         + QString::fromLatin1(buf.toBase64());
}
