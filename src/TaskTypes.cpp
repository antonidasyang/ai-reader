#include "TaskTypes.h"

#include <QCoreApplication>

namespace Tasks {

// The keys are what goes on disk and into an exclusion key, so they outlive
// any rename in the viewer: a task written by an older build has to be
// recognised by a newer one. Change a label freely; never change a key.

QString kindKey(Kind kind)
{
    switch (kind) {
    case Kind::Translate:       return QStringLiteral("translate");
    case Kind::Segment:         return QStringLiteral("segment");
    case Kind::Toc:             return QStringLiteral("toc");
    case Kind::Vision:          return QStringLiteral("vision");
    case Kind::QuickInterpret:  return QStringLiteral("quick_interpret");
    case Kind::DeepInterpret:   return QStringLiteral("deep_interpret");
    case Kind::BatchInterpret:  return QStringLiteral("batch_interpret");
    case Kind::LibraryAnalysis: return QStringLiteral("library_analysis");
    case Kind::Other:           break;
    }
    return QStringLiteral("other");
}

Kind kindFromKey(const QString &key)
{
    if (key == QLatin1String("translate"))        return Kind::Translate;
    if (key == QLatin1String("segment"))          return Kind::Segment;
    if (key == QLatin1String("toc"))              return Kind::Toc;
    if (key == QLatin1String("vision"))           return Kind::Vision;
    if (key == QLatin1String("quick_interpret"))  return Kind::QuickInterpret;
    if (key == QLatin1String("deep_interpret"))   return Kind::DeepInterpret;
    if (key == QLatin1String("batch_interpret"))  return Kind::BatchInterpret;
    if (key == QLatin1String("library_analysis")) return Kind::LibraryAnalysis;
    return Kind::Other;
}

QString kindLabel(Kind kind)
{
    switch (kind) {
    case Kind::Translate:
        return QCoreApplication::translate("Tasks", "Translation");
    case Kind::Segment:
        return QCoreApplication::translate("Tasks", "Paragraph segmentation");
    case Kind::Toc:
        return QCoreApplication::translate("Tasks", "Table of contents");
    case Kind::Vision:
        return QCoreApplication::translate("Tasks", "Page reading");
    case Kind::QuickInterpret:
        return QCoreApplication::translate("Tasks", "Quick interpretation");
    case Kind::DeepInterpret:
        return QCoreApplication::translate("Tasks", "Deep reading");
    case Kind::BatchInterpret:
        return QCoreApplication::translate("Tasks", "Batch interpretation");
    case Kind::LibraryAnalysis:
        return QCoreApplication::translate("Tasks", "Library analysis");
    case Kind::Other:
        break;
    }
    return QCoreApplication::translate("Tasks", "Task");
}

QString stateKey(State state)
{
    switch (state) {
    case State::Queued:      return QStringLiteral("queued");
    case State::Running:     return QStringLiteral("running");
    case State::Succeeded:   return QStringLiteral("succeeded");
    case State::Failed:      return QStringLiteral("failed");
    case State::Canceled:    return QStringLiteral("canceled");
    case State::Interrupted: break;
    }
    return QStringLiteral("interrupted");
}

State stateFromKey(const QString &key)
{
    if (key == QLatin1String("running"))     return State::Running;
    if (key == QLatin1String("succeeded"))   return State::Succeeded;
    if (key == QLatin1String("failed"))      return State::Failed;
    if (key == QLatin1String("canceled"))    return State::Canceled;
    if (key == QLatin1String("interrupted")) return State::Interrupted;
    return State::Queued;
}

QString stateLabel(State state)
{
    switch (state) {
    case State::Queued:
        return QCoreApplication::translate("Tasks", "Waiting");
    case State::Running:
        return QCoreApplication::translate("Tasks", "Running");
    case State::Succeeded:
        return QCoreApplication::translate("Tasks", "Done");
    case State::Failed:
        return QCoreApplication::translate("Tasks", "Failed");
    case State::Canceled:
        return QCoreApplication::translate("Tasks", "Canceled");
    case State::Interrupted:
        break;
    }
    // Not a failure: the app closed underneath it, and it is being offered
    // back rather than reported.
    return QCoreApplication::translate("Tasks", "Interrupted");
}

} // namespace Tasks
