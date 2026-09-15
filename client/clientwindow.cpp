#include "clientwindow.h"
#include "../common/i18n.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <utility>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>
#include <utility>

namespace rr {

// Sur une liste vide, currentData() est invalide et toInt() renvoie 0 :
// on ouvrirait alors le périphérique numéro 0, qui n'est pas celui voulu.
static int deviceIndexOf(const QComboBox *box)
{
    if (!box) return -1;
    const QVariant v = box->currentData();
    return v.isValid() ? v.toInt() : -1;
}

static const char *kTxStyle = "background:#8c1c1c;color:#ffdede;font-size:20px;"
                              "font-weight:bold;padding:10px 18px;border-radius:6px;";
static const char *kRxStyle = "background:#1c4a1c;color:#d8ffd8;font-size:20px;"
                              "font-weight:bold;padding:10px 18px;border-radius:6px;";

ClientWindow::ClientWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("RemoteRig — Client"));
    resize(820, 720);
    addLanguageMenu(this, QStringLiteral("RemoteRigClient"));

    m_core = new ClientCore;
    m_core->moveToThread(&m_netThread);
    m_netThread.start(QThread::TimeCriticalPriority);

    m_rigctld = new RigctldServer(this);

    connect(m_core, &ClientCore::connectionChanged, this, &ClientWindow::onConnectionChanged);
    connect(m_core, &ClientCore::stateChanged,      this, &ClientWindow::onStateChanged);
    connect(m_core, &ClientCore::capsChanged,       this, &ClientWindow::onCapsChanged);
    connect(m_core, &ClientCore::statsUpdated,      this, &ClientWindow::onStats);
    connect(m_core, &ClientCore::logMessage,        this, &ClientWindow::appendLog);
    connect(m_core, &ClientCore::receiveOnly,       this, &ClientWindow::onReceiveOnly);

    // Les applications numériques locales pilotent la station distante.
    connect(m_rigctld, &RigctldServer::requestFrequency, m_core, &ClientCore::setFrequency);
    connect(m_rigctld, &RigctldServer::requestMode,      m_core, &ClientCore::setMode);
    connect(m_rigctld, &RigctldServer::requestVfo,       m_core, &ClientCore::setVfo);
    connect(m_rigctld, &RigctldServer::requestPtt,       this,   &ClientWindow::setPtt);
    connect(m_rigctld, &RigctldServer::logMessage,       this,   &ClientWindow::appendLog);

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);

    // ------------------------------------------------------------ connexion
    auto *connBox = new QGroupBox(tr("Remote station"));
    auto *cl = new QHBoxLayout(connBox);
    m_host = new QLineEdit("192.168.1.10");
    m_port = new QSpinBox; m_port->setRange(1, 65535); m_port->setValue(7300);
    m_udpPort = new QSpinBox;
    m_udpPort->setRange(0, 65535);
    m_udpPort->setValue(0);
    // Zero laisse le serveur annoncer son port ; une valeur force l'autre bout,
    // utile derriere une redirection NAT qui change le numero.
    m_udpPort->setSpecialValueText(tr("auto"));
    m_udpPort->setToolTip(tr("UDP audio port. Leave on auto unless a NAT rule remaps it."));
    m_password = new QLineEdit; m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("password"));
    m_encrypt = new QCheckBox(tr("Encrypt"));
    m_codec = new QComboBox;
    m_codec->addItem(tr("Opus low latency"), "opus");
    m_codec->addItem(tr("16-bit PCM"), "pcm");
    if (!AudioCodec::opusAvailable()) { m_codec->setCurrentIndex(1); m_codec->setEnabled(false); }
    // Bascule a chaud : inutile de couper la liaison pour passer en numerique.
    connect(m_codec, &QComboBox::activated, this, [this] {
        if (!m_connected) return;
        QMetaObject::invokeMethod(m_core, "setCodec", Qt::QueuedConnection,
                                  Q_ARG(QString, m_codec->currentData().toString()),
                                  Q_ARG(int, m_bitrate->value()));
        appendLog(tr("Codec switched to %1").arg(m_codec->currentText()));
    });
    m_bitrate = new QSpinBox;
    m_bitrate->setRange(16000, 128000); m_bitrate->setSingleStep(8000);
    m_bitrate->setValue(48000); m_bitrate->setSuffix(" bit/s");
    m_connectBtn = new QPushButton(tr("Connect"));
    connect(m_connectBtn, &QPushButton::clicked, this, &ClientWindow::onConnectClicked);

    cl->addWidget(new QLabel(tr("Host")));   cl->addWidget(m_host, 2);
    cl->addWidget(new QLabel(tr("Port")));   cl->addWidget(m_port);
    cl->addWidget(new QLabel(tr("UDP")));    cl->addWidget(m_udpPort);
    cl->addWidget(m_password, 1);
    cl->addWidget(m_encrypt);
    cl->addWidget(m_codec);
    cl->addWidget(m_bitrate);
    cl->addWidget(m_connectBtn);
    root->addWidget(connBox);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildStationPage(), tr("Station"));
    tabs->addTab(buildAudioPage(),   tr("Audio"));
    tabs->addTab(buildDataPage(),    tr("Data modes"));
    root->addWidget(tabs, 1);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    m_log->setMaximumHeight(140);
    root->addWidget(m_log);

    setCentralWidget(central);

    AudioEngine::initialiseLibrary();
    refreshDevices();
    loadSettings();
    setConnectedUi(false);
}

ClientWindow::~ClientWindow()
{
    QMetaObject::invokeMethod(m_core, "disconnectFromStation", Qt::BlockingQueuedConnection);
    m_netThread.quit();
    m_netThread.wait(2000);
    delete m_core;
    AudioEngine::terminateLibrary();
}

void ClientWindow::closeEvent(QCloseEvent *e) { saveSettings(); e->accept(); }

// --------------------------------------------------------------- page station
QWidget *ClientWindow::buildStationPage()
{
    auto *w = new QWidget;
    auto *v = new QVBoxLayout(w);

    // Fréquence : l'élément principal de la fenêtre.
    m_freqLabel = new QLabel("—.——— ———");
    QFont f = m_freqLabel->font();
    f.setPointSize(44);
    f.setFamily("DejaVu Sans Mono");
    f.setBold(true);
    m_freqLabel->setFont(f);
    m_freqLabel->setAlignment(Qt::AlignCenter);
    m_freqLabel->setStyleSheet("color:#f0c674;padding:8px;");
    m_freqLabel->setCursor(Qt::PointingHandCursor);
    m_freqLabel->setToolTip(tr("Click to type a frequency"));
    m_freqLabel->installEventFilter(this);
    v->addWidget(m_freqLabel);

    auto *line = new QHBoxLayout;
    m_modeLabel = new QLabel("—");
    m_modeLabel->setStyleSheet("font-size:15px;");
    m_txLed = new QLabel("RX");
    m_txLed->setStyleSheet(kRxStyle);
    m_txLed->setAlignment(Qt::AlignCenter);
    m_txLed->setMinimumWidth(90);
    line->addWidget(m_modeLabel, 1);
    line->addWidget(m_txLed);
    v->addLayout(line);

    // S-mètre
    auto *sl = new QHBoxLayout;
    m_sMeter = new QProgressBar;
    m_sMeter->setRange(-54, 60);   // dB par rapport à S9
    m_sMeter->setValue(-54);
    m_sMeter->setTextVisible(false);
    m_sLabel = new QLabel("S0");
    m_sLabel->setMinimumWidth(70);
    sl->addWidget(new QLabel(tr("Signal")));
    sl->addWidget(m_sMeter, 1);
    sl->addWidget(m_sLabel);
    v->addLayout(sl);

    // VFO, mode, pas
    auto *ctl = new QHBoxLayout;
    // De simples boutons : le VFO actif se lit a la couleur, pas a un enfoncement
    // qui laissait croire a une bascule verrouillee.
    m_vfoA = new QPushButton("VFO A");
    m_vfoB = new QPushButton("VFO B");
    connect(m_vfoA, &QPushButton::clicked, this, [this] {
        QMetaObject::invokeMethod(m_core, "setVfo", Qt::QueuedConnection, Q_ARG(QString, "A")); });
    connect(m_vfoB, &QPushButton::clicked, this, [this] {
        QMetaObject::invokeMethod(m_core, "setVfo", Qt::QueuedConnection, Q_ARG(QString, "B")); });

    m_mode = new QComboBox;
    m_mode->addItems({"USB", "LSB", "CW", "CWR", "AM", "FM", "RTTY", "PKTUSB", "PKTLSB", "PKTFM"});
    connect(m_mode, &QComboBox::activated, this, [this] {
        QMetaObject::invokeMethod(m_core, "setMode", Qt::QueuedConnection,
                                  Q_ARG(QString, m_mode->currentText()), Q_ARG(int, 0)); });

    m_step = new QComboBox;
    for (int s : {10, 100, 1000, 5000, 10000, 100000})
        m_step->addItem(s < 1000 ? QString("%1 Hz").arg(s) : QString("%1 kHz").arg(s / 1000.0), s);
    m_step->setCurrentIndex(2);

    auto *down = new QPushButton("◀");
    auto *up   = new QPushButton("▶");
    connect(down, &QPushButton::clicked, this, [this] { tuneBy(-m_step->currentData().toInt()); });
    connect(up,   &QPushButton::clicked, this, [this] { tuneBy( m_step->currentData().toInt()); });

    ctl->addWidget(m_vfoA); ctl->addWidget(m_vfoB);
    ctl->addWidget(m_mode);
    ctl->addWidget(down); ctl->addWidget(m_step); ctl->addWidget(up);
    v->addLayout(ctl);

    m_catWidgets << m_vfoA << m_vfoB << m_mode << down << m_step << up;

    // Bandes : la grille est remplie a la connexion, d'apres ce que le poste
    // declare savoir emettre. Hors connexion, le plan complet sert de repere.
    m_bandGrid = new QGridLayout;
    m_bandGrid->setHorizontalSpacing(6);
    m_bandGrid->setVerticalSpacing(6);
    v->addLayout(m_bandGrid);
    rebuildBands(standardBandPlan());

    auto *txRow = new QHBoxLayout;
    m_pttBtn = new QPushButton(tr("Transmit  (hold, or press space)"));
    m_pttBtn->setMinimumHeight(56);
    m_pttBtn->setStyleSheet("font-size:16px;font-weight:bold;");
    connect(m_pttBtn, &QPushButton::pressed,  this, &ClientWindow::onPttPressed);
    connect(m_pttBtn, &QPushButton::released, this, &ClientWindow::onPttReleased);

    m_tuneBtn = new QPushButton(tr("Tune"));
    m_tuneBtn->setMinimumHeight(56);
    m_tuneBtn->setMinimumWidth(110);
    m_tuneBtn->setToolTip(tr("Start the radio's antenna tuner"));
    m_tuneBtn->setEnabled(false);
    connect(m_tuneBtn, &QPushButton::clicked, this, [this] {
        QMetaObject::invokeMethod(m_core, "startTune", Qt::QueuedConnection);
    });

    txRow->addWidget(m_pttBtn, 1);
    txRow->addWidget(m_tuneBtn);
    v->addLayout(txRow);

    m_statsLabel = new QLabel(tr("Offline"));
    m_statsLabel->setStyleSheet("color:#888;");
    v->addWidget(m_statsLabel);

    setCatEnabled(false);

    v->addStretch(1);
    return w;
}

QWidget *ClientWindow::buildAudioPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    m_hostApi = new QComboBox;
    for (const auto &h : AudioEngine::hostApis())
        m_hostApi->addItem(h.second, h.first);
    {
        const int def = m_hostApi->findData(AudioEngine::defaultHostApi());
        if (def >= 0) m_hostApi->setCurrentIndex(def);
    }
    connect(m_hostApi, &QComboBox::currentIndexChanged, this, [this] {
        refreshDevices();
        updateRateLabel();
    });
    f->addRow(tr("Audio interface"), m_hostApi);

    m_inDev = new QComboBox;
    f->addRow(tr("Microphone (or virtual cable output)"), m_inDev);
    m_outDev = new QComboBox;
    f->addRow(tr("Monitor (or virtual cable input)"), m_outDev);

    m_rateLabel = new QLabel("—");
    m_rateLabel->setWordWrap(true);
    f->addRow(tr("Negotiated rate"), m_rateLabel);

    auto *rescan = new QPushButton(tr("Look for devices again"));
    rescan->setToolTip(tr("Needed after plugging in a USB sound card: "
                          "the device list is read once at startup."));
    connect(rescan, &QPushButton::clicked, this, &ClientWindow::onRescanDevices);
    f->addRow("", rescan);
    connect(m_inDev,  &QComboBox::currentIndexChanged, this, &ClientWindow::updateRateLabel);
    connect(m_outDev, &QComboBox::currentIndexChanged, this, &ClientWindow::updateRateLabel);

    // Bascule a chaud : inutile de couper la liaison pour changer de carte.
    connect(m_inDev, &QComboBox::activated, this, [this] {
        if (!m_connected) return;
        QMetaObject::invokeMethod(m_core, "setInputDevice", Qt::QueuedConnection,
                                  Q_ARG(int, deviceIndexOf(m_inDev)));
    });
    connect(m_outDev, &QComboBox::activated, this, [this] {
        if (!m_connected) return;
        QMetaObject::invokeMethod(m_core, "setOutputDevice", Qt::QueuedConnection,
                                  Q_ARG(int, deviceIndexOf(m_outDev)));
    });

    m_frames = new QComboBox;
    m_frames->addItem(tr("120 samples — 2.5 ms"), 120);
    m_frames->addItem(tr("240 samples — 5 ms"),   240);
    m_frames->addItem(tr("480 samples — 10 ms"),  480);
    m_frames->addItem(tr("960 samples — 20 ms"),  960);
    m_frames->setCurrentIndex(2);
    f->addRow(tr("Sound card buffer"), m_frames);

    m_jitter = new QSpinBox;
    m_jitter->setRange(10, 300); m_jitter->setValue(40); m_jitter->setSuffix(" ms");
    connect(m_jitter, &QSpinBox::valueChanged, this, [this](int v) {
        QMetaObject::invokeMethod(m_core, "setJitterMs", Qt::QueuedConnection, Q_ARG(int, v)); });
    f->addRow(tr("Jitter buffer"), m_jitter);

    m_rxGain = new QDoubleSpinBox; m_rxGain->setRange(0.1, 8.0); m_rxGain->setValue(1.0); m_rxGain->setSingleStep(0.1);
    m_txGain = new QDoubleSpinBox; m_txGain->setRange(0.1, 8.0); m_txGain->setValue(1.0); m_txGain->setSingleStep(0.1);
    auto pushGains = [this] {
        QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                                  Q_ARG(float, float(m_rxGain->value())),
                                  Q_ARG(float, float(m_txGain->value()))); };
    connect(m_rxGain, &QDoubleSpinBox::valueChanged, this, pushGains);
    connect(m_txGain, &QDoubleSpinBox::valueChanged, this, pushGains);
    f->addRow(tr("Receive volume"), m_rxGain);
    f->addRow(tr("Transmit level"), m_txGain);

    m_rxMeter = new QProgressBar; m_rxMeter->setRange(0, 100); m_rxMeter->setTextVisible(false);
    m_txMeter = new QProgressBar; m_txMeter->setRange(0, 100); m_txMeter->setTextVisible(false);
    f->addRow(tr("Received level"), m_rxMeter);
    f->addRow(tr("Transmitted level"), m_txMeter);

    // ------------------------------------------------ mise en forme du micro
    auto *sep = new QLabel(QStringLiteral("<b>%1</b>").arg(tr("Microphone shaping")));
    f->addRow(sep);

    m_speechPreset = new QComboBox;
    m_speechPreset->addItem(tr("None — flat, required for data modes"), 0);
    m_speechPreset->addItem(tr("Headset boom microphone"), 1);
    m_speechPreset->addItem(tr("Desk microphone"), 2);
    m_speechPreset->addItem(tr("Custom"), 3);
    f->addRow(tr("Preset"), m_speechPreset);

    m_speechHp = new QComboBox;
    m_speechHp->addItem(tr("off"), 0);
    for (int hz : {150, 200, 250, 300, 400})
        m_speechHp->addItem(QStringLiteral("%1 Hz").arg(hz), hz);
    f->addRow(tr("High-pass"), m_speechHp);

    m_speechPresence = new QDoubleSpinBox;
    m_speechPresence->setRange(0.0, 12.0);
    m_speechPresence->setSingleStep(0.5);
    m_speechPresence->setSuffix(tr(" dB at 2 kHz"));
    f->addRow(tr("Presence"), m_speechPresence);

    m_speechLp = new QComboBox;
    m_speechLp->addItem(tr("off"), 0);
    for (int hz : {2700, 3000, 3200, 3500})
        m_speechLp->addItem(QStringLiteral("%1 Hz").arg(hz), hz);
    f->addRow(tr("Low-pass"), m_speechLp);

    m_speechComp = new QComboBox;
    m_speechComp->addItem(tr("none"), 10);
    m_speechComp->addItem(tr("light — 2:1"), 20);
    m_speechComp->addItem(tr("medium — 3:1"), 30);
    m_speechComp->addItem(tr("firm — 4:1"), 40);
    f->addRow(tr("Compression"), m_speechComp);

    m_compMeter = new QProgressBar;
    m_compMeter->setRange(0, 20);
    m_compMeter->setFormat(tr("%v dB"));
    f->addRow(tr("Gain reduction"), m_compMeter);

    connect(m_speechPreset, &QComboBox::activated, this, &ClientWindow::applySpeechPreset);
    for (QComboBox *c : {m_speechHp, m_speechLp, m_speechComp})
        connect(c, &QComboBox::activated, this, [this] {
            if (!m_applyingPreset) m_speechPreset->setCurrentIndex(3);
            pushSpeechSettings();
        });
    connect(m_speechPresence, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_applyingPreset) m_speechPreset->setCurrentIndex(3);
        pushSpeechSettings();
    });

    auto *hint = new QLabel(tr(
        "For voice, keep Opus: it fits in 48 kbit/s for about 25 ms of total latency.\n"
        "For data modes (FT8, VARA, PSK), switch to 16-bit PCM: Opus distorts narrow tones "
        "and decode rates collapse."));
    hint->setWordWrap(true);
    f->addRow(hint);

    return w;
}

QWidget *ClientWindow::buildDataPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    m_rigctldOn = new QCheckBox(tr("Publish a rigctld interface for data-mode software"));
    f->addRow(m_rigctldOn);

    m_rigctldPort = new QSpinBox;
    m_rigctldPort->setRange(1, 65535); m_rigctldPort->setValue(4532);
    f->addRow(tr("Port"), m_rigctldPort);

    m_rigctldAny = new QCheckBox(tr("Also accept connections from other machines"));
    f->addRow(m_rigctldAny);

    auto pushRigctld = [this] {
        if (m_rigctldOn->isChecked())
            m_rigctld->start(quint16(m_rigctldPort->value()), !m_rigctldAny->isChecked());
        else
            m_rigctld->stop();
    };
    connect(m_rigctldOn,   &QCheckBox::toggled,      this, pushRigctld);
    connect(m_rigctldAny,  &QCheckBox::toggled,      this, pushRigctld);
    connect(m_rigctldPort, &QSpinBox::valueChanged,  this, pushRigctld);

    auto *hint = new QLabel(tr(
        "In WSJT-X / fldigi / JS8Call, set the radio to \"Hamlib NET rigctl\", "
        "address 127.0.0.1:4532, PTT \"CAT\".\n\n"
        "For audio, pick the virtual cable in the Audio tab (VB-Audio Cable on Windows, "
        "a PulseAudio or PipeWire null-sink module on Linux), and point the data-mode "
        "software at the other end of that same cable.\n\n"
        "VARA works the same way: its PTT goes through rigctld, its audio through the cable."));
    hint->setWordWrap(true);
    f->addRow(hint);

    return w;
}

// -------------------------------------------------------------------- actions
void ClientWindow::refreshDevices()
{
    const int api = m_hostApi ? m_hostApi->currentData().toInt() : -1;
    const QSignalBlocker b1(m_inDev), b2(m_outDev);

    m_inDev->clear();
    for (const auto &d : AudioEngine::inputDevices(api))
        m_inDev->addItem(d.name, d.index);
    // Une liste vide doit se lire, pas se deviner. L'index -1 signale
    // l'absence de peripherique au reste du programme.
    if (m_inDev->count() == 0)
        m_inDev->addItem(tr("No microphone found — receive only"), -1);

    m_outDev->clear();
    for (const auto &d : AudioEngine::outputDevices(api))
        m_outDev->addItem(d.name, d.index);
    if (m_outDev->count() == 0)
        m_outDev->addItem(tr("No playback device found"), -1);
}

void ClientWindow::onRescanDevices()
{
    if (m_connected) {
        appendLog(tr("Disconnect first: the device list cannot be reread "
                     "while the audio streams are open."));
        return;
    }
    AudioEngine::rescanDevices();
    const QString keptApi = m_hostApi->currentText();
    {
        const QSignalBlocker b(m_hostApi);
        m_hostApi->clear();
        for (const auto &h : AudioEngine::hostApis())
            m_hostApi->addItem(h.second, h.first);
        const int i = m_hostApi->findText(keptApi);
        if (i >= 0) m_hostApi->setCurrentIndex(i);
    }
    refreshDevices();
    updateRateLabel();
    appendLog(tr("Device list reread."));
}

void ClientWindow::onReceiveOnly(bool on)
{
    m_rxOnly = on;
    m_pttBtn->setEnabled(m_connected && !on);
    m_pttBtn->setText(on ? tr("Transmit unavailable — no microphone")
                         : tr("Transmit  (hold, or press space)"));
}

rr::SpeechSettings ClientWindow::currentSpeech() const
{
    rr::SpeechSettings s;
    s.highPassHz = m_speechHp->currentData().toDouble();
    s.presenceHz = 2000.0;
    s.presenceDb = m_speechPresence->value();
    s.lowPassHz  = m_speechLp->currentData().toDouble();
    s.compRatio  = m_speechComp->currentData().toInt() / 10.0;
    s.compThreshDb = -18.0;
    s.enabled = (m_speechPreset->currentIndex() != 0) &&
                (s.highPassHz > 0 || s.presenceDb > 0 || s.lowPassHz > 0 || s.compRatio > 1.0);
    return s;
}

void ClientWindow::pushSpeechSettings()
{
    const bool custom = m_speechPreset->currentIndex() != 0;
    const QList<QWidget *> shaping{m_speechHp, m_speechPresence, m_speechLp, m_speechComp};
    for (QWidget *w : shaping) w->setEnabled(custom);
    m_compMeter->setEnabled(custom);

    QMetaObject::invokeMethod(m_core, "setSpeechSettings", Qt::QueuedConnection,
                              Q_ARG(rr::SpeechSettings, currentSpeech()));
}

void ClientWindow::applySpeechPreset(int index)
{
    m_applyingPreset = true;
    switch (index) {
    case 0:   // aucun : indispensable en numerique
        m_speechHp->setCurrentIndex(0);
        m_speechPresence->setValue(0.0);
        m_speechLp->setCurrentIndex(0);
        m_speechComp->setCurrentIndex(0);
        break;
    case 1:   // casque a perche : effet de proximite marque
        m_speechHp->setCurrentIndex(m_speechHp->findData(300));
        m_speechPresence->setValue(6.0);
        m_speechLp->setCurrentIndex(m_speechLp->findData(3200));
        m_speechComp->setCurrentIndex(2);
        break;
    case 2:   // micro de table, a distance
        m_speechHp->setCurrentIndex(m_speechHp->findData(200));
        m_speechPresence->setValue(4.0);
        m_speechLp->setCurrentIndex(m_speechLp->findData(3200));
        m_speechComp->setCurrentIndex(1);
        break;
    default:
        break;
    }
    m_applyingPreset = false;
    pushSpeechSettings();
}

void ClientWindow::updateRateLabel()
{
    if (!m_rateLabel) return;
    const int inIdx  = deviceIndexOf(m_inDev);
    const int outIdx = deviceIndexOf(m_outDev);
    const double in  = inIdx  < 0 ? -1.0 : AudioEngine::probeRate(inIdx, true);
    const double out = outIdx < 0 ? -1.0 : AudioEngine::probeRate(outIdx, false);

    auto describe = [](double r) {
        if (r < 0.0)  return tr("no device");
        if (r == 0.0) return tr("rejected");
        if (int(r) == AudioEngine::kAudioRate) return tr("48000 Hz, direct");
        return tr("%1 Hz, resampled").arg(int(r));
    };
    m_rateLabel->setText(tr("Microphone: %1  ·  Monitor: %2").arg(describe(in), describe(out)));
}

void ClientWindow::onConnectClicked()
{
    if (m_connected) {
        QMetaObject::invokeMethod(m_core, "disconnectFromStation", Qt::QueuedConnection);
        setConnectedUi(false);
        appendLog(tr("Disconnected"));
        return;
    }

    ClientConfig c;
    c.host     = m_host->text().trimmed();
    c.tcpPort  = quint16(m_port->value());
    c.udpPort  = quint16(m_udpPort->value());
    c.password = m_password->text();
    c.encrypt  = m_encrypt->isChecked();
    c.codec    = m_codec->currentData().toString();
    c.bitrate  = m_bitrate->value();
    c.inputDevice  = deviceIndexOf(m_inDev);
    c.outputDevice = deviceIndexOf(m_outDev);
    if (c.outputDevice < 0) {
        appendLog(tr("No playback device: nothing could be heard."));
        return;
    }
    c.framesPerBuffer = m_frames->currentData().toInt();
    c.jitterMs = m_jitter->value();
    c.rxGain = float(m_rxGain->value());
    c.txGain = float(m_txGain->value());
    c.speech = currentSpeech();

    QMetaObject::invokeMethod(m_core, "connectToStation", Qt::QueuedConnection,
                              Q_ARG(rr::ClientConfig, c));
}

void ClientWindow::setConnectedUi(bool up)
{
    m_connected = up;
    m_connectBtn->setText(up ? tr("Disconnect") : tr("Connect"));
    m_pttBtn->setEnabled(up && !m_rxOnly);
    if (!up) {
        m_rxOnly = false;
        m_pttBtn->setText(tr("Transmit  (hold, or press space)"));
    }
    if (!up) {
        m_statsLabel->setText(tr("Offline"));
        m_txLed->setText("RX");
        m_txLed->setStyleSheet(kRxStyle);
        m_tuneBtn->setEnabled(false);
        m_freqLabel->setText(QStringLiteral("—.——— ———"));
        m_modeLabel->setText(QStringLiteral("—"));
        m_sMeter->setValue(-54);
        m_sLabel->setText(QStringLiteral("S0"));
    }
    setCatEnabled(up && m_state.hasCat);
}

void ClientWindow::onConnectionChanged(bool up, const QString &msg)
{
    setConnectedUi(up);
    appendLog(msg);
}

void ClientWindow::onStateChanged(const RigState &st)
{
    m_state = st;
    m_rigctld->updateState(st);
    setCatEnabled(m_connected && st.hasCat);

    const quint64 hz = (st.vfo == "B") ? st.freqB : st.freqA;
    if (st.hasCat && hz > 0) {
        // 14.074.000 -> "14.074.000"
        QString s = QString::number(hz);
        for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, '.');
        m_freqLabel->setText(s);
        m_modeLabel->setText(tr("%1 · VFO %2 · %3").arg(st.mode, st.vfo, st.rigName));
        const int i = m_mode->findText(st.mode);
        if (i >= 0 && !m_mode->hasFocus()) m_mode->setCurrentIndex(i);
        const QString activeVfo = "font-weight:bold;color:#f0c674;";
        m_vfoA->setStyleSheet(st.vfo == "A" ? activeVfo : QString());
        m_vfoB->setStyleSheet(st.vfo == "B" ? activeVfo : QString());
    } else {
        m_freqLabel->setText(tr("no CAT"));
        m_modeLabel->setText(st.rigName.isEmpty() ? tr("PTT only") : st.rigName);
    }

    m_sMeter->setValue(st.strength);
    const int sUnits = qBound(0, (st.strength + 54) / 6, 9);
    m_sLabel->setText(st.strength > 0 ? QString("S9+%1").arg(st.strength)
                                      : QString("S%1").arg(sUnits));

    m_txLed->setText(st.tuning ? tr("TUNE") : (st.ptt ? "TX" : "RX"));
    m_txLed->setStyleSheet(st.ptt || st.tuning ? kTxStyle : kRxStyle);

    // Pendant l'accord le poste emet deja : le PTT reste inaccessible.
    m_pttBtn->setEnabled(m_connected && !st.tuning);
    m_tuneBtn->setEnabled(m_connected && m_core->caps().hasTune && !st.tuning && !st.ptt);
}

void ClientWindow::onStats(int rttMs, int lost, int jitterMs, float rxLevel, float txLevel,
                           float gainReductionDb)
{
    m_compMeter->setValue(int(gainReductionDb + 0.5f));
    m_rxMeter->setValue(int(rxLevel * 100));
    m_txMeter->setValue(int(txLevel * 100));
    m_statsLabel->setText(tr("Round trip %1 ms · buffer %2 ms · %3 frames lost")
                              .arg(rttMs).arg(jitterMs).arg(lost));
}

void ClientWindow::tuneBy(qint64 delta)
{
    if (!m_state.hasCat) return;
    const quint64 hz = (m_state.vfo == "B") ? m_state.freqB : m_state.freqA;
    const qint64 nw = qint64(hz) + delta;
    if (nw < 1000) return;
    QMetaObject::invokeMethod(m_core, "setFrequency", Qt::QueuedConnection,
                              Q_ARG(quint64, quint64(nw)));
}

void ClientWindow::setPtt(bool on)
{
    if (m_ptt == on || !m_connected || m_rxOnly) return;
    m_ptt = on;
    QMetaObject::invokeMethod(m_core, "setPtt", Qt::QueuedConnection, Q_ARG(bool, on));
    m_txLed->setText(on ? "TX" : "RX");
    m_txLed->setStyleSheet(on ? kTxStyle : kRxStyle);
}

// Sans CAT sur la station, rien de tout cela n'aboutirait : mieux vaut
// griser les commandes que laisser l'opérateur cliquer dans le vide.
void ClientWindow::rebuildBands(const QList<rr::Band> &bands)
{
    for (QPushButton *b : std::as_const(m_bandButtons)) {
        m_catWidgets.removeAll(b);
        delete b;
    }
    m_bandButtons.clear();

    int col = 0, row = 0;
    for (const rr::Band &band : bands) {
        auto *btn = new QPushButton(band.name);
        btn->setMinimumHeight(30);
        const quint64 hz = band.preset;
        connect(btn, &QPushButton::clicked, this, [this, hz] {
            QMetaObject::invokeMethod(m_core, "setFrequency", Qt::QueuedConnection,
                                      Q_ARG(quint64, hz));
        });
        m_bandGrid->addWidget(btn, row, col);
        m_bandButtons << btn;
        m_catWidgets << btn;
        if (++col == 6) { col = 0; ++row; }
    }
    setCatEnabled(m_connected && m_state.hasCat);
}

void ClientWindow::onCapsChanged(const rr::RigCaps &caps)
{
    rebuildBands(bandsWithin(caps.txRanges));
    m_tuneBtn->setEnabled(m_connected && caps.hasTune);
    appendLog(caps.hasTune ? tr("%1 bands, antenna tuner available").arg(m_bandButtons.size())
                           : tr("%1 bands, no antenna tuner").arg(m_bandButtons.size()));
}

void ClientWindow::setCatEnabled(bool on)
{
    for (QWidget *w : std::as_const(m_catWidgets))
        w->setEnabled(on);
    m_sMeter->setEnabled(on);
    m_sLabel->setEnabled(on);
    m_freqLabel->setStyleSheet(on ? "color:#f0c674;padding:8px;"
                                  : "color:#5a5a5a;padding:8px;");
}

// Un clic sur l'afficheur ouvre la saisie. Sans CAT il n'y a rien a regler,
// donc rien ne s'ouvre.
bool ClientWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_freqLabel && event->type() == QEvent::MouseButtonRelease) {
        if (m_connected && m_state.hasCat) promptFrequency();
        return true;
    }
    return QMainWindow::eventFilter(watched, event);
}

void ClientWindow::promptFrequency()
{
    const quint64 current = (m_state.vfo == "B") ? m_state.freqB : m_state.freqA;

    bool accepted = false;
    const QString text = QInputDialog::getText(
        this, tr("Frequency"),
        tr("MHz, kHz or Hz — 14.074, 14074 and 14074000 all work:"),
        QLineEdit::Normal,
        QString::number(double(current) / 1e6, 'f', 6), &accepted);
    if (!accepted) return;

    bool valid = false;
    const quint64 hz = parseFrequency(text, &valid);
    if (!valid) {
        appendLog(tr("Frequency not understood: %1").arg(text));
        return;
    }
    QMetaObject::invokeMethod(m_core, "setFrequency", Qt::QueuedConnection, Q_ARG(quint64, hz));
}

void ClientWindow::onPttPressed()  { setPtt(true); }
void ClientWindow::onPttReleased() { setPtt(false); }

void ClientWindow::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { setPtt(true); return; }
    QMainWindow::keyPressEvent(e);
}

void ClientWindow::keyReleaseEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Space && !e->isAutoRepeat()) { setPtt(false); return; }
    QMainWindow::keyReleaseEvent(e);
}

void ClientWindow::appendLog(const QString &msg)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + msg);
}

// ------------------------------------------------------------------ reglages
void ClientWindow::loadSettings()
{
    QSettings s("F4JTV", "RemoteRigClient");
    const int api = m_hostApi->findText(s.value("hostApi").toString());
    if (api >= 0) { m_hostApi->setCurrentIndex(api); refreshDevices(); }
    m_host->setText(s.value("host", "192.168.1.10").toString());
    m_port->setValue(s.value("port", 7300).toInt());
    m_udpPort->setValue(s.value("udpPort", 0).toInt());
    m_password->setText(s.value("password").toString());
    m_encrypt->setChecked(s.value("encrypt", false).toBool());
    const int ci = m_codec->findData(s.value("codec", "opus").toString());
    if (ci >= 0) m_codec->setCurrentIndex(ci);
    m_bitrate->setValue(s.value("bitrate", 48000).toInt());
    m_frames->setCurrentIndex(s.value("framesIdx", 2).toInt());
    m_jitter->setValue(s.value("jitter", 40).toInt());
    m_rxGain->setValue(s.value("rxGain", 1.0).toDouble());
    m_txGain->setValue(s.value("txGain", 1.0).toDouble());
    const int in = m_inDev->findText(s.value("inDev").toString());
    if (in >= 0) m_inDev->setCurrentIndex(in);
    const int out = m_outDev->findText(s.value("outDev").toString());
    if (out >= 0) m_outDev->setCurrentIndex(out);
    m_rigctldPort->setValue(s.value("rigctldPort", 4532).toInt());
    m_rigctldAny->setChecked(s.value("rigctldAny", false).toBool());
    m_speechPreset->setCurrentIndex(s.value("speechPreset", 1).toInt());
    applySpeechPreset(m_speechPreset->currentIndex());
    if (m_speechPreset->currentIndex() == 3) {
        const int hp = m_speechHp->findData(s.value("speechHp", 300).toInt());
        if (hp >= 0) m_speechHp->setCurrentIndex(hp);
        m_speechPresence->setValue(s.value("speechPresence", 6.0).toDouble());
        const int lp = m_speechLp->findData(s.value("speechLp", 3200).toInt());
        if (lp >= 0) m_speechLp->setCurrentIndex(lp);
        m_speechComp->setCurrentIndex(s.value("speechComp", 2).toInt());
        pushSpeechSettings();
    }
    m_rigctldOn->setChecked(s.value("rigctldOn", false).toBool());
    updateRateLabel();
}

void ClientWindow::saveSettings()
{
    QSettings s("F4JTV", "RemoteRigClient");
    s.setValue("host", m_host->text());
    s.setValue("port", m_port->value());
    s.setValue("udpPort", m_udpPort->value());
    s.setValue("password", m_password->text());
    s.setValue("encrypt", m_encrypt->isChecked());
    s.setValue("codec", m_codec->currentData().toString());
    s.setValue("bitrate", m_bitrate->value());
    s.setValue("framesIdx", m_frames->currentIndex());
    s.setValue("jitter", m_jitter->value());
    s.setValue("rxGain", m_rxGain->value());
    s.setValue("txGain", m_txGain->value());
    s.setValue("inDev", m_inDev->currentText());
    s.setValue("outDev", m_outDev->currentText());
    s.setValue("rigctldOn", m_rigctldOn->isChecked());
    s.setValue("rigctldPort", m_rigctldPort->value());
    s.setValue("rigctldAny", m_rigctldAny->isChecked());
    s.setValue("hostApi", m_hostApi->currentText());
    s.setValue("speechPreset", m_speechPreset->currentIndex());
    s.setValue("speechHp", m_speechHp->currentData().toInt());
    s.setValue("speechPresence", m_speechPresence->value());
    s.setValue("speechLp", m_speechLp->currentData().toInt());
    s.setValue("speechComp", m_speechComp->currentIndex());
}

} // namespace rr
