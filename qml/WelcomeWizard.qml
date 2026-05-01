import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// First-run tour. Walks the user through the five major capabilities:
// open a paper, the panel layout, paragraph translation, AI chat with
// sessions, and where to configure the LLM. Auto-shown once on first
// launch (gated by layoutSettings.wizardSeen) and re-launchable from
// the toolbar's "?" Help button.
Dialog {
    id: root
    modal: true
    title: qsTr("Welcome to AI Reader")
    standardButtons: Dialog.NoButton
    closePolicy: Popup.NoAutoClose
    width: Math.min(parent ? parent.width  - 60 : 560, 560)
    height: Math.min(parent ? parent.height - 60 : 460, 460)

    property int step: 0
    readonly property int stepCount: 5

    function reset() { step = 0 }
    function finish() {
        if (typeof layoutSettings !== "undefined")
            layoutSettings.setWizardSeen(true)
        root.close()
    }

    onAboutToShow: reset()

    contentItem: ColumnLayout {
        spacing: 14

        // Step indicator dots — centered above the body.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Repeater {
                model: root.stepCount
                delegate: Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: index === root.step ? "#4c8bf5" : "#cfcfcf"
                    Behavior on color { ColorAnimation { duration: 120 } }
                }
            }
        }

        // Per-step body. Each step renders its own subtree via a
        // switching Loader so we don't keep all five mounted at once.
        Loader {
            id: bodyLoader
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent:
                root.step === 0 ? step1
              : root.step === 1 ? step2
              : root.step === 2 ? step3
              : root.step === 3 ? step4
                                : step5
        }

        // Footer: Skip on the left, Back/Next/Finish on the right.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                text: qsTr("Skip")
                flat: true
                visible: root.step < root.stepCount - 1
                onClicked: root.finish()
            }
            Item { Layout.fillWidth: true }
            Button {
                text: qsTr("Back")
                enabled: root.step > 0
                onClicked: root.step = root.step - 1
            }
            Button {
                text: root.step === root.stepCount - 1
                      ? qsTr("Got it!")
                      : qsTr("Next")
                highlighted: true
                onClicked: {
                    if (root.step === root.stepCount - 1) root.finish()
                    else root.step = root.step + 1
                }
            }
        }
    }

    // ── Step components ────────────────────────────────────────────
    // Each step is a self-contained ColumnLayout with a heading + a
    // bulleted body explaining what to look for in the actual UI.

    Component {
        id: step1
        ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("1 · Open a paper")
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("AI Reader helps you read academic PDFs with side-by-side translation, AI chat, and an auto-generated table of contents.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#444"
                text: qsTr("To get started, click <b>Open…</b> on the toolbar to load one PDF, or <b>Open folder…</b> to browse a whole library. You can also drag-and-drop a .pdf into the window. Each paper opens in its own tab — close a tab with the × on the right edge.")
            }
        }
    }

    Component {
        id: step2
        ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("2 · Panels and layout")
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The window is split into panes — Folder, TOC, PDF, Paragraphs, Interpretation, Chat. Toggle each one on or off from the toolbar.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#444"
                text: qsTr("Each pane has a small ⋮⋮ grip in its top-left corner. Drag a grip onto another pane's edge to <b>reorder the panels</b> — the layout is remembered between launches. Drag the splitter bars to resize.")
            }
        }
    }

    Component {
        id: step3
        ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("3 · Translate paragraphs")
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("The Paragraphs pane shows extracted text from the PDF. Click <b>Translate</b> in the toolbar to translate every paragraph at once.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#444"
                text: qsTr("<b>Right-click</b> a paragraph for more: translate just that paragraph, ask AI about it, split it at the cursor, merge it with the neighbour, or delete it. The ▲▼ chevrons next to each paragraph hide/show the source or translation.")
            }
        }
    }

    Component {
        id: step4
        ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("4 · Chat with the paper")
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Open the Chat pane and ask questions about the paper. The model has tools to read pages, search the text, look up captions, and view rendered pages with vision.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#444"
                text: qsTr("Each paper keeps its own list of <b>chat sessions</b> in the tab strip on top of the pane: + adds a new session, × closes one, double-click to rename. To quote a passage, right-click it in the Paragraphs pane and choose <b>Ask AI about this</b>.")
            }
        }
    }

    Component {
        id: step5
        ColumnLayout {
            spacing: 10
            Label {
                text: qsTr("5 · Configure your LLM")
                font.bold: true
                font.pixelSize: 16
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: qsTr("Open <b>Settings…</b> from the toolbar to add a model and API key. Anthropic Claude and any OpenAI-compatible endpoint (DeepSeek, OpenRouter, local llama.cpp, …) are supported.")
            }
            Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#444"
                text: qsTr("Use <b>Prompts…</b> to customise the system prompts for translation, summary, TOC, and chat. You can re-open this tour any time from the <b>?</b> button on the toolbar.")
            }
        }
    }
}
