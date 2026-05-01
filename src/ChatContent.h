#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class MarkdownRenderer;

// Splits a chat assistant's Markdown reply into a list of typed
// segments — text/code/math — for the chat pane to render via
// per-type QML delegates instead of the single TextEdit/RichText
// fallback that's used during streaming.
//
// Each entry in the returned list is a QVariantMap with a "type"
// discriminator:
//   { "type": "text", "html":     <pre-rendered HTML> }
//   { "type": "code", "source":   <raw source>,
//                     "html":     <syntax-highlighted HTML>,
//                     "language": <fence info> }
//   { "type": "math", "source":   <raw LaTeX>,
//                     "dataUrl":  <PNG data URL or empty> }
//
// The renderer is exposed to QML as the `chatContent` context
// property; ChatPane.qml binds Repeater.model to chatContent.render()
// and switches the delegate based on the entry type.
class ChatContent : public QObject
{
    Q_OBJECT
public:
    explicit ChatContent(MarkdownRenderer *markdown,
                         QObject *parent = nullptr);

    Q_INVOKABLE QVariantList render(const QString &markdown) const;

private:
    MarkdownRenderer *m_markdown = nullptr;
};
