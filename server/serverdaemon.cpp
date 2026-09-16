#include "serverdaemon.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QFile>
#include <QSettings>
#include <QTextStream>
#include <QTimer>

#include <atomic>

namespace rr {

static std::atomic<bool> g_stopRequested{false};

void ServerDaemon::requestStop() { g_stopRequested.store(true); }

ServerDaemon::ServerDaemon(QObject *parent) : QObject(parent)
{
    m_core = new ServerCore;
    m_rig  = new RigController;
    m_core->moveToThread(&m_netThread);
    m_rig->moveToThread(&m_rigThread);

    connect(m_core, &ServerCore::requestPtt,       m_rig,  &RigController::setPtt);
    connect(m_core, &ServerCore::requestFrequency, m_rig,  &RigController::setFrequency);
    connect(m_core, &ServerCore::requestMode,      m_rig,  &RigController::setMode);
    connect(m_core, &ServerCore::requestVfo,       m_rig,  &RigController::setVfo);
    connect(m_core, &ServerCore::requestTune,      m_rig,  &RigController::startTune);
    connect(m_core, &ServerCore::requestMorse,     m_rig,  &RigController::sendMorse);
    connect(m_core, &ServerCore::requestMorseStop, m_rig,  &RigController::stopMorse);
    connect(m_core, &ServerCore::requestKeySpeed,  m_rig,  &RigController::setKeySpeed);
    connect(m_rig,  &RigController::stateChanged,  m_core, &ServerCore::onRigState);
    connect(m_rig,  &RigController::capsChanged,   m_core, &ServerCore::onRigCaps);

    connect(m_core, &ServerCore::logMessage,    this, &ServerDaemon::onLog);
    connect(m_core, &ServerCore::started,       this, &ServerDaemon::onStarted);
    connect(m_core, &ServerCore::clientChanged, this, &ServerDaemon::onClientChanged);
    connect(m_rig,  &RigController::logMessage, this, &ServerDaemon::onLog);
    connect(m_rig,  &RigController::opened,     this, &ServerDaemon::onRigOpened);
    connect(m_rig,  &RigController::stateChanged, this, &ServerDaemon::onRigState);

    m_netThread.start(QThread::TimeCriticalPriority);
    m_rigThread.start();
}

ServerDaemon::~ServerDaemon()
{
    stop();
    m_netThread.quit(); m_netThread.wait(2000);
    m_rigThread.quit(); m_rigThread.wait(2000);
    delete m_core;
    delete m_rig;
}

// --------------------------------------------------------------- journal
void ServerDaemon::report(const QString &line) const
{
    QTextStream out(stdout);
    out << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
        << "  " << line << Qt::endl;
}

void ServerDaemon::onLog(const QString &message) { report(message); }

void ServerDaemon::onStarted(bool ok, const QString &message)
{
    report(message);
    if (!ok) {
        report(tr("Nothing to serve, stopping."));
        QCoreApplication::exit(1);
    }
}

void ServerDaemon::onRigOpened(bool ok, const QString &message)
{
    // RigController a deja journalise ce message par logMessage ; on ne le
    // repete pas. Un echec n'arrete pas le serveur : l'audio seul reste utile.
    Q_UNUSED(message)
    if (!ok) report(tr("Continuing without radio control."));
}

void ServerDaemon::onClientChanged(const QString &peer, bool connected,
                                   bool encrypted, const QString &codec)
{
    // La deconnexion est deja journalisee par ServerCore ; on n'ajoute que
    // l'arrivee, avec le detail du codec et du chiffrement.
    if (connected)
        report(tr("Client %1 connected (%2, %3)")
                   .arg(peer, codec, encrypted ? tr("encrypted") : tr("clear")));
}

void ServerDaemon::onRigState(const rr::RigState &state)
{
    // Sans --verbose, seules les bascules d'emission sont journalisees : le
    // reste defilerait cinq fois par seconde pour rien.
    if (state.ptt != m_lastPtt) {
        m_lastPtt = state.ptt;
        report(state.ptt ? tr("TX") : tr("RX"));
    }
    if (!m_verbose || !state.hasCat) return;

    const quint64 hz = (state.vfo == QLatin1String("B")) ? state.freqB : state.freqA;
    const QString line = QStringLiteral("%1 Hz  %2  VFO %3")
                             .arg(hz).arg(state.mode, state.vfo);
    if (line != m_lastRigLine) {
        m_lastRigLine = line;
        report(line);
    }
}

// Ce que l'operateur distant doit saisir : autant l'afficher au demarrage.
void ServerDaemon::reportAddresses(quint16 tcpPort) const
{
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            if (e.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            report(tr("Reachable at %1:%2 (%3)")
                       .arg(e.ip().toString()).arg(tcpPort, 0, 10)
                       .arg(iface.humanReadableName()));
        }
    }
}

// ----------------------------------------------------------- configuration
namespace {

// Le peripherique est memorise par son nom : un index changerait au gre des
// branchements. On le retrouve dans la liste, sinon on prend celui du systeme.
int deviceByName(const QString &wanted, bool input)
{
    if (wanted.isEmpty())
        return input ? AudioEngine::defaultInput() : AudioEngine::defaultOutput();

    const QList<AudioDevice> devices = input ? AudioEngine::inputDevices()
                                             : AudioEngine::outputDevices();
    for (const AudioDevice &d : devices)
        if (d.name == wanted) return d.index;
    for (const AudioDevice &d : devices)
        if (d.name.contains(wanted, Qt::CaseInsensitive)) return d.index;
    return -1;
}

QString portNameOf(const QString &label)
{
    return label.section(QStringLiteral("  ("), 0, 0).trimmed();
}

} // namespace

bool ServerDaemon::start(const QString &configPath, bool verbose)
{
    m_verbose = verbose;

    QSettings *settings = configPath.isEmpty()
        ? new QSettings(QStringLiteral("F4JTV"), QStringLiteral("RemoteRigServer"), this)
        : new QSettings(configPath, QSettings::IniFormat, this);

    if (!configPath.isEmpty() && !QFile::exists(configPath)) {
        report(tr("Configuration file not found: %1").arg(configPath));
        return false;
    }
    report(tr("Configuration: %1").arg(settings->fileName()));

    if (!AudioEngine::initialiseLibrary()) {
        report(tr("PortAudio failed to start"));
        return false;
    }

    RigConfig rc;
    // L'interface memorise le pilotage par son rang dans la liste ; on accepte
    // aussi un nom, plus lisible dans un fichier ecrit a la main.
    const QString backendName = settings->value(QStringLiteral("backendName")).toString().toLower();
    if (backendName == QLatin1String("hamlib"))      rc.backend = RigConfig::Hamlib;
    else if (backendName == QLatin1String("serial")) rc.backend = RigConfig::SerialPttOnly;
    else if (backendName == QLatin1String("none"))   rc.backend = RigConfig::None;
    else rc.backend = RigConfig::Backend(settings->value(QStringLiteral("backend"), 1).toInt());

    rc.hamlibModel = settings->value(QStringLiteral("model"), 0).toInt();
    rc.catPort     = portNameOf(settings->value(QStringLiteral("catPort")).toString());
    rc.catBaud     = settings->value(QStringLiteral("catBaud"), 38400).toInt();
    rc.pttPort     = portNameOf(settings->value(QStringLiteral("pttPort")).toString());
    rc.pttType     = settings->value(QStringLiteral("pttType"), "RTS").toString();
    rc.pollMs      = settings->value(QStringLiteral("pollMs"), 200).toInt();
    rc.dtrOnAlways = settings->value(QStringLiteral("dtrAlways"), false).toBool();

    ServerConfig sc;
    sc.tcpPort  = quint16(settings->value(QStringLiteral("tcpPort"), 7300).toInt());
    sc.udpPort  = quint16(settings->value(QStringLiteral("udpPort"), 7301).toInt());
    sc.password = settings->value(QStringLiteral("password")).toString();
    sc.requireEncryption = settings->value(QStringLiteral("forceEnc"), false).toBool();
    sc.rxGain   = float(settings->value(QStringLiteral("rxGain"), 1.0).toDouble());
    sc.txGain   = float(settings->value(QStringLiteral("txGain"), 1.0).toDouble());
    sc.pttTailMs = settings->value(QStringLiteral("tailMs"), 120).toInt();
    sc.enforceBandEdges = settings->value(QStringLiteral("bandEdges"), true).toBool();

    static const int kFrames[] = {120, 240, 480, 960};
    const int framesIdx = qBound(0, settings->value(QStringLiteral("framesIdx"), 2).toInt(), 3);
    sc.framesPerBuffer = kFrames[framesIdx];

    const QString inName  = settings->value(QStringLiteral("audioIn"),
                                settings->value(QStringLiteral("inDev"))).toString();
    const QString outName = settings->value(QStringLiteral("audioOut"),
                                settings->value(QStringLiteral("outDev"))).toString();
    sc.inputDevice  = deviceByName(inName, true);
    sc.outputDevice = deviceByName(outName, false);

    if (sc.password.isEmpty())
        report(tr("Warning: empty password, anyone reaching the port can key the rig."));
    if (sc.inputDevice < 0)
        report(tr("Audio input not found: %1").arg(inName));
    if (sc.outputDevice < 0)
        report(tr("Audio output not found: %1").arg(outName));

    report(tr("Audio in: %1").arg(inName.isEmpty() ? tr("system default") : inName));
    report(tr("Audio out: %1").arg(outName.isEmpty() ? tr("system default") : outName));

    QMetaObject::invokeMethod(m_rig,  "open",  Qt::QueuedConnection, Q_ARG(rr::RigConfig, rc));
    QMetaObject::invokeMethod(m_core, "start", Qt::QueuedConnection, Q_ARG(rr::ServerConfig, sc));

    reportAddresses(sc.tcpPort);

    auto *stopTimer = new QTimer(this);
    connect(stopTimer, &QTimer::timeout, this, &ServerDaemon::checkStopRequest);
    stopTimer->start(200);
    return true;
}

void ServerDaemon::checkStopRequest()
{
    if (!g_stopRequested.load()) return;
    report(tr("Stopping."));
    QCoreApplication::quit();
}

void ServerDaemon::stop()
{
    if (!m_core) return;
    QMetaObject::invokeMethod(m_core, "stop",  Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_rig,  "close", Qt::BlockingQueuedConnection);
}

} // namespace rr
