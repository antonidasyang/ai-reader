#include "LayoutSettings.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>

namespace {
constexpr auto kKey               = "layout/paneOrder";
constexpr auto kWizardKey         = "wizard/seen";
constexpr auto kSplitterStateKey  = "layout/splitterState";
constexpr auto kLastSeenVersionKey = "changelog/lastSeenVersion";
} // namespace

LayoutSettings::LayoutSettings(QObject *parent)
    : QObject(parent)
{
    m_widthSaveTimer.setSingleShot(true);
    m_widthSaveTimer.setInterval(300);
    connect(&m_widthSaveTimer, &QTimer::timeout,
            this, &LayoutSettings::flushPendingWidths);
}

QString LayoutSettings::paneOrder() const
{
    return m_qs.value(kKey).toString();
}

void LayoutSettings::setPaneOrder(const QString &csv)
{
    if (m_qs.value(kKey).toString() == csv)
        return;
    m_qs.setValue(kKey, csv);
    m_qs.sync();
}

bool LayoutSettings::wizardSeen() const
{
    return m_qs.value(kWizardKey, false).toBool();
}

void LayoutSettings::setWizardSeen(bool seen)
{
    if (m_qs.value(kWizardKey, false).toBool() == seen)
        return;
    m_qs.setValue(kWizardKey, seen);
    m_qs.sync();
}

QByteArray LayoutSettings::splitterState() const
{
    return m_qs.value(kSplitterStateKey).toByteArray();
}

void LayoutSettings::setSplitterState(const QByteArray &state)
{
    if (m_qs.value(kSplitterStateKey).toByteArray() == state)
        return;
    m_qs.setValue(kSplitterStateKey, state);
    m_qs.sync();
}

QString LayoutSettings::lastSeenVersion() const
{
    return m_qs.value(kLastSeenVersionKey).toString();
}

void LayoutSettings::setLastSeenVersion(const QString &v)
{
    if (m_qs.value(kLastSeenVersionKey).toString() == v)
        return;
    m_qs.setValue(kLastSeenVersionKey, v);
    m_qs.sync();
}

QString LayoutSettings::readChangelog(const QString &localeCode) const
{
    // Build the candidate filename list in priority order. We try
    // localized first, then fall back to plain CHANGELOG.md. Each
    // name is checked against (a) the Qt resource and (b) a sibling
    // file beside the executable, in that order.
    QStringList names;
    QString locale = localeCode.trimmed();
    if (locale.isEmpty()) {
        // Empty in Settings means "follow the system locale". Map a
        // few common forms to the file basenames we ship.
        locale = QLocale::system().name();
    }
    if (!locale.isEmpty()) {
        names << QStringLiteral("CHANGELOG.%1.md").arg(locale);
        // Generic-Chinese fallback — if we ship CHANGELOG.zh_CN.md
        // we still want a zh_TW user to see Chinese rather than
        // jumping straight to English.
        if (locale.startsWith(QStringLiteral("zh"), Qt::CaseInsensitive)
            && locale != QStringLiteral("zh_CN")) {
            names << QStringLiteral("CHANGELOG.zh_CN.md");
        }
    }
    names << QStringLiteral("CHANGELOG.md");

    const QString exeDir = QCoreApplication::applicationDirPath();
    for (const QString &name : std::as_const(names)) {
        QFile fromResource(QStringLiteral(":/") + name);
        if (fromResource.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString::fromUtf8(fromResource.readAll());
        QFile fromDisk(exeDir + QChar('/') + name);
        if (fromDisk.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString::fromUtf8(fromDisk.readAll());
    }
    return {};
}

bool LayoutSettings::paneVisible(const QString &name, bool defaultValue) const
{
    if (name.isEmpty()) return defaultValue;
    return m_qs.value(QStringLiteral("panes/%1/visible").arg(name),
                      defaultValue).toBool();
}

void LayoutSettings::setPaneVisible(const QString &name, bool visible)
{
    if (name.isEmpty()) return;
    const QString key = QStringLiteral("panes/%1/visible").arg(name);
    if (m_qs.value(key).toBool() == visible
        && m_qs.contains(key)) return;  // skip the redundant write
    m_qs.setValue(key, visible);
    m_qs.sync();
}

int LayoutSettings::paneWidth(const QString &name, int defaultWidth) const
{
    if (name.isEmpty()) return defaultWidth;
    return m_qs.value(QStringLiteral("panes/%1/width").arg(name),
                      defaultWidth).toInt();
}

void LayoutSettings::setPaneWidth(const QString &name, int width)
{
    if (name.isEmpty()) return;
    // Skip transient zeros that fire while a hidden pane is being
    // re-laid-out. Saving 0 would resurrect the pane at zero width
    // on the next launch.
    if (width <= 0) return;

    m_pendingWidths.insert(name, width);
    if (!m_widthSaveTimer.isActive())
        m_widthSaveTimer.start();
}

void LayoutSettings::flushPendingWidths()
{
    bool anyChange = false;
    for (auto it = m_pendingWidths.cbegin(); it != m_pendingWidths.cend(); ++it) {
        const QString key = QStringLiteral("panes/%1/width").arg(it.key());
        if (m_qs.value(key).toInt() != it.value()) {
            m_qs.setValue(key, it.value());
            anyChange = true;
        }
    }
    m_pendingWidths.clear();
    if (anyChange) m_qs.sync();
}
