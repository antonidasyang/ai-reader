#include "CrashReporter.h"

#include "Settings.h"

#include <QDir>
#include <QStandardPaths>
#include <QString>
#include <QtGlobal>

#ifdef AIREADER_HAS_SENTRY
#  include <sentry.h>
#endif

namespace CrashReporter {

namespace {
bool g_started = false;
} // namespace

void start(Settings *settings)
{
#ifdef AIREADER_HAS_SENTRY
    if (g_started) return;
    if (!settings || !settings->crashReportsOptIn()) return;

    constexpr const char *dsn = AIREADER_SENTRY_DSN;
    if (!dsn || !*dsn) {
        qWarning("CrashReporter: built with Sentry but DSN is empty; skipping init.");
        return;
    }

    // Sentry-Native keeps a per-process database (queued unsent
    // events, deduplication state, last-known-good context) under a
    // directory we own. Park it inside the app's per-user AppData
    // path so we don't pollute the user's home, and so an uninstall
    // wipes it along with the rest of our state.
    const QString cacheDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
        + QStringLiteral("/sentry");
    QDir().mkpath(cacheDir);
    const QByteArray cacheBytes = cacheDir.toLocal8Bit();

    sentry_options_t *opts = sentry_options_new();
    sentry_options_set_dsn(opts, dsn);
    sentry_options_set_release(opts, "ai-reader@" AIREADER_VERSION);
    sentry_options_set_database_path(opts, cacheBytes.constData());
    sentry_options_set_debug(opts, 0);
    // No PII collection — the user opted in to *crash* reports, not
    // to handing us their hostname / username.
    sentry_options_set_send_default_pii(opts, 0);

    if (sentry_init(opts) != 0) {
        qWarning("CrashReporter: sentry_init returned non-zero.");
        return;
    }
    g_started = true;
    qInfo("CrashReporter: Sentry-Native initialised (release ai-reader@%s).",
          AIREADER_VERSION);
#else
    Q_UNUSED(settings);
#endif
}

void stop()
{
#ifdef AIREADER_HAS_SENTRY
    if (!g_started) return;
    sentry_close();
    g_started = false;
#endif
}

} // namespace CrashReporter
