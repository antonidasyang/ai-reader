#include "LayoutSettings.h"

namespace {
constexpr auto kKeyOrder           = "layout/paneOrder";
constexpr auto kKeyShowSource      = "layout/showSource";
constexpr auto kKeyShowTranslation = "layout/showTranslation";
} // namespace

LayoutSettings::LayoutSettings(QObject *parent)
    : QObject(parent)
    , m_showSource(m_qs.value(kKeyShowSource, true).toBool())
    , m_showTranslation(m_qs.value(kKeyShowTranslation, true).toBool())
{
}

QString LayoutSettings::paneOrder() const
{
    return m_qs.value(kKeyOrder).toString();
}

void LayoutSettings::setPaneOrder(const QString &csv)
{
    if (m_qs.value(kKeyOrder).toString() == csv)
        return;
    m_qs.setValue(kKeyOrder, csv);
    m_qs.sync();
}

void LayoutSettings::setShowSource(bool v)
{
    if (m_showSource == v) return;
    m_showSource = v;
    m_qs.setValue(kKeyShowSource, v);
    m_qs.sync();
    emit showSourceChanged();
}

void LayoutSettings::setShowTranslation(bool v)
{
    if (m_showTranslation == v) return;
    m_showTranslation = v;
    m_qs.setValue(kKeyShowTranslation, v);
    m_qs.sync();
    emit showTranslationChanged();
}
