#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVector>

// Rows behind the floating selection-translation cards. The user can
// leave several pinned over the page at once — each right-click →
// Translate appends a row, and only the card's × removes one — so this
// is a list rather than the single slot the feature started out as.
//
// A row either mirrors a paragraph the app knows (blockRow >= 0, text
// pushed in by TranslationService as the block streams) or carries its
// own ad-hoc translation of exactly what the user selected.
class SnippetModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        SnippetIdRole = Qt::UserRole + 1,
        SourceRole,
        TextRole,
        StatusRole,      // "translating" | "done" | "failed"
        ErrorRole,
        ParagraphRole,   // header reads "paragraph" vs "selection"
        BlockRowRole,    // block row being mirrored, or -1
    };

    struct Snippet {
        int id = 0;
        QString source;
        QString text;
        QString status;
        QString error;
        bool paragraph = false;
        int blockRow = -1;
    };

    explicit SnippetModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Returns the new row's id.
    int add(Snippet s);
    void remove(int id);
    void clear();
    bool isEmpty() const { return m_items.isEmpty(); }

    const Snippet *byId(int id) const;
    bool hasBlockRow(int blockRow) const;
    QVector<int> idsForBlockRow(int blockRow) const;

    void setText(int id, const QString &text);
    void appendText(int id, const QString &chunk);
    void setStatus(int id, const QString &status, const QString &error = {});
    // Editing paragraphs renumbers rows, so mirroring has to stop — but
    // the text already on screen stays, and the card keeps calling
    // itself a paragraph.
    void detachBlockRows();

private:
    int indexOfId(int id) const;
    void changed(int index, const QVector<int> &roles);

    QVector<Snippet> m_items;
    int m_nextId = 1;
};
