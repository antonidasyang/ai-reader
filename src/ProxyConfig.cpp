#include "ProxyConfig.h"

#include "Stall.h"

#include <QDeadlineTimer>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QMutex>
#include <QNetworkProxy>
#include <QNetworkProxyFactory>
#include <QThread>
#include <QWaitCondition>
#include <QtConcurrent>

namespace {

// How long a request may wait for the lookup if it somehow beats the
// warm-up. The warm-up starts at launch and the first request is seconds
// later, so in practice nothing ever waits; this is the ceiling for the
// case where the network stack is slower than the app.
constexpr int kWaitMs = 3000;

class CachedSystemProxyFactory : public QNetworkProxyFactory
{
public:
    // Called from whichever thread is about to make a request -- the GUI
    // thread, and Qt's own HTTP thread. Cheap and lock-guarded either way.
    QList<QNetworkProxy> queryProxy(const QNetworkProxyQuery &) override
    {
        QMutexLocker lock(&m_mutex);
        if (!m_ready) {
            // Only ever hit if a request beats the warm-up. Waiting here is
            // the same block this class exists to remove, so it is bounded
            // and it happens at most once.
            Stall::Mark mark("waiting for the system's proxy settings");
            m_cond.wait(&m_mutex, QDeadlineTimer(kWaitMs));
            if (!m_ready) {
                qWarning("proxy: the system did not answer in %d ms; "
                         "going direct for the rest of this session.",
                         kWaitMs);
                m_proxies = {QNetworkProxy::NoProxy};
                m_ready = true;
            }
        }
        return m_proxies;
    }

    void warm(const QNetworkProxyQuery &query)
    {
        QElapsedTimer t;
        t.start();
        QList<QNetworkProxy> found =
            QNetworkProxyFactory::systemProxyForQuery(query);
        const qint64 ms = t.elapsed();
        if (found.isEmpty())
            found = {QNetworkProxy::NoProxy};

        QMutexLocker lock(&m_mutex);
        if (m_ready)
            return;                      // a timed-out waiter already gave up
        m_proxies = found;
        m_ready = true;
        m_cond.wakeAll();
        // Worth a line either way: a slow answer here is the whole reason
        // this class exists, and a fast one says the machine is not the
        // problem.
        qInfo("proxy: the system took %lld ms to answer; using %s.", ms,
              m_proxies.constFirst().type() == QNetworkProxy::NoProxy
                  ? "a direct connection"
                  : qUtf8Printable(m_proxies.constFirst().hostName()));
    }

private:
    QMutex m_mutex;
    QWaitCondition m_cond;
    QList<QNetworkProxy> m_proxies;
    bool m_ready = false;
};

} // namespace

namespace ProxyConfig {

void install(const QUrl &serverUrl)
{
    auto *factory = new CachedSystemProxyFactory;
    // Takes ownership.
    QNetworkProxyFactory::setApplicationProxyFactory(factory);

    const QNetworkProxyQuery query(serverUrl.isValid()
                                       ? serverUrl
                                       : QUrl(QStringLiteral("https://example.com")));
    // Off this thread, and deliberately not waited on: the first request is
    // seconds away and the factory blocks only if it somehow is not.
    (void)QtConcurrent::run([factory, query] { factory->warm(query); });
}

} // namespace ProxyConfig
