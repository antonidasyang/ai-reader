#pragma once

#include <QString>

struct Section
{
    QString id;
    int     level = 1;     // 1, 2, 3 …
    QString title;
    int     startBlockId = -1;
    int     startPage = 0;
};
