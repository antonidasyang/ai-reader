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
    Q_PROPERTY(QString targetLang   READ targetLang   WRITE setTargetLang   NOTIFY targetLangChanged)
    Q_PROPERTY(bool    isConfigured READ isConfigured                       NOTIFY configurationChanged)

    Q_PROPERTY(QStringList availableModels READ availableModels NOTIFY availableModelsChanged)
    Q_PROPERTY(bool    fetchingModels READ fetchingModels       NOTIFY fetchingModelsChanged)
    Q_PROPERTY(QString modelsError    READ modelsError          NOTIFY modelsErrorChanged)

public:
    explicit Settings(QObject *parent = nullptr);
    ~Settings() override;

    QString provider()    const { return m_provider; }
    QString model()       const { return m_model; }
    QString baseUrl()     const { return m_baseUrl; }
    QString apiKey()      const { return m_apiKey; }
    double  temperature() const { return m_temperature; }
    QString targetLang()  const { return m_targetLang; }
    bool    isConfigured() const;

    QStringList availableModels() const { return m_availableModels; }
    bool        fetchingModels()  const { return m_fetchingModels; }
    QString     modelsError()     const { return m_modelsError; }

    void setProvider(const QString &v);
    void setModel(const QString &v);
    void setBaseUrl(const QString &v);
    void setApiKey(const QString &v);
    void setTemperature(double v);
    void setTargetLang(const QString &v);

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
    void targetLangChanged();
    void configurationChanged();

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
    QString m_targetLang;

    QNetworkAccessManager *m_nam = nullptr;
    QPointer<QNetworkReply> m_modelsReply;
    QStringList m_availableModels;
    bool m_fetchingModels = false;
    QString m_modelsError;
};
