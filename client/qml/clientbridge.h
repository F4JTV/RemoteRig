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
    Q_PROPERTY(QStringList bands  READ bandNames     NOTIFY capsChanged)
    Q_PROPERTY(int sMeterDb       READ sMeterDb      NOTIFY stateChanged)
    Q_PROPERTY(QString sMeterText READ sMeterText    NOTIFY stateChanged)
    Q_PROPERTY(int rttMs          READ rttMs         NOTIFY statsChanged)
    Q_PROPERTY(int jitterMs       READ jitterMs      NOTIFY statsChanged)
    Q_PROPERTY(int lostFrames     READ lostFrames    NOTIFY statsChanged)
    Q_PROPERTY(double rxLevel     READ rxLevel       NOTIFY statsChanged)
    Q_PROPERTY(double txLevel     READ txLevel       NOTIFY statsChanged)
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
    int sMeterDb() const        { return m_state.strength; }
    QString sMeterText() const;
    int rttMs() const           { return m_rtt; }
    int jitterMs() const        { return m_jitter; }
    int lostFrames() const      { return m_lost; }
    double rxLevel() const      { return m_rxLevel; }
    double txLevel() const      { return m_txLevel; }
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
    Q_INVOKABLE void refreshDevices();
    QStringList bandNames() const;
    double bandFrequency(int index) const;
    QStringList modeNames() const;
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
    int  m_speechPreset = 1;
    int  m_theme = 0;
    bool m_pttOnVolumeKey = false;
    bool m_rigctldEnabled = false;
    RigctldServer *m_rigctld = nullptr;
};

} // namespace rr
