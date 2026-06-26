#include "LatexRenderer.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QString>
#include <QStringList>

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
    // MicroTeX needs its res/ tree (font + glyph definitions) at
    // startup. Candidate locations, most-specific first:
    //
    //   1. macOS only: <bundle>/Contents/Resources/microtex_res —
    //      staged by the APPLE POST_BUILD step in CMakeLists.txt and
    //      carried into the signed .app/.dmg by installer/macos/make-dmg.sh.
    //   2. <exe-dir>/microtex_res — what the Windows packaged install
    //      ships (its own POST_BUILD mirrors res/ there; AiReader.iss
    //      bundles it).
    //   3. AIREADER_MICROTEX_RES_DIR — the absolute build-machine path
    //      baked in by CMake; valid only from the developer checkout.
    //
    // A directory counts as a res root iff it holds the
    // ".clatexmath-res_root" marker MicroTeX itself looks for (see
    // queryResourceLocation in microtex/src/latex.cpp). The previous
    // "/TeXFonts" probe matched an older upstream layout that the
    // pinned MicroTeX master no longer ships, which silently disabled
    // math on every platform.
    const auto isResDir = [](const QString &dir) {
        return QFileInfo::exists(dir + QStringLiteral("/.clatexmath-res_root"));
    };

    const QString exeDir = QCoreApplication::applicationDirPath();
    QStringList candidates;
#ifdef Q_OS_MACOS
    candidates << QDir::cleanPath(exeDir
                   + QStringLiteral("/../Resources/microtex_res"));
#endif
    candidates << exeDir + QStringLiteral("/microtex_res");
    candidates << QString::fromUtf8(AIREADER_MICROTEX_RES_DIR);

    QString resDir;
    for (const QString &candidate : candidates) {
        if (isResDir(candidate)) {
            resDir = candidate;
            break;
        }
    }

    if (resDir.isEmpty()) {
        qWarning("LatexRenderer: no MicroTeX res dir found (tried %s); "
                 "math will fall back to raw LaTeX.",
                 qUtf8Printable(candidates.join(QStringLiteral(", "))));
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
