pragma Singleton
import QtQuick

// Central light/dark color tokens. `dark` tracks the OS appearance live
// (Qt 6.5+ exposes Qt.styleHints.colorScheme, which emits a change signal
// when the user flips the system theme), so every binding through Theme
// re-themes the whole UI without a restart. Panes hardcoded light colors
// before this existed, which rendered as white-on-white in dark mode.
QtObject {
    readonly property bool dark: Qt.styleHints.colorScheme === Qt.Dark

    // ── Surfaces ──────────────────────────────────────────────────────
    readonly property color paneBg:   dark ? "#1e1f22" : "#fafafa"
    readonly property color headerBg: dark ? "#2b2d30" : "#ececec"
    readonly property color inputBg:  dark ? "#262729" : "#f0f0f0"
    readonly property color border:   dark ? "#3a3d41" : "#dddddd"
    readonly property color divider:  dark ? "#34363a" : "#ececec"

    // ── Text ──────────────────────────────────────────────────────────
    readonly property color text:     dark ? "#e6e6e6" : "#1d1d1d"  // primary
    readonly property color bodyText: dark ? "#c9ccd1" : "#5f6368"  // source paragraphs
    readonly property color dimText:  dark ? "#9aa0a6" : "#888888"  // labels / hints

    // ── Interaction ───────────────────────────────────────────────────
    readonly property color hover:     dark ? "#2f3136" : "#f0f3ff"
    readonly property color activeRow: dark ? "#314a6e" : "#d4e2f5"

    // ── Chat bubbles ──────────────────────────────────────────────────
    readonly property color bubbleUser:      dark ? "#2b3a55" : "#dee5ff"
    readonly property color bubbleAssistant: dark ? "#26272b" : "#ffffff"

    // ── Accents (semantic — chosen to read on both themes) ─────────────
    readonly property color accent:  dark ? "#7aa7ff" : "#1565c0"
    readonly property color danger:  dark ? "#ff6b6b" : "#c62828"
    readonly property color heading: dark ? "#9ab8ff" : "#1a237e"
}
