#include "LayoutSettings.h"

#include <QCoreApplication>
#include <QFile>

namespace {
constexpr auto kKey               = "layout/paneOrder";
constexpr auto kWizardKey         = "wizard/seen";
constexpr auto kSplitterStateKey  = "layout/splitterState";
constexpr auto kLastSeenVersionKey = "changelog/lastSeenVersion";
} // namespace

LayoutSettings::LayoutSettings(QObject *parent)
    : QObject(parent)
{
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

QString LayoutSettings::readChangelog() const
{
    // 1. Qt resource — the production path. CMake's qt_add_resources
    //    bakes CHANGELOG.md into the binary at qrc:/CHANGELOG.md so
    //    every install carries the version that matches the binary.
    QFile fromResource(QStringLiteral(":/CHANGELOG.md"));
    if (fromResource.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString::fromUtf8(fromResource.readAll());

    // 2. Filesystem fallback — handles dev runs from a build dir
    //    that for some reason didn't include the resource (incremental
    //    build skipped the qt_add_resources regeneration), and any
    //    install whose installer dropped a sibling copy.
    QFile fromDisk(QCoreApplication::applicationDirPath()
                   + QStringLiteral("/CHANGELOG.md"));
    if (fromDisk.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString::fromUtf8(fromDisk.readAll());

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
