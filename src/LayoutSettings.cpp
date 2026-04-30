#include "LayoutSettings.h"

namespace {
constexpr auto kKey = "layout/paneOrder";
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
