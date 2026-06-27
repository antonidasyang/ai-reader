#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QVariantMap>

class LibraryModel;
class PaperController;

// Auto-completes an item's bibliographic fields from an identifier:
//   DOI   -> doi.org content negotiation (CSL-JSON)
//   arXiv -> export.arxiv.org Atom feed
// autoFill() extracts the identifier from the current paper's head text;
// resolveIdentifier() takes a user-supplied DOI / arXiv id. Results are written
// back via LibraryModel.updateItem (which syncs). Network-only; failures don't
// block the import.
class MetadataService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)

public:
    MetadataService(LibraryModel *lib, PaperController *paper,
                    QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString status() const { return m_status; }

    Q_INVOKABLE void autoFill(const QString &itemId);
    Q_INVOKABLE void resolveIdentifier(const QString &itemId,
                                       const QString &identifier);

signals:
    void busyChanged();
    void statusChanged();
    void resolved(const QString &itemId, bool ok);

private:
    void resolveDoi(const QString &itemId, const QString &doi);
    void resolveArxiv(const QString &itemId, const QString &arxivId);
    void applyFields(const QString &itemId, const QVariantMap &fields);
    void fail(const QString &itemId, const QString &message);
    void setBusy(bool v);
    void setStatus(const QString &s);

    QNetworkAccessManager m_nam;
    LibraryModel *m_lib;
    PaperController *m_paper;
    bool m_busy = false;
    QString m_status;
};
