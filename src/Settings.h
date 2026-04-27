#pragma once

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
    Q_PROPERTY(bool    isConfigured READ isConfigured                       NOTIFY configurationChanged)

    // Custom system prompts. Empty string ⇒ service uses its built-in
    // default. Where templates support variables, the supported tokens
    // are listed in the docstring above each setter.
    Q_PROPERTY(QString summaryPrompt     READ summaryPrompt     WRITE setSummaryPrompt     NOTIFY summaryPromptChanged)
    Q_PROPERTY(QString translationPrompt READ translationPrompt WRITE setTranslationPrompt NOTIFY translationPromptChanged)
    Q_PROPERTY(QString tocPrompt         READ tocPrompt         WRITE setTocPrompt         NOTIFY tocPromptChanged)
    Q_PROPERTY(QString visionPrompt      READ visionPrompt      WRITE setVisionPrompt      NOTIFY visionPromptChanged)
    Q_PROPERTY(QString chatPrompt        READ chatPrompt        WRITE setChatPrompt        NOTIFY chatPromptChanged)
    Q_PROPERTY(bool    chatIncludePaperText READ chatIncludePaperText WRITE setChatIncludePaperText NOTIFY chatIncludePaperTextChanged)

    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(bool    fetchingModels READ fetchingModels       NOTIFY fetchingModelsChanged)
    Q_PROPERTY(QString modelsError    READ modelsError          NOTIFY modelsErrorChanged)

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
    bool    isConfigured()  const;

    QString summaryPrompt()     const { return m_summaryPrompt; }
    QString translationPrompt() const { return m_translationPrompt; }
    QString tocPrompt()         const { return m_tocPrompt; }
    QString visionPrompt()      const { return m_visionPrompt; }
    QString chatPrompt()        const { return m_chatPrompt; }
    bool    chatIncludePaperText() const { return m_chatIncludePaperText; }

    QStringList availableModels() const { return m_availableModels; }
    bool        fetchingModels()  const { return m_fetchingModels; }
    QString     modelsError()     const { return m_modelsError; }

    void setProvider(const QString &v);
    void setModel(const QString &v);
    void setBaseUrl(const QString &v);
    void setApiKey(const QString &v);
    void setTemperature(double v);
    void setMaxTokens(int v);
    void setContextWindow(int v);
    void setToolBudget(int v);
    void setTargetLang(const QString &v);

    // Supports variable {{lang}}. Empty ⇒ built-in default.
    void setSummaryPrompt(const QString &v);
    // Supports variable {{lang}}. Empty ⇒ built-in default.
    void setTranslationPrompt(const QString &v);
    // No variables. Empty ⇒ built-in default.
    void setTocPrompt(const QString &v);
    // No variables. Empty ⇒ built-in default.
    void setVisionPrompt(const QString &v);
    // No variables. Empty ⇒ built-in default.
    void setChatPrompt(const QString &v);
    void setChatIncludePaperText(bool v);

    LlmClient *createClient(QObject *parent = nullptr) const;

    // Probe the provider's /v1/models endpoint with the *given* values
    // (so the dialog can preview using unsaved input).
    Q_INVOKABLE void fetchModels(const QString &provider,
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
    void configurationChanged();

    void summaryPromptChanged();
    void translationPromptChanged();
    void tocPromptChanged();
    void visionPromptChanged();
    void chatPromptChanged();
    void chatIncludePaperTextChanged();

    void availableModelsChanged();
    void fetchingModelsChanged();
    void modelsErrorChanged();

private:
    void load();
    void save();
    void setFetchingModels(bool v);
    void setModelsError(const QString &err);
    void setAvailableModels(QStringList list);

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
    QString m_summaryPrompt;
    QString m_translationPrompt;
    QString m_tocPrompt;
    QString m_visionPrompt;
    QString m_chatPrompt;
    bool    m_chatIncludePaperText = false;

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_modelsReply;
    QStringList m_availableModels;
    bool m_fetchingModels = false;
    QString m_modelsError;
};
