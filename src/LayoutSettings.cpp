#include "LayoutSettings.h"

namespace {
constexpr auto kKey         = "layout/paneOrder";
constexpr auto kWizardKey   = "wizard/seen";
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
