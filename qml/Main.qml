import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Pdf

ApplicationWindow {
    id: window
    width: 1200
    height: 800
    visible: true
    title: pdfDoc.status === PdfDocument.Ready && currentFileName.length > 0
           ? "AI Reader — " + currentFileName
           : "AI Reader"

    property string currentFileName: ""

    function fileNameFromUrl(url) {
        const s = url.toString()
        const slash = s.lastIndexOf("/")
        return slash >= 0 ? decodeURIComponent(s.substring(slash + 1)) : s
    }

    function errorText(err) {
        switch (err) {
        case PdfDocument.NoError: return ""
        case PdfDocument.DataNotYetAvailable: return qsTr("PDF data is not yet available.")
        case PdfDocument.FileNotFound: return qsTr("File not found.")
        case PdfDocument.InvalidFileFormat: return qsTr("Invalid PDF file.")
        case PdfDocument.IncorrectPassword: return qsTr("Incorrect password.")
        case PdfDocument.UnsupportedSecurityScheme: return qsTr("Unsupported PDF security scheme.")
        default: return qsTr("Failed to open PDF.")
        }
    }

    function openPdf(url) {
        window.currentFileName = fileNameFromUrl(url)
        errorBanner.visible = false
        pdfDoc.password = ""
        pdfDoc.source = url
    }

    PdfDocument {
        id: pdfDoc
        onPasswordRequired: passwordDialog.open()
        onStatusChanged: {
            if (status === PdfDocument.Error && error !== PdfDocument.IncorrectPassword) {
                errorBanner.text = window.errorText(error)
                errorBanner.visible = true
            } else if (status === PdfDocument.Ready) {
                errorBanner.visible = false
            }
        }
    }

    PasswordDialog {
        id: passwordDialog
        anchors.centerIn: Overlay.overlay
        promptText: qsTr("\"%1\" is encrypted. Enter the password:").arg(window.currentFileName)
        onAccepted: pdfDoc.password = password
        onRejected: {
            errorBanner.text = qsTr("Cancelled — password not provided.")
            errorBanner.visible = true
            pdfDoc.source = ""
            window.currentFileName = ""
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Open PDF")
        nameFilters: ["PDF files (*.pdf)", "All files (*)"]
        onAccepted: window.openPdf(selectedFile)
    }

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            ToolButton {
                text: qsTr("Open…")
                onClicked: fileDialog.open()
            }
            Item { Layout.fillWidth: true }
            Label {
                text: pdfDoc.status === PdfDocument.Ready
                      ? qsTr("%1 pages").arg(pdfDoc.pageCount)
                      : ""
                Layout.rightMargin: 4
            }
        }
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]

        onDropped: function(drop) {
            if (drop.hasUrls && drop.urls.length > 0) {
                for (let i = 0; i < drop.urls.length; ++i) {
                    const u = drop.urls[i].toString()
                    if (u.toLowerCase().endsWith(".pdf")) {
                        window.openPdf(drop.urls[i])
                        drop.accepted = true
                        return
                    }
                }
                errorBanner.text = qsTr("Dropped file is not a PDF.")
                errorBanner.visible = true
            }
        }

        // Reader pane
        PdfMultiPageView {
            id: pdfView
            anchors.fill: parent
            document: pdfDoc
            visible: pdfDoc.status === PdfDocument.Ready
        }

        // Empty state
        Rectangle {
            anchors.fill: parent
            visible: pdfDoc.status === PdfDocument.Null
                     || (pdfDoc.status === PdfDocument.Error && pdfDoc.source.toString().length === 0)
            color: "#1e1f22"
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                Label {
                    text: qsTr("Drag a PDF here, or click Open…")
                    color: "#bbbbbb"
                    font.pixelSize: 18
                    horizontalAlignment: Text.AlignHCenter
                    Layout.alignment: Qt.AlignHCenter
                }
                Label {
                    text: qsTr("AI Reader — milestone 1")
                    color: "#666666"
                    font.pixelSize: 12
                    Layout.alignment: Qt.AlignHCenter
                }
            }
        }

        // Drop hover overlay
        Rectangle {
            anchors.fill: parent
            visible: dropArea.containsDrag
            color: "#332b6cff"
            border.color: "#5b8def"
            border.width: 2
            Label {
                anchors.centerIn: parent
                text: qsTr("Drop PDF to open")
                color: "white"
                font.pixelSize: 20
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            running: pdfDoc.status === PdfDocument.Loading
            visible: running
        }
    }

    Rectangle {
        id: errorBanner
        property alias text: errorLabel.text
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: visible ? 36 : 0
        visible: false
        color: "#5a1f1f"

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 8
            Label {
                id: errorLabel
                color: "white"
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            ToolButton {
                text: "✕"
                onClicked: errorBanner.visible = false
            }
        }
    }
}
