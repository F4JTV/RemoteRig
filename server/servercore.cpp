#include "servercore.h"
#include "../common/crypto.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QDateTime>
#include <QRandomGenerator>
#include <QtEndian>
#include <vector>

namespace rr {

ServerCore::ServerCore(QObject *parent) : QObject(parent) {}
ServerCore::~ServerCore() { stop(); }

void ServerCore::start(const ServerConfig &cfg)
{
    stop();
    m_cfg = cfg;

    if (!AudioEngine::initialiseLibrary()) {
        emit started(false, tr("PortAudio failed to start"));
        return;
    }

    m_audio.setCaptureGain(cfg.rxGain);
    m_audio.setPlaybackGain(cfg.txGain);

    if (!m_audio.startCapture(cfg.inputDevice, cfg.framesPerBuffer)) {
        emit started(false, tr("Audio input: %1").arg(m_audio.lastError()));
        return;
    }
    if (!m_audio.startPlayback(cfg.outputDevice, cfg.framesPerBuffer)) {
        m_audio.stopCapture();
        emit started(false, tr("Audio output: %1").arg(m_audio.lastError()));
        return;
    }
    m_audio.setPlaybackMuted(true);   // rien vers le poste tant qu'on n'émet pas

    m_encoder.setCodec(CODEC_PCM16);
    m_decoder.setCodec(CODEC_PCM16);

    m_tcp = new QTcpServer(this);
    connect(m_tcp, &QTcpServer::newConnection, this, &ServerCore::onNewConnection);
    if (!m_tcp->listen(QHostAddress::Any, cfg.tcpPort)) {
        m_audio.stopAll();
        delete m_tcp; m_tcp = nullptr;
        emit started(false, tr("TCP port %1 unavailable").arg(cfg.tcpPort));
        return;
    }

    m_udp = new QUdpSocket(this);
    if (!m_udp->bind(QHostAddress::Any, cfg.udpPort)) {
        m_audio.stopAll();
        delete m_tcp; m_tcp = nullptr;
        delete m_udp; m_udp = nullptr;
        emit started(false, tr("UDP port %1 unavailable").arg(cfg.udpPort));
        return;
    }
    connect(m_udp, &QUdpSocket::readyRead, this, &ServerCore::onUdpReadyRead);

    m_audioTimer = new QTimer(this);
    m_audioTimer->setTimerType(Qt::PreciseTimer);
    connect(m_audioTimer, &QTimer::timeout, this, &ServerCore::onAudioTick);
    m_audioTimer->start(5);

    m_tailTimer = new QTimer(this);
    m_tailTimer->setSingleShot(true);
    connect(m_tailTimer, &QTimer::timeout, this, &ServerCore::onPttTail);

    m_statsTimer = new QTimer(this);
    connect(m_statsTimer, &QTimer::timeout, this, [this] {
        emit statsUpdated(0, m_lostIn, m_audio.captureLevel(), m_audio.playbackLevel());
    });
    m_statsTimer->start(150);

    emit started(true, tr("Listening on TCP %1 / UDP %2").arg(cfg.tcpPort).arg(cfg.udpPort));
}

void ServerCore::stop()
{
    if (m_audioTimer) { m_audioTimer->stop(); delete m_audioTimer; m_audioTimer = nullptr; }
    if (m_tailTimer)  { m_tailTimer->stop();  delete m_tailTimer;  m_tailTimer  = nullptr; }
    if (m_statsTimer) { m_statsTimer->stop(); delete m_statsTimer; m_statsTimer = nullptr; }

    if (m_tx) { m_tx = false; emit requestPtt(false); }

    // Même précaution que côté client : abort() émet disconnected() sur-le-champ,
    // ce qui rappellerait onTcpDisconnected() en pleine destruction.
    m_authenticated = false;

    if (m_sock) {
        QTcpSocket *s = m_sock;
        m_sock = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
    }
    if (m_tcp) {
        QTcpServer *t = m_tcp;
        m_tcp = nullptr;
        t->disconnect(this);
        t->close();
        t->deleteLater();
    }
    if (m_udp) {
        QUdpSocket *u = m_udp;
        m_udp = nullptr;
        u->disconnect(this);
        u->close();
        u->deleteLater();
    }

    m_audio.stopAll();
    m_encrypted = false;
    m_clientUdpPort = 0;
    emit stopped();
}

void ServerCore::setGains(float rx, float tx)
{
    m_cfg.rxGain = rx; m_cfg.txGain = tx;
    m_audio.setCaptureGain(rx);
    m_audio.setPlaybackGain(tx);
}

// ------------------------------------------------------------------- connexion
void ServerCore::onNewConnection()
{
    QTcpSocket *s = m_tcp->nextPendingConnection();
    if (!s) return;

    if (m_sock) {   // une seule station à la fois
        s->write(frameJson(QJsonObject{{"t", "error"},
                                       {"msg", tr("Station already in use")}}, {}, 0));
        s->flush();
        s->disconnectFromHost();
        s->deleteLater();
        return;
    }

    m_sock = s;
    m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_sock, &QTcpSocket::readyRead,    this, &ServerCore::onTcpReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, &ServerCore::onTcpDisconnected);

    m_rxBuffer.clear();
    m_authenticated = false;
    m_encrypted = false;
    m_txCounter = m_rxCounter = 0;
    m_seqOut = m_tsOut = 0;
    m_lostIn = 0;
    m_salt        = randomBytes(16);
    m_serverNonce = randomBytes(16);
    m_session     = quint32(QRandomGenerator::system()->generate());

    sendJson(QJsonObject{{"t", "challenge"},
                         {"proto", int(kVersion)},
                         {"salt",  QString::fromLatin1(m_salt.toHex())},
                         {"nonce", QString::fromLatin1(m_serverNonce.toHex())},
                         {"opus",  AudioCodec::opusAvailable()}});

    emit logMessage(tr("Incoming connection from %1").arg(m_sock->peerAddress().toString()));
}

void ServerCore::onTcpDisconnected()
{
    if (m_tx) setTx(false);
    emit clientChanged({}, false, false, {});
    emit logMessage(tr("Client disconnected"));
    if (m_sock) { m_sock->deleteLater(); m_sock = nullptr; }
    m_authenticated = false;
    m_clientUdpPort = 0;
}

void ServerCore::dropClient(const QString &why)
{
    emit logMessage(tr("Client rejected: %1").arg(why));
    if (m_sock) {
        sendJson(QJsonObject{{"t", "error"}, {"msg", why}});
        m_sock->flush();
        m_sock->disconnectFromHost();
    }
}

void ServerCore::sendJson(const QJsonObject &o)
{
    if (!m_sock) return;
    const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
    m_sock->write(frameJson(o, key, m_txCounter++));
}

void ServerCore::onTcpReadyRead()
{
    m_rxBuffer.append(m_sock->readAll());
    while (m_rxBuffer.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_rxBuffer.constData()));
        if (len > 65536) { dropClient(tr("Invalid control frame")); return; }
        if (quint32(m_rxBuffer.size()) < 4 + len) return;

        const QByteArray body = m_rxBuffer.mid(4, int(len));
        m_rxBuffer.remove(0, int(4 + len));

        QJsonObject o;
        const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
        if (!parseJsonFrame(body, key, m_rxCounter++, &o)) {
            dropClient(tr("Unreadable control frame"));
            return;
        }
        handleControl(o);
    }
}

void ServerCore::handleControl(const QJsonObject &o)
{
    const QString t = o["t"].toString();

    if (t == "auth") {
        const QByteArray clientNonce = QByteArray::fromHex(o["nonce"].toString().toLatin1());
        const QByteArray mac = QByteArray::fromHex(o["mac"].toString().toLatin1());
        const QByteArray key = deriveKey(m_cfg.password, m_salt);
        const QByteArray expect = hmac(key, m_serverNonce + clientNonce);

        if (mac.size() != expect.size() || mac != expect) {
            dropClient(tr("Wrong password"));
            return;
        }

        const bool wantEnc = o["encrypt"].toBool();
        if (m_cfg.requireEncryption && !wantEnc) {
            dropClient(tr("The server requires encryption"));
            return;
        }

        m_masterKey = key;
        m_udpKey    = subKey(key, "udp");
        m_pttToken  = subKey(key, "ptt").left(kPttTokenLen);
        m_authenticated = true;

        const QString codecName = o["codec"].toString(QStringLiteral("pcm"));
        Codec c = (codecName == "opus" && AudioCodec::opusAvailable()) ? CODEC_OPUS : CODEC_PCM16;
        const int bitrate = o["bitrate"].toInt(48000);
        m_encoder.setCodec(c, bitrate);
        m_decoder.setCodec(c, bitrate);

        // La réponse part encore en clair, puis on bascule.
        sendJson(QJsonObject{
            {"t", "authOk"},
            {"session",  double(m_session)},
            {"udpPort",  int(m_cfg.udpPort)},
            {"encrypt",  wantEnc},
            {"codec",    c == CODEC_OPUS ? "opus" : "pcm"},
            {"pttToken", QString::fromLatin1(m_pttToken.toHex())},
            {"state",    m_state.toJson()}});

        m_encrypted = wantEnc;
        emit clientChanged(m_sock->peerAddress().toString(), true, m_encrypted,
                           c == CODEC_OPUS ? "Opus" : "PCM");
        emit logMessage(tr("Client authenticated (%1, %2)")
                            .arg(c == CODEC_OPUS ? "Opus" : "16-bit PCM",
                                 m_encrypted ? tr("encrypted") : tr("unencrypted")));
        return;
    }

    if (!m_authenticated) { dropClient(tr("Authentication required")); return; }

    if (t == "cmd") {
        const QString c = o["c"].toString();
        if      (c == "freq") emit requestFrequency(quint64(o["v"].toDouble()));
        else if (c == "mode") emit requestMode(o["v"].toString(), o["pb"].toInt());
        else if (c == "vfo")  emit requestVfo(o["v"].toString());
        else if (c == "ptt")  setTx(o["v"].toBool());
        else if (c == "codec") {
            Codec cc = (o["v"].toString() == "opus" && AudioCodec::opusAvailable())
                           ? CODEC_OPUS : CODEC_PCM16;
            const int br = o["bitrate"].toInt(48000);
            m_encoder.setCodec(cc, br);
            m_decoder.setCodec(cc, br);
            emit logMessage(tr("Codec switched to %1").arg(cc == CODEC_OPUS ? "Opus" : "PCM"));
        }
        return;
    }

    if (t == "ping") {
        sendJson(QJsonObject{{"t", "pong"}, {"ts", o["ts"]}});
        return;
    }
}

void ServerCore::onRigState(const RigState &st)
{
    m_state = st;
    if (m_authenticated) sendJson(QJsonObject{{"t", "state"}, {"s", st.toJson()}});
}

// ------------------------------------------------------------------- bascule TX
void ServerCore::setTx(bool on)
{
    if (on) {
        if (m_tailTimer) m_tailTimer->stop();
        if (m_tx) return;
        m_tx = true;
        m_audio.setCaptureMuted(true);     // on coupe l'écoute RX
        m_audio.flushPlayback();
        m_audio.setPlaybackMuted(false);   // on ouvre la modulation vers le poste
        emit requestPtt(true);
    } else {
        if (!m_tx) return;
        // On laisse écouler l'audio encore en file avant de lâcher le PTT.
        if (m_tailTimer) m_tailTimer->start(m_cfg.pttTailMs);
        else onPttTail();
    }
}

void ServerCore::onPttTail()
{
    m_tx = false;
    emit requestPtt(false);
    m_audio.setPlaybackMuted(true);
    m_audio.flushPlayback();
    m_audio.flushCapture();
    m_audio.setCaptureMuted(false);
}

// ---------------------------------------------------------------------- audio
void ServerCore::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(m_udp->pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        m_udp->readDatagram(dg.data(), dg.size(), &from, &fromPort);

        if (!m_authenticated) continue;

        PktHeader h{};
        QByteArray payload;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        if (!parsePacket(dg, key, &h, &payload)) continue;
        if (h.session != m_session) continue;

        // On mémorise l'adresse réelle du client (traversée de NAT).
        if (m_clientUdpPort != fromPort || m_clientAddr != from) {
            m_clientAddr = from;
            m_clientUdpPort = fromPort;
        }

        if (h.type == PKT_PTT) {
            if (payload.size() < kPttTokenLen + 1) continue;
            if (payload.left(kPttTokenLen) != m_pttToken) continue;   // anti-usurpation
            setTx(payload.at(kPttTokenLen) != 0);
            continue;
        }

        if (h.type == PKT_AUDIO) {
            if (!m_tx) continue;   // audio TX ignoré hors émission
            if (m_lastSeqIn && h.seq > m_lastSeqIn + 1)
                m_lostIn += int(h.seq - m_lastSeqIn - 1);
            if (h.seq <= m_lastSeqIn && m_lastSeqIn - h.seq < 100) continue;  // retardataire
            m_lastSeqIn = h.seq;

            int16_t pcm[kFrameSamples];
            if (m_decoder.decode(payload, pcm))
                m_audio.pushPlayback(pcm, kFrameSamples);
        }
    }
}

void ServerCore::onAudioTick()
{
    if (!m_authenticated || m_clientUdpPort == 0) {
        m_audio.flushCapture();
        return;
    }
    if (m_tx) return;   // pas d'audio RX pendant l'émission

    int16_t pcm[kFrameSamples];
    while (m_audio.capturedAvailable() >= size_t(kFrameSamples)) {
        m_audio.readCaptured(pcm, kFrameSamples);
        const QByteArray payload = m_encoder.encode(pcm);
        if (payload.isEmpty()) continue;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        const QByteArray dg = buildPacket(PKT_AUDIO, m_encoder.codec(), 0,
                                          m_session, m_seqOut++, m_tsOut,
                                          payload, key);
        m_tsOut += kFrameSamples;
        m_udp->writeDatagram(dg, m_clientAddr, m_clientUdpPort);
    }
}

} // namespace rr
