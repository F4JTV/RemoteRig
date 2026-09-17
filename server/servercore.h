// Cœur reseau du serveur : controle TCP + audio UDP.
// Vit dans son propre thread ; le RigController vit dans un autre thread
// pour qu'un poste CAT lent ne retarde jamais l'audio.
#pragma once

#include <QObject>
#include <QHostAddress>
#include <QByteArray>
#include <QElapsedTimer>
#include "../common/protocol.h"
#include "../common/audiocodec.h"
#include "../common/audioengine.h"
#include "rigcontroller.h"

class QTcpServer;
class QTcpSocket;
class QUdpSocket;
class QTimer;

namespace rr {

struct ServerConfig {
    quint16 tcpPort      = 7300;
    quint16 udpPort      = 7301;
    QString password     = QStringLiteral("changeme");
    bool    requireEncryption = false;   // sinon le client decide
    int     inputDevice  = -1;           // audio venant du poste (RX)
    int     outputDevice = -1;           // audio vers le poste (TX)
    int     framesPerBuffer = 480;
    float   rxGain       = 1.0f;
    float   txGain       = 1.0f;
    int     pttTailMs    = 120;
    // Un transverter, ou un usage hors bande amateur, travaille en dehors des
    // plages que le poste declare : le garde-fou doit pouvoir etre leve.
    bool    enforceBandEdges = true;
};

class ServerCore : public QObject {
    Q_OBJECT
public:
    explicit ServerCore(QObject *parent = nullptr);
    ~ServerCore() override;

    AudioEngine *audio() { return &m_audio; }

public slots:
    void start(const rr::ServerConfig &cfg);
    void stop();
    void onRigState(const rr::RigState &st);

public:
    RigState currentState() const { return m_state; }

public slots:
    void onRigCaps(const rr::RigCaps &caps);

private slots:
    void checkClientAlive();

public slots:
    void setGains(float rx, float tx);

signals:
    void started(bool ok, const QString &message);
    void stopped();
    void logMessage(const QString &msg);
    void clientChanged(const QString &peer, bool connected, bool encrypted, const QString &codec);
    void statsUpdated(int rttMs, int lostPackets, float rxLevel, float txLevel);

    // Vers le RigController (connexions en file d'attente)
    void requestPtt(bool on);
    void requestFrequency(quint64 hz);
    void requestMode(const QString &mode, int passband);
    void requestVfo(const QString &vfo);
    void requestTune();
    void requestMorse(const QString &text);
    void requestMorseStop();
    void requestKeySpeed(int wpm);

private slots:
    void onNewConnection();
    void onTcpReadyRead();
    void onTcpDisconnected();
    void onUdpReadyRead();
    void onAudioTick();
    void onPttTail();

private:
    void sendJson(const QJsonObject &o);
    void handleControl(const QJsonObject &o);
    void dropClient(const QString &why);
    void setTx(bool on);

    ServerConfig m_cfg;
    QTcpServer  *m_tcp = nullptr;
    QTcpSocket  *m_sock = nullptr;
    QUdpSocket  *m_udp = nullptr;
    QTimer      *m_audioTimer = nullptr;
    QTimer      *m_tailTimer  = nullptr;
    QTimer      *m_statsTimer = nullptr;

    AudioEngine m_audio;
    AudioCodec  m_encoder;   // RX poste -> client
    AudioCodec  m_decoder;   // client -> TX poste

    QByteArray  m_rxBuffer;      // trame TCP en cours
    QByteArray  m_masterKey;
    QByteArray  m_udpKey;
    QByteArray  m_pttToken;
    QByteArray  m_salt, m_serverNonce;
    quint64     m_txCounter = 0, m_rxCounter = 0;
    bool        m_authenticated = false;
    bool        m_encrypted = false;
    quint32     m_session = 0;
    quint32     m_seqOut = 0;
    quint32     m_tsOut = 0;
    quint32     m_lastSeqIn = 0;
    int         m_lostIn = 0;

    QHostAddress m_clientAddr;
    quint16      m_clientUdpPort = 0;
    bool         m_tx = false;
    RigState     m_state;
    RigCaps      m_caps;
    bool         m_tuning = false;
    bool         m_cw = false;
    // Etat precedent du garde-fou. Il ne peut pas etre relu dans m_state :
    // celui-ci vient d'etre ecrase par l'etat recu du poste, qui ne porte
    // pas cette information.
    bool         m_txAllowed = true;

    // Surveillance du client. Un client qui disparait sans fermer sa connexion
    // — coupure de courant, telephone qui s'eteint — laisserait sinon le poste
    // en emission indefiniment, et garderait la station verrouillee.
    QElapsedTimer m_lastHeard;
    QTimer       *m_watchdog = nullptr;
    QElapsedTimer m_cwClock;
    QElapsedTimer m_tuneClock;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::ServerConfig)
