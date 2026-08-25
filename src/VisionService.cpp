#include "VisionService.h"

#include "LlmClient.h"
#include "PaperController.h"
#include "Settings.h"
#include "TaskManager.h"

#include <QBuffer>
#include <QImage>
#include <QJsonObject>
#include <QTimer>

namespace {

QByteArray encodePng(const QImage &img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes;
}

} // namespace

VisionService::VisionService(Settings *settings,
                             PaperController *paper,
                             QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_paper(paper)
    , m_clients(settings, this)
{
}

VisionService::~VisionService() = default;

void VisionService::clear()
{
    if (m_text.isEmpty() && m_status == Idle && m_lastError.isEmpty()
        && m_page < 0)
        return;
    m_text.clear();
    m_lastError.clear();
    m_status = Idle;
    m_page = -1;
    emit textChanged();
    emit pageChanged();
    emit statusChanged();
}

void VisionService::cancelReply()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    if (m_status == Rendering || m_status == Generating)
        setStatus(Idle);
}

void VisionService::cancel()
{
    cancelReply();
    // A read still waiting its turn in the queue is over too: the reader
    // asked for it and has now asked for something else, and nothing would
    // ever collect its answer.
    if (m_tasks && !m_taskId.isEmpty()) {
        const QString id = m_taskId;
        m_taskId.clear();
        m_taskPaperId.clear();
        m_taskPage = -1;
        m_tasks->cancel(id);
    }
}

void VisionService::setTasks(TaskManager *tasks)
{
    m_tasks = tasks;
    if (!m_tasks)
        return;

    // A read the app closed on can be started again from nothing -- but
    // only where its paper is the one on screen, since the answer has
    // nowhere else to go.
    m_tasks->registerResumer(Tasks::Kind::Vision,
                             [this](const QJsonObject &resume) {
        if (!m_paper)
            return false;
        if (m_paper->paperId()
            != resume.value(QStringLiteral("paperId")).toString())
            return false;
        const int page = resume.value(QStringLiteral("page")).toInt(-1);
        if (page < 0)
            return false;
        // A resumer that cannot start says no and leaves the screen alone:
        // readPage() would refuse an unconfigured model or a page the paper
        // no longer has by putting a failure on the dialog, for a read the
        // user never watched start.
        if (!couldRead(page))
            return false;
        readPage(page);
        return true;
    });
}

bool VisionService::canRead(int pageIdx)
{
    if (!m_settings || !m_paper)
        return false;
    if (!m_settings->isConfigured()) {
        setStatus(Failed,
                  tr("LLM is not configured. Open Settings to add a model and API key."));
        return false;
    }
    if (pageIdx < 0 || pageIdx >= m_paper->pageCount()) {
        setStatus(Failed, tr("Page %1 is out of range.").arg(pageIdx + 1));
        return false;
    }
    return true;
}

bool VisionService::couldRead(int pageIdx) const
{
    // The silent twin of canRead() above; the two refuse on the same
    // grounds and have to stay in step.
    return m_settings && m_paper && m_settings->isConfigured()
        && pageIdx >= 0 && pageIdx < m_paper->pageCount();
}

void VisionService::finishTask(bool ok, const QString &error)
{
    if (!m_tasks || m_taskId.isEmpty())
        return;
    const QString id = m_taskId;
    m_taskId.clear();
    m_taskPaperId.clear();
    m_taskPage = -1;
    m_tasks->finish(id, ok, error);
}

void VisionService::cancelTask()
{
    if (!m_tasks || m_taskId.isEmpty())
        return;
    const QString id = m_taskId;
    m_taskId.clear();
    m_taskPaperId.clear();
    m_taskPage = -1;
    m_tasks->markCanceled(id);
}

void VisionService::readPage(int pageIdx)
{
    // The refusals stay in front of the queue: a page that cannot be read
    // should say so at once rather than wait its turn to fail.
    if (!canRead(pageIdx))
        return;

    if (!m_tasks) {
        runReadPage(pageIdx);
        return;
    }

    Tasks::Request req;
    req.kind = Tasks::Kind::Vision;
    req.title = tr("Read page %1").arg(pageIdx + 1);
    req.paperId = m_paper->paperId();
    req.paperTitle = m_paper->fileName();
    req.steps = 1;
    // A page is its own piece of work: reading page 3 while page 7 is
    // running is two answers about two pages, not the same job twice.
    req.exclusiveKey = QStringLiteral("vision|%1|%2")
                           .arg(req.paperId)
                           .arg(pageIdx);
    req.resume = QJsonObject{
        {QStringLiteral("paperId"), req.paperId},
        {QStringLiteral("path"), m_paper->pdfSource().toLocalFile()},
        {QStringLiteral("page"), pageIdx},
    };

    const QString previous = m_taskId;
    const QString id = m_tasks->submit(req,
        [this, pageIdx, paperId = req.paperId] {
            // submit() only hands the id back when it returns, and it may
            // call this from inside itself -- one turn of the event loop
            // and the read has its id on record.
            QTimer::singleShot(0, this, [this, pageIdx, paperId] {
                // Cancelled in the turn between being admitted and
                // starting: the stop callback has already dropped the id,
                // or another page has taken its place. The id on record is
                // not ours to settle either way.
                if (m_taskId.isEmpty() || m_taskPaperId != paperId
                    || m_taskPage != pageIdx)
                    return;
                if (!m_paper || m_paper->paperId() != paperId) {
                    // Admitted after the reader moved on: page 4 of the
                    // paper now on screen is not what was asked for.
                    // Nothing failed — the paper closed under it.
                    cancelTask();
                    return;
                }
                runReadPage(pageIdx);
            });
        },
        [this, pageIdx, paperId = req.paperId] {
            // The manager is stopping us; it owns the outcome from here,
            // so drop the id rather than finishing the task ourselves --
            // and only when the id still belongs to this page of this
            // paper, since a later read may already have taken its place
            // and that one's id is not ours to drop. The request in flight
            // is aborted either way.
            if (m_taskPaperId == paperId && m_taskPage == pageIdx) {
                m_taskId.clear();
                m_taskPaperId.clear();
                m_taskPage = -1;
            }
            cancelReply();
        });
    if (id.isEmpty())
        return;             // this page is already being read
    m_taskId = id;
    m_taskPaperId = req.paperId;
    m_taskPage = pageIdx;

    // Only one page is ever on display, so the read that was here before
    // is over -- including one still waiting its turn, whose answer would
    // otherwise land on top of this one.
    if (!previous.isEmpty() && previous != id)
        m_tasks->cancel(previous);
}

void VisionService::runReadPage(int pageIdx)
{
    if (!canRead(pageIdx)) {
        // canRead() puts its own reason on screen for the two refusals it
        // knows; with no settings or no paper at all it says nothing, and
        // the task still has to be told something honest.
        finishTask(false, m_lastError.isEmpty() ? tr("No paper open.")
                                                : m_lastError);
        return;
    }

    // Only the reply, never the task: this is the task's own body.
    cancelReply();
    m_text.clear();
    emit textChanged();
    if (m_page != pageIdx) {
        m_page = pageIdx;
        emit pageChanged();
    }
    setStatus(Rendering);

    if (m_tasks && !m_taskId.isEmpty())
        m_tasks->setNote(m_taskId, tr("Rendering the page"));
    const QImage img = m_paper->renderPage(pageIdx, 1280);
    if (img.isNull()) {
        setStatus(Failed, tr("Failed to render page %1.").arg(pageIdx + 1));
        finishTask(false, m_lastError);
        return;
    }
    const QByteArray png = encodePng(img);
    if (png.isEmpty()) {
        setStatus(Failed, tr("Failed to encode page image."));
        finishTask(false, m_lastError);
        return;
    }

    // Rebuilt when the configuration moved: switching provider needs a
    // different client, not different fields on the old one.
    m_client = m_clients.client();
    if (!m_client) {
        setStatus(Failed, tr("No model is configured."));
        finishTask(false, m_lastError);
        return;
    }

    LlmClient::Request req;
    req.system = systemPrompt();
    LlmClient::Message msg;
    msg.role = QStringLiteral("user");
    msg.content = userPrompt(pageIdx);
    msg.images.append(png);
    req.messages.append(msg);
    req.temperature = 0.0;
    req.maxTokens = m_settings->maxTokens();
    req.stream = true;

    setStatus(Generating);
    if (m_tasks && !m_taskId.isEmpty())
        m_tasks->setNote(m_taskId, tr("Reading the page"));
    m_reply = m_client->send(req);

    connect(m_reply, &LlmReply::chunkReceived, this,
            [this](const QString &chunk) {
        m_text += chunk;
        emit textChanged();
    });
    connect(m_reply, &LlmReply::finished, this, [this]() {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        if (m_status == Generating)
            setStatus(Done);
        if (m_tasks && !m_taskId.isEmpty())
            m_tasks->setProgress(m_taskId, 1, 1);
        finishTask(true);
    });
    connect(m_reply, &LlmReply::errorOccurred, this,
            [this](const QString &message) {
        if (m_reply) m_reply->deleteLater();
        m_reply.clear();
        setStatus(Failed, message);
        finishTask(false, message);
    });
}

QString VisionService::systemPrompt() const
{
    if (m_settings) {
        const QString custom = m_settings->visionPrompt();
        if (!custom.isEmpty()) return custom;
    }
    return defaultSystemPrompt();
}

QString VisionService::defaultSystemPrompt() const
{
    return QStringLiteral(
        "You are reading one rendered page of an academic paper.\n"
        "\n"
        "Extract the page's content as clean Markdown:\n"
        "- Section headings as `##` / `###`.\n"
        "- Body paragraphs as plain text. Reflow line breaks; keep paragraph "
        "breaks.\n"
        "- Math: inline as `$…$`, display as `$$…$$`. Transcribe formulas "
        "verbatim where readable.\n"
        "- Figures and diagrams: describe what is shown (chart type, axes, "
        "trend) under a `**Figure N (described):**` label.\n"
        "- Tables: render as Markdown tables when feasible; otherwise "
        "describe.\n"
        "- Drop running headers, footers, and page numbers.\n"
        "\n"
        "Output ONLY the page's Markdown content. No preamble, no closing "
        "remarks.");
}

QString VisionService::userPrompt(int pageIdx) const
{
    return QStringLiteral("Extract the content of page %1.").arg(pageIdx + 1);
}

void VisionService::setStatus(Status s, const QString &err)
{
    if (s == m_status && err == m_lastError)
        return;
    m_status = s;
    m_lastError = err;
    emit statusChanged();
}
