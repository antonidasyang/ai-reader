// HarmonyOS clipboard read permission.
//
// Reading the pasteboard on HarmonyOS needs ohos.permission.READ_PASTEBOARD,
// a user_grant permission that must be requested at runtime. Qt's platform
// plugin only *checks* it: without it every paste — Ctrl+V into a TextField,
// the context menu's Paste, QClipboard::text() — silently yields nothing,
// while copying keeps working (writing needs no permission).
//
// PasteboardAccess closes that gap without touching the QML:
//   * it filters the application's events; a paste shortcut pressed while the
//     permission is missing is held back, the system permission dialog is
//     shown, and once granted the same key press is replayed, so the paste the
//     user asked for still happens;
//   * a right-click into a text field asks too (the context menu's Paste
//     doesn't go through a key event we could replay);
//   * code that reads the clipboard on its own (UrlImportService) can call
//     ensureReadable() to ask, and gets an empty clipboard until granted.
//
// After the user has declined, the system won't show its dialog again; an
// explicit paste then opens the permission page in the system settings sheet
// instead. Nothing is asked unprompted at startup.
#pragma once

#include <QObject>
#include <QPointer>
#include <functional>
#include <vector>

class QWindow;

class PasteboardAccess : public QObject
{
    Q_OBJECT
public:
    // Installs the application-wide filter. Call once, after the
    // QGuiApplication exists.
    static void install();
    static PasteboardAccess *instance();

    static bool granted();

    // Ask for the permission if it's missing. `explicitPaste` marks a direct
    // user paste: only those escalate to the settings sheet after a refusal.
    // `done` (optional) runs on the Qt thread with the outcome.
    void ensureReadable(bool explicitPaste, std::function<void(bool)> done = {});

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit PasteboardAccess(QObject *parent);
    void finish(bool ok);

    bool m_requesting = false;
    bool m_userDialogTried = false;      // declined once this session
    std::vector<std::function<void(bool)>> m_waiting;
};
