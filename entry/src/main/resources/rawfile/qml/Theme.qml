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

    // ── Dialog chrome (additive tokens — shared by the popup dialogs) ──
    readonly property color dialogBg:     dark ? "#232529" : "#ffffff"  // elevated surface
    readonly property color overlayDim:   dark ? "#99000000" : "#59000000"  // modal scrim
    readonly property color dialogShadow: dark ? "#66000000" : "#26000000"  // soft drop hint
    readonly property color cardBg:       dark ? "#282a2f" : "#f6f7f9"  // grouped section card

    // ── Inputs (dialog form fields) ────────────────────────────────────
    readonly property color fieldBg:     dark ? "#1c1d20" : "#f7f8fa"
    readonly property color fieldBorder: dark ? "#44474d" : "#d7dade"
    readonly property color focusRing:   Qt.alpha(accent, dark ? 0.35 : 0.22)

    // ── Buttons (primary = filled, secondary = quiet) ──────────────────
    // Primary fill gets its own trio instead of raw `accent`: accent
    // (Material blue 800 in light mode) is tuned for links/focus rings
    // and reads harsh as a large filled surface. Mainstream look in
    // BOTH themes: Fluent blue fill + white text (the pale-blue fill
    // with dark text we shipped first read as ugly in dark mode).
    readonly property color primaryBg:      dark ? "#1180dc" : "#0078d4"
    readonly property color primaryHover:   dark ? "#2b8fe4" : "#106ebe"
    readonly property color primaryPressed: dark ? "#0d67b5" : "#005a9e"
    readonly property color onPrimary:      "#ffffff"
    readonly property color onAccent:      dark ? "#10131a" : "#ffffff"
    readonly property color accentHover:   dark ? "#8db4ff" : "#1a70d6"
    readonly property color accentPressed: dark ? "#6b95e8" : "#11529c"
    readonly property color buttonBg:      dark ? "#2d2f34" : "#f2f3f5"
    readonly property color buttonHover:   dark ? "#36393f" : "#e9ebef"
    readonly property color buttonPressed: dark ? "#3d4148" : "#dee1e6"

    // ── Selected text ──────────────────────────────────────────────────
    // QtQuick.Pdf fills a selection with the palette highlight at half
    // alpha. The paragraph pane used to take Fusion's own selection colour
    // instead -- flat, opaque and much darker -- so the same sentence
    // selected in the page and in the paragraph looked like two different
    // things. One token now paints both, and being translucent it leaves the
    // text itself readable rather than reversing it out.
    readonly property color selection: Qt.alpha(accent, 0.45)

    // ── Status ─────────────────────────────────────────────────────────
    readonly property color success: dark ? "#7bc67e" : "#2e7d32"

    // How long the small hover/press transitions run. Set to 0 by Main.qml
    // when this session renders in software for a remote desktop: there,
    // every animated frame is a full-window bitmap that has to be encoded
    // and sent, so a 120 ms colour fade is the most expensive decoration
    // in the app.
    property int animMs: 120

    // ── Metrics (shared spacing / radius scale for dialogs) ────────────
    readonly property int radiusL: 12   // dialog corners
    readonly property int radiusM: 8    // cards, popups
    readonly property int radiusS: 6    // inputs, buttons
    readonly property int spaceXs: 4
    readonly property int spaceS: 8
    readonly property int spaceM: 12
    readonly property int spaceL: 16
    readonly property int spaceXl: 24
    readonly property int controlH: 32       // uniform input/button height
    readonly property int dialogPadding: 20  // dialog body / header inset
}
