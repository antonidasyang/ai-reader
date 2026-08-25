#include "TaskStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace {

// Bumped only if the shape below changes in a way an older build would
// misread. load() ignores what it does not recognise instead, which has
// covered every change so far.
constexpr int kFormatVersion = 1;

QJsonObject toJson(const Tasks::PendingTask &task)
{
    const Tasks::Request &r = task.request;
    return QJsonObject{
        {QStringLiteral("id"), task.id},
        {QStringLiteral("kind"), Tasks::kindKey(r.kind)},
        {QStringLiteral("title"), r.title},
        {QStringLiteral("paperId"), r.paperId},
        {QStringLiteral("paperTitle"), r.paperTitle},
        {QStringLiteral("projectId"), r.projectId},
        {QStringLiteral("exclusiveKey"), r.exclusiveKey},
        {QStringLiteral("group"), r.group},
        {QStringLiteral("steps"), r.steps},
        {QStringLiteral("done"), task.done},
        {QStringLiteral("resume"), r.resume}};
}

Tasks::PendingTask fromJson(const QJsonObject &o)
{
    Tasks::PendingTask t;
    t.id = o.value(QStringLiteral("id")).toString();
    t.done = o.value(QStringLiteral("done")).toInt();
    t.request.kind = Tasks::kindFromKey(o.value(QStringLiteral("kind")).toString());
    t.request.title = o.value(QStringLiteral("title")).toString();
    t.request.paperId = o.value(QStringLiteral("paperId")).toString();
    t.request.paperTitle = o.value(QStringLiteral("paperTitle")).toString();
    t.request.projectId = o.value(QStringLiteral("projectId")).toString();
    t.request.exclusiveKey = o.value(QStringLiteral("exclusiveKey")).toString();
    t.request.steps = o.value(QStringLiteral("steps")).toInt();
    t.request.resume = o.value(QStringLiteral("resume")).toObject();
    const QString group = o.value(QStringLiteral("group")).toString();
    if (!group.isEmpty())
        t.request.group = group;
    return t;
}

} // namespace

namespace TaskStore {

QString filePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/tasks/pending.json");
}

bool save(const QVector<Tasks::PendingTask> &tasks)
{
    QJsonArray array;
    for (const Tasks::PendingTask &t : tasks) {
        if (t.request.resume.isEmpty())
            continue;
        array.append(toJson(t));
    }

    QJsonObject root{
        {QStringLiteral("version"), kFormatVersion},
        {QStringLiteral("savedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("tasks"), array}};

    const QString path = filePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    // Written beside the real file and moved into place, because this is
    // written on the way out: a power cut halfway through a quit must leave
    // the last good list, not half of a new one.
    const QString tmp = path + QStringLiteral(".tmp");
    QFile f(tmp);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("TaskStore: cannot write %s: %s", qUtf8Printable(tmp),
                 qUtf8Printable(f.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    const bool written = f.write(bytes) == bytes.size();
    f.close();
    if (!written) {
        qWarning("TaskStore: short write to %s", qUtf8Printable(tmp));
        QFile::remove(tmp);
        return false;
    }

    // rename() will not replace an existing file on Windows, so the old one
    // goes first. The window between the two is what the .tmp file covers.
    QFile::remove(path);
    if (!QFile::rename(tmp, path)) {
        qWarning("TaskStore: cannot move %s into place", qUtf8Printable(tmp));
        QFile::remove(tmp);
        return false;
    }
    return true;
}

QVector<Tasks::PendingTask> load()
{
    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return {};

    QVector<Tasks::PendingTask> out;
    const QJsonArray array =
        doc.object().value(QStringLiteral("tasks")).toArray();
    out.reserve(array.size());
    for (const QJsonValue &v : array) {
        const Tasks::PendingTask t = fromJson(v.toObject());
        if (t.id.isEmpty() || t.request.resume.isEmpty())
            continue;
        out.append(t);
    }
    return out;
}

void clear()
{
    QFile::remove(filePath());
}

} // namespace TaskStore
