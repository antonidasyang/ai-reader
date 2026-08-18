import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AiReader

// Coach-mark style first-run tour. Instead of a centered modal that
// hides the UI it's trying to teach, we draw a four-rectangle dim
// mask over the whole window with a cut-out around the target widget,
// outline the cut-out in accent blue, and float a callout card next
// to it pointing at the highlighted control.
//
// Steps are configured by the parent (Main.qml) at component-completed
// time as an array of { target, title, body } objects, where `target`
// is a reference to the QML item to spotlight (typically a toolbar
// button or a pane). Re-use it later via the toolbar's "?" button.
Popup {
    id: root

    parent: Overlay.overlay
    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    modal: false
    closePolicy: Popup.NoAutoClose
    background: Item {}

    property var steps: []
    property int stepIndex: 0

    readonly property var currentStep:
        (steps && stepIndex >= 0 && stepIndex < steps.length)
            ? steps[stepIndex] : null

    // A step's `target` may be a single Item OR an array of Items —
    // useful when one explanation covers a related group of buttons
    // (Open + Open folder, the three pane-toggle buttons, etc.).
    readonly property var targetItems: {
        if (!currentStep || !currentStep.target) return []
        const t = currentStep.target
        return Array.isArray(t) ? t : [t]
    }

    // Padding around the bounding rect so the spotlight outline
    // doesn't clip the button's own bevel/shadow.
    property int spotPadding: 8

    // Bounding rect that covers every visible target item in this
    // popup's coordinate system. Reading each item's x/y/w/h + the
    // popup's own size makes the binding re-evaluate on resize, pane
    // toggles, etc.
    readonly property rect targetRect: {
        const w = root.width, h = root.height
        if (targetItems.length === 0)
            return Qt.rect(w / 2 - 1, h / 2 - 1, 2, 2)
        let minX = Infinity, minY = Infinity
        let maxX = -Infinity, maxY = -Infinity
        let any = false
        for (let i = 0; i < targetItems.length; ++i) {
            const it = targetItems[i]
            if (!it || !it.visible) continue
            // Touch deps so the binding tracks layout changes.
            const _ = it.x + it.y + it.width + it.height
            const p = it.mapToItem(content, 0, 0)
            if (p.x < minX) minX = p.x
            if (p.y < minY) minY = p.y
            if (p.x + it.width  > maxX) maxX = p.x + it.width
            if (p.y + it.height > maxY) maxY = p.y + it.height
            any = true
        }
        if (!any) return Qt.rect(w / 2 - 1, h / 2 - 1, 2, 2)
        return Qt.rect(minX, minY, maxX - minX, maxY - minY)
    }
    readonly property rect spotRect: Qt.rect(
        targetRect.x - spotPadding,
        targetRect.y - spotPadding,
        targetRect.width  + 2 * spotPadding,
        targetRect.height + 2 * spotPadding
    )

    function start()  { stepIndex = 0; open() }
    function finish() {
        if (typeof layoutSettings !== "undefined") {
            layoutSettings.setWizardSeen(true)
            // Stamp the version: the tour replays once per release.
            if (typeof settings !== "undefined")
                layoutSettings.setWizardSeenVersion(settings.appVersion)
            // Stamp the version too so the changelog dialog doesn't
            // pop immediately after the wizard on a brand-new
            // install. The user has effectively seen what's new --
            // they just installed it.
            if (typeof settings !== "undefined")
                layoutSettings.setLastSeenVersion(settings.appVersion)
        }
        close()
    }

    // Same button language as the dialogs' footers (see e.g.
    // SettingsDialog): primary = accent-filled, ghost = quiet text.
    component ActionButton: Button {
        id: ab
        property bool primary: false
        property bool ghost: false
        implicitHeight: Theme.controlH
        leftPadding: Theme.spaceL
        rightPadding: Theme.spaceL
        contentItem: Text {
            text: ab.text
            font.pixelSize: 13
            font.weight: ab.primary ? Font.DemiBold : Font.Normal
            color: ab.primary ? Theme.onPrimary
                   : ab.ghost ? (ab.hovered ? Theme.text : Theme.dimText)
                   : Theme.text
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            opacity: ab.enabled ? 1 : 0.45
            Behavior on color { ColorAnimation { duration: 120 } }
        }
        background: Rectangle {
            radius: Theme.radiusS
            color: ab.primary
                   ? (ab.down ? Theme.primaryPressed : ab.hovered ? Theme.primaryHover : Theme.primaryBg)
                   : ab.ghost
                     ? (ab.down ? Theme.buttonPressed : ab.hovered ? Theme.buttonHover : "transparent")
                     : (ab.down ? Theme.buttonPressed : ab.hovered ? Theme.buttonHover : Theme.buttonBg)
            border.width: ab.primary || ab.ghost ? 0 : 1
            border.color: ab.visualFocus ? Theme.accent : Theme.border
            opacity: ab.enabled ? 1 : 0.45
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    contentItem: Item {
        id: content

        // Top-level click-blocker so clicks to underlying widgets are
        // swallowed while the tour is up. The user follows the wizard's
        // Next / Back / Skip buttons.
        MouseArea {
            anchors.fill: parent
            preventStealing: true
            onPressed: function(mouse) { mouse.accepted = true }
            onReleased: function(mouse) { mouse.accepted = true }
            onClicked: function(mouse) { mouse.accepted = true }
        }

        // ── 4-piece dim mask leaving a cut-out around spotRect ──────
        Rectangle {  // top
            x: 0; y: 0
            width: parent.width
            height: Math.max(0, root.spotRect.y)
            color: "#a0000000"
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }
        Rectangle {  // bottom
            x: 0
            y: root.spotRect.y + root.spotRect.height
            width: parent.width
            height: Math.max(0, parent.height - y)
            color: "#a0000000"
            Behavior on y      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }
        Rectangle {  // left
            x: 0
            y: root.spotRect.y
            width: Math.max(0, root.spotRect.x)
            height: root.spotRect.height
            color: "#a0000000"
            Behavior on y      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on width  { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }
        Rectangle {  // right
            x: root.spotRect.x + root.spotRect.width
            y: root.spotRect.y
            width: Math.max(0, parent.width - x)
            height: root.spotRect.height
            color: "#a0000000"
            Behavior on x      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on width  { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }

        // Spotlight outline + soft halo.
        Rectangle {
            x: root.spotRect.x - 4
            y: root.spotRect.y - 4
            width: root.spotRect.width + 8
            height: root.spotRect.height + 8
            color: "transparent"
            border.color: Qt.alpha(Theme.accent, 0.4)
            border.width: 4
            radius: Theme.radiusL
            Behavior on x      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on width  { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }
        Rectangle {
            x: root.spotRect.x
            y: root.spotRect.y
            width: root.spotRect.width
            height: root.spotRect.height
            color: "transparent"
            border.color: Theme.accent
            border.width: 2
            radius: Theme.radiusM
            Behavior on x      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y      { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on width  { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
        }

        // Numbered marker badge anchored to the spotlight's upper-left.
        Rectangle {
            x: Math.max(8, root.spotRect.x - width / 2)
            y: Math.max(8, root.spotRect.y - height / 2)
            width: 28; height: 28
            radius: 14
            // primaryBg (Fluent blue) keeps the white number legible in
            // both themes; accent is pale in dark mode.
            color: Theme.primaryBg
            border.color: Theme.dialogBg
            border.width: 2
            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Label {
                anchors.centerIn: parent
                text: (root.stepIndex + 1).toString()
                color: Theme.onPrimary
                font.bold: true
                font.pixelSize: 13
            }
        }

        // ── Floating callout card with the explanation. ─────────────
        // Picks the side (below/above/right/left) that fits in the
        // window; falls back to centered when nothing fits.
        Rectangle {
            id: callout
            width: 380
            height: calloutBody.implicitHeight + 2 * Theme.dialogPadding
            radius: Theme.radiusL
            color: Theme.dialogBg
            border.color: Theme.border
            border.width: 1

            readonly property real gap: 24
            readonly property bool fitsBelow: root.spotRect.y + root.spotRect.height + gap + height + 16 <= parent.height
            readonly property bool fitsAbove: root.spotRect.y - gap - height - 16 >= 0
            readonly property bool fitsRight: root.spotRect.x + root.spotRect.width + gap + width + 16 <= parent.width
            readonly property bool fitsLeft:  root.spotRect.x - gap - width - 16 >= 0

            x: {
                if (fitsBelow || fitsAbove)
                    return Math.max(16, Math.min(parent.width - width - 16,
                        root.spotRect.x + root.spotRect.width / 2 - width / 2))
                if (fitsRight)
                    return root.spotRect.x + root.spotRect.width + gap
                if (fitsLeft)
                    return root.spotRect.x - gap - width
                return (parent.width - width) / 2
            }
            y: {
                if (fitsBelow)
                    return root.spotRect.y + root.spotRect.height + gap
                if (fitsAbove)
                    return root.spotRect.y - gap - height
                if (fitsRight || fitsLeft)
                    return Math.max(16, Math.min(parent.height - height - 16,
                        root.spotRect.y + root.spotRect.height / 2 - height / 2))
                return (parent.height - height) / 2
            }

            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }

            // Soft shadow so the card lifts off the dim mask.
            Rectangle {
                z: -1
                x: 0; y: 3
                width: parent.width
                height: parent.height
                radius: parent.radius + 1
                color: Theme.dialogShadow
            }

            ColumnLayout {
                id: calloutBody
                anchors.fill: parent
                anchors.margins: Theme.dialogPadding
                spacing: Theme.spaceM

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spaceXs + 2
                    Label {
                        text: qsTr("Step %1 of %2")
                              .arg(root.stepIndex + 1)
                              .arg(root.steps.length)
                        color: Theme.dimText
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    // Step indicator dots; the current one stretches to a pill.
                    Repeater {
                        model: root.steps.length
                        delegate: Rectangle {
                            width: index === root.stepIndex ? 18 : 7
                            height: 7
                            radius: 3.5
                            color: index === root.stepIndex ? Theme.accent : Theme.fieldBorder
                            Behavior on color { ColorAnimation { duration: 120 } }
                            Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                        }
                    }
                }

                Label {
                    text: root.currentStep ? root.currentStep.title : ""
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                    color: Theme.text
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Label {
                    text: root.currentStep ? root.currentStep.body : ""
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Theme.bodyText
                    font.pixelSize: 13
                    lineHeight: 1.25
                    textFormat: Text.RichText
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.spaceXs
                    spacing: Theme.spaceS
                    ActionButton {
                        text: qsTr("Skip")
                        ghost: true
                        visible: root.stepIndex < root.steps.length - 1
                        onClicked: root.finish()
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton {
                        text: qsTr("Back")
                        enabled: root.stepIndex > 0
                        onClicked: root.stepIndex = root.stepIndex - 1
                    }
                    ActionButton {
                        text: root.stepIndex === root.steps.length - 1
                              ? qsTr("Got it!")
                              : qsTr("Next")
                        primary: true
                        onClicked: {
                            if (root.stepIndex === root.steps.length - 1)
                                root.finish()
                            else
                                root.stepIndex = root.stepIndex + 1
                        }
                    }
                }
            }
        }
    }
}
