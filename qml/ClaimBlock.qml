import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One statement out of an interpretation, with everything the reader needs to
// judge it: where it came from, the passages behind it, and the handful of
// follow-up moves §5 asks for.
//
// Used by both the quick interpretation and the close reading, so a claim
// looks and behaves the same wherever it appears.
ColumnLayout {
    id: root

    // { text, source, evidence: [{blockId, quote, verified, page}],
    //   type?, unsupported?, dimension?, verdict? }
    property var claim: null
    property int fs: 13
    property string bullet: "• "
    // A project-wide statement is not about one paper, so "save as a note"
    // and "add to the comparison" have nothing to attach to. The follow-up
    // questions still make sense, so only these two are gated.
    property bool allowNotes: true
    property bool allowCompare: true

    signal evidenceRequested(int page, int blockId)
    signal askAiRequested(string text)
    signal noteRequested(string text)
    signal compareRequested(string text)

    spacing: 2

    readonly property string claimText: claim && claim.text ? claim.text : ""

    function sourceColor(code) {
        switch (code) {
        case "author_claim":  return Theme.accent
        case "experimental":  return Theme.success
        case "speculation":   return Theme.danger
        default:              return Theme.dimText
        }
    }
    function sourceLabel(code) {
        switch (code) {
        case "author_claim":  return qsTr("authors")
        case "experimental":  return qsTr("experiment")
        case "speculation":   return qsTr("speculation")
        default:              return qsTr("AI reading")
        }
    }
    function sourceHint(code) {
        switch (code) {
        case "author_claim":  return qsTr("The authors state this.")
        case "experimental":  return qsTr("An experiment in this paper shows this.")
        case "speculation":   return qsTr("A guess — nothing in the paper supports it.")
        default:              return qsTr("The model's own reading, not stated in the paper.")
        }
    }
    function typeLabel(code) {
        switch (code) {
        case "problem":     return qsTr("new problem")
        case "method":      return qsTr("new method")
        case "system":      return qsTr("system")
        case "dataset":     return qsTr("dataset / benchmark")
        case "finding":     return qsTr("empirical finding")
        case "theory":      return qsTr("theory")
        case "engineering": return qsTr("engineering")
        default:            return code || ""
        }
    }
    function verdictColor(v) {
        switch (v) {
        case "solid":    return Theme.success
        case "adequate": return Theme.accent
        case "weak":     return Theme.danger
        default:         return Theme.dimText
        }
    }
    function verdictLabel(v) {
        switch (v) {
        case "solid":    return qsTr("holds up")
        case "adequate": return qsTr("adequate")
        case "weak":     return qsTr("weak")
        default:         return qsTr("can't tell")
        }
    }

    component MiniPill: Rectangle {
        id: pill
        property string label: ""
        property color tint: Theme.dimText
        property string hint: ""
        implicitWidth: pillText.implicitWidth + 12
        implicitHeight: root.fs + 6
        radius: height / 2
        color: Qt.alpha(pill.tint, Theme.dark ? 0.22 : 0.13)
        border.color: Qt.alpha(pill.tint, 0.5)
        Label {
            id: pillText
            anchors.centerIn: parent
            text: pill.label
            color: pill.tint
            font.pixelSize: root.fs - 2
        }
        ToolTip.visible: pillHover.hovered && pill.hint.length > 0
        ToolTip.text: pill.hint
        ToolTip.delay: 400
        HoverHandler { id: pillHover }
    }

    Label {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.text
        font.pixelSize: root.fs
        text: root.bullet + root.claimText
    }

    Flow {
        Layout.fillWidth: true
        Layout.leftMargin: 10
        spacing: 4

        MiniPill {
            visible: !!(root.claim && root.claim.verdict)
            label: root.verdictLabel(root.claim ? root.claim.verdict : "")
            tint: root.verdictColor(root.claim ? root.claim.verdict : "")
        }
        MiniPill {
            label: root.sourceLabel(root.claim ? root.claim.source : "")
            tint: root.sourceColor(root.claim ? root.claim.source : "")
            hint: root.sourceHint(root.claim ? root.claim.source : "")
        }
        MiniPill {
            visible: !!(root.claim && root.claim.type)
            label: root.typeLabel(root.claim ? root.claim.type : "")
            tint: Theme.heading
        }
        MiniPill {
            visible: !!(root.claim && root.claim.unsupported === true)
            label: qsTr("no evidence found")
            tint: Theme.danger
            hint: qsTr("This was presented as the authors' or as an experimental "
                       + "result, but nothing in the paper backed it up, so it is "
                       + "shown as the model's own reading.")
        }

        Repeater {
            model: root.claim && root.claim.evidence ? root.claim.evidence : []
            delegate: Button {
                required property var modelData
                implicitHeight: root.fs + 8
                padding: 4
                flat: true
                enabled: modelData.verified === true
                font.pixelSize: root.fs - 2
                text: modelData.verified === true
                      ? qsTr("p%1").arg(modelData.page)
                      : qsTr("unverified")
                palette.buttonText: modelData.verified === true ? Theme.accent
                                                                : Theme.danger
                ToolTip.visible: hovered
                ToolTip.delay: 300
                ToolTip.text: modelData.verified === true
                              ? qsTr("“%1”\nClick to open this passage.")
                                .arg(modelData.quote || "")
                              : qsTr("The model cited a passage that is not in "
                                     + "the paper: “%1”").arg(modelData.quote || "")
                onClicked: root.evidenceRequested(modelData.page || 0,
                                                  modelData.blockId || -1)
            }
        }

        // §5: the follow-up moves on one statement.
        ToolButton {
            implicitHeight: root.fs + 8
            implicitWidth: root.fs + 12
            padding: 2
            font.pixelSize: root.fs - 2
            text: "⋯"
            onClicked: claimMenu.popup()
            Menu {
                id: claimMenu
                MenuItem {
                    text: qsTr("Explain this more simply")
                    onTriggered: root.askAiRequested(
                        qsTr("Explain this more simply, for someone new to the "
                             + "area:\n\n%1").arg(root.claimText))
                }
                MenuItem {
                    text: qsTr("Give me an example")
                    onTriggered: root.askAiRequested(
                        qsTr("Give a concrete example that makes this clear:"
                             + "\n\n%1").arg(root.claimText))
                }
                MenuItem {
                    text: qsTr("Explain the equation / figure behind it")
                    onTriggered: root.askAiRequested(
                        qsTr("Walk me through the equation, figure or table this "
                             + "rests on, step by step:\n\n%1").arg(root.claimText))
                }
                MenuItem {
                    text: qsTr("Challenge this")
                    onTriggered: root.askAiRequested(
                        qsTr("I am not convinced by this. What would have to be "
                             + "true for it to hold, what in the paper argues "
                             + "against it, and what would settle it?\n\n%1")
                        .arg(root.claimText))
                }
                MenuItem {
                    text: qsTr("Ask about this")
                    onTriggered: root.askAiRequested(
                        qsTr("About this point:\n\n%1\n\n").arg(root.claimText))
                }
                MenuSeparator {
                    visible: root.allowNotes || root.allowCompare
                }
                MenuItem {
                    text: qsTr("Save as a note")
                    visible: root.allowNotes
                    height: visible ? implicitHeight : 0
                    onTriggered: root.noteRequested(root.claimText)
                }
                MenuItem {
                    text: qsTr("Add to the comparison")
                    visible: root.allowCompare
                    height: visible ? implicitHeight : 0
                    onTriggered: root.compareRequested(root.claimText)
                }
            }
        }
    }
}
