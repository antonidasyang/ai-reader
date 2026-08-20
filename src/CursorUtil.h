#pragma once

#include <QCursor>
#include <QHash>
#include <QObject>
#include <QString>

class QQuickItem;

// QML's cursorShape only accepts the standard Qt.CursorShape enum;
// pixmap cursors (the hand-tool artwork) need QQuickItem::setCursor
// from C++. Exposed to QML as the context property `cursorUtil`.
class CursorUtil : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    // Hand-tool cursor: open hand normally, the short-fingered grab
    // while dragging — same Lucide artwork as the toolbar button,
    // rendered white-on-dark-rim so it reads on any page content.
    Q_INVOKABLE void setPanCursor(QQuickItem *item, bool grabbing);
    Q_INVOKABLE void clearCursor(QQuickItem *item);

    // Application-wide cursor for the length of a drag. An item's own
    // cursorShape only applies while the pointer is over that item, and
    // a pane grip's drag travels across the whole window — the shape
    // would flicker back to whatever it passes over. Idempotent on
    // purpose: a second push is ignored and a pop with nothing pushed
    // does nothing, so an unbalanced call can't strand the cursor.
    Q_INVOKABLE void pushOverrideCursor(int shape);
    Q_INVOKABLE void popOverrideCursor();

private:
    QCursor cursorFor(const QString &path, Qt::CursorShape fallback);

    QHash<QString, QCursor> m_cache;
    bool m_overrideActive = false;
};
