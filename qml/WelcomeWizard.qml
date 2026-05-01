import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
    readonly property Item targetItem:
        currentStep && currentStep.target ? currentStep.target : null

    // Padding around the target so the spotlight outline doesn't
    // clip the button's own bevel/shadow.
    property int spotPadding: 8

    // Bounding rect of the spotlight target in this popup's
    // coordinate system. Touches targetItem.x/y/width/height + the
    // popup's own size so binding re-evaluates on resize, pane
    // toggles, etc.
    readonly property rect targetRect: {
        const w = root.width, h = root.height
        if (!targetItem || !targetItem.visible)
            return Qt.rect(w / 2 - 1, h / 2 - 1, 2, 2)
        // Touch deps so the binding tracks layout changes.
        const _ = targetItem.x + targetItem.y
                + targetItem.width + targetItem.height
        const p = targetItem.mapToItem(content, 0, 0)
        return Qt.rect(p.x, p.y, targetItem.width, targetItem.height)
    }
    readonly property rect spotRect: Qt.rect(
        targetRect.x - spotPadding,
        targetRect.y - spotPadding,
        targetRect.width  + 2 * spotPadding,
        targetRect.height + 2 * spotPadding
    )

    function start()  { stepIndex = 0; open() }
    function finish() {
        if (typeof layoutSettings !== "undefined")
            layoutSettings.setWizardSeen(true)
        close()
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
            border.color: "#664c8bf5"
            border.width: 4
            radius: 10
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
            border.color: "#4c8bf5"
            border.width: 2
            radius: 6
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
            color: "#4c8bf5"
            border.color: "#ffffff"
            border.width: 2
            Behavior on x { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.InOutQuad } }
            Label {
                anchors.centerIn: parent
                text: (root.stepIndex + 1).toString()
                color: "#ffffff"
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
            height: calloutBody.implicitHeight + 32
            radius: 8
            color: "#ffffff"
            border.color: "#cccccc"
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

            // Subtle shadow so the card lifts off the dim mask.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                z: -1
                color: "transparent"
                border.color: "#33000000"
                border.width: 1
                radius: 9
            }

            ColumnLayout {
                id: calloutBody
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("Step %1 of %2")
                              .arg(root.stepIndex + 1)
                              .arg(root.steps.length)
                        color: "#888"
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    // Step indicator dots.
                    Repeater {
                        model: root.steps.length
                        delegate: Rectangle {
                            width: 8; height: 8; radius: 4
                            color: index === root.stepIndex ? "#4c8bf5" : "#cfcfcf"
                            Behavior on color { ColorAnimation { duration: 120 } }
                        }
                    }
                }

                Label {
                    text: root.currentStep ? root.currentStep.title : ""
                    font.bold: true
                    font.pixelSize: 16
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
                Label {
                    text: root.currentStep ? root.currentStep.body : ""
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: "#444"
                    textFormat: Text.RichText
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Button {
                        text: qsTr("Skip")
                        flat: true
                        visible: root.stepIndex < root.steps.length - 1
                        onClicked: root.finish()
                    }
                    Item { Layout.fillWidth: true }
                    Button {
                        text: qsTr("Back")
                        enabled: root.stepIndex > 0
                        onClicked: root.stepIndex = root.stepIndex - 1
                    }
                    Button {
                        text: root.stepIndex === root.steps.length - 1
                              ? qsTr("Got it!")
                              : qsTr("Next")
                        highlighted: true
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
