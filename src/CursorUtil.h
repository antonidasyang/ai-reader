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

private:
    QCursor cursorFor(const QString &path, Qt::CursorShape fallback);

    QHash<QString, QCursor> m_cache;
};
