#include "TocModel.h"

TocModel::TocModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int TocModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_sections.size();
}

QVariant TocModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_sections.size())
        return {};
    const Section &s = m_sections.at(index.row());
    switch (role) {
    case IdRole:           return s.id;
    case LevelRole:        return s.level;
    case TitleRole:        return s.title;
    case StartBlockRole:   return s.startBlockId;
    case StartPageRole:    return s.startPage;
    case IndentRole:       return qMax(0, s.level - 1) * 14;
    default:               return {};
    }
}

QHash<int, QByteArray> TocModel::roleNames() const
{
    return {
        {IdRole,         "sectionId"},
        {LevelRole,      "level"},
        {TitleRole,      "title"},
        {StartBlockRole, "startBlockId"},
        {StartPageRole,  "startPage"},
        {IndentRole,     "indent"},
    };
}

void TocModel::setSections(QVector<Section> sections)
{
    beginResetModel();
    m_sections = std::move(sections);
    endResetModel();
}

void TocModel::clear()
{
    if (m_sections.isEmpty())
        return;
    beginResetModel();
    m_sections.clear();
    endResetModel();
}
