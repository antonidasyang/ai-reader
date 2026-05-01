#pragma once

class Settings;

// Tiny façade over Sentry-Native. Defined unconditionally so callers
// don't have to litter their code with #ifdefs; when Sentry isn't
// linked in (the default build), every entry point is a no-op.
//
// start() is gated by *both* AIREADER_HAS_SENTRY (compiled in) AND
// settings.crashReportsOptIn() (user opted in at runtime), so a
// release build with Sentry linked still respects a user who never
// flipped the switch. The DSN is baked in at configure time via
// AIREADER_SENTRY_DSN — Sentry DSNs are project-id strings, not
// secrets, so safe to embed.
namespace CrashReporter {

// Initialise the SDK. Safe to call multiple times — subsequent
// calls after a successful init are ignored.
void start(Settings *settings);

// Flush queued events and shut the SDK down. Safe to call without
// a matching start(). Wire to QGuiApplication::aboutToQuit so
// in-flight reports get a chance to leave the machine.
void stop();

} // namespace CrashReporter
