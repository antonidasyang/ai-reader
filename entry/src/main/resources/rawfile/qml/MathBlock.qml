import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One display-math block ($$ … $$) inside an assistant chat reply.
// Backed by MicroTeX via LatexRenderer (C++) which produces a PNG data
// URL. When the renderer fails (uninitialized fonts, parse error) we
// fall back to showing the raw LaTeX in a yellow-tinted code box so the
// user can still read what the model wrote.
Item {
    id: root

    property string latex: ""
    property string dataUrl: ""

    Layout.fillWidth: true
    implicitHeight: dataUrl.length > 0
                    ? mathImage.implicitHeight + 16
                    : fallback.implicitHeight + 16

    // Intentionally theme-independent: LatexRenderer (C++) draws the
    // formula PNG with fixed near-black ink (#1d1d1d), so this card must
    // stay light in BOTH themes or the glyphs disappear in dark mode.
    // Everything inside uses explicit dark-on-light colors.
    Rectangle {
        anchors.fill: parent
        color: "#fafafa"
        border.color: "#eaeaea"
        border.width: 1
        radius: 4
    }

    // Successful render → centered Image.
    Image {
        id: mathImage
        visible: root.dataUrl.length > 0
        anchors.centerIn: parent
        source: root.dataUrl
        fillMode: Image.PreserveAspectFit
        // Cap on huge formulas so the bubble doesn't blow up.
        property int maxWidth: parent.width - 16
        width: implicitWidth > maxWidth ? maxWidth : implicitWidth
        height: implicitWidth > 0
                ? width * (implicitHeight / Math.max(1, implicitWidth))
                : 0

        // Right-click → copy raw LaTeX to clipboard.
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: copyMenu.popup()
        }
        Menu {
            id: copyMenu
            MenuItem {
                text: qsTr("Copy LaTeX source")
                onTriggered: {
                    fallbackText.text = root.latex
                    fallbackText.selectAll()
                    fallbackText.copy()
                }
            }
        }
    }

    // Fallback: render-failed or renderer not yet initialized — show the
    // LaTeX source verbatim so nothing is silently dropped.
    Rectangle {
        id: fallback
        visible: root.dataUrl.length === 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: 8
        color: "#fff8d6"  // fixed pale yellow — self-contained warning tint (~9:1 with the ink)
        radius: 3
        implicitHeight: fallbackText.implicitHeight + 12

        TextEdit {
            id: fallbackText
            anchors.fill: parent
            anchors.margins: 6
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            color: "#5a3e00"          // fixed dark amber on the fixed light box
            selectionColor: "#b6d7ff" // fixed light selection: ambient dark palette must not leak in
            selectedTextColor: "#1d1d1d"
            font.family: "monospace"
            text: "$$" + root.latex + "$$"
        }
    }
}
