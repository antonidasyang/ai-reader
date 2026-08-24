#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class AnalysisStore;

// The research profile of the current project (§6): what the reader is
// trying to find out, in their own words.
//
// It exists because a generic summary is not what a researcher needs. Every
// interpretation prompt in the app carries promptBlock() so the model
// answers "what does this paper mean for THIS project" rather than "what
// does this paper say". hash() feeds the staleness check: change the
// profile and last month's interpretations are marked as possibly out of
// date rather than quietly kept (§17).
//
// One profile per project, shared by the members, edited by anyone who can
// write to the project.
class ProjectProfileController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap profile READ profile NOTIFY changed)
    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY changed)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString updatedAt READ updatedAt NOTIFY changed)
    Q_PROPERTY(QString updatedByEmail READ updatedByEmail NOTIFY changed)

public:
    explicit ProjectProfileController(AnalysisStore *store,
                                      QObject *parent = nullptr);

    QVariantMap profile() const;
    bool hasProfile() const;
    bool canEdit() const;
    QString summary() const;
    QString updatedAt() const;
    QString updatedByEmail() const;

    Q_INVOKABLE bool save(const QVariantMap &fields);
    Q_INVOKABLE bool clearProfile();
    // The field names, in display order, that save() understands.
    Q_INVOKABLE static QStringList fieldNames();

    // What gets pasted into every interpretation prompt. Empty when the
    // project has no profile yet, in which case the prompts say so rather
    // than pretending to know the reader's goals.
    QString promptBlock() const;
    // Stable digest of the profile, part of every analysis's inputHash.
    QString hash() const;

signals:
    void changed();

private:
    AnalysisStore *m_store;
};
