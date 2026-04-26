#pragma once

#include <QObject>
#include <QSettings>
#include <QString>

class LlmClient;

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

public:
    explicit Settings(QObject *parent = nullptr);

    QString provider()    const { return m_provider; }
    QString model()       const { return m_model; }
    QString baseUrl()     const { return m_baseUrl; }
    QString apiKey()      const { return m_apiKey; }
    double  temperature() const { return m_temperature; }
    QString targetLang()  const { return m_targetLang; }
    bool    isConfigured() const;

    void setProvider(const QString &v);
    void setModel(const QString &v);
    void setBaseUrl(const QString &v);
    void setApiKey(const QString &v);
    void setTemperature(double v);
    void setTargetLang(const QString &v);

    // Build a fully-configured client for the active profile. Caller owns it.
    LlmClient *createClient(QObject *parent = nullptr) const;

signals:
    void providerChanged();
    void modelChanged();
    void baseUrlChanged();
    void apiKeyChanged();
    void temperatureChanged();
    void targetLangChanged();
    void configurationChanged();

private:
    void load();
    void save();

    QSettings m_qs;
    QString m_provider;
    QString m_model;
    QString m_baseUrl;
    QString m_apiKey;
    double  m_temperature = 0.2;
    QString m_targetLang;
};
