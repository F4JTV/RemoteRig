#include "clientcore.h"
#include "../common/crypto.h"

#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QJsonObject>
#include <QDateTime>
#include <QHostInfo>
#include <QRegularExpression>
#include <QtEndian>

namespace rr {

quint64 parseFrequency(const QString &text, bool *ok)
{
    if (ok) *ok = false;

    QString t = text.simplified();
    t.remove(QRegularExpression(QStringLiteral("(?i)\\s*(hz|khz|mhz)\\s*$")));
    const bool wasKhz = text.contains(QStringLiteral("kHz"), Qt::CaseInsensitive);
    const bool wasHz  = !wasKhz && text.contains(QStringLiteral("Hz"), Qt::CaseInsensitive)
                        && !text.contains(QStringLiteral("MHz"), Qt::CaseInsensitive);

    // Espaces et apostrophes de groupement, virgule decimale a la francaise.
    t.remove(QLatin1Char(' ')).remove(QLatin1Char('\''));
    t.replace(QLatin1Char(','), QLatin1Char('.'));

    bool valid = false;
    const double value = t.toDouble(&valid);
    if (!valid || value <= 0) return 0;

    double hz;
    if (wasHz)        hz = value;
    else if (wasKhz)  hz = value * 1e3;
    // Sans unite, l'ordre de grandeur tranche : 14.074 est en megahertz,
    // 14074 en kilohertz, 14074000 en hertz. C'est ainsi qu'on ecrit une
    // frequence a la main.
    else if (value < 1000.0)     hz = value * 1e6;
    else if (value < 100000.0)   hz = value * 1e3;
    else                         hz = value;

    if (hz < 1000.0 || hz > 3e10) return 0;
    if (ok) *ok = true;
    return quint64(hz + 0.5);
}

ClientCore::ClientCore(QObject *parent) : QObject(parent) {}
ClientCore::~ClientCore() { disconnectFromStation(); }

void ClientCore::connectToStation(const ClientConfig &cfg)
{
    shutdownLink();
    cancelRetry();
    m_cfg = cfg;
    m_wantConnected = true;
    m_retryDelay = 1;
    m_retryAttempt = 0;
    openLink();
}

// Ouverture effective du lien. Separee de connectToStation pour qu'une
// tentative de reconnexion reprenne exactement la meme sequence.
void ClientCore::openLink()
{
    const ClientConfig &cfg = m_cfg;

    if (!AudioEngine::initialiseLibrary()) {
        emit connectionChanged(false, tr("PortAudio failed to start"));
        scheduleRetry();
        return;
    }

    m_audio.setCaptureGain(cfg.txGain);
    m_audio.setPlaybackGain(cfg.rxGain);
    if (!m_audio.startPlayback(cfg.outputDevice, cfg.framesPerBuffer)) {
        emit connectionChanged(false, tr("Audio output: %1").arg(m_audio.lastError()));
        scheduleRetry();
        return;
    }

    // Sans micro, la liaison reste utile en écoute : on se connecte quand même
    // et on interdit simplement l'émission. Un Raspberry Pi n'a par exemple
    // aucune entrée analogique, sa prise jack étant une sortie.
    m_rxOnly = (cfg.inputDevice < 0);
    if (!m_rxOnly && !m_audio.startCapture(cfg.inputDevice, cfg.framesPerBuffer)) {
        emit logMessage(tr("Microphone unavailable: %1").arg(m_audio.lastError()));
        m_rxOnly = true;
    }
    if (m_rxOnly)
        emit logMessage(tr("Receive only: no microphone, transmit is disabled"));
    m_audio.setCaptureMuted(true);   // le micro ne part qu'en émission

    m_encoder.setCodec(CODEC_PCM16);
    m_decoder.setCodec(CODEC_PCM16);
    m_speech.configure(AudioEngine::kAudioRate, cfg.speech);

    m_sock = new QTcpSocket(this);
    m_sock->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    connect(m_sock, &QTcpSocket::connected,    this, &ClientCore::onTcpConnected);
    connect(m_sock, &QTcpSocket::readyRead,    this, &ClientCore::onTcpReadyRead);
    connect(m_sock, &QTcpSocket::disconnected, this, &ClientCore::onTcpDisconnected);
    connect(m_sock, &QTcpSocket::errorOccurred, this, &ClientCore::onTcpError);

    m_rxBuffer.clear();
    m_txCounter = m_rxCounter = 0;
    m_seqOut = m_tsOut = 0;
    m_expectedSeq = 0;
    m_lost = 0;
    m_prefilled = false;

    emit logMessage(tr("Connecting to %1:%2…").arg(cfg.host).arg(cfg.tcpPort));
    m_sock->connectToHost(cfg.host, cfg.tcpPort);
}

void ClientCore::disconnectFromStation()
{
    m_wantConnected = false;
    cancelRetry();
    shutdownLink();
}

// Fermeture du lien sans toucher a l'intention : sert aussi bien a une coupure
// subie qu'a une deconnexion voulue.
void ClientCore::shutdownLink()
{
    if (m_ptt) setPtt(false);

    // QAbstractSocket::abort() émet disconnected() immédiatement, dans la
    // foulée de l'appel. Sans précaution, ce signal rappelle cette même
    // fonction, met m_sock à nullptr, et l'appel extérieur reprend ensuite sur
    // un pointeur mort. On coupe donc la boucle avant de toucher au socket :
    // m_authenticated à false neutralise le gestionnaire, et le pointeur est
    // détaché de l'objet avant abort().
    m_authenticated = false;

    for (QTimer **t : {&m_audioTimer, &m_keepTimer, &m_statsTimer}) {
        if (*t) { (*t)->stop(); delete *t; *t = nullptr; }
    }

    if (m_sock) {
        QTcpSocket *s = m_sock;
        m_sock = nullptr;
        s->disconnect(this);
        s->abort();
        s->deleteLater();
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
    m_rxOnly = false;
    m_ptt = false;
    m_serverUdpPort = 0;
}

// Recul exponentiel, plafonne : inutile de marteler un serveur eteint, mais une
// coupure passagere doit se rattraper en une seconde.
void ClientCore::scheduleRetry()
{
    if (!m_wantConnected || !m_cfg.autoReconnect) return;

    ++m_retryAttempt;
    m_retrySeconds = m_retryDelay;
    m_retryDelay = qMin(m_retryDelay * 2, 30);

    if (!m_retryTimer) {
        m_retryTimer = new QTimer(this);
        connect(m_retryTimer, &QTimer::timeout, this, &ClientCore::onRetryTick);
    }
    m_retryTimer->start(1000);
    emit retryCountdown(m_retrySeconds, m_retryAttempt);
    emit logMessage(tr("Link lost, retrying in %1 s (attempt %2)")
                        .arg(m_retrySeconds).arg(m_retryAttempt));
}

void ClientCore::cancelRetry()
{
    if (m_retryTimer) m_retryTimer->stop();
    m_retrySeconds = 0;
    emit retryCountdown(0, 0);
}

void ClientCore::onRetryTick()
{
    if (--m_retrySeconds > 0) {
        emit retryCountdown(m_retrySeconds, m_retryAttempt);
        return;
    }
    m_retryTimer->stop();
    emit retryCountdown(0, m_retryAttempt);
    if (m_wantConnected) openLink();
}


void ClientCore::teardown(const QString &why)
{
    shutdownLink();
    emit connectionChanged(false, why);
    scheduleRetry();
}

// ------------------------------------------------------------------ handshake
void ClientCore::onTcpConnected()
{
    emit logMessage(tr("Control link established, authenticating…"));
}

void ClientCore::onTcpError()
{
    if (!m_sock) return;
    teardown(m_sock->errorString());
}

void ClientCore::onTcpDisconnected()
{
    if (m_authenticated) teardown(tr("The station closed the link"));
}

void ClientCore::sendJson(const QJsonObject &o)
{
    if (!m_sock || m_sock->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
    m_sock->write(frameJson(o, key, m_txCounter++));
}

void ClientCore::onTcpReadyRead()
{
    m_rxBuffer.append(m_sock->readAll());
    while (m_rxBuffer.size() >= 4) {
        const quint32 len = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_rxBuffer.constData()));
        if (len > 65536) { teardown(tr("Invalid control frame")); return; }
        if (quint32(m_rxBuffer.size()) < 4 + len) return;
        const QByteArray body = m_rxBuffer.mid(4, int(len));
        m_rxBuffer.remove(0, int(4 + len));

        QJsonObject o;
        const QByteArray key = m_encrypted ? m_masterKey : QByteArray();
        if (!parseJsonFrame(body, key, m_rxCounter++, &o)) {
            teardown(tr("Unreadable control frame"));
            return;
        }
        handleControl(o);
    }
}

void ClientCore::handleControl(const QJsonObject &o)
{
    const QString t = o["t"].toString();

    if (t == "error") {
        teardown(o["msg"].toString(tr("Rejected by the station")));
        return;
    }

    if (t == "challenge") {
        const QByteArray salt        = QByteArray::fromHex(o["salt"].toString().toLatin1());
        const QByteArray serverNonce = QByteArray::fromHex(o["nonce"].toString().toLatin1());
        const QByteArray clientNonce = randomBytes(16);
        m_masterKey = deriveKey(m_cfg.password, salt);

        QString codec = m_cfg.codec;
        if (codec == "opus" && (!o["opus"].toBool() || !AudioCodec::opusAvailable()))
            codec = "pcm";

        sendJson(QJsonObject{
            {"t", "auth"},
            {"nonce",   QString::fromLatin1(clientNonce.toHex())},
            {"mac",     QString::fromLatin1(hmac(m_masterKey, serverNonce + clientNonce).toHex())},
            {"encrypt", m_cfg.encrypt},
            {"codec",   codec},
            {"bitrate", m_cfg.bitrate}});
        return;
    }

    if (t == "authOk") {
        m_session       = quint32(o["session"].toDouble());
        // Un port force sert quand une redirection NAT expose l'audio sur un
        // autre port que celui que le serveur croit utiliser.
        m_serverUdpPort = m_cfg.udpPort > 0 ? m_cfg.udpPort
                                            : quint16(o["udpPort"].toInt());
        m_pttToken      = QByteArray::fromHex(o["pttToken"].toString().toLatin1());
        m_udpKey        = subKey(m_masterKey, "udp");
        m_encrypted     = o["encrypt"].toBool();
        m_authenticated = true;

        const Codec c = (o["codec"].toString() == "opus") ? CODEC_OPUS : CODEC_PCM16;
        m_encoder.setCodec(c, m_cfg.bitrate);
        m_decoder.setCodec(c, m_cfg.bitrate);

        m_state = RigState::fromJson(o["state"].toObject());
        emit stateChanged(m_state);
        m_caps = RigCaps::fromJson(o["caps"].toObject());
        emit capsChanged(m_caps);

        m_serverAddr = m_sock->peerAddress();
        m_udp = new QUdpSocket(this);
        m_udp->bind(QHostAddress::AnyIPv4, 0);
        connect(m_udp, &QUdpSocket::readyRead, this, &ClientCore::onUdpReadyRead);

        m_audioTimer = new QTimer(this);
        m_audioTimer->setTimerType(Qt::PreciseTimer);
        connect(m_audioTimer, &QTimer::timeout, this, &ClientCore::onAudioTick);
        m_audioTimer->start(5);

        m_keepTimer = new QTimer(this);
        connect(m_keepTimer, &QTimer::timeout, this, &ClientCore::onKeepalive);
        m_keepTimer->start(1000);
        onKeepalive();   // ouvre tout de suite le chemin retour dans le NAT

        m_statsTimer = new QTimer(this);
        connect(m_statsTimer, &QTimer::timeout, this, &ClientCore::onStatsTick);
        m_statsTimer->start(200);

        m_retryDelay = 1;
        if (m_retryAttempt > 0) {
            emit logMessage(tr("Link restored after %1 attempt(s)").arg(m_retryAttempt));
            m_retryAttempt = 0;
        }
        cancelRetry();

        emit connectionChanged(true, tr("Station connected (%1, %2)")
                                         .arg(c == CODEC_OPUS ? "Opus" : "16-bit PCM",
                                              m_encrypted ? tr("encrypted") : tr("unencrypted")));
        return;
    }

    if (t == "state") {
        m_state = RigState::fromJson(o["s"].toObject());
        emit stateChanged(m_state);
        return;
    }

    if (t == "notice") {
        // Avis du serveur, destine a l'operateur : il s'affiche tel quel.
        emit logMessage(o["s"].toString());
        return;
    }

    if (t == "catReply") {
        const QString answer = o["s"].toString();
        emit catReply(answer);
        return;
    }

    if (t == "caps") {
        m_caps = RigCaps::fromJson(o["c"].toObject());
        emit capsChanged(m_caps);
        return;
    }

    if (t == "pong") {
        const qint64 sent = qint64(o["ts"].toDouble());
        m_rttMs = int(QDateTime::currentMSecsSinceEpoch() - sent);
        return;
    }
}

// ---------------------------------------------------------------- commandes
void ClientCore::setFrequency(quint64 hz)
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "cmd"}, {"c", "freq"}, {"v", double(hz)}});
}

void ClientCore::setMode(const QString &mode, int passband)
{
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "mode"}, {"v", mode}, {"pb", passband}});
}

void ClientCore::setVfo(const QString &vfo)
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "cmd"}, {"c", "vfo"}, {"v", vfo}});
}

void ClientCore::startTune()
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "cmd"}, {"c", "tune"}});
}

void ClientCore::sendMorse(const QString &text)
{
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "cw"}, {"text", text}});
}

void ClientCore::stopMorse()
{
    if (m_authenticated) sendJson(QJsonObject{{"t", "cmd"}, {"c", "cwstop"}});
}

void ClientCore::sendCatString(const QString &command)
{
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "cat"}, {"s", command}});
}

void ClientCore::setKeySpeed(int wpm)
{
    if (m_authenticated)
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "keyspd"}, {"wpm", wpm}});
}

void ClientCore::setCodec(const QString &codec, int bitrate)
{
    if (!m_authenticated) return;
    const Codec c = (codec == "opus" && AudioCodec::opusAvailable()) ? CODEC_OPUS : CODEC_PCM16;
    sendJson(QJsonObject{{"t", "cmd"}, {"c", "codec"}, {"v", codec}, {"bitrate", bitrate}});
    m_encoder.setCodec(c, bitrate);
    m_decoder.setCodec(c, bitrate);
    m_cfg.codec = codec;
    m_cfg.bitrate = bitrate;
    m_prefilled = false;
    m_audio.flushPlayback();
}

void ClientCore::setGains(float rx, float tx)
{
    m_cfg.rxGain = rx; m_cfg.txGain = tx;
    m_audio.setPlaybackGain(rx);
    m_audio.setCaptureGain(tx);
}

void ClientCore::setJitterMs(int ms) { m_cfg.jitterMs = ms; }

void ClientCore::setAutoReconnect(bool on)
{
    m_cfg.autoReconnect = on;
    if (!on) cancelRetry();
}

// Changement de micro en cours de liaison : on referme le flux de capture et
// on en ouvre un autre. Le reseau, le codec et le PTT ne sont pas touches, donc
// la liaison tient ; seul l'audio d'emission s'interrompt le temps du
// basculement, quelques dizaines de millisecondes.
void ClientCore::setInputDevice(int deviceIndex)
{
    m_cfg.inputDevice = deviceIndex;
    if (!m_authenticated) return;   // sera pris a la prochaine connexion

    m_audio.stopCapture();
    m_rxOnly = (deviceIndex < 0);
    if (m_rxOnly) {
        emit logMessage(tr("No microphone: receive only"));
        return;
    }

    if (!m_audio.startCapture(deviceIndex, m_cfg.framesPerBuffer)) {
        m_rxOnly = true;
        emit logMessage(tr("Audio input: %1").arg(m_audio.lastError()));
        return;
    }
    m_audio.setCaptureGain(m_cfg.txGain);
    // Le micro ne part qu'en emission : on retrouve l'etat courant du PTT.
    m_audio.setCaptureMuted(!m_ptt);
    m_speech.reset();
    emit logMessage(tr("Microphone switched"));
}

void ClientCore::setOutputDevice(int deviceIndex)
{
    m_cfg.outputDevice = deviceIndex;
    if (!m_authenticated) return;

    m_audio.stopPlayback();
    if (deviceIndex < 0) {
        emit logMessage(tr("No playback device: nothing could be heard."));
        return;
    }

    if (!m_audio.startPlayback(deviceIndex, m_cfg.framesPerBuffer)) {
        emit logMessage(tr("Audio output: %1").arg(m_audio.lastError()));
        return;
    }
    m_audio.setPlaybackGain(m_cfg.rxGain);
    m_audio.setPlaybackMuted(m_ptt);
    // Le tampon de gigue repart de zero : il doit se remplir avant de jouer.
    m_prefilled = false;
    m_expectedSeq = 0;
    emit logMessage(tr("Playback switched"));
}

void ClientCore::setSpeechSettings(const rr::SpeechSettings &s)
{
    m_cfg.speech = s;
    m_speech.configure(AudioEngine::kAudioRate, s);
}

void ClientCore::sendPttPacket(bool on)
{
    if (!m_udp || !m_authenticated) return;
    QByteArray payload = m_pttToken;
    payload.append(char(on ? 1 : 0));
    const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
    const QByteArray dg = buildPacket(PKT_PTT, m_encoder.codec(), FLAG_TX,
                                      m_session, m_seqOut++, m_tsOut, payload, key);
    // Trois copies : un datagramme perdu ne doit jamais laisser le poste en l'air.
    for (int i = 0; i < 3; ++i)
        m_udp->writeDatagram(dg, m_serverAddr, m_serverUdpPort);
}

void ClientCore::setPtt(bool on)
{
    if (m_rxOnly) return;
    if (!m_authenticated || m_ptt == on) return;
    m_ptt = on;

    if (on) {
        // Le PTT part avant l'audio, sur les deux canaux.
        sendPttPacket(true);
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "ptt"}, {"v", true}});
        m_speech.reset();   // etats des filtres remis a zero a chaque alternat
        m_audio.flushCapture();
        m_audio.setCaptureMuted(false);
        m_audio.setPlaybackMuted(true);
        m_audio.flushPlayback();
    } else {
        m_audio.setCaptureMuted(true);
        sendPttPacket(false);
        sendJson(QJsonObject{{"t", "cmd"}, {"c", "ptt"}, {"v", false}});
        m_audio.setPlaybackMuted(false);
        m_prefilled = false;
    }
}

// -------------------------------------------------------------------- audio
void ClientCore::onKeepalive()
{
    if (!m_udp || !m_authenticated) return;
    const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
    const QByteArray dg = buildPacket(PKT_KEEPALIVE, m_encoder.codec(), FLAG_TX,
                                      m_session, m_seqOut++, m_tsOut, {}, key);
    m_udp->writeDatagram(dg, m_serverAddr, m_serverUdpPort);

    m_pingSentAt = QDateTime::currentMSecsSinceEpoch();
    sendJson(QJsonObject{{"t", "ping"}, {"ts", double(m_pingSentAt)}});
}

void ClientCore::onUdpReadyRead()
{
    while (m_udp && m_udp->hasPendingDatagrams()) {
        QByteArray dg;
        dg.resize(int(m_udp->pendingDatagramSize()));
        m_udp->readDatagram(dg.data(), dg.size());

        PktHeader h{};
        QByteArray payload;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        if (!parsePacket(dg, key, &h, &payload)) continue;
        if (h.session != m_session || h.type != PKT_AUDIO) continue;
        if (m_ptt) continue;   // on n'écoute pas pendant l'émission

        int16_t pcm[kFrameSamples];

        if (m_expectedSeq == 0) m_expectedSeq = h.seq;

        if (h.seq > m_expectedSeq) {
            // Trous : on comble par masquage de perte plutôt que par du silence.
            const quint32 missing = h.seq - m_expectedSeq;
            if (missing < 20) {
                for (quint32 i = 0; i < missing; ++i) {
                    m_decoder.decode({}, pcm);
                    m_audio.pushPlayback(pcm, kFrameSamples);
                }
            }
            m_lost += int(missing);
        } else if (h.seq < m_expectedSeq) {
            continue;   // paquet en retard : trop tard pour être joué
        }
        m_expectedSeq = h.seq + 1;

        if (m_decoder.decode(payload, pcm))
            m_audio.pushPlayback(pcm, kFrameSamples);

        // Démarrage : on attend d'avoir la profondeur de tampon demandée.
        if (!m_prefilled) {
            const size_t target = size_t(m_cfg.jitterMs) * kSampleRate / 1000;
            if (m_audio.playbackQueued() >= target) {
                m_prefilled = true;
                m_audio.setPlaybackMuted(false);
            } else {
                m_audio.setPlaybackMuted(true);
            }
        }
    }
}

void ClientCore::onAudioTick()
{
    if (!m_authenticated || !m_ptt) return;

    int16_t pcm[kFrameSamples];
    while (m_audio.capturedAvailable() >= size_t(kFrameSamples)) {
        m_audio.readCaptured(pcm, kFrameSamples);
        // Mise en forme avant encodage : le codec profite d'un signal déjà
        // débarrassé des graves inutiles.
        m_speech.process(pcm, kFrameSamples);
        const QByteArray payload = m_encoder.encode(pcm);
        if (payload.isEmpty()) continue;
        const QByteArray key = m_encrypted ? m_udpKey : QByteArray();
        const QByteArray dg = buildPacket(PKT_AUDIO, m_encoder.codec(), FLAG_TX,
                                          m_session, m_seqOut++, m_tsOut, payload, key);
        m_tsOut += kFrameSamples;
        m_udp->writeDatagram(dg, m_serverAddr, m_serverUdpPort);
    }
}

void ClientCore::onStatsTick()
{
    const int queueMs = int(m_audio.playbackQueued() * 1000 / kSampleRate);
    // Les drapeaux d'ecretage sont lus a chaque tour : ils se remettent a zero
    // a la lecture, donc rien ne s'accumule.
    emit statsUpdated(m_rttMs, m_lost, queueMs,
                      m_audio.playbackLevel(), m_audio.captureLevel(),
                      m_ptt ? m_speech.gainReductionDb() : 0.0f,
                      m_audio.playbackClipped(), m_audio.captureClipped());
}

} // namespace rr
