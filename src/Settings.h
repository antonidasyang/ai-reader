#pragma once

#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QSettings>
#include <QString>
#include <QStringList>

class LlmClient;
class QNetworkAccessManager;
class QNetworkReply;

class Settings : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString provider     READ provider     WRITE setProvider     NOTIFY providerChanged)
    Q_PROPERTY(QString model        READ model        WRITE setModel        NOTIFY modelChanged)
    Q_PROPERTY(QString baseUrl      READ baseUrl      WRITE setBaseUrl      NOTIFY baseUrlChanged)
    Q_PROPERTY(QString apiKey       READ apiKey       WRITE setApiKey       NOTIFY apiKeyChanged)
    Q_PROPERTY(double  temperature  READ temperature  WRITE setTemperature  NOTIFY temperatureChanged)
    Q_PROPERTY(int     maxTokens    READ maxTokens    WRITE setMaxTokens    NOTIFY maxTokensChanged)
    Q_PROPERTY(int     contextWindow READ contextWindow WRITE setContextWindow NOTIFY contextWindowChanged)
    Q_PROPERTY(int     toolBudget   READ toolBudget   WRITE setToolBudget   NOTIFY toolBudgetChanged)
    Q_PROPERTY(QString targetLang   READ targetLang   WRITE setTargetLang   NOTIFY targetLangChanged)
    // UI language: empty string ⇒ follow QLocale::system(); otherwise an
    // ISO code we know how to load a .qm for ("en", "zh_CN").
    Q_PROPERTY(QString uiLanguage   READ uiLanguage   WRITE setUiLanguage   NOTIFY uiLanguageChanged)
    Q_PROPERTY(bool    isConfigured READ isConfigured                       NOTIFY configurationChanged)

    // Custom system prompts. Empty string ⇒ service uses its built-in
    // default. Where templates support variables, the supported tokens
    // are listed in the docstring above each setter.
    // Auto-update + privacy. updateManifestUrl falls back to the
    // server's /update/manifest endpoint (kDefaultManifestUrl in
    // UpdateChecker.cpp) when blank so users never have to type a
    // URL to opt back into update checks.
    Q_PROPERTY(bool    autoCheckUpdates     READ autoCheckUpdates     WRITE setAutoCheckUpdates     NOTIFY autoCheckUpdatesChanged)
    Q_PROPERTY(QString updateManifestUrl    READ updateManifestUrl    WRITE setUpdateManifestUrl    NOTIFY updateManifestUrlChanged)
    Q_PROPERTY(bool    crashReportsOptIn    READ crashReportsOptIn    WRITE setCrashReportsOptIn    NOTIFY crashReportsOptInChanged)

    // GROBID paragraph segmentation. When enabled and the service at
    // grobidUrl answers, freshly-opened papers get their paragraphs
    // from GROBID's document model instead of the geometric clusterer
    // (StructureService). Falls back to the clusterer silently.
    Q_PROPERTY(bool    grobidEnabled READ grobidEnabled WRITE setGrobidEnabled NOTIFY grobidEnabledChanged)
    Q_PROPERTY(QString grobidUrl     READ grobidUrl     WRITE setGrobidUrl     NOTIFY grobidUrlChanged)

    // Whether opening a paper that has no saved paragraphs starts the
    // segmentation (clusterer, then the GROBID upgrade) by itself. Off
    // by default: on a large PDF that work costs seconds of CPU and a
    // round trip, which is wasted when the reader only wanted to look
    // at the pages. The toolbar's Segment button runs it on demand.
    Q_PROPERTY(bool    autoSegment   READ autoSegment   WRITE setAutoSegment   NOTIFY autoSegmentChanged)

    // Whether the paragraph segmentation and the translations of a paper go
    // into the current research project, so the same account on another
    // machine — and collaborators who have not done the work themselves —
    // get them instead of paying the CPU and the tokens again. Local work
    // always wins over anything pulled down; see PaperSyncService.
    Q_PROPERTY(bool    sharePaperData READ sharePaperData WRITE setSharePaperData NOTIFY sharePaperDataChanged)

    // How many paragraph translations may be in the air at once, across every
    // paper being translated. It is a rate limit, not a speed dial: raise it
    // and the provider is the next thing to push back.
    Q_PROPERTY(int     translationConcurrency READ translationConcurrency WRITE setTranslationConcurrency NOTIFY translationConcurrencyChanged)

    // Per-pane body font size (px). Each pane uses the value as the
    // baseline; headings/labels in that pane scale up relative to
    // it (typically +2 px) so the visual hierarchy stays intact.
    Q_PROPERTY(int     tocFontSize          READ tocFontSize          WRITE setTocFontSize          NOTIFY tocFontSizeChanged)
    Q_PROPERTY(int     summaryFontSize      READ summaryFontSize      WRITE setSummaryFontSize      NOTIFY summaryFontSizeChanged)
    Q_PROPERTY(int     paragraphFontSize    READ paragraphFontSize    WRITE setParagraphFontSize    NOTIFY paragraphFontSizeChanged)
    Q_PROPERTY(int     chatFontSize         READ chatFontSize         WRITE setChatFontSize         NOTIFY chatFontSizeChanged)

    Q_PROPERTY(QString translationPrompt READ translationPrompt WRITE setTranslationPrompt NOTIFY translationPromptChanged)
    Q_PROPERTY(QString tocPrompt         READ tocPrompt         WRITE setTocPrompt         NOTIFY tocPromptChanged)
    Q_PROPERTY(QString visionPrompt      READ visionPrompt      WRITE setVisionPrompt      NOTIFY visionPromptChanged)
    Q_PROPERTY(QString chatPrompt        READ chatPrompt        WRITE setChatPrompt        NOTIFY chatPromptChanged)
    Q_PROPERTY(bool    chatIncludePaperText READ chatIncludePaperText WRITE setChatIncludePaperText NOTIFY chatIncludePaperTextChanged)

    // Rendering for a remote desktop: "auto" (detect an RDP session),
    // "on", "off". Read straight out of QSettings before the application
    // object exists, since it decides environment variables Qt consumes
    // while starting up -- so a change only takes effect on the next run.
    Q_PROPERTY(QString remoteMode READ remoteMode WRITE setRemoteMode NOTIFY remoteModeChanged)
    // Whether this run actually took that path.
    Q_PROPERTY(bool remoteRenderingActive READ remoteRenderingActive CONSTANT)

    // How the chat input sends. "enter": Enter sends, Shift+Enter makes a
    // newline. "ctrl-enter": Enter makes a newline, Ctrl+Enter sends.
    Q_PROPERTY(QString chatSendKey     READ chatSendKey     WRITE setChatSendKey     NOTIFY chatSendKeyChanged)
    // Height of the chat input box in px. It used to be one line tall,
    // which is not enough room to see a question being written.
    Q_PROPERTY(int     chatInputHeight READ chatInputHeight WRITE setChatInputHeight NOTIFY chatInputHeightChanged)

    // Per-feature model. Translation is the one job with a different shape
    // from the rest: it runs on every paragraph of every paper, so it wants
    // something fast and cheap, while reading a paper closely, chatting about
    // it and comparing papers all want the best model available. So the main
    // configuration above drives interpretation, chat, summaries and vision,
    // and translation alone can be pointed somewhere else. Every field left
    // blank here falls back to the main configuration, which is what a user
    // who never opens this section gets.
    Q_PROPERTY(QString translationProvider READ translationProvider WRITE setTranslationProvider NOTIFY translationConfigChanged)
    Q_PROPERTY(QString translationModel    READ translationModel    WRITE setTranslationModel    NOTIFY translationConfigChanged)
    Q_PROPERTY(QString translationBaseUrl  READ translationBaseUrl  WRITE setTranslationBaseUrl  NOTIFY translationConfigChanged)
    Q_PROPERTY(QString translationApiKey   READ translationApiKey   WRITE setTranslationApiKey   NOTIFY translationConfigChanged)
    // What a translation would actually run on, after the fallbacks.
    Q_PROPERTY(QString translationModelInUse READ translationModelInUse NOTIFY translationConfigChanged)
    Q_PROPERTY(bool    translationOverridden READ translationOverridden NOTIFY translationConfigChanged)

    // Interpretation runs on the main model; these two are about the work,
    // not about which endpoint does it.
    Q_PROPERTY(int     analysisMaxTokens  READ analysisMaxTokens  WRITE setAnalysisMaxTokens  NOTIFY analysisConfigChanged)
    // How many interpretations may be in the air at once during a batch.
    Q_PROPERTY(int     analysisConcurrency READ analysisConcurrency WRITE setAnalysisConcurrency NOTIFY analysisConfigChanged)

    // Compile-time version baked in via CMake's target_compile_definitions
    // (AIREADER_VERSION="${PROJECT_VERSION}"). CONSTANT — never changes
    // at runtime, so QML bindings don't need a NOTIFY signal.
    Q_PROPERTY(QString appVersion    READ appVersion                          CONSTANT)

    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(bool    fetchingModels READ fetchingModels       NOTIFY fetchingModelsChanged)
    Q_PROPERTY(QString modelsError    READ modelsError          NOTIFY modelsErrorChanged)
    // The translation endpoint gets its own list: it may be a different
    // gateway entirely, and one shared list would have the two sections
    // quietly overwriting each other's models.
    Q_PROPERTY(QStringList availableTranslationModels READ availableTranslationModels NOTIFY availableTranslationModelsChanged)
    Q_PROPERTY(bool    fetchingTranslationModels READ fetchingTranslationModels NOTIFY fetchingTranslationModelsChanged)
    Q_PROPERTY(QString translationModelsError    READ translationModelsError    NOTIFY translationModelsErrorChanged)
    Q_PROPERTY(QString keychainStatus READ keychainStatus       NOTIFY keychainStatusChanged)

public:
    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    QString provider()      const { return m_provider; }
    QString model()         const { return m_model; }
    QString baseUrl()       const { return m_baseUrl; }
    QString apiKey()        const { return m_apiKey; }
    double  temperature()   const { return m_temperature; }
    int     maxTokens()     const { return m_maxTokens; }
    int     contextWindow() const { return m_contextWindow; }
    int     toolBudget()    const { return m_toolBudget; }
    QString targetLang()    const { return m_targetLang; }
    QString uiLanguage()    const { return m_uiLanguage; }
    bool    isConfigured()  const;
    QString appVersion()    const { return QStringLiteral(AIREADER_VERSION); }

    bool    autoCheckUpdates()  const { return m_autoCheckUpdates; }
    QString updateManifestUrl() const { return m_updateManifestUrl; }
    bool    crashReportsOptIn() const { return m_crashReportsOptIn; }

    bool    grobidEnabled() const { return m_grobidEnabled; }
    QString grobidUrl()     const { return m_grobidUrl; }
    bool    autoSegment()   const { return m_autoSegment; }
    bool    sharePaperData() const { return m_sharePaperData; }
    int     translationConcurrency() const { return m_translationConcurrency; }

    int     tocFontSize()       const { return m_tocFontSize; }
    int     summaryFontSize()   const { return m_summaryFontSize; }
    int     paragraphFontSize() const { return m_paragraphFontSize; }
    int     chatFontSize()      const { return m_chatFontSize; }

    QString translationPrompt() const { return m_translationPrompt; }
    QString tocPrompt()         const { return m_tocPrompt; }
    QString visionPrompt()      const { return m_visionPrompt; }
    QString chatPrompt()        const { return m_chatPrompt; }
    bool    chatIncludePaperText() const { return m_chatIncludePaperText; }

    QString remoteMode() const { return m_remoteMode; }
    bool remoteRenderingActive() const { return m_remoteRenderingActive; }
    // Told by main(), which is the only place that knows.
    void setRemoteRenderingActive(bool v) { m_remoteRenderingActive = v; }

    QString chatSendKey()     const { return m_chatSendKey; }
    int     chatInputHeight() const { return m_chatInputHeight; }
    QString translationProvider() const { return m_translationProvider; }
    QString translationModel()    const { return m_translationModel; }
    QString translationBaseUrl()  const { return m_translationBaseUrl; }
    QString translationApiKey()   const { return m_translationApiKey; }
    QString translationModelInUse() const;
    QString translationProviderInUse() const;
    QString translationBaseUrlInUse() const;
    QString translationApiKeyInUse() const;
    bool    translationOverridden() const;
    int     analysisMaxTokens()  const { return m_analysisMaxTokens; }
    int     analysisConcurrency() const { return m_analysisConcurrency; }

    QStringList availableModels() const { return m_availableModels; }
    bool        fetchingModels()  const { return m_fetchingModels; }
    QString     modelsError()     const { return m_modelsError; }
    QStringList availableTranslationModels() const { return m_availableTranslationModels; }
    bool        fetchingTranslationModels() const { return m_fetchingTranslationModels; }
    QString     translationModelsError() const { return m_translationModelsError; }
    QString     keychainStatus()  const { return m_keychainStatus; }

    void setProvider(const QString &v);
    void setModel(const QString &v);
    void setBaseUrl(const QString &v);
    void setApiKey(const QString &v);
    void setTemperature(double v);
    void setMaxTokens(int v);
    void setContextWindow(int v);
    void setToolBudget(int v);
    void setTargetLang(const QString &v);
    void setUiLanguage(const QString &v);

    void setAutoCheckUpdates(bool v);
    void setUpdateManifestUrl(const QString &v);
    void setCrashReportsOptIn(bool v);
    void setGrobidEnabled(bool v);
    void setGrobidUrl(const QString &v);
    void setAutoSegment(bool v);
    void setSharePaperData(bool v);
    void setTranslationConcurrency(int v);

    void setTocFontSize(int v);
    void setSummaryFontSize(int v);
    void setParagraphFontSize(int v);
    void setChatFontSize(int v);

    // Supports variable {{lang}}. Empty ⇒ built-in default.
    void setTranslationPrompt(const QString &v);
    // No variables. Empty ⇒ built-in default.
    void setTocPrompt(const QString &v);
    // No variables. Empty ⇒ built-in default.
    void setVisionPrompt(const QString &v);
    // No variables. Empty ⇒ built-in default.
    void setChatPrompt(const QString &v);
    void setChatIncludePaperText(bool v);

    void setRemoteMode(const QString &v);
    void setChatSendKey(const QString &v);
    void setChatInputHeight(int v);
    void setTranslationProvider(const QString &v);
    void setTranslationModel(const QString &v);
    void setTranslationBaseUrl(const QString &v);
    void setTranslationApiKey(const QString &v);
    void setAnalysisMaxTokens(int v);
    void setAnalysisConcurrency(int v);

    // Where a provider's API actually lives. The three named providers have
    // exactly one endpoint each and it is not the reader's business to type
    // it; only "openai-compatible" -- which is the whole point of that entry
    // -- has an address of its own. Keeping this in one place is what stops
    // a Base URL left over from an earlier provider from quietly sending
    // every request somewhere else.
    // Bumped whenever anything that decides which endpoint, protocol or
    // credential a request uses changes. LlmClientCache watches it, because
    // switching provider needs a different client object, not a different
    // field on the old one.
    int configRevision() const { return m_configRevision; }

    Q_INVOKABLE static QString officialBaseUrl(const QString &provider);
    Q_INVOKABLE static bool providerTakesCustomUrl(const QString &provider);
    // What a client for `provider` will really talk to, given a configured
    // custom URL that only counts when the provider allows one.
    Q_INVOKABLE static QString resolveBaseUrl(const QString &provider,
                                              const QString &customUrl);

    // ── Settings that follow the user's account ──────────────────────
    // The one authoritative list of QSettings keys that travel with the
    // signed-in user; UserPrefsSync pushes and pulls exactly these and
    // nothing else. What is missing from it is missing on purpose: API
    // keys and tokens live in the OS keychain and must never leave the
    // machine, and window geometry, the live pane visibility/width/order,
    // reading positions, open tabs, the last local folder, ui/remoteMode,
    // server/sessionActive, privacy/crashReportsOptIn, server/url,
    // grobid/url and updates/manifestUrl describe this screen, this disk
    // or this network -- not this user.
    //
    // The saved LAYOUTS are the exception that proves that rule and the one
    // key here Settings does not itself own: "layouts/presets" is a document
    // LayoutPresets writes, and Settings only carries the string in and out
    // of the payload. It travels because a named arrangement is a decision
    // the reader made -- and its widths are fractions of the window rather
    // than pixels precisely so it can be carried to a machine with a
    // different screen. Which of them is currently applied does not travel.
    static const QStringList &accountSettingKeys();

    // The current value of every key above. Types are the natural JSON
    // ones (string / number / bool) so two machines produce byte-identical
    // payloads for identical configuration, which is what lets the sync
    // layer skip a redundant write.
    QJsonObject exportAccountSettings() const;

    // What a fresh install would export. The sync layer needs it to tell
    // "the user configured this machine" from "nothing here was ever
    // touched", which is what decides who wins on a first pull.
    static QJsonObject defaultAccountSettings();

    // Apply an account payload that arrived from the server. Every value
    // goes through the ordinary setter, so it is clamped and validated
    // exactly like typed input and emits the ordinary change signals --
    // the running app picks the change up without a restart. A value of
    // the wrong type, or one outside what the setter would accept from a
    // person, is ignored rather than forced through; a key that is absent
    // leaves the local value alone, because absent means "the account has
    // no opinion", not "reset this to the default".
    void importAccountSettings(const QJsonObject &obj);

    LlmClient *createClient(QObject *parent = nullptr) const;
    // The client TranslationService talks to: the translation override where
    // one is set, the main configuration everywhere it is not. Everything
    // else in the app -- interpretation, chat, summaries, vision -- uses
    // createClient() above.
    LlmClient *createTranslationClient(QObject *parent = nullptr) const;

    // Probe the provider's /v1/models endpoint with the *given* values
    // (so the dialog can preview using unsaved input).
    Q_INVOKABLE void fetchModels(const QString &provider,
                                 const QString &baseUrl,
                                 const QString &apiKey);
    // Same probe against the translation section's endpoint. Blank fields
    // there mean "the main one", so the caller passes what it resolved.
    Q_INVOKABLE void fetchTranslationModels(const QString &provider,
                                            const QString &baseUrl,
                                            const QString &apiKey);

signals:
    void providerChanged();
    void modelChanged();
    void baseUrlChanged();
    void apiKeyChanged();
    void temperatureChanged();
    void maxTokensChanged();
    void contextWindowChanged();
    void toolBudgetChanged();
    void targetLangChanged();
    void uiLanguageChanged();
    void configurationChanged();

    void autoCheckUpdatesChanged();
    void updateManifestUrlChanged();
    void crashReportsOptInChanged();
    void grobidEnabledChanged();
    void grobidUrlChanged();
    void autoSegmentChanged();
    void sharePaperDataChanged();
    void translationConcurrencyChanged();

    void tocFontSizeChanged();
    void summaryFontSizeChanged();
    void paragraphFontSizeChanged();
    void chatFontSizeChanged();

    void translationPromptChanged();
    void tocPromptChanged();
    void visionPromptChanged();
    void chatPromptChanged();
    void chatIncludePaperTextChanged();
    void remoteModeChanged();
    void chatSendKeyChanged();
    void chatInputHeightChanged();
    void analysisConfigChanged();
    void translationConfigChanged();

    void availableModelsChanged();
    void fetchingModelsChanged();
    void modelsErrorChanged();
    void availableTranslationModelsChanged();
    void fetchingTranslationModelsChanged();
    void translationModelsErrorChanged();
    void keychainStatusChanged();

    // The saved-layouts document changed. Raised when an account payload
    // brings a different one in (LayoutPresets listens, so the menu follows
    // without a restart), and relayed from LayoutPresets by main() when the
    // reader saves one here (so the sync layer has something to push --
    // Settings does not hold the value, so no ordinary setter's signal
    // would ever fire for it).
    void layoutPresetsChanged();

private:
    void load();
    void save();
    void setFetchingModels(bool v);
    void setModelsError(const QString &err);
    void setAvailableModels(QStringList list);
    // Which of the two endpoints a /v1/models probe is for.
    enum class ModelSlot { Main, Translation };
    void fetchModelsInto(ModelSlot slot, const QString &provider,
                         const QString &baseUrl, const QString &apiKey);
    void setFetchingTranslationModels(bool v);
    void setTranslationModelsError(const QString &err);
    void setAvailableTranslationModels(QStringList list);
    void setKeychainStatus(const QString &s);
    void readApiKeyFromKeychain();
    void writeApiKeyToKeychain(const QString &value);
    // Same, for the translation override's own key (kept under its own
    // keychain entry so clearing one never disturbs the other).
    void readTranslationKeyFromKeychain();
    void writeTranslationKeyToKeychain(const QString &value);

    QSettings m_qs;
    QString m_provider;
    QString m_model;
    QString m_baseUrl;
    QString m_apiKey;
    double  m_temperature = 0.2;
    int     m_maxTokens = 8192;
    int     m_contextWindow = 128000;
    int     m_toolBudget = 30;
    QString m_targetLang;
    QString m_uiLanguage;

    bool    m_autoCheckUpdates = true;
    QString m_updateManifestUrl;
    bool    m_crashReportsOptIn = false;
    bool    m_grobidEnabled = true;
    QString m_grobidUrl;
    bool    m_autoSegment = false;
    bool    m_sharePaperData = true;
    int     m_translationConcurrency = 2;

    // Defaults match the previously hard-coded values in each pane.
    int     m_tocFontSize       = 12;
    int     m_summaryFontSize   = 13;
    int     m_paragraphFontSize = 12;
    int     m_chatFontSize      = 14;

    QString m_translationPrompt;
    QString m_tocPrompt;
    QString m_visionPrompt;
    QString m_chatPrompt;
    bool    m_chatIncludePaperText = false;

    QString m_remoteMode = QStringLiteral("auto");
    bool    m_remoteRenderingActive = false;
    QString m_chatSendKey = QStringLiteral("enter");
    int     m_chatInputHeight = 88;
    QString m_translationProvider;
    QString m_translationModel;
    QString m_translationBaseUrl;
    QString m_translationApiKey;
    int     m_analysisMaxTokens = 8192;
    int     m_analysisConcurrency = 2;

    int     m_configRevision = 0;

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_modelsReply;
    QPointer<QNetworkReply> m_translationModelsReply;
    QStringList m_availableModels;
    bool m_fetchingModels = false;
    QString m_modelsError;
    QStringList m_availableTranslationModels;
    bool m_fetchingTranslationModels = false;
    QString m_translationModelsError;
    QString m_keychainStatus;
};
