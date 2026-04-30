#pragma once

#include <QRectF>
#include <QString>

struct Block
{
    enum Kind { Paragraph, Heading, Caption, ListItem, Equation };

    enum TranslationStatus {
        NotTranslated,
        Queued,
        Translating,
        Translated,
        Failed,
        Skipped,
    };

    int id = 0;
    int ord = 0;
    int page = 0;
    Kind kind = Paragraph;
    QString text;

    QString translation;
    TranslationStatus translationStatus = NotTranslated;
    QString translationError;

    QRectF bbox;

    // Per-paragraph view toggles. Default to both visible. When the
    // user collapses one side via the header chevron, the bool flips
    // and we persist via BlockCache so the choice survives reopens.
    bool sourceVisible = true;
    bool translationVisible = true;
};
