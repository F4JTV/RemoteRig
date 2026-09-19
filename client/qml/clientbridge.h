// Pont entre le coeur C++ du client et l'interface QML.
//
// clientcore.cpp est repris tel quel : ce pont ne fait qu'exposer son etat
// sous forme de proprietes et de methodes appelables depuis QML, et heberge le
// thread reseau comme le fait la fenetre Widgets sur le bureau.
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QThread>
#include "../clientcore.h"
#include "../rigctldserver.h"
#include "androidservice.h"

namespace rr {

class ClientBridge : public QObject, public VolumePttSink {
    Q_OBJECT

    Q_PROPERTY(bool connected     READ connected     NOTIFY connectionChanged)
    Q_PROPERTY(QString statusText READ statusText    NOTIFY statusChanged)
    Q_PROPERTY(bool hasCat        READ hasCat        NOTIFY stateChanged)
    Q_PROPERTY(QString freqText   READ freqText      NOTIFY stateChanged)
    Q_PROPERTY(QString modeText   READ modeText      NOTIFY stateChanged)
    Q_PROPERTY(QString vfoText    READ vfoText       NOTIFY stateChanged)
    Q_PROPERTY(QString rigName    READ rigName       NOTIFY stateChanged)
    Q_PROPERTY(bool ptt           READ ptt           NOTIFY pttChanged)
    Q_PROPERTY(bool tuning        READ tuning        NOTIFY stateChanged)
    Q_PROPERTY(bool hasTune       READ hasTune       NOTIFY capsChanged)
    Q_PROPERTY(bool hasMorse      READ hasMorse      NOTIFY capsChanged)
    Q_PROPERTY(bool hasSwr        READ hasSwr        NOTIFY capsChanged)
    Q_PROPERTY(bool hasVfoSet     READ hasVfoSet     NOTIFY capsChanged)
    Q_PROPERTY(double swr         READ swr           NOTIFY stateChanged)
    Q_PROPERTY(QString swrText    READ swrText       NOTIFY stateChanged)
    Q_PROPERTY(int  wpmMin        READ wpmMin        NOTIFY capsChanged)
    Q_PROPERTY(int  wpmMax        READ wpmMax        NOTIFY capsChanged)
    Q_PROPERTY(bool cwBusy        READ cwBusy        NOTIFY stateChanged)
    Q_PROPERTY(bool txAllowed     READ txAllowed     NOTIFY stateChanged)
    Q_PROPERTY(QString emissionText READ emissionText NOTIFY stateChanged)
    Q_PROPERTY(int  wpm           READ wpm    WRITE setWpm    NOTIFY cwChanged)
    Q_PROPERTY(QString myCall     READ myCall WRITE setMyCall NOTIFY cwChanged)
    Q_PROPERTY(QStringList cwMacros READ cwMacros    NOTIFY cwChanged)
    // Macros CAT : deux listes paralleles, le libelle et la sequence. Un
    // libelle parce qu'une sequence brute ne se lit pas dans un bouton.
    Q_PROPERTY(QStringList catLabels   READ catLabels   NOTIFY catMacrosChanged)
    Q_PROPERTY(QStringList catCommands READ catCommands NOTIFY catMacrosChanged)
    Q_PROPERTY(QStringList bands  READ bandNames     NOTIFY capsChanged)
    Q_PROPERTY(QStringList modes  READ modeNames     NOTIFY capsChanged)
    Q_PROPERTY(QStringList filters READ filterNames  NOTIFY stateChanged)
    Q_PROPERTY(int passband       READ passband      NOTIFY stateChanged)
    // Rang de la largeur courante dans la liste des filtres, -1 si elle n'y
    // figure pas. Sert a recaler la liste deroulante sur l'etat du poste.
    Q_PROPERTY(int filterIndex    READ filterIndex   NOTIFY stateChanged)
    Q_PROPERTY(int sMeterDb       READ sMeterDb      NOTIFY stateChanged)
    Q_PROPERTY(QString sMeterText READ sMeterText    NOTIFY stateChanged)
    Q_PROPERTY(int rttMs          READ rttMs         NOTIFY statsChanged)
    Q_PROPERTY(int jitterMs       READ jitterMs      NOTIFY statsChanged)
    Q_PROPERTY(int lostFrames     READ lostFrames    NOTIFY statsChanged)
    Q_PROPERTY(double rxLevel     READ rxLevel       NOTIFY statsChanged)
    Q_PROPERTY(double txLevel     READ txLevel       NOTIFY statsChanged)
    Q_PROPERTY(bool rxClipped     READ rxClipped     NOTIFY statsChanged)
    Q_PROPERTY(bool txClipped     READ txClipped     NOTIFY statsChanged)
    Q_PROPERTY(double gainReductionDb READ gainReductionDb NOTIFY statsChanged)
    Q_PROPERTY(QString logText    READ logText       NOTIFY logChanged)
    Q_PROPERTY(QString buildStamp READ buildStamp    CONSTANT)
    Q_PROPERTY(int statusBarHeight READ statusBarHeight CONSTANT)
    Q_PROPERTY(QVariantList inputDevices  READ inputDevices  NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList outputDevices READ outputDevices NOTIFY devicesChanged)
    Q_PROPERTY(int inputDevice  READ inputDevice  WRITE setInputDevice  NOTIFY settingsChanged)
    Q_PROPERTY(int outputDevice READ outputDevice WRITE setOutputDevice NOTIFY settingsChanged)
    Q_PROPERTY(bool pttOnVolumeKey READ pttOnVolumeKey WRITE setPttOnVolumeKey NOTIFY settingsChanged)
    Q_PROPERTY(bool rigctldEnabled READ rigctldEnabled WRITE setRigctldEnabled NOTIFY settingsChanged)
    Q_PROPERTY(int rigctldPort     READ rigctldPort     CONSTANT)
    Q_PROPERTY(QString insetInfo   READ insetInfo       CONSTANT)

    // Reglages, memorises entre deux lancements.
    Q_PROPERTY(QString host    READ host    WRITE setHost    NOTIFY settingsChanged)
    Q_PROPERTY(int port        READ port    WRITE setPort    NOTIFY settingsChanged)
    Q_PROPERTY(int udpPort     READ udpPort WRITE setUdpPort NOTIFY settingsChanged)
    Q_PROPERTY(QString password READ password WRITE setPassword NOTIFY settingsChanged)
    Q_PROPERTY(bool encrypt    READ encrypt WRITE setEncrypt NOTIFY settingsChanged)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect NOTIFY settingsChanged)
    Q_PROPERTY(bool retrying   READ retrying   NOTIFY retryChanged)
    Q_PROPERTY(QString retryText READ retryText NOTIFY retryChanged)
    Q_PROPERTY(QString codec   READ codec   WRITE setCodec   NOTIFY settingsChanged)
    Q_PROPERTY(int jitterTarget READ jitterTarget WRITE setJitterTarget NOTIFY settingsChanged)
    Q_PROPERTY(int speechPreset READ speechPreset WRITE setSpeechPreset NOTIFY settingsChanged)
    Q_PROPERTY(double rxGain   READ rxGain  WRITE setRxGain  NOTIFY settingsChanged)
    Q_PROPERTY(double txGain   READ txGain  WRITE setTxGain  NOTIFY settingsChanged)
    Q_PROPERTY(int theme       READ theme   WRITE setTheme   NOTIFY themeChanged)

public:
    explicit ClientBridge(QObject *parent = nullptr);
    ~ClientBridge() override;

    bool connected() const      { return m_connected; }
    QString statusText() const  { return m_status; }
    bool hasCat() const         { return m_state.hasCat; }
    QString freqText() const;
    QString modeText() const    { return m_state.mode; }
    QString vfoText() const     { return m_state.vfo; }
    QString rigName() const     { return m_state.rigName; }
    bool ptt() const            { return m_ptt; }
    bool tuning() const         { return m_state.tuning; }
    bool hasTune() const        { return m_caps.hasTune; }
    bool hasMorse() const       { return m_caps.hasMorse; }
    bool hasSwr() const         { return m_caps.hasSwr; }
    bool hasVfoSet() const      { return m_caps.hasVfoSet; }
    double swr() const          { return double(m_state.swr); }
    QString swrText() const;
    int  wpmMin() const         { return m_caps.wpmMin; }
    int  wpmMax() const         { return m_caps.wpmMax; }
    bool cwBusy() const         { return m_state.cw; }
    bool txAllowed() const      { return m_state.txAllowed; }
    QString emissionText() const;
    int  wpm() const            { return m_wpm; }
    QString myCall() const      { return m_myCall; }
    QStringList cwMacros() const { return m_cwMacros; }
    int sMeterDb() const        { return m_state.strength; }
    QString sMeterText() const;
    int rttMs() const           { return m_rtt; }
    int jitterMs() const        { return m_jitter; }
    int lostFrames() const      { return m_lost; }
    double rxLevel() const      { return m_rxLevel; }
    double txLevel() const      { return m_txLevel; }
    bool rxClipped() const      { return m_rxClipped; }
    bool txClipped() const      { return m_txClipped; }
    double gainReductionDb() const { return m_reduction; }
    QString logText() const     { return m_log; }
    QString buildStamp() const;
    int statusBarHeight() const;
    QVariantList inputDevices() const;
    QVariantList outputDevices() const;
    int  inputDevice() const  { return m_cfg.inputDevice; }
    int  outputDevice() const { return m_cfg.outputDevice; }
    bool pttOnVolumeKey() const { return m_pttOnVolumeKey; }
    bool rigctldEnabled() const { return m_rigctldEnabled; }
    int  rigctldPort() const    { return 4532; }
    QString insetInfo() const;

    // VolumePttSink
    bool volumePttWanted() const override { return m_pttOnVolumeKey; }
    void volumePttChanged(bool pressed) override;

    QString host() const        { return m_cfg.host; }
    int port() const            { return m_cfg.tcpPort; }
    int udpPort() const         { return m_cfg.udpPort; }
    QString password() const    { return m_cfg.password; }
    bool encrypt() const        { return m_cfg.encrypt; }
    bool autoReconnect() const  { return m_cfg.autoReconnect; }
    bool retrying() const       { return m_retrySeconds > 0; }
    QString retryText() const;
    QString codec() const       { return m_cfg.codec; }
    int jitterTarget() const    { return m_cfg.jitterMs; }
    int speechPreset() const    { return m_speechPreset; }
    double rxGain() const       { return m_cfg.rxGain; }
    double txGain() const       { return m_cfg.txGain; }
    int theme() const           { return m_theme; }

    void setHost(const QString &v);
    void setPort(int v);
    void setUdpPort(int v);
    void setPassword(const QString &v);
    void setEncrypt(bool v);
    void setAutoReconnect(bool v);
    void setCodec(const QString &v);
    void setJitterTarget(int v);
    void setSpeechPreset(int v);
    void setRxGain(double v);
    void setTxGain(double v);
    void setTheme(int v);
    void setInputDevice(int v);
    void setOutputDevice(int v);
    void setPttOnVolumeKey(bool v);
    void setRigctldEnabled(bool v);

public slots:
    void connectToStation();
    void disconnectFromStation();
    void setPtt(bool on);
    void tuneBy(int deltaHz);
    void gotoFrequency(double hz);
    bool setFrequencyFromText(const QString &text);
    QString frequencyForEditing() const;
    void setMode(const QString &mode);
    void setVfo(const QString &vfo);
    void startTune();
    void setWpm(int v);
    void setMyCall(const QString &call);
    Q_INVOKABLE void setCwMacro(int index, const QString &text);
    // Developpe %c en indicatif avant d'envoyer.
    Q_INVOKABLE QString expandMacro(const QString &text) const;
    Q_INVOKABLE void sendCw(const QString &text);
    Q_INVOKABLE void stopCw();
    Q_INVOKABLE void sendCat(const QString &command);

    QStringList catLabels() const   { return m_catLabels; }
    QStringList catCommands() const { return m_catCommands; }
    Q_INVOKABLE void addCatMacro();
    Q_INVOKABLE void setCatLabel(int index, const QString &text);
    Q_INVOKABLE void setCatCommand(int index, const QString &text);
    Q_INVOKABLE void removeCatMacro(int index);
    Q_INVOKABLE void sendCatMacro(int index);
    Q_INVOKABLE void refreshDevices();
    QStringList bandNames() const;
    double bandFrequency(int index) const;
    QStringList modeNames() const;
    QStringList filterNames() const;
    int filterIndex() const;
    int passband() const        { return m_state.passband; }
    Q_INVOKABLE void setFilter(int index);
    QStringList stepLabels() const;
    int stepValue(int index) const;
    QString audioSummary() const;
    void saveSettings();

signals:
    void connectionChanged();
    void statusChanged();
    void stateChanged();
    void pttChanged();
    void statsChanged();
    void logChanged();
    void settingsChanged();
    void themeChanged();
    void devicesChanged();
    void capsChanged();
    void cwChanged();
    void catMacrosChanged();
    void retryChanged();

private:
    void loadSettings();
    void appendLog(const QString &line);
    void pushSpeech();

    QThread     m_netThread;
    ClientCore *m_core = nullptr;
    ClientConfig m_cfg;
    RigState    m_state;
    RigCaps     m_caps;
    QList<rr::Band> m_bands;
    QString     m_status;
    QString     m_log;
    bool m_connected = false;
    bool m_ptt = false;
    int  m_rtt = 0, m_jitter = 0, m_lost = 0;
    double m_rxLevel = 0, m_txLevel = 0, m_reduction = 0;
    bool   m_rxClipped = false, m_txClipped = false;
    int  m_speechPreset = 1;
    int  m_theme = 0;
    int  m_wpm = 20;
    QString m_myCall;
    QStringList m_cwMacros;
    QStringList m_catLabels;
    QStringList m_catCommands;
    int  m_retrySeconds = 0;
    int  m_retryAttempt = 0;
    bool m_pttOnVolumeKey = false;
    bool m_rigctldEnabled = false;
    RigctldServer *m_rigctld = nullptr;
};

} // namespace rr
