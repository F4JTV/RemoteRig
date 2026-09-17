#include "clientbridge.h"

#include <QDateTime>
#include <QLocale>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

#include "androidservice.h"

namespace rr {

namespace {
const int kSteps[] = {10, 100, 1000, 5000, 10000, 100000};
} // namespace

ClientBridge::ClientBridge(QObject *parent) : QObject(parent)
{
    qRegisterMetaType<rr::RigState>("rr::RigState");
    qRegisterMetaType<rr::ClientConfig>("rr::ClientConfig");
    qRegisterMetaType<rr::SpeechSettings>("rr::SpeechSettings");

    m_core = new ClientCore;
    m_core->moveToThread(&m_netThread);
    m_netThread.start(QThread::TimeCriticalPriority);

    connect(m_core, &ClientCore::connectionChanged, this,
            [this](bool up, const QString &msg) {
                m_connected = up;
                m_status = msg;
                // Le service de premier plan ne vit que le temps de la liaison.
                if (up) startAndroidService(); else stopAndroidService();
                if (!up) {
                    m_state = RigState();
                    m_caps = RigCaps();
                    m_bands = standardBandPlan();
                    emit capsChanged();
                    m_ptt = false;
                    emit pttChanged();
                    emit stateChanged();
                }
                appendLog(msg);
                emit connectionChanged();
                emit statusChanged();
            });

    connect(m_core, &ClientCore::stateChanged, this, [this](const rr::RigState &st) {
        m_state = st;
        if (m_rigctld) m_rigctld->updateState(st);
        emit stateChanged();
    });

    connect(m_core, &ClientCore::statsUpdated, this,
            [this](int rtt, int lost, int jitter, float rx, float tx, float reduction,
                   bool rxClip, bool txClip) {
                m_rtt = rtt; m_lost = lost; m_jitter = jitter;
                m_rxLevel = rx; m_txLevel = tx; m_reduction = reduction;
                m_rxClipped = rxClip; m_txClipped = txClip;
                emit statsChanged();
            });

    connect(m_core, &ClientCore::logMessage, this, &ClientBridge::appendLog);

    connect(m_core, &ClientCore::retryCountdown, this, [this](int seconds, int attempt) {
        m_retrySeconds = seconds;
        m_retryAttempt = attempt;
        emit retryChanged();
    });

    setVolumePttSink(this);
    connect(m_core, &ClientCore::capsChanged, this, [this](const rr::RigCaps &caps) {
        m_caps = caps;
        m_bands = bandsWithin(caps.txRanges);
        QStringList found;
        if (caps.hasTune)  found << tr("tuner");
        if (caps.hasMorse) found << tr("keyer");
        if (caps.hasSwr)   found << tr("SWR");
        appendLog(tr("%1 bands · %2").arg(m_bands.size())
                      .arg(found.isEmpty() ? tr("no extra capability")
                                           : found.join(QStringLiteral(", "))));
        emit capsChanged();
    });

    m_bands = standardBandPlan();
    loadSettings();
}

ClientBridge::~ClientBridge()
{
    setVolumePttSink(nullptr);
    saveSettings();
    QMetaObject::invokeMethod(m_core, "disconnectFromStation", Qt::BlockingQueuedConnection);
    m_netThread.quit();
    m_netThread.wait(2000);
    delete m_core;
}

// ------------------------------------------------------------------ affichage
QString ClientBridge::freqText() const
{
    if (!m_state.hasCat) return tr("no CAT");
    const quint64 hz = (m_state.vfo == QLatin1String("B")) ? m_state.freqB : m_state.freqA;
    if (hz == 0) return QStringLiteral("—");
    QString s = QString::number(hz);
    for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, '.');
    return s;
}

// Date et heure figees a la compilation : le seul moyen sur de savoir quelle
// version tourne reellement sur le telephone.
QString ClientBridge::buildStamp() const
{
    return QStringLiteral("%1 %2").arg(QLatin1String(__DATE__), QLatin1String(__TIME__));
}

int ClientBridge::statusBarHeight() const { return androidStatusBarHeight(); }

// Affiche en clair ce qui sert a calculer la marge du haut : sans mesure sur
// l'appareil, impossible de savoir si la valeur est la bonne.
QString ClientBridge::insetInfo() const
{
    const qreal dpr = QGuiApplication::primaryScreen()
                          ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1.0;
    const int px = androidStatusBarHeight();
    return QStringLiteral("status bar %1 px · ratio %2 · top %3")
        .arg(px).arg(dpr, 0, 'f', 2).arg(int(px / (dpr > 0 ? dpr : 1.0)));
}

// Appelee depuis le fil d'interface d'Android : on repasse par la boucle
// d'evenements de l'objet avant de toucher au PTT.
void ClientBridge::volumePttChanged(bool pressed)
{
    QMetaObject::invokeMethod(this, [this, pressed] { setPtt(pressed); },
                              Qt::QueuedConnection);
}

static QVariantList deviceList(const QList<AudioDevice> &devices)
{
    QVariantList out;
    for (const AudioDevice &d : devices)
        out << QVariantMap{{QStringLiteral("id"), d.index},
                           {QStringLiteral("name"), d.name}};
    return out;
}

QVariantList ClientBridge::inputDevices() const  { return deviceList(AudioEngine::inputDevices());  }
QVariantList ClientBridge::outputDevices() const { return deviceList(AudioEngine::outputDevices()); }

// Brancher un casque ou une cle USB change la liste : le QML redemande.
void ClientBridge::refreshDevices()
{
    AudioEngine::clearProbeCache();
    emit devicesChanged();
}

void ClientBridge::setInputDevice(int v)
{
    if (m_cfg.inputDevice == v) return;
    m_cfg.inputDevice = v;
    QMetaObject::invokeMethod(m_core, "setInputDevice", Qt::QueuedConnection, Q_ARG(int, v));
    emit settingsChanged();
}

void ClientBridge::setOutputDevice(int v)
{
    if (m_cfg.outputDevice == v) return;
    m_cfg.outputDevice = v;
    QMetaObject::invokeMethod(m_core, "setOutputDevice", Qt::QueuedConnection, Q_ARG(int, v));
    emit settingsChanged();
}

void ClientBridge::setPttOnVolumeKey(bool v)
{
    if (m_pttOnVolumeKey == v) return;
    m_pttOnVolumeKey = v;
    emit settingsChanged();
}

// Interface rigctld : un logiciel numerique local pilote la station distante.
void ClientBridge::setRigctldEnabled(bool v)
{
    if (m_rigctldEnabled == v) return;
    m_rigctldEnabled = v;
    if (v) {
        if (!m_rigctld) {
            m_rigctld = new RigctldServer(this);
            connect(m_rigctld, &RigctldServer::requestFrequency, m_core, &ClientCore::setFrequency);
            connect(m_rigctld, &RigctldServer::requestMode,      m_core, &ClientCore::setMode);
            connect(m_rigctld, &RigctldServer::requestVfo,       m_core, &ClientCore::setVfo);
            connect(m_rigctld, &RigctldServer::requestPtt,       this,   &ClientBridge::setPtt);
            connect(m_rigctld, &RigctldServer::logMessage,       this,   &ClientBridge::appendLog);
        }
        m_rigctld->updateState(m_state);
        m_rigctld->start(quint16(rigctldPort()), true);
    } else if (m_rigctld) {
        m_rigctld->stop();
    }
    emit settingsChanged();
}

// Deux decimales n'apprennent rien : un ROS se lit a la dizaine pres.
// Ce que l'emission occuperait reellement : l'operateur voit alors pourquoi la
// meme frequence passe en LSB et pas en USB.
QString ClientBridge::emissionText() const
{
    if (!m_state.hasCat || m_state.mode.isEmpty()) return QString();
    const quint64 hz = (m_state.vfo == QLatin1String("B")) ? m_state.freqB : m_state.freqA;
    const EmissionSpan s = occupiedSpan(hz, m_state.mode, m_state.passband);
    return tr("%1: %2 – %3 kHz")
        .arg(m_state.mode)
        .arg(double(s.low) / 1000.0, 0, 'f', 1)
        .arg(double(s.high) / 1000.0, 0, 'f', 1);
}

QString ClientBridge::swrText() const
{
    if (m_state.swr < 1.0f) return QStringLiteral("—");
    if (m_state.swr >= 9.9f) return QStringLiteral("> 9:1");
    return QStringLiteral("%1:1").arg(double(m_state.swr), 0, 'f', 1);
}

QString ClientBridge::sMeterText() const
{
    if (m_state.strength > 0) return QStringLiteral("S9+%1").arg(m_state.strength);
    return QStringLiteral("S%1").arg(qBound(0, (m_state.strength + 54) / 6, 9));
}

void ClientBridge::appendLog(const QString &line)
{
    const QString stamped = QDateTime::currentDateTime().toString("HH:mm:ss  ") + line;
    m_log = m_log.isEmpty() ? stamped : (stamped + '\n' + m_log);
    // On garde les trente dernieres lignes : au-dela, personne ne remonte.
    const QStringList lines = m_log.split('\n');
    if (lines.size() > 30) m_log = lines.mid(0, 30).join('\n');
    emit logChanged();
}

// ------------------------------------------------------------------ commandes
void ClientBridge::connectToStation()
{
    m_cfg.framesPerBuffer = 480;
    QMetaObject::invokeMethod(m_core, "connectToStation", Qt::QueuedConnection,
                              Q_ARG(rr::ClientConfig, m_cfg));
    saveSettings();
}

void ClientBridge::disconnectFromStation()
{
    QMetaObject::invokeMethod(m_core, "disconnectFromStation", Qt::QueuedConnection);
    m_connected = false;
    m_status = tr("Disconnected");
    appendLog(m_status);
    emit connectionChanged();
    emit statusChanged();
}

void ClientBridge::setPtt(bool on)
{
    if (!m_connected || m_ptt == on) return;
    m_ptt = on;
    QMetaObject::invokeMethod(m_core, "setPtt", Qt::QueuedConnection, Q_ARG(bool, on));
    emit pttChanged();
}

void ClientBridge::tuneBy(int deltaHz)
{
    if (!m_state.hasCat) return;
    const quint64 hz = (m_state.vfo == QLatin1String("B")) ? m_state.freqB : m_state.freqA;
    const qint64 target = qint64(hz) + deltaHz;
    if (target < 1000) return;
    gotoFrequency(double(target));
}

void ClientBridge::gotoFrequency(double hz)
{
    QMetaObject::invokeMethod(m_core, "setFrequency", Qt::QueuedConnection,
                              Q_ARG(quint64, quint64(hz)));
}

// Saisie manuelle : la meme analyse que sur le bureau, megahertz, kilohertz
// ou hertz selon l'ordre de grandeur.
bool ClientBridge::setFrequencyFromText(const QString &text)
{
    bool ok = false;
    const quint64 hz = parseFrequency(text, &ok);
    if (!ok) {
        appendLog(tr("Frequency not understood: %1").arg(text));
        return false;
    }
    gotoFrequency(double(hz));
    return true;
}

QString ClientBridge::frequencyForEditing() const
{
    const quint64 hz = (m_state.vfo == QLatin1String("B")) ? m_state.freqB : m_state.freqA;
    return QString::number(double(hz) / 1e6, 'f', 6);
}

void ClientBridge::setMode(const QString &mode)
{
    QMetaObject::invokeMethod(m_core, "setMode", Qt::QueuedConnection,
                              Q_ARG(QString, mode), Q_ARG(int, 0));
}

void ClientBridge::setVfo(const QString &vfo)
{
    QMetaObject::invokeMethod(m_core, "setVfo", Qt::QueuedConnection, Q_ARG(QString, vfo));
}

void ClientBridge::startTune()
{
    QMetaObject::invokeMethod(m_core, "startTune", Qt::QueuedConnection);
}

// ------------------------------------------------------------------ morse
void ClientBridge::setWpm(int v)
{
    if (m_wpm == v) return;
    m_wpm = v;
    if (m_connected)
        QMetaObject::invokeMethod(m_core, "setKeySpeed", Qt::QueuedConnection, Q_ARG(int, v));
    emit cwChanged();
}

void ClientBridge::setMyCall(const QString &call)
{
    const QString up = call.trimmed().toUpper();
    if (m_myCall == up) return;
    m_myCall = up;
    emit cwChanged();
}

void ClientBridge::setCwMacro(int index, const QString &text)
{
    if (index < 0 || index >= m_cwMacros.size()) return;
    if (m_cwMacros.at(index) == text) return;
    m_cwMacros[index] = text;
    emit cwChanged();
}

// %c est remplace par l'indicatif de l'operateur : les memoires restent ainsi
// valables quel que soit celui qui utilise l'application.
QString ClientBridge::expandMacro(const QString &text) const
{
    QString out = text;
    out.replace(QLatin1String("%c"), m_myCall.isEmpty() ? QStringLiteral("?") : m_myCall);
    return out.toUpper();
}

void ClientBridge::sendCw(const QString &text)
{
    const QString payload = expandMacro(text).trimmed();
    if (payload.isEmpty()) return;
    QMetaObject::invokeMethod(m_core, "sendMorse", Qt::QueuedConnection,
                              Q_ARG(QString, payload));
    appendLog(tr("CW: %1").arg(payload));
}

void ClientBridge::stopCw()
{
    QMetaObject::invokeMethod(m_core, "stopMorse", Qt::QueuedConnection);
}

// Les bandes viennent du poste : Hamlib rapporte ses plages d'emission, on y
// taille le plan de bandes standard. Hors connexion, le plan complet sert de
// repere.
QStringList ClientBridge::bandNames() const
{
    QStringList l;
    for (const rr::Band &b : m_bands) l << b.name;
    return l;
}

double ClientBridge::bandFrequency(int index) const
{
    if (index < 0 || index >= m_bands.size()) return 0;
    return double(m_bands.at(index).preset);
}

// Les modes viennent du poste. Hors connexion, une liste de repere, la meme
// que celle du client bureau.
QStringList ClientBridge::modeNames() const
{
    if (!m_caps.modes.isEmpty()) return m_caps.modes;
    return {"USB", "LSB", "CW", "CWR", "AM", "FM", "RTTY", "PKTUSB", "PKTLSB", "PKTFM"};
}

// Largeur abregee : « 2,4 k » plutot que « 2400 Hz ». La liste doit tenir a
// cote du mode et du VFO sur un ecran de telephone.
static QString shortWidth(int hz)
{
    if (hz >= 10000) return QStringLiteral("%1 k").arg(hz / 1000);
    return QStringLiteral("%1 k").arg(double(hz) / 1000.0, 0, 'f', 1);
}

// Trois largeurs normalisees, annoncees avec leur valeur : l'operateur sait
// ce qu'il choisit. Hamlib les calcule pour le mode courant.
QStringList ClientBridge::filterNames() const
{
    // Sur un telephone, la largeur seule : elle tient dans la liste, et c'est
    // ainsi que les postes etiquettent leurs filtres. Les trois valeurs sont
    // rangees de la plus large a la plus etroite, l'ordre porte le sens.
    QStringList l;
    const int w[3] = {m_state.pbWide, m_state.pbNormal, m_state.pbNarrow};
    for (int i = 0; i < 3; ++i)
        if (w[i] > 0) l << shortWidth(w[i]);
    return l;
}

int ClientBridge::filterIndex() const
{
    const int w[3] = {m_state.pbWide, m_state.pbNormal, m_state.pbNarrow};
    int rank = 0;
    for (int i = 0; i < 3; ++i) {
        if (w[i] <= 0) continue;
        if (w[i] == m_state.passband) return rank;
        ++rank;
    }
    return -1;
}

void ClientBridge::setFilter(int index)
{
    const int w[3] = {m_state.pbWide, m_state.pbNormal, m_state.pbNarrow};
    QList<int> avail;
    for (int i = 0; i < 3; ++i) if (w[i] > 0) avail << w[i];
    if (index < 0 || index >= avail.size()) return;
    // Le mode ne change pas : seule la largeur suit.
    QMetaObject::invokeMethod(m_core, "setMode", Qt::QueuedConnection,
                              Q_ARG(QString, m_state.mode), Q_ARG(int, avail.at(index)));
}

QStringList ClientBridge::stepLabels() const
{
    QStringList l;
    for (int s : kSteps)
        l << (s < 1000 ? QStringLiteral("%1 Hz").arg(s)
                       : QStringLiteral("%1 kHz").arg(s / 1000.0));
    return l;
}

int ClientBridge::stepValue(int index) const
{
    if (index < 0 || index >= int(sizeof(kSteps) / sizeof(kSteps[0]))) return 1000;
    return kSteps[index];
}

QString ClientBridge::audioSummary() const
{
    return tr("%1 Hz mono, route chosen by the system")
        .arg(int(AudioEngine::probeRate(AudioEngine::defaultOutput(), false)));
}

// ------------------------------------------------------------------ reglages
void ClientBridge::setHost(const QString &v)      { if (m_cfg.host==v) return; m_cfg.host=v; emit settingsChanged(); }
void ClientBridge::setPort(int v)                 { if (m_cfg.tcpPort==v) return; m_cfg.tcpPort=quint16(v); emit settingsChanged(); }
void ClientBridge::setUdpPort(int v)              { if (m_cfg.udpPort==v) return; m_cfg.udpPort=quint16(v); emit settingsChanged(); }
void ClientBridge::setPassword(const QString &v)  { if (m_cfg.password==v) return; m_cfg.password=v; emit settingsChanged(); }
void ClientBridge::setEncrypt(bool v)             { if (m_cfg.encrypt==v) return; m_cfg.encrypt=v; emit settingsChanged(); }

void ClientBridge::setAutoReconnect(bool v)
{
    if (m_cfg.autoReconnect == v) return;
    m_cfg.autoReconnect = v;
    QMetaObject::invokeMethod(m_core, "setAutoReconnect", Qt::QueuedConnection, Q_ARG(bool, v));
    emit settingsChanged();
}

QString ClientBridge::retryText() const
{
    if (m_retrySeconds <= 0) return QString();
    return tr("Reconnecting in %1 s — attempt %2").arg(m_retrySeconds).arg(m_retryAttempt);
}
// Le codec se change sans couper la liaison : le serveur bascule son encodeur
// et son decodeur sur reception de la commande.
void ClientBridge::setCodec(const QString &v)
{
    if (m_cfg.codec == v) return;
    m_cfg.codec = v;
    if (m_connected)
        QMetaObject::invokeMethod(m_core, "setCodec", Qt::QueuedConnection,
                                  Q_ARG(QString, v), Q_ARG(int, m_cfg.bitrate));
    emit settingsChanged();
}

void ClientBridge::setJitterTarget(int v)
{
    if (m_cfg.jitterMs == v) return;
    m_cfg.jitterMs = v;
    QMetaObject::invokeMethod(m_core, "setJitterMs", Qt::QueuedConnection, Q_ARG(int, v));
    emit settingsChanged();
}

void ClientBridge::setRxGain(double v)
{
    if (qFuzzyCompare(m_cfg.rxGain, float(v))) return;
    m_cfg.rxGain = float(v);
    QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                              Q_ARG(float, m_cfg.rxGain), Q_ARG(float, m_cfg.txGain));
    emit settingsChanged();
}

void ClientBridge::setTxGain(double v)
{
    if (qFuzzyCompare(m_cfg.txGain, float(v))) return;
    m_cfg.txGain = float(v);
    QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                              Q_ARG(float, m_cfg.rxGain), Q_ARG(float, m_cfg.txGain));
    emit settingsChanged();
}

void ClientBridge::setTheme(int v)
{
    if (m_theme == v) return;
    m_theme = v;
    QSettings(QStringLiteral("F4JTV"), QStringLiteral("RemoteRigClient"))
        .setValue(QStringLiteral("theme"), v);
    emit themeChanged();
}

void ClientBridge::setSpeechPreset(int v)
{
    if (m_speechPreset == v) return;
    m_speechPreset = v;
    pushSpeech();
    emit settingsChanged();
}

void ClientBridge::pushSpeech()
{
    SpeechSettings s;
    switch (m_speechPreset) {
    case 1:   // micro-casque a perche
        s.enabled = true; s.highPassHz = 300; s.presenceDb = 6;
        s.lowPassHz = 3200; s.compRatio = 3.0; break;
    case 2:   // micro du telephone, tenu a distance
        s.enabled = true; s.highPassHz = 200; s.presenceDb = 4;
        s.lowPassHz = 3200; s.compRatio = 2.0; break;
    default:  // aucun traitement
        s.enabled = false; s.highPassHz = 0; s.presenceDb = 0;
        s.lowPassHz = 0; s.compRatio = 1.0; break;
    }
    m_cfg.speech = s;
    QMetaObject::invokeMethod(m_core, "setSpeechSettings", Qt::QueuedConnection,
                              Q_ARG(rr::SpeechSettings, s));
}

void ClientBridge::loadSettings()
{
    QSettings s(QStringLiteral("F4JTV"), QStringLiteral("RemoteRigClient"));
    m_cfg.host     = s.value("host", "192.168.1.10").toString();
    m_cfg.tcpPort  = quint16(s.value("port", 7300).toInt());
    m_cfg.udpPort  = quint16(s.value("udpPort", 0).toInt());
    m_cfg.password = s.value("password").toString();
    m_cfg.encrypt  = s.value("encrypt", true).toBool();
    m_cfg.autoReconnect = s.value("autoReconnect", true).toBool();
    m_cfg.codec    = s.value("codec", "opus").toString();
    m_cfg.jitterMs = s.value("jitter", 60).toInt();
    m_cfg.rxGain   = float(s.value("rxGain", 1.0).toDouble());
    m_cfg.txGain   = float(s.value("txGain", 1.0).toDouble());
    m_speechPreset = s.value("speechPreset", 1).toInt();
    m_theme        = s.value("theme", 0).toInt();
    m_wpm          = s.value("wpm", 20).toInt();
    m_myCall       = s.value("myCall").toString();
    // Memoires par defaut : %c sera remplace par l'indicatif saisi.
    m_cwMacros     = s.value("cwMacros", QStringList{
                         QStringLiteral("CQ CQ DE %c %c K"),
                         QStringLiteral("%c"),
                         QStringLiteral("RST 599 599"),
                         QStringLiteral("TU 73 E E"),
                         QStringLiteral("AGN?"),
                         QStringLiteral("QRZ? DE %c")}).toStringList();
    m_cfg.inputDevice   = s.value("inputDevice",  AudioEngine::defaultInput()).toInt();
    m_cfg.outputDevice  = s.value("outputDevice", AudioEngine::defaultOutput()).toInt();
    m_pttOnVolumeKey    = s.value("pttOnVolumeKey", false).toBool();
    const bool wantRigctld = s.value("rigctld", false).toBool();
    pushSpeech();
    if (wantRigctld) setRigctldEnabled(true);
    emit settingsChanged();
}

void ClientBridge::saveSettings()
{
    QSettings s(QStringLiteral("F4JTV"), QStringLiteral("RemoteRigClient"));
    s.setValue("host", m_cfg.host);
    s.setValue("port", m_cfg.tcpPort);
    s.setValue("udpPort", m_cfg.udpPort);
    s.setValue("password", m_cfg.password);
    s.setValue("encrypt", m_cfg.encrypt);
    s.setValue("autoReconnect", m_cfg.autoReconnect);
    s.setValue("codec", m_cfg.codec);
    s.setValue("jitter", m_cfg.jitterMs);
    s.setValue("rxGain", m_cfg.rxGain);
    s.setValue("txGain", m_cfg.txGain);
    s.setValue("speechPreset", m_speechPreset);
    s.setValue("theme", m_theme);
    s.setValue("wpm", m_wpm);
    s.setValue("myCall", m_myCall);
    s.setValue("cwMacros", m_cwMacros);
    s.setValue("inputDevice", m_cfg.inputDevice);
    s.setValue("outputDevice", m_cfg.outputDevice);
    s.setValue("pttOnVolumeKey", m_pttOnVolumeKey);
    s.setValue("rigctld", m_rigctldEnabled);
}

} // namespace rr
