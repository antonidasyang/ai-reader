#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class ImportService;
class ProjectController;
class QNetworkReply;

// "Here's a link, get me the paper": turns what the user pastes into a PDF
// on disk, then hands that file to the reader and — when there is a project
// to write to — to ImportService, exactly as if it had been picked from the
// folder pane.
//
// What it understands:
//   arXiv     https://arxiv.org/abs/2609.02309, …/pdf/2609.02309v2,
//             export.arxiv.org, huggingface.co/papers/…, alphaxiv.org/abs/…,
//             arXiv:2609.02309, a bare 2609.02309, old ids (hep-th/9901001),
//             and arXiv's own DOI (10.48550/arXiv.2609.02309)
//   OpenReview  …/forum?id=X or …/pdf?id=X
//   anything else over http(s): downloaded as is; if it turns out to be a
//             web page rather than a PDF, the page's citation_pdf_url meta
//             tag (which most publishers and proceedings sites carry) is
//             followed once.
//
// Files land in <Downloads>/AiReader. An arXiv paper already downloaded is
// not fetched again. The arXiv id travels with the file so the metadata
// lookup uses it instead of guessing from the first page's text, where a
// cited DOI could win.
class UrlImportService : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    // 0..1 while the size is known, -1 while it isn't.
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    // Where downloads go, for the dialog to say so.
    Q_PROPERTY(QString downloadFolder READ downloadFolder CONSTANT)

public:
    UrlImportService(ImportService *importer, ProjectController *projects,
                     QObject *parent = nullptr);

    bool busy() const { return m_reply != nullptr; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QString downloadFolder() const;

    Q_INVOKABLE void fetch(const QString &input);
    Q_INVOKABLE void cancel();
    // True when `input` is something fetch() knows what to do with — used
    // to route a link dropped on the window, and to pre-fill the dialog.
    Q_INVOKABLE bool recognises(const QString &input) const;
    // The clipboard's text when it is a link fetch() understands, else "".
    Q_INVOKABLE QString clipboardLink() const;

    // Where a pasted string leads. Empty pdfUrl: not understood.
    struct Target {
        QUrl pdfUrl;
        QString fileName;    // suggested name; empty = decide from the reply
        QString identifier;  // "arXiv:<id>" for the metadata lookup, or ""
    };
    static Target resolve(const QString &input);
    // <Downloads>/AiReader unless pointed elsewhere (the harness does, so a
    // test run never writes into the user's Downloads).
    QString downloadDir() const { return m_downloadDir; }
    void setDownloadDir(const QString &dir) { m_downloadDir = dir; }
    static QString defaultDownloadDir();

signals:
    void busyChanged();
    void statusChanged();
    void progressChanged();
    // `fileUrl` is a file:// URL, ready for tabs.openPaper().
    void downloaded(const QString &fileUrl, bool addedToLibrary);
    void failed(const QString &message);

private:
    void start(const QUrl &url, bool mayFollowPage);
    void onFinished();
    void finishWith(const QString &localPath);
    void fail(const QString &message);
    void setStatus(const QString &s);
    void setProgress(double p);

    ImportService *m_importer;
    ProjectController *m_projects;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    Target m_target;
    QString m_downloadDir;
    bool m_mayFollowPage = false;
    QString m_status;
    double m_progress = -1;
};
