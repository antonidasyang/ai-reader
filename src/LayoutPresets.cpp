#include "LayoutPresets.h"

#include <QHash>
#include <QJsonDocument>
#include <QJsonValue>
#include <QVector>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace {
constexpr auto kPresetsKey = "layouts/presets";
// Which preset the arrangement on screen came from. Local on purpose: it
// describes this window, not the account, and syncing it would have every
// machine claiming the last one anybody applied anywhere.
constexpr auto kCurrentKey = "layouts/current";

constexpr auto kVersion = "version";
constexpr auto kPresets = "presets";
constexpr auto kName    = "name";
constexpr auto kOrder   = "order";
constexpr auto kPanes   = "panes";
constexpr auto kVisible = "visible";
constexpr auto kWidth   = "width";
constexpr auto kFound   = "found";

constexpr int kSchemaVersion = 1;

// Four places is 0.4 px on a 4K screen and keeps the serialized document
// byte-stable, which is what lets the sync layer skip a redundant write.
double roundedFraction(double v)
{
    const double clamped = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
    return std::round(clamped * 10000.0) / 10000.0;
}

// One preset, cleaned up. Everything this build does not recognise is
// carried through untouched -- a preset saved by a newer version may name
// panes and carry fields this one has never heard of, and emptying those
// out would mean running the old build once cost the reader their layout.
// An empty object means "not a preset": no usable name.
QJsonObject normalizePreset(const QJsonObject &in)
{
    const QString name = in.value(QLatin1String(kName)).toString().trimmed();
    if (name.isEmpty())
        return {};

    QJsonObject out = in;
    out.insert(QLatin1String(kName), name);

    QJsonArray order;
    QStringList seen;
    const QJsonValue ov = in.value(QLatin1String(kOrder));
    if (ov.isArray()) {
        const QJsonArray src = ov.toArray();
        for (const QJsonValue &v : src) {
            const QString id = v.toString().trimmed();
            if (id.isEmpty() || seen.contains(id))
                continue;
            seen << id;
            order.append(id);
        }
    }
    out.insert(QLatin1String(kOrder), order);

    QJsonObject panes;
    const QJsonValue pv = in.value(QLatin1String(kPanes));
    if (pv.isObject()) {
        const QJsonObject src = pv.toObject();
        for (auto it = src.begin(); it != src.end(); ++it) {
            const QString id = it.key().trimmed();
            if (id.isEmpty() || !it.value().isObject())
                continue;
            QJsonObject entry = it.value().toObject();
            // A pane entry with no "visible" is a visible pane: the only
            // way to end up here without one is a hand-edited file, and
            // hiding a pane is the answer that is harder to undo.
            entry.insert(QLatin1String(kVisible),
                         entry.value(QLatin1String(kVisible)).toBool(true));
            const QJsonValue w = entry.value(QLatin1String(kWidth));
            if (w.isDouble() && w.toDouble() > 0.0)
                entry.insert(QLatin1String(kWidth), roundedFraction(w.toDouble()));
            else
                entry.remove(QLatin1String(kWidth));  // no opinion about it
            panes.insert(id, entry);
        }
    }
    out.insert(QLatin1String(kPanes), panes);
    return out;
}

QString nameOf(const QJsonValue &v)
{
    return v.toObject().value(QLatin1String(kName)).toString();
}

// Case-insensitive, with a case-sensitive tiebreak so the order is total
// and two machines that saved the same names agree on the bytes.
void sortPresets(QJsonArray &presets)
{
    QVector<QJsonObject> v;
    v.reserve(presets.size());
    for (const QJsonValue &p : std::as_const(presets))
        v.append(p.toObject());
    std::sort(v.begin(), v.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const QString an = a.value(QLatin1String(kName)).toString();
        const QString bn = b.value(QLatin1String(kName)).toString();
        const int c = an.compare(bn, Qt::CaseInsensitive);
        return c != 0 ? c < 0 : an < bn;
    });
    QJsonArray out;
    for (const QJsonObject &o : std::as_const(v))
        out.append(o);
    presets = out;
}

// The document, or nothing. Nothing means "this is not a presets document",
// which every caller answers by keeping what it already has.
std::optional<QJsonArray> parseDocument(const QString &text)
{
    if (text.trimmed().isEmpty())
        return std::nullopt;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return std::nullopt;
    const QJsonValue list = doc.object().value(QLatin1String(kPresets));
    if (!list.isArray())
        return std::nullopt;

    QJsonArray out;
    QStringList taken;
    const QJsonArray src = list.toArray();
    for (const QJsonValue &v : src) {
        if (!v.isObject())
            continue;
        const QJsonObject p = normalizePreset(v.toObject());
        if (p.isEmpty())
            continue;
        const QString name = nameOf(p);
        // Two presets of the same name cannot both be reachable from a
        // menu; the first one wins, which is the one a sorted document
        // shows first.
        bool clash = false;
        for (const QString &t : std::as_const(taken)) {
            if (t.compare(name, Qt::CaseInsensitive) == 0) {
                clash = true;
                break;
            }
        }
        if (clash)
            continue;
        taken << name;
        out.append(p);
    }
    return out;   // may legitimately be empty: an account with no layouts
}
} // namespace

LayoutPresets::LayoutPresets(QObject *parent)
    : QObject(parent)
{
    m_current = m_qs.value(QLatin1String(kCurrentKey)).toString();
    reload();
}

QString LayoutPresets::settingsKey()
{
    return QString::fromLatin1(kPresetsKey);
}

bool LayoutPresets::isPresetDocument(const QString &json)
{
    // Stricter than the parse above, and deliberately so. Reading this
    // machine's own file, an entry that cannot be used is stepped over --
    // losing one layout beats losing all of them. Deciding whether a
    // payload from somewhere else IS a presets document is the opposite
    // question: an array of things that are not presets is exactly how
    // another application's key of the same name would look, and answering
    // "close enough" to that is how a reader's layouts get overwritten with
    // somebody else's data. An empty list is still a document, because an
    // account whose last layout was deleted has to be able to say so.
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonValue list = doc.object().value(QLatin1String(kPresets));
    if (!list.isArray())
        return false;
    const QJsonArray presets = list.toArray();
    for (const QJsonValue &v : presets) {
        if (!v.isObject())
            return false;
        if (v.toObject().value(QLatin1String(kName)).toString().trimmed().isEmpty())
            return false;
    }
    return true;
}

QStringList LayoutPresets::names() const
{
    QStringList out;
    out.reserve(m_presets.size());
    for (const QJsonValue &p : std::as_const(m_presets))
        out << nameOf(p);
    return out;
}

int LayoutPresets::indexOf(const QString &name) const
{
    const QString wanted = name.trimmed();
    if (wanted.isEmpty())
        return -1;
    for (int i = 0; i < m_presets.size(); ++i) {
        if (nameOf(m_presets.at(i)).compare(wanted, Qt::CaseInsensitive) == 0)
            return i;
    }
    return -1;
}

bool LayoutPresets::contains(const QString &name) const
{
    return indexOf(name) >= 0;
}

QString LayoutPresets::existingName(const QString &name) const
{
    const int idx = indexOf(name);
    return idx < 0 ? QString() : nameOf(m_presets.at(idx));
}

void LayoutPresets::setCurrent(const QString &name)
{
    // Only a name that still means something. Anything else is "no current
    // layout" -- an arrangement that came from a preset the reader has
    // since deleted is just an arrangement.
    QString v = name.trimmed();
    if (!v.isEmpty())
        v = existingName(v);
    if (v == m_current)
        return;
    m_current = v;
    if (m_current.isEmpty())
        m_qs.remove(QLatin1String(kCurrentKey));
    else
        m_qs.setValue(QLatin1String(kCurrentKey), m_current);
    m_qs.sync();
    emit currentChanged();
}

void LayoutPresets::save(const QString &name, const QVariantMap &snapshot)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
        return;

    QJsonObject preset = QJsonObject::fromVariantMap(snapshot);
    preset.insert(QLatin1String(kName), trimmed);
    preset = normalizePreset(preset);
    if (preset.isEmpty())
        return;

    QJsonArray next = m_presets;
    const int idx = indexOf(trimmed);
    if (idx >= 0)
        next.replace(idx, preset);   // the new spelling wins: it was typed
    else
        next.append(preset);
    sortPresets(next);
    commit(next);
    setCurrent(trimmed);
}

QVariantMap LayoutPresets::load(const QString &name) const
{
    const int idx = indexOf(name);
    if (idx < 0)
        return {};
    return m_presets.at(idx).toObject().toVariantMap();
}

bool LayoutPresets::rename(const QString &from, const QString &to)
{
    const QString target = to.trimmed();
    if (target.isEmpty())
        return false;
    const int idx = indexOf(from);
    if (idx < 0)
        return false;
    const int clash = indexOf(target);
    if (clash >= 0 && clash != idx)
        return false;   // a different preset is already called that

    const QString was = nameOf(m_presets.at(idx));
    QJsonObject preset = m_presets.at(idx).toObject();
    preset.insert(QLatin1String(kName), target);
    QJsonArray next = m_presets;
    next.replace(idx, preset);
    sortPresets(next);
    commit(next);
    if (was.compare(m_current, Qt::CaseInsensitive) == 0)
        setCurrent(target);
    return true;
}

bool LayoutPresets::remove(const QString &name)
{
    const int idx = indexOf(name);
    if (idx < 0)
        return false;
    const QString was = nameOf(m_presets.at(idx));
    QJsonArray next = m_presets;
    next.removeAt(idx);
    commit(next);
    if (was.compare(m_current, Qt::CaseInsensitive) == 0)
        setCurrent(QString());
    return true;
}

QVariantMap LayoutPresets::resolve(const QString &name, qreal contentWidth,
                                   const QVariantMap &minimums) const
{
    QVariantMap out;
    const int idx = indexOf(name);
    if (idx < 0) {
        out.insert(QLatin1String(kFound), false);
        return out;
    }
    const QJsonObject preset = m_presets.at(idx).toObject();
    const QJsonObject panes = preset.value(QLatin1String(kPanes)).toObject();
    const double width = contentWidth > 0.0 ? double(contentWidth) : 0.0;

    QStringList ids;
    QHash<QString, bool> visible;
    QHash<QString, double> minimum;
    QHash<QString, double> want;     // < 0 ⇒ the preset has no width for it

    for (auto it = panes.begin(); it != panes.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QString id = it.key();
        ids << id;
        visible.insert(id, entry.value(QLatin1String(kVisible)).toBool(true));
        double mn = minimums.value(id).toDouble();
        if (!(mn > 0.0))
            mn = 0.0;
        if (mn > width)
            mn = width;              // a minimum wider than the window is not one
        minimum.insert(id, mn);

        const QJsonValue w = entry.value(QLatin1String(kWidth));
        double px = -1.0;
        if (w.isDouble() && width > 0.0) {
            px = w.toDouble() * width;
            if (px < mn) px = mn;
            if (px > width) px = width;
        }
        want.insert(id, px);
    }

    // Make the visible ones fit. Every pane keeps its minimum; what is left
    // of the window is shared out in proportion to what each asked for above
    // that minimum, so a layout saved wide arrives narrow with its
    // proportions intact and nothing squeezed out of existence.
    double floorSum = 0.0, wantSum = 0.0;
    for (const QString &id : std::as_const(ids)) {
        if (!visible.value(id))
            continue;
        const double mn = minimum.value(id);
        const double w = want.value(id) >= 0.0 ? want.value(id) : mn;
        floorSum += mn;
        wantSum += w;
    }
    if (width > 0.0 && wantSum > width) {
        if (floorSum >= width) {
            // Not even the minimums fit -- more panes than this window can
            // hold. Hand back the minimums and let the SplitView do what it
            // does with them; anything cleverer here would be a guess.
            for (const QString &id : std::as_const(ids)) {
                if (visible.value(id) && want.value(id) >= 0.0)
                    want.insert(id, minimum.value(id));
            }
        } else {
            const double slack = width - floorSum;
            const double extra = wantSum - floorSum;
            for (const QString &id : std::as_const(ids)) {
                if (!visible.value(id) || want.value(id) < 0.0)
                    continue;
                const double mn = minimum.value(id);
                want.insert(id, mn + (want.value(id) - mn) * (slack / extra));
            }
        }
    }

    // Whole pixels, and then the rounding paid for out of the widest pane
    // so the row cannot come out one pixel wider than the window it is for.
    QHash<QString, int> px;
    double used = 0.0;
    for (const QString &id : std::as_const(ids)) {
        if (want.value(id) < 0.0)
            continue;
        const int v = int(std::lround(want.value(id)));
        px.insert(id, v);
        if (visible.value(id))
            used += v;
    }
    if (width > 0.0 && used > width) {
        int over = int(used - width);
        while (over > 0) {
            QString widest;
            int best = 0;
            for (const QString &id : std::as_const(ids)) {
                if (!visible.value(id) || !px.contains(id))
                    continue;
                const int room = px.value(id) - int(minimum.value(id));
                if (room > best) {
                    best = room;
                    widest = id;
                }
            }
            if (widest.isEmpty())
                break;               // everything is already at its minimum
            const int take = qMin(over, best);
            px.insert(widest, px.value(widest) - take);
            over -= take;
        }
    }

    QVariantMap resolved;
    for (const QString &id : std::as_const(ids)) {
        QVariantMap entry;
        entry.insert(QLatin1String(kVisible), visible.value(id));
        if (px.contains(id))
            entry.insert(QLatin1String(kWidth), px.value(id));
        resolved.insert(id, entry);
    }

    out.insert(QLatin1String(kFound), true);
    out.insert(QLatin1String(kName), nameOf(preset));
    out.insert(QLatin1String(kOrder),
               preset.value(QLatin1String(kOrder)).toVariant());
    out.insert(QLatin1String(kPanes), resolved);
    return out;
}

void LayoutPresets::reload()
{
    const auto parsed = parseDocument(m_qs.value(QLatin1String(kPresetsKey)).toString());
    // A value that is not a presets document -- a hand-edited file, a
    // truncated write, something else's key -- leaves the app perfectly
    // usable: no saved layouts in the menu, and the rubbish left where it
    // is until the reader saves one over it.
    QJsonArray next = parsed ? *parsed : QJsonArray{};
    sortPresets(next);
    if (next != m_presets) {
        const QStringList before = names();
        m_presets = next;
        if (names() != before)
            emit namesChanged();
    }
    if (!m_current.isEmpty() && indexOf(m_current) < 0)
        setCurrent(QString());
}

void LayoutPresets::commit(const QJsonArray &next)
{
    const QStringList before = names();
    m_presets = next;
    store();
    if (names() != before)
        emit namesChanged();
}

void LayoutPresets::store()
{
    QJsonObject root;
    root.insert(QLatin1String(kVersion), kSchemaVersion);
    root.insert(QLatin1String(kPresets), m_presets);
    const QString text =
        QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (m_qs.value(QLatin1String(kPresetsKey)).toString() == text)
        return;
    m_qs.setValue(QLatin1String(kPresetsKey), text);
    m_qs.sync();
    emit presetsChanged();
}
