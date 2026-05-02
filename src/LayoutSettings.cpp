#include "LayoutSettings.h"

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
