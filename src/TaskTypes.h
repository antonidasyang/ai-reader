#pragma once

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

// One vocabulary for everything that takes time.
//
// Every operation that calls a model -- translating a paper, splitting it,
// reading its table of contents, interpreting it, analysing a whole project
// -- used to start wherever it was clicked and report progress in its own
// way, on its own property. Two of them could run against the same paper at
// once, and nothing in the UI could tell you what was in flight, how far
// along it was, or when it would be done. They are tasks now: submitted to
// one queue, admitted by one policy, watched in one place.
namespace Tasks {

Q_NAMESPACE

// Where a task is in its life. Interrupted is its own state and not a
// failure: it means the app closed while the task was running, and the task
// is offered back to the user on the next launch.
enum class State {
    Queued,
    Running,
    Succeeded,
    Failed,
    Canceled,
    Interrupted,
};
Q_ENUM_NS(State)

// What kind of work it is. The kind decides the label in the viewer, the
// icon, and -- through the exclusion key -- what it refuses to run beside.
enum class Kind {
    Translate,        // a paper's paragraphs
    Segment,          // splitting a PDF into paragraphs
    Toc,              // table of contents extraction
    Vision,           // read a page as an image
    QuickInterpret,   // the one-screen digest
    DeepInterpret,    // the nine close-reading modules
    BatchInterpret,   // a project's papers, one after another
    LibraryAnalysis,  // one project-wide question
    Other,
};
Q_ENUM_NS(Kind)

QString kindKey(Kind kind);            // stable, for storage
Kind    kindFromKey(const QString &key);
QString kindLabel(Kind kind);          // translated, for the viewer
QString stateKey(State state);
State   stateFromKey(const QString &key);
QString stateLabel(State state);

// What a caller asks for when it wants to run.
struct Request {
    Kind kind = Kind::Other;

    // What the viewer shows. `title` is the work ("Translate"), paperTitle
    // the thing it is being done to; the row reads "Translate — <paper>".
    QString title;
    QString paperId;      // empty for project-level work
    QString paperTitle;
    QString projectId;

    // Two tasks with the same exclusion key never run at the same time; the
    // second is rejected outright rather than queued, because it would do
    // the same work twice. Empty means "<kind>|<paperId>", which is what
    // keeps two runs off one paper and stops one paper's answers from
    // landing in another's.
    QString exclusiveKey;

    // Which budget the task draws from. Everything that calls a model
    // shares "llm" so a queue of them cannot flood the provider.
    QString group = QStringLiteral("llm");

    // 0 means the task cannot say how much there is to do; the viewer shows
    // a moving bar and no estimate.
    int steps = 0;

    // Enough to start this task again from nothing. Written to disk when
    // the app closes with the task unfinished, handed back to the kind's
    // resumer on the next launch. Leave empty for work that must not be
    // resumed automatically.
    QJsonObject resume;
};

} // namespace Tasks
