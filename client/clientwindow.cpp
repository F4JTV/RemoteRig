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

namespace rr {

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
    connect(m_core, &ClientCore::statsUpdated,      this, &ClientWindow::onStats);
    connect(m_core, &ClientCore::logMessage,        this, &ClientWindow::appendLog);

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
    m_password = new QLineEdit; m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(tr("password"));
    m_encrypt = new QCheckBox(tr("Encrypt"));
    m_codec = new QComboBox;
    m_codec->addItem(tr("Opus low latency"), "opus");
    m_codec->addItem(tr("16-bit PCM"), "pcm");
    if (!AudioCodec::opusAvailable()) { m_codec->setCurrentIndex(1); m_codec->setEnabled(false); }
    m_bitrate = new QSpinBox;
    m_bitrate->setRange(16000, 128000); m_bitrate->setSingleStep(8000);
    m_bitrate->setValue(48000); m_bitrate->setSuffix(" bit/s");
    m_connectBtn = new QPushButton(tr("Connect"));
    connect(m_connectBtn, &QPushButton::clicked, this, &ClientWindow::onConnectClicked);

    cl->addWidget(new QLabel(tr("Host")));   cl->addWidget(m_host, 2);
    cl->addWidget(new QLabel(tr("Port")));   cl->addWidget(m_port);
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
    m_vfoA = new QPushButton("VFO A"); m_vfoA->setCheckable(true); m_vfoA->setChecked(true);
    m_vfoB = new QPushButton("VFO B"); m_vfoB->setCheckable(true);
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

    // Bandes
    auto *bands = new QGridLayout;
    struct { const char *name; quint64 hz; } table[] = {
        {"160 m", 1840000},  {"80 m", 3650000},   {"60 m", 5354000},
        {"40 m", 7100000},   {"30 m", 10130000},  {"20 m", 14200000},
        {"17 m", 18130000},  {"15 m", 21250000},  {"12 m", 24950000},
        {"10 m", 28400000},  {"6 m", 50200000},   {"2 m", 145500000},
        {"70 cm", 433500000}
    };
    int col = 0, row = 0;
    for (const auto &b : table) {
        auto *btn = new QPushButton(b.name);
        const quint64 hz = b.hz;
        connect(btn, &QPushButton::clicked, this, [this, hz] {
            QMetaObject::invokeMethod(m_core, "setFrequency", Qt::QueuedConnection,
                                      Q_ARG(quint64, hz)); });
        bands->addWidget(btn, row, col);
        if (++col == 7) { col = 0; ++row; }
    }
    v->addLayout(bands);

    m_pttBtn = new QPushButton(tr("Transmit  (hold, or press space)"));
    m_pttBtn->setMinimumHeight(56);
    m_pttBtn->setStyleSheet("font-size:16px;font-weight:bold;");
    connect(m_pttBtn, &QPushButton::pressed,  this, &ClientWindow::onPttPressed);
    connect(m_pttBtn, &QPushButton::released, this, &ClientWindow::onPttReleased);
    v->addWidget(m_pttBtn);

    m_statsLabel = new QLabel(tr("Offline"));
    m_statsLabel->setStyleSheet("color:#888;");
    v->addWidget(m_statsLabel);

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
    connect(m_inDev,  &QComboBox::currentIndexChanged, this, &ClientWindow::updateRateLabel);
    connect(m_outDev, &QComboBox::currentIndexChanged, this, &ClientWindow::updateRateLabel);

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
    m_outDev->clear();
    for (const auto &d : AudioEngine::outputDevices(api))
        m_outDev->addItem(d.name, d.index);
}

void ClientWindow::updateRateLabel()
{
    if (!m_rateLabel) return;
    const double in  = AudioEngine::probeRate(m_inDev->currentData().toInt(), true);
    const double out = AudioEngine::probeRate(m_outDev->currentData().toInt(), false);

    auto describe = [](double r) {
        if (r <= 0.0) return tr("rejected");
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
    c.password = m_password->text();
    c.encrypt  = m_encrypt->isChecked();
    c.codec    = m_codec->currentData().toString();
    c.bitrate  = m_bitrate->value();
    c.inputDevice  = m_inDev->currentData().toInt();
    c.outputDevice = m_outDev->currentData().toInt();
    c.framesPerBuffer = m_frames->currentData().toInt();
    c.jitterMs = m_jitter->value();
    c.rxGain = float(m_rxGain->value());
    c.txGain = float(m_txGain->value());

    QMetaObject::invokeMethod(m_core, "connectToStation", Qt::QueuedConnection,
                              Q_ARG(rr::ClientConfig, c));
}

void ClientWindow::setConnectedUi(bool up)
{
    m_connected = up;
    m_connectBtn->setText(up ? tr("Disconnect") : tr("Connect"));
    m_pttBtn->setEnabled(up);
    if (!up) {
        m_statsLabel->setText(tr("Offline"));
        m_txLed->setText("RX");
        m_txLed->setStyleSheet(kRxStyle);
    }
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

    const quint64 hz = (st.vfo == "B") ? st.freqB : st.freqA;
    if (st.hasCat && hz > 0) {
        // 14.074.000 -> "14.074.000"
        QString s = QString::number(hz);
        for (int i = s.size() - 3; i > 0; i -= 3) s.insert(i, '.');
        m_freqLabel->setText(s);
        m_modeLabel->setText(tr("%1 · VFO %2 · %3").arg(st.mode, st.vfo, st.rigName));
        const int i = m_mode->findText(st.mode);
        if (i >= 0 && !m_mode->hasFocus()) m_mode->setCurrentIndex(i);
        m_vfoA->setChecked(st.vfo == "A");
        m_vfoB->setChecked(st.vfo == "B");
    } else {
        m_freqLabel->setText(tr("no CAT"));
        m_modeLabel->setText(st.rigName.isEmpty() ? tr("PTT only") : st.rigName);
    }

    m_sMeter->setValue(st.strength);
    const int sUnits = qBound(0, (st.strength + 54) / 6, 9);
    m_sLabel->setText(st.strength > 0 ? QString("S9+%1").arg(st.strength)
                                      : QString("S%1").arg(sUnits));

    m_txLed->setText(st.ptt ? "TX" : "RX");
    m_txLed->setStyleSheet(st.ptt ? kTxStyle : kRxStyle);
}

void ClientWindow::onStats(int rttMs, int lost, int jitterMs, float rxLevel, float txLevel)
{
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
    if (m_ptt == on || !m_connected) return;
    m_ptt = on;
    QMetaObject::invokeMethod(m_core, "setPtt", Qt::QueuedConnection, Q_ARG(bool, on));
    m_txLed->setText(on ? "TX" : "RX");
    m_txLed->setStyleSheet(on ? kTxStyle : kRxStyle);
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
    m_rigctldOn->setChecked(s.value("rigctldOn", false).toBool());
    updateRateLabel();
}

void ClientWindow::saveSettings()
{
    QSettings s("F4JTV", "RemoteRigClient");
    s.setValue("host", m_host->text());
    s.setValue("port", m_port->value());
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
}

} // namespace rr
