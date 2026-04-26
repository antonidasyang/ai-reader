#pragma once

#include "Section.h"

#include <QAbstractListModel>
#include <QVector>

class TocModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        LevelRole,
        TitleRole,
        StartBlockRole,
        StartPageRole,
        IndentRole,
    };

    explicit TocModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSections(QVector<Section> sections);
    void clear();

    int sectionCount() const { return m_sections.size(); }

private:
    QVector<Section> m_sections;
};
