// Translations for strings that belong to Qt, not to us.
//
// Qt 6.9 gave every TextField and TextArea a built-in right-click menu —
// Undo / Redo / Cut / Copy / Paste / Delete / Select All. Those strings live
// in QtQuick.Controls.impl's own QML (UndoAction.qml, CutAction.qml, …), so
// their translation context is the file's base name, and Qt's shipped
// qtdeclarative_zh_CN.qm has no entry for any of them: on a Chinese system
// the whole app was Chinese except this one menu, which stayed English.
//
// Nothing here is ever called. It exists so lupdate finds the contexts and
// keeps them in i18n/ai-reader_zh_CN.ts, and so QCoreApplication::translate()
// — which walks the installed translators newest-first, finds nothing for
// these contexts in qtbase, and falls through to ours — has somewhere to land.
// Drop this file the day Qt's own catalogs carry the strings.

#include <QtGlobal>

namespace {

[[maybe_unused]] const char *const kQtEditMenuStrings[] = {
    QT_TRANSLATE_NOOP("UndoAction",      "Undo"),
    QT_TRANSLATE_NOOP("RedoAction",      "Redo"),
    QT_TRANSLATE_NOOP("CutAction",       "Cut"),
    QT_TRANSLATE_NOOP("CopyAction",      "Copy"),
    QT_TRANSLATE_NOOP("PasteAction",     "Paste"),
    QT_TRANSLATE_NOOP("DeleteAction",    "Delete"),
    QT_TRANSLATE_NOOP("SelectAllAction", "Select All"),
};

}  // namespace
