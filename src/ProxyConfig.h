#pragma once

#include <QUrl>

// Where the app's HTTP requests learn whether they need a proxy.
//
// Qt asks the operating system for a proxy on every single request, and it
// asks on the thread the reply lives on -- which is the GUI thread. On
// Windows with "automatically detect settings" turned on, that question is
// WPAD: a DHCP/DNS round trip, and on some networks a PAC fetch. It blocks
// whoever asked. Measured in the field: four seconds for the first request
// of a session and about 1.4 for each one after, with the window frozen
// solid for every one of them and nothing in the app's own code to blame.
//
// The answer cannot change often enough to be worth asking twice, so this
// asks once, off the GUI thread, at startup -- long before the first
// request -- and hands out the cached answer from then on.
namespace ProxyConfig {

// Installs the caching factory and starts the one lookup, warmed for
// `serverUrl` (the host every request in the app goes to).
void install(const QUrl &serverUrl);

} // namespace ProxyConfig
