#include "CursorUtil.h"

#include <QGuiApplication>
#include <QImageReader>
#include <QPixmap>
#include <QQuickItem>
#include <QScreen>

QCursor CursorUtil::cursorFor(const QString &path, Qt::CursorShape fallback)
{
    const auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd())
        return *it;

    // Render at the highest screen DPR so the cursor stays crisp on
    // retina displays; 24 logical px matches the OS hand cursors.
    qreal dpr = 1.0;
    const auto screens = QGuiApplication::screens();
    for (const QScreen *s : screens)
        dpr = qMax(dpr, s->devicePixelRatio());
    const int logical = 24;

    QImageReader reader(path);
    reader.setScaledSize(QSize(qRound(logical * dpr), qRound(logical * dpr)));
    const QImage img = reader.read();
    QCursor cur;
    if (img.isNull()) {
        // No SVG image plugin — fall back to the system cursor.
        cur = QCursor(fallback);
    } else {
        QPixmap pm = QPixmap::fromImage(img);
        pm.setDevicePixelRatio(dpr);
        // Hotspot at the palm center, in device-independent pixels.
        cur = QCursor(pm, logical / 2, logical / 2);
    }
    m_cache.insert(path, cur);
    return cur;
}

void CursorUtil::setPanCursor(QQuickItem *item, bool grabbing)
{
    if (!item)
        return;
    item->setCursor(grabbing
        ? cursorFor(QStringLiteral(":/icons/cursor-hand-grab.svg"),
                    Qt::ClosedHandCursor)
        : cursorFor(QStringLiteral(":/icons/cursor-hand-open.svg"),
                    Qt::OpenHandCursor));
}

void CursorUtil::clearCursor(QQuickItem *item)
{
    if (item)
        item->unsetCursor();
}

void CursorUtil::pushOverrideCursor(int shape)
{
    if (m_overrideActive)
        return;
    QGuiApplication::setOverrideCursor(
        QCursor(static_cast<Qt::CursorShape>(shape)));
    m_overrideActive = true;
}

void CursorUtil::popOverrideCursor()
{
    if (!m_overrideActive)
        return;
    QGuiApplication::restoreOverrideCursor();
    m_overrideActive = false;
}
