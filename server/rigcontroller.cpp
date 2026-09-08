#include "rigcontroller.h"

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

QList<QPair<int, QString>> RigController::hamlibModels()
{
    QList<QPair<int, QString>> out;
#ifdef RR_HAVE_HAMLIB
    // Le chargement des backends écrit une ligne par pilote sur la console.
    rig_set_debug(RIG_DEBUG_NONE);
    rig_load_all_backends();
    rig_set_debug(RIG_DEBUG_ERR);
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

void RigController::emitState()
{
    RigState copy;
    { QMutexLocker lock(&m_mutex); copy = m_state; }
    emit stateChanged(copy);
}

// ------------------------------------------------------------------ ouverture
void RigController::open(const RigConfig &cfg)
{
    close();
    m_cfg = cfg;

    bool ok = false;
    QString msg;

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
    rig_set_debug(RIG_DEBUG_ERR);
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

    if (!m_cfg.catPort.isEmpty()) setConf("rig_pathname", m_cfg.catPort);
    setConf("serial_speed", QString::number(m_cfg.catBaud));

    if (m_cfg.pttType == "RTS")       setConf("ptt_type", "RTS");
    else if (m_cfg.pttType == "DTR")  setConf("ptt_type", "DTR");
    else if (m_cfg.pttType == "NONE") setConf("ptt_type", "None");
    else                              setConf("ptt_type", "RIG");

    if (!m_cfg.pttPort.isEmpty() && m_cfg.pttPort != m_cfg.catPort)
        setConf("ptt_pathname", m_cfg.pttPort);

    if (m_cfg.dtrOnAlways && m_cfg.pttType != "DTR")
        setConf("dtr_state", "ON");

    const int r = rig_open(rig);
    if (r != RIG_OK) {
        QMutexLocker lock(&m_mutex);
        m_state.error = tr("rig_open failed: %1").arg(QString::fromLatin1(rigerror(r)));
        rig_cleanup(rig);
        return false;
    }

    m_rig = rig;
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
            emit logMessage(tr("PTT refused: %1").arg(QString::fromLatin1(rigerror(r))));
    } else
#endif
    {
        applySerialPtt(on);
    }

    { QMutexLocker lock(&m_mutex); m_state.ptt = on; }
    emitState();
}

// ------------------------------------------------------------------- commandes
void RigController::setFrequency(quint64 hz)
{
#ifdef RR_HAVE_HAMLIB
    if (!m_rig) return;
    const int r = rig_set_freq(RIGP(m_rig), RIG_VFO_CURR, freq_t(hz));
    if (r != RIG_OK) { emit logMessage(tr("Frequency refused: %1").arg(QString::fromLatin1(rigerror(r)))); return; }
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
    if (r != RIG_OK) { emit logMessage(tr("Mode refused: %1").arg(QString::fromLatin1(rigerror(r)))); return; }
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
    if (r != RIG_OK) { emit logMessage(tr("VFO refused: %1").arg(QString::fromLatin1(rigerror(r)))); return; }
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

    if (rig_get_freq(rig, RIG_VFO_CURR, &f) == RIG_OK) {
        if (st.vfo == "B") st.freqB = quint64(f); else st.freqA = quint64(f);
    }
    if (rig_get_mode(rig, RIG_VFO_CURR, &m, &w) == RIG_OK) {
        st.mode = modeName(m);
        st.passband = int(w);
    }
    if (rig_get_ptt(rig, RIG_VFO_CURR, &p) == RIG_OK)
        st.ptt = (p != RIG_PTT_OFF);

    // Le S-mètre n'est lu qu'en réception : certains postes bloquent en TX.
    if (!st.ptt && rig_get_level(rig, RIG_VFO_CURR, RIG_LEVEL_STRENGTH, &lvl) == RIG_OK)
        st.strength = lvl.i;

    bool changed;
    { QMutexLocker lock(&m_mutex);
      changed = (st.freqA != m_state.freqA || st.freqB != m_state.freqB ||
                 st.mode != m_state.mode || st.vfo != m_state.vfo ||
                 st.ptt != m_state.ptt || st.strength != m_state.strength);
      m_state = st; }
    if (changed) emit stateChanged(st);
#endif
}

} // namespace rr
