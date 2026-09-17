#include "rigcontroller.h"

#include <QCoreApplication>
#include <QMutexLocker>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QTimer>
#include <algorithm>

#ifdef RR_HAVE_HAMLIB
#include <hamlib/rig.h>
#endif

namespace rr {

#ifdef RR_HAVE_HAMLIB
#define RIGP(x) static_cast<RIG *>(x)

// Les tables de modes changent peu ; on passe par les chaines Hamlib.
static rmode_t modeFromName(const QString &s)
{
    return rig_parse_mode(s.toLatin1().constData());
}
// Hamlib renvoie desormais toute sa pile de deboguage dans rigerror : illisible
// dans un journal. On traduit le code en une phrase, et on ne retombe sur le
// texte d'origine que pour les cas non prevus.
static QString hamlibError(int code)
{
    switch (-code) {
    case RIG_EIO:
        return QCoreApplication::translate("RigController",
            "I/O error — the serial port has disappeared; check the USB cable");
    case RIG_ETIMEOUT:
        return QCoreApplication::translate("RigController",
            "no answer from the rig — check the port, the speed and that it is on");
    case RIG_ENIMPL:
        return QCoreApplication::translate("RigController",
            "this rig's Hamlib backend does not implement that command");
    case RIG_ENAVAIL:
        return QCoreApplication::translate("RigController",
            "this rig does not offer that function");
    case RIG_ERJCTED:
        return QCoreApplication::translate("RigController", "the rig rejected the command");
    case RIG_EPROTO:
        return QCoreApplication::translate("RigController", "protocol error on the CAT link");
    case RIG_EINVAL:
        return QCoreApplication::translate("RigController", "invalid parameter");
    default:
        return QString::fromLatin1(rigerror(code)).section(QLatin1Char('\n'), 0, 0).trimmed();
    }
}

static QString modeName(rmode_t m)
{
    const char *n = rig_strrmode(m);
    return n ? QString::fromLatin1(n) : QStringLiteral("?");
}
#endif

RigController::RigController(QObject *parent) : QObject(parent)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &RigController::poll);
}

RigController::~RigController() { close(); }

bool RigController::hamlibAvailable()
{
#ifdef RR_HAVE_HAMLIB
    return true;
#else
    return false;
#endif
}

QStringList RigController::supportedModes()
{
    return {"USB", "LSB", "CW", "CWR", "AM", "FM", "RTTY", "RTTYR", "PKTUSB", "PKTLSB", "PKTFM"};
}

// Hamlib decide qu'un chemin est une adresse reseau des qu'il ne ressemble pas
// a un peripherique. Sous Linux, QSerialPortInfo::portName() rend « ttyUSB0 »,
// sans le repertoire : transmis tel quel, Hamlib tente une resolution DNS et
// echoue sur « Invalid configuration ». Sous Windows, « COM3 » convient deja,
// d'ou un defaut qui ne s'y voit pas.
QString RigController::serialDevicePath(const QString &portName)
{
    if (portName.isEmpty()) return portName;

    // Deja un chemin absolu, ou un port Windows : on n'y touche pas.
    if (portName.startsWith(QLatin1Char('/'))
        || portName.startsWith(QLatin1String("COM"), Qt::CaseInsensitive)
        || portName.startsWith(QLatin1String("\\\\")))
        return portName;

    // On demande son chemin systeme a Qt plutot que de le fabriquer : les
    // liens de /dev/serial/by-id, par exemple, ne suivent pas la regle.
    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &p : ports)
        if (p.portName() == portName) return p.systemLocation();

    return QStringLiteral("/dev/") + portName;
}

QStringList RigController::serialPorts()
{
    QStringList l;
    const auto ports = QSerialPortInfo::availablePorts();
    for (const auto &p : ports) {
        QString label = p.portName();
        if (!p.description().isEmpty()) label += "  (" + p.description() + ")";
        l << label;
    }
    return l;
}

#ifdef RR_HAVE_HAMLIB
static int modelCallback(const struct rig_caps *caps, rig_ptr_t data)
{
    auto *out = static_cast<QList<QPair<int, QString>> *>(data);
    out->append({int(caps->rig_model),
                 QString("%1 %2").arg(QString::fromLatin1(caps->mfg_name),
                                      QString::fromLatin1(caps->model_name))});
    return 1;
}
#endif

// Hamlib bavarde beaucoup, meme au niveau « erreurs » : il repete par exemple
// « no get_vfo » a chaque scrutation sur un poste qui n'a pas cette commande.
// Nos propres messages suffisent, et un journal lisible vaut mieux qu'un
// journal complet. RR_HAMLIB_DEBUG=1 rouvre le robinet pour un diagnostic.
static void applyHamlibDebugLevel()
{
#ifdef RR_HAVE_HAMLIB
    const QByteArray want = qgetenv("RR_HAMLIB_DEBUG");
    rig_set_debug(want.isEmpty() || want == "0" ? RIG_DEBUG_NONE : RIG_DEBUG_VERBOSE);
#endif
}

QList<QPair<int, QString>> RigController::hamlibModels()
{
    QList<QPair<int, QString>> out;
#ifdef RR_HAVE_HAMLIB
    // Le chargement des backends écrit une ligne par pilote sur la console.
    rig_set_debug(RIG_DEBUG_NONE);
    rig_load_all_backends();
    applyHamlibDebugLevel();
    rig_list_foreach(modelCallback, &out);
    std::sort(out.begin(), out.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.second.localeAwareCompare(b.second) < 0;
              });
#endif
    return out;
}

RigState RigController::state() const
{
    QMutexLocker lock(&m_mutex);
    return m_state;
}

RigCaps RigController::caps() const
{
    QMutexLocker lock(&m_mutex);
    return m_caps;
}

// Le poste manipule lui-meme : le texte part par le CAT et c'est son manipulateur
// electronique qui genere les elements. Passer du CW par le PTT reseau serait
// inutilisable, la gigue detruirait l'espacement.
// Envoi direct, sans file ni decoupage.
//
// J'avais ajoute une file cadencee et une attente du PTT pour contourner la
// memoire de manipulateur saturee du poste. Cela a coute le pilotage CAT :
// chaque attente occupait le fil qui scrute le poste, et les commandes
// s'empilaient sur un bus deja en difficulte. Le probleme des longs messages
// reste donc ouvert, mais il ne bloque plus le reste.
void RigController::sendMorse(const QString &text)
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig || text.isEmpty()) return;
    const QByteArray latin = text.toLatin1();
    const int r = rig_send_morse(RIGP(m_rig), RIG_VFO_CURR, latin.constData());
    if (r != RIG_OK) {
        emit logMessage(tr("Morse refused: %1").arg(hamlibError(r)));
    } else {
        emit logMessage(tr("Sending: %1").arg(text));
    }
#else
    Q_UNUSED(text)
#endif
}

void RigController::stopMorse()
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    rig_stop_morse(RIGP(m_rig), RIG_VFO_CURR);
    emit logMessage(tr("Morse stopped"));
#endif
}

void RigController::setKeySpeed(int wpm)
{
    // Memorisee meme sans poste : elle sert a cadencer la file d'envoi.
    m_wpm = wpm;
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    value_t v; v.i = wpm;
    const int r = rig_set_level(RIGP(m_rig), RIG_VFO_CURR, RIG_LEVEL_KEYSPD, v);
    if (r != RIG_OK)
        emit logMessage(tr("Key speed refused: %1").arg(hamlibError(r)));
#else
    Q_UNUSED(wpm)
#endif
}

// Un cycle d'accord met le poste en emission plusieurs secondes. Le serveur
// verrouille le PTT pendant ce temps ; ici on se contente de lancer.
void RigController::startTune()
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    const int r = rig_vfo_op(RIGP(m_rig), RIG_VFO_CURR, RIG_OP_TUNE);
    if (r != RIG_OK)
        emit logMessage(tr("Tune refused: %1").arg(hamlibError(r)));
    else
        emit logMessage(tr("Tuning started"));
#endif
}

void RigController::emitState()
{
    RigState copy;
    { QMutexLocker lock(&m_mutex); copy = m_state; }
    emit stateChanged(copy);
}

// ------------------------------------------------------------------ ouverture
void RigController::open(const rr::RigConfig &cfg)
{
    close();
    m_cfg = cfg;

    bool ok = false;
    QString msg;

    // Les capacites repartent de zero a chaque ouverture. Sans cela, un client
    // deja connecte garderait celles du poste precedent : passer d'un poste
    // pilote en CAT a un simple PTT serie laisserait un bouton d'accord et une
    // grille de bandes qui ne correspondent plus a rien.
    {
        QMutexLocker lock(&m_mutex);
        m_caps = RigCaps();
    }
    if (cfg.backend != RigConfig::Hamlib)
        emit capsChanged(RigCaps());

    switch (cfg.backend) {
    case RigConfig::Hamlib:
        ok = openHamlib();
        msg = ok ? tr("Radio opened through Hamlib") : m_state.error;
        break;
    case RigConfig::SerialPttOnly:
        ok = openSerialPtt();
        msg = ok ? tr("Serial PTT ready (no CAT)") : m_state.error;
        break;
    case RigConfig::None:
        { QMutexLocker lock(&m_mutex); m_state = RigState(); m_state.connected = true; }
        ok = true;
        msg = tr("No radio control (audio only)");
        break;
    }

    if (ok && cfg.pollMs > 0 && cfg.backend == RigConfig::Hamlib)
        m_timer->start(cfg.pollMs);

    emit logMessage(msg);
    emit opened(ok, msg);
    emitState();
}

bool RigController::openHamlib()
{
#ifdef RR_HAVE_HAMLIB
    applyHamlibDebugLevel();
    rig_load_all_backends();

    RIG *rig = rig_init(rig_model_t(m_cfg.hamlibModel));
    if (!rig) {
        QMutexLocker lock(&m_mutex);
        m_state.error = tr("Unknown Hamlib model %1").arg(m_cfg.hamlibModel);
        return false;
    }

    // rig_set_conf reste stable d'une version de Hamlib à l'autre,
    // contrairement à l'accès direct aux champs de rig->state.
    auto setConf = [&](const char *name, const QString &value) {
        auto t = rig_token_lookup(rig, name);
        if (t == RIG_CONF_END) return;
        rig_set_conf(rig, t, value.toLatin1().constData());
    };

    if (!m_cfg.catPort.isEmpty())
        setConf("rig_pathname", serialDevicePath(m_cfg.catPort));
    setConf("serial_speed", QString::number(m_cfg.catBaud));

    if (m_cfg.pttType == "RTS")       setConf("ptt_type", "RTS");
    else if (m_cfg.pttType == "DTR")  setConf("ptt_type", "DTR");
    else if (m_cfg.pttType == "NONE") setConf("ptt_type", "None");
    else                              setConf("ptt_type", "RIG");

    if (!m_cfg.pttPort.isEmpty() && m_cfg.pttPort != m_cfg.catPort)
        setConf("ptt_pathname", serialDevicePath(m_cfg.pttPort));

    if (m_cfg.dtrOnAlways && m_cfg.pttType != "DTR")
        setConf("dtr_state", "ON");

    const int r = rig_open(rig);
    if (r != RIG_OK) {
        QMutexLocker lock(&m_mutex);
        m_state.error = tr("rig_open failed: %1").arg(hamlibError(r));
        rig_cleanup(rig);
        return false;
    }

    m_rig = rig;

    // Capacites declarees par le poste : plages d'emission normalisees par
    // Hamlib a l'ouverture, et disponibilite du cycle d'accord.
    RigCaps caps;
    caps.hasTune = (rig_has_vfo_op(rig, RIG_OP_TUNE) & RIG_OP_TUNE) != 0;
    // Le manipulateur n'a pas de drapeau de capacite : c'est la presence du
    // pointeur de fonction dans le backend qui fait foi.
    caps.hasMorse = (rig->caps->send_morse != nullptr);
    caps.hasSwr   = (rig_has_get_level(rig, RIG_LEVEL_SWR) & RIG_LEVEL_SWR) != 0;
    caps.hasVfoSet = (rig->caps->set_vfo != nullptr);

    // Modes declares par le backend. On parcourt les bits du masque plutot que
    // d'afficher une liste figee : un bibande FM n'a que faire de PKTLSB.
    for (int bit = 0; bit < 64; ++bit) {
        const rmode_t m = rmode_t(1ULL) << bit;
        if (!(rig->state.mode_list & m)) continue;
        const char *n = rig_strrmode(m);
        if (n && *n) caps.modes << QString::fromLatin1(n);
    }
    for (int i = 0; i < HAMLIB_FRQRANGESIZ; ++i) {
        const freq_range_t &fr = rig->state.tx_range_list[i];
        if (fr.startf == 0 && fr.endf == 0) break;      // fin de liste
        if (fr.endf <= fr.startf) continue;
        caps.txRanges.append({quint64(fr.startf), quint64(fr.endf)});
    }
    {
        QMutexLocker lock(&m_mutex);
        m_caps = caps;
    }
    {
        QStringList found;
        if (caps.hasTune)  found << tr("tuner");
        if (caps.hasMorse) found << tr("keyer");
        if (caps.hasSwr)   found << tr("SWR");
        emit logMessage(tr("Capabilities: %1 transmit range(s) · %2")
                            .arg(caps.txRanges.size())
                            .arg(found.isEmpty() ? tr("no extra capability")
                                                 : found.join(QStringLiteral(", "))));
    }
    emit capsChanged(caps);

    {
        QMutexLocker lock(&m_mutex);
        m_state = RigState();
        m_state.connected = true;
        m_state.hasCat    = true;
        m_state.rigName   = QString("%1 %2").arg(QString::fromLatin1(rig->caps->mfg_name),
                                                 QString::fromLatin1(rig->caps->model_name));
    }
    poll();
    return true;
#else
    QMutexLocker lock(&m_mutex);
    m_state.error = tr("Built without Hamlib");
    return false;
#endif
}

bool RigController::openSerialPtt()
{
    m_serial = new QSerialPort(this);
    // QSerialPort prend le nom court et ajoute « /dev/ » lui-meme : ici,
    // contrairement a Hamlib, il ne faut surtout pas donner un chemin.
    m_serial->setPortName(m_cfg.pttPort.isEmpty() ? m_cfg.catPort : m_cfg.pttPort);
    m_serial->setBaudRate(m_cfg.catBaud);

    if (!m_serial->open(QIODevice::ReadWrite)) {
        QMutexLocker lock(&m_mutex);
        m_state.error = tr("Cannot open port %1: %2")
                            .arg(m_serial->portName(), m_serial->errorString());
        delete m_serial;
        m_serial = nullptr;
        return false;
    }

    m_serial->setRequestToSend(false);
    m_serial->setDataTerminalReady(m_cfg.dtrOnAlways && m_cfg.pttType != "DTR");

    QMutexLocker lock(&m_mutex);
    m_state = RigState();
    m_state.connected = true;
    m_state.hasCat    = false;
    m_state.rigName   = tr("Serial PTT on %1").arg(m_serial->portName());
    return true;
}

void RigController::close()
{
    if (m_timer) m_timer->stop();
    setPtt(false);

#ifdef RR_HAVE_HAMLIB
    if (m_rig) {
        rig_close(RIGP(m_rig));
        rig_cleanup(RIGP(m_rig));
        m_rig = nullptr;
    }
#endif
    if (m_serial) {
        m_serial->setRequestToSend(false);
        m_serial->setDataTerminalReady(false);
        m_serial->close();
        delete m_serial;
        m_serial = nullptr;
    }

    QMutexLocker lock(&m_mutex);
    m_state = RigState();
}

// ------------------------------------------------------------------------ PTT
void RigController::applySerialPtt(bool on)
{
    if (!m_serial || !m_serial->isOpen()) return;
    if (m_cfg.pttType == "DTR") m_serial->setDataTerminalReady(on);
    else                        m_serial->setRequestToSend(on);
}

void RigController::setPtt(bool on)
{
    if (m_pttWanted == on) return;
    m_pttWanted = on;

#ifdef RR_HAVE_HAMLIB
    if (m_rig) {
        const int r = rig_set_ptt(RIGP(m_rig), RIG_VFO_CURR, on ? RIG_PTT_ON : RIG_PTT_OFF);
        if (r != RIG_OK)
            emit logMessage(tr("PTT refused: %1").arg(hamlibError(r)));
    } else
#endif
    {
        applySerialPtt(on);
    }

    { QMutexLocker lock(&m_mutex);
      if (on && !m_state.ptt) m_state.swr = 0.0f;
      m_state.ptt = on; }
    emitState();
}

// ------------------------------------------------------------------- commandes
void RigController::setFrequency(quint64 hz)
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    const int r = rig_set_freq(RIGP(m_rig), RIG_VFO_CURR, freq_t(hz));
    if (r != RIG_OK) { emit logMessage(tr("Frequency refused: %1").arg(hamlibError(r))); return; }
    { QMutexLocker lock(&m_mutex);
      if (m_state.vfo == "B") m_state.freqB = hz; else m_state.freqA = hz; }
    emitState();
#else
    Q_UNUSED(hz)
#endif
}

void RigController::setMode(const QString &mode, int passband)
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    const rmode_t m = modeFromName(mode);
    if (m == RIG_MODE_NONE) return;
    const int r = rig_set_mode(RIGP(m_rig), RIG_VFO_CURR, m,
                               passband > 0 ? pbwidth_t(passband) : RIG_PASSBAND_NORMAL);
    if (r != RIG_OK) { emit logMessage(tr("Mode refused: %1").arg(hamlibError(r))); return; }
    { QMutexLocker lock(&m_mutex); m_state.mode = mode; m_state.passband = passband; }
    emitState();
#else
    Q_UNUSED(mode) Q_UNUSED(passband)
#endif
}

void RigController::setVfo(const QString &vfo)
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    const vfo_t v = (vfo == "B") ? RIG_VFO_B : RIG_VFO_A;
    const int r = rig_set_vfo(RIGP(m_rig), v);
    if (r != RIG_OK) {
        emit logMessage(tr("VFO refused: %1").arg(hamlibError(r)));
        // -11 signifie que le poste ne sait pas changer de VFO par le CAT : on
        // le retient et on cesse de le proposer, plutot que d'echouer a chaque
        // appui. Le FT-891 est dans ce cas.
        if (-r == RIG_ENAVAIL) {
            QMutexLocker lock(&m_mutex);
            if (m_caps.hasVfoSet) {
                m_caps.hasVfoSet = false;
                emit capsChanged(m_caps);
            }
        }
        return;
    }
    { QMutexLocker lock(&m_mutex); m_state.vfo = (vfo == "B") ? "B" : "A"; }
    emitState();
#else
    Q_UNUSED(vfo)
#endif
}

void RigController::poll()
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    RIG *rig = RIGP(m_rig);

    freq_t f = 0;
    rmode_t m = RIG_MODE_NONE;
    pbwidth_t w = 0;
    vfo_t v = RIG_VFO_A;
    ptt_t p = RIG_PTT_OFF;
    value_t lvl{};

    RigState st;
    { QMutexLocker lock(&m_mutex); st = m_state; }

    if (rig_get_vfo(rig, &v) == RIG_OK)
        st.vfo = (v == RIG_VFO_B) ? "B" : "A";

    // La lecture de la frequence sert de sonde : si elle echoue plusieurs fois
    // de suite, le port a disparu — cable USB debranche, adaptateur redemarre
    // par un retour de puissance. Sans cela, l'affichage resterait fige sur la
    // derniere valeur connue et l'operateur croirait le poste toujours pilote.
    const int rf = rig_get_freq(rig, RIG_VFO_CURR, &f);
    if (rf == RIG_OK) {
        if (st.vfo == "B") st.freqB = quint64(f); else st.freqA = quint64(f);
        if (m_readFailures >= 3) emit logMessage(tr("CAT link back"));
        m_readFailures = 0;
        st.hasCat = true;
    } else if (++m_readFailures == 3) {
        emit logMessage(tr("CAT link lost: %1").arg(hamlibError(rf)));
        st.hasCat = false;
    } else if (m_readFailures > 3) {
        st.hasCat = false;
    }
    if (rig_get_mode(rig, RIG_VFO_CURR, &m, &w) == RIG_OK) {
        st.mode = modeName(m);
        st.passband = int(w);
        // Largeurs normalisees du mode courant : elles changent avec lui.
        st.pbWide   = int(rig_passband_wide(rig, m));
        st.pbNormal = int(rig_passband_normal(rig, m));
        st.pbNarrow = int(rig_passband_narrow(rig, m));
    }
    if (rig_get_ptt(rig, RIG_VFO_CURR, &p) == RIG_OK)
        st.ptt = (p != RIG_PTT_OFF);

    // Le S-mètre n'est lu qu'en réception : certains postes bloquent en TX.
    if (!st.ptt && rig_get_level(rig, RIG_VFO_CURR, RIG_LEVEL_STRENGTH, &lvl) == RIG_OK)
        st.strength = lvl.i;

    // Le ROS, lui, ne se mesure qu'en emission : il n'y a pas d'onde reflechie
    // a mesurer en reception. Hors emission, la derniere valeur est conservee,
    // comme le fait l'aiguille d'un ROS-metre, sinon elle disparaitrait au
    // relachement du PTT, juste avant qu'on ait eu le temps de la lire.
    if (st.ptt && m_caps.hasSwr
        && rig_get_level(rig, RIG_VFO_CURR, RIG_LEVEL_SWR, &lvl) == RIG_OK
        && lvl.f >= 1.0f)
        st.swr = lvl.f;

    bool changed;
    { QMutexLocker lock(&m_mutex);
      changed = (st.freqA != m_state.freqA || st.freqB != m_state.freqB ||
                 st.hasCat != m_state.hasCat ||
                 st.mode != m_state.mode || st.vfo != m_state.vfo ||
                 st.ptt != m_state.ptt || st.strength != m_state.strength ||
                 !qFuzzyCompare(st.swr, m_state.swr) ||
                 st.passband != m_state.passband);
      m_state = st; }
    if (changed) emit stateChanged(st);
#endif
}

} // namespace rr
