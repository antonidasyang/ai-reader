#include "pasteboardaccess.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointer>
#include <QWindow>
#include <QtCore/qcoreapplication_platform.h>
#include <QtCore/qharmonyappjscontext.h>
#include <QtCore/qharmonyjscontext.h>

#include <accesstoken/ability_access_control.h>
#include <napi/native_api.h>

namespace {

constexpr char kPermission[] = "ohos.permission.READ_PASTEBOARD";

PasteboardAccess *s_instance = nullptr;

// ── N-API helpers (JS thread only) ──────────────────────────────────

napi_value prop(napi_env env, napi_value obj, const char *name)
{
    napi_value v = nullptr;
    if (obj)
        napi_get_named_property(env, obj, name, &v);
    return v;
}

napi_value call(napi_env env, napi_value self, const char *method,
                std::initializer_list<napi_value> args)
{
    napi_value fn = prop(env, self, method);
    napi_value result = nullptr;
    if (fn)
        napi_call_function(env, self, fn, args.size(), args.begin(), &result);
    return result;
}

int32_t intAt(napi_env env, napi_value array, uint32_t i, int32_t fallback)
{
    napi_value v = nullptr;
    int32_t out = fallback;
    if (array && napi_get_element(env, array, i, &v) == napi_ok)
        napi_get_value_int32(env, v, &out);
    return out;
}

bool boolAt(napi_env env, napi_value array, uint32_t i, bool fallback)
{
    napi_value v = nullptr;
    bool out = fallback;
    if (array && napi_get_element(env, array, i, &v) == napi_ok)
        napi_get_value_bool(env, v, &out);
    return out;
}

// Calls promise.then(onValue, onError). Exactly one of them runs, once.
void whenSettled(napi_env env, napi_value promise,
                 std::function<void(napi_env, napi_value)> onValue,
                 std::function<void()> onError)
{
    struct Handlers {
        std::function<void(napi_env, napi_value)> value;
        std::function<void()> error;
    };
    auto *h = new Handlers{std::move(onValue), std::move(onError)};
    const auto make = [&](bool ok) {
        napi_value fn = nullptr;
        napi_create_function(env, ok ? "onValue" : "onError", NAPI_AUTO_LENGTH,
            ok ? +[](napi_env e, napi_callback_info info) -> napi_value {
                     size_t argc = 1; napi_value arg = nullptr; void *data = nullptr;
                     napi_get_cb_info(e, info, &argc, &arg, nullptr, &data);
                     auto *h = static_cast<Handlers *>(data);
                     h->value(e, arg);
                     delete h;
                     return nullptr;
                 }
               : +[](napi_env e, napi_callback_info info) -> napi_value {
                     void *data = nullptr;
                     napi_get_cb_info(e, info, nullptr, nullptr, nullptr, &data);
                     auto *h = static_cast<Handlers *>(data);
                     h->error();
                     delete h;
                     return nullptr;
                 },
            h, &fn);
        return fn;
    };
    if (!promise || !call(env, promise, "then", {make(true), make(false)})) {
        h->error();
        delete h;
    }
}

// Runs the system permission flow on the JS thread and reports the result
// to `report` (called on the JS thread). `viaSettings` uses the settings
// sheet, the only way back after the user declined the plain dialog.
void requestOnJsThread(bool viaSettings,
                       std::function<void(bool granted, bool dialogShown)> report)
{
    QNativeInterface::QHarmonyApplication::runOnJsThread(
        [viaSettings, report = std::move(report)](QHarmonyJsContext &js) {
            napi_env env = js.env();
            napi_value mod = js.tryImportModule("@ohos.abilityAccessCtrl");
            napi_value atManager = call(env, mod, "createAtManager", {});
            napi_value ability = QHarmonyAppJsContext::fromJsContext(js).defaultAbility();
            napi_value context = prop(env, ability, "context");
            napi_value perms = nullptr, name = nullptr;
            napi_create_array_with_length(env, 1, &perms);
            napi_create_string_utf8(env, kPermission, NAPI_AUTO_LENGTH, &name);
            napi_set_element(env, perms, 0, name);
            if (!atManager || !context) {
                qWarning("pasteboard: cannot reach the permission manager");
                report(false, false);
                return;
            }
            napi_value promise = call(env, atManager,
                viaSettings ? "requestPermissionOnSetting" : "requestPermissionsFromUser",
                {context, perms});
            whenSettled(env, promise,
                [viaSettings, report](napi_env e, napi_value result) {
                    if (viaSettings) {
                        // Array<GrantStatus>; PERMISSION_GRANTED is 0.
                        report(intAt(e, result, 0, -1) == 0, true);
                    } else {
                        // PermissionRequestResult: authResults 0 = granted;
                        // dialogShownResults false = no dialog (declined before).
                        report(intAt(e, prop(e, result, "authResults"), 0, -1) == 0,
                               boolAt(e, prop(e, result, "dialogShownResults"), 0, true));
                    }
                },
                [report]() { report(false, false); });
            bool pending = false;
            napi_is_exception_pending(env, &pending);
            if (pending) {
                napi_value ex = nullptr;
                napi_get_and_clear_last_exception(env, &ex);
                qWarning("pasteboard: permission request threw");
            }
        });
}

bool isTextEditor(const QObject *o)
{
    return o && (o->inherits("QQuickTextInput") || o->inherits("QQuickTextEdit"));
}

} // namespace

PasteboardAccess::PasteboardAccess(QObject *parent) : QObject(parent) {}

void PasteboardAccess::install()
{
    if (s_instance)
        return;
    s_instance = new PasteboardAccess(qApp);
    qApp->installEventFilter(s_instance);
}

PasteboardAccess *PasteboardAccess::instance()
{
    return s_instance;
}

bool PasteboardAccess::granted()
{
    return OH_AT_CheckSelfPermission(kPermission);
}

void PasteboardAccess::ensureReadable(bool explicitPaste, std::function<void(bool)> done)
{
    if (granted()) {
        if (done)
            done(true);
        return;
    }
    // After a refusal only a deliberate paste may ask again, and then via
    // the settings sheet; background readers just get nothing.
    if (m_userDialogTried && !explicitPaste) {
        if (done)
            done(false);
        return;
    }
    if (done)
        m_waiting.push_back(std::move(done));
    if (m_requesting)
        return;
    m_requesting = true;
    const bool viaSettings = m_userDialogTried;
    QPointer<PasteboardAccess> self(this);
    requestOnJsThread(viaSettings, [self, viaSettings](bool ok, bool dialogShown) {
        QMetaObject::invokeMethod(qApp, [self, viaSettings, ok, dialogShown]() {
            if (!self)
                return;
            qInfo("pasteboard: %s %s", viaSettings ? "settings sheet" : "permission dialog",
                  ok ? "granted" : dialogShown ? "declined" : "not shown (declined earlier)");
            if (!ok && !viaSettings) {
                self->m_userDialogTried = true;
                // No dialog at all means it was declined in an earlier
                // session; a deliberate paste goes straight to settings.
                if (!dialogShown && !self->m_waiting.empty()) {
                    self->m_requesting = false;
                    auto waiting = std::move(self->m_waiting);
                    self->m_waiting.clear();
                    for (auto &w : waiting)
                        self->ensureReadable(true, std::move(w));
                    return;
                }
            }
            self->finish(ok);
        }, Qt::QueuedConnection);
    });
}

void PasteboardAccess::finish(bool ok)
{
    m_requesting = false;
    auto waiting = std::move(m_waiting);
    m_waiting.clear();
    for (auto &w : waiting)
        w(ok);
}

bool PasteboardAccess::eventFilter(QObject *watched, QEvent *event)
{
    // Key and mouse events reach the window first; that's the one place to
    // look, and the one to replay a held-back paste to.
    if (!watched->isWindowType())
        return false;
    auto *window = static_cast<QWindow *>(watched);

    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if (!ke->matches(QKeySequence::Paste) || !isTextEditor(window->focusObject())
            || granted())
            return false;
        // Hold the paste back, ask, and replay it once allowed.
        QPointer<QWindow> target(window);
        const int key = ke->key();
        const auto mods = ke->modifiers();
        const QString text = ke->text();
        ensureReadable(true, [target, key, mods, text](bool ok) {
            if (!ok || !target)
                return;
            QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, mods, text));
            QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, mods, text));
        });
        return true;
    }

    if (event->type() == QEvent::MouseButtonRelease
        && static_cast<QMouseEvent *>(event)->button() == Qt::RightButton
        && !m_requesting && !granted()) {
        // The press has focused whatever was right-clicked; a text field's
        // context menu offers Paste, which we can't replay — ask now so
        // it works when chosen.
        if (isTextEditor(window->focusObject()))
            ensureReadable(false);
    }
    return false;
}
