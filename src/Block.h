#pragma once

#include <QRectF>
#include <QString>

struct Block
{
    enum Kind { Paragraph, Heading, Caption, ListItem, Equation };

    int id = 0;
    int ord = 0;
    int page = 0;
    Kind kind = Paragraph;
    QString text;
    QRectF bbox;
};
