// Cœur du client : contrôle TCP, audio UDP, tampon de gigue adaptatif.
#pragma once

#include <QObject>
#include <QHostAddress>
#include <QMap>
#include "../common/protocol.h"
#include "../common/audiocodec.h"
#include "../common/audioengine.h"
#include "../common/speechproc.h"

class QTcpSocket;
class QUdpSocket;
class QTimer;

namespace rr {

// Accepte « 14.074 », « 14074 », « 14 074 000 » ou « 14.074 MHz ».
// Sous 100 000 la valeur est lue en MHz, au-dela en Hz : personne ne saisit
// 14074000 a la main, et personne ne travaille sous 100 kHz.
quint64 parseFrequency(const QString &text, bool *ok = nullptr);

struct ClientConfig {
    QString host        = QStringLiteral("192.168.1.10");
    quint16 tcpPort     = 7300;
    quint16 udpPort     = 0;      // 0 : celui annonce par le serveur
    QString password;
    bool    encrypt     = false;
    QString codec       = QStringLiteral("opus");   // "opus" ou "pcm"
    int     bitrate     = 48000;
    int     inputDevice  = -1;    // micro, ou sortie du câble virtuel ; -1 = écoute seule
    int     outputDevice = -1;    // casque, ou entrée du câble virtuel
    int     monitorDevice = -1;   // seconde sortie facultative (écoute)
    int     framesPerBuffer = 480;
    int     jitterMs    = 40;
    float   rxGain      = 1.0f;
    float   txGain      = 1.0f;
    SpeechSettings speech;   // mise en forme de la modulation
};

class ClientCore : public QObject {
    Q_OBJECT
public:
    explicit ClientCore(QObject *parent = nullptr);
    ~ClientCore() override;

    bool connected() const { return m_authenticated; }
    RigState state() const { return m_state; }
    RigCaps  caps() const  { return m_caps; }

public slots:
    void connectToStation(const rr::ClientConfig &cfg);
    void disconnectFromStation();
    void setPtt(bool on);
    void setFrequency(quint64 hz);
    void setMode(const QString &mode, int passband);
    void setVfo(const QString &vfo);
    void startTune();
    void setCodec(const QString &codec, int bitrate);
    void setGains(float rx, float tx);
    void setJitterMs(int ms);
    void setSpeechSettings(const rr::SpeechSettings &s);
    void setInputDevice(int deviceIndex);
    void setOutputDevice(int deviceIndex);

signals:
    void connectionChanged(bool up, const QString &message);
    void receiveOnly(bool on);   // pas de micro : émission impossible
    void stateChanged(const rr::RigState &st);
    void capsChanged(const rr::RigCaps &caps);
    void logMessage(const QString &msg);
    void statsUpdated(int rttMs, int lostPackets, int jitterQueueMs,
                      float rxLevel, float txLevel, float gainReductionDb);

private slots:
    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpError();
    void onTcpDisconnected();
    void onUdpReadyRead();
    void onAudioTick();
    void onKeepalive();
    void onStatsTick();

private:
    void sendJson(const QJsonObject &o);
    void handleControl(const QJsonObject &o);
    void sendPttPacket(bool on);
    void teardown(const QString &why);

    ClientConfig m_cfg;
    QTcpSocket *m_sock = nullptr;
    QUdpSocket *m_udp  = nullptr;
    QTimer *m_audioTimer = nullptr;
    QTimer *m_keepTimer  = nullptr;
    QTimer *m_statsTimer = nullptr;

    AudioEngine m_audio;
    AudioCodec  m_encoder;   // micro -> station
    AudioCodec  m_decoder;   // station -> casque
    SpeechProcessor m_speech;

    QByteArray m_rxBuffer, m_masterKey, m_udpKey, m_pttToken;
    quint64 m_txCounter = 0, m_rxCounter = 0;
    bool    m_authenticated = false;
    bool    m_encrypted = false;
    bool    m_ptt = false;
    bool    m_rxOnly = false;
    quint32 m_session = 0;
    quint32 m_seqOut = 0, m_tsOut = 0;
    quint32 m_expectedSeq = 0;
    int     m_lost = 0;
    int     m_rttMs = 0;
    qint64  m_pingSentAt = 0;
    bool    m_prefilled = false;

    QHostAddress m_serverAddr;
    quint16 m_serverUdpPort = 0;
    RigState m_state;
    RigCaps  m_caps;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::ClientConfig)
