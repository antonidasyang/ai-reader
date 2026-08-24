#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class AnalysisStore;
class CompareService;
class LibraryAnalysisService;
class ProjectController;
class ProjectProfileController;

// Getting the work back out as Markdown (§16).
//
// The export is not a screenshot of the pane: it carries the provenance and
// the checked citations with it, because a reading whose statements no longer
// say where they came from is exactly the thing this feature exists to avoid.
// An unverified citation is exported as unverified, and a demoted claim is
// exported as the model's own reading.
class AnalysisExporter : public QObject
{
    Q_OBJECT
public:
    AnalysisExporter(AnalysisStore *store, ProjectController *projects,
                     ProjectProfileController *profile,
                     LibraryAnalysisService *research, CompareService *compare,
                     QObject *parent = nullptr);

    // One paper: the quick interpretation, the close reading and the
    // reader's own notes.
    Q_INVOKABLE QString paperMarkdown(const QString &paperId) const;
    // The current comparison.
    Q_INVOKABLE QString comparisonMarkdown() const;
    // The whole project: the profile, every project-wide analysis that has
    // been generated, and a one-line index of every interpreted paper.
    Q_INVOKABLE QString projectMarkdown() const;

    // Writes UTF-8. Returns false when the file could not be written.
    Q_INVOKABLE bool save(const QString &markdown, const QUrl &dest) const;
    Q_INVOKABLE QString suggestedPaperName(const QString &paperId) const;

private:
    QString claimLine(const QJsonObject &claim, int indent = 0) const;
    QString claimList(const QJsonArray &claims, int indent = 0) const;
    QString stringList(const QJsonArray &items, int indent = 0) const;
    QString quickSection(const QJsonObject &digest) const;
    QString deepSection(const QJsonObject &deep) const;
    QString librarySection(const QString &kind) const;

    AnalysisStore *m_store;
    ProjectController *m_projects;
    ProjectProfileController *m_profile;
    LibraryAnalysisService *m_research;
    CompareService *m_compare;
};
