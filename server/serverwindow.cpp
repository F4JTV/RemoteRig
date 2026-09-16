#include "serverwindow.h"
#include "../common/about.h"
#include "../common/i18n.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QNetworkInterface>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace rr {

// Sur une liste vide, currentData() est invalide et toInt() renvoie 0 :
// on ouvrirait alors le périphérique numéro 0, qui n'est pas celui voulu.
static int deviceIndexOf(const QComboBox *box)
{
    if (!box) return -1;
    const QVariant v = box->currentData();
    return v.isValid() ? v.toInt() : -1;
}

ServerWindow::ServerWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(tr("RemoteRig — Station server"));
    resize(760, 660);
    addLanguageMenu(this, QStringLiteral("RemoteRigServer"));
    addAboutMenu(this, QStringLiteral("RemoteRig Server"),
                 QStringLiteral(":/icons/remoterig-server.png"),
                 tr("Runs next to the transceiver and shares it over the network: "
                    "two-way audio, PTT, and CAT control through Hamlib or plain "
                    "serial RTS/DTR keying. A client, on the desktop or on a phone, "
                    "then operates the station from anywhere."));

    m_core = new ServerCore;
    m_rig  = new RigController;
    m_core->moveToThread(&m_netThread);
    m_rig->moveToThread(&m_rigThread);

    // Le cœur réseau parle au poste sans jamais bloquer : tout est en file d'attente.
    connect(m_core, &ServerCore::requestPtt,       m_rig, &RigController::setPtt);
    connect(m_core, &ServerCore::requestFrequency, m_rig, &RigController::setFrequency);
    connect(m_core, &ServerCore::requestMode,      m_rig, &RigController::setMode);
    connect(m_core, &ServerCore::requestVfo,       m_rig, &RigController::setVfo);
    connect(m_core, &ServerCore::requestTune,      m_rig, &RigController::startTune);
    connect(m_core, &ServerCore::requestMorse,     m_rig, &RigController::sendMorse);
    connect(m_core, &ServerCore::requestMorseStop, m_rig, &RigController::stopMorse);
    connect(m_core, &ServerCore::requestKeySpeed,  m_rig, &RigController::setKeySpeed);
    connect(m_rig,  &RigController::stateChanged,  m_core, &ServerCore::onRigState);
    connect(m_rig,  &RigController::capsChanged,   m_core, &ServerCore::onRigCaps);

    connect(m_rig,  &RigController::stateChanged,  this, &ServerWindow::onRigState);
    connect(m_rig,  &RigController::logMessage,    this, &ServerWindow::appendLog);
    connect(m_core, &ServerCore::logMessage,       this, &ServerWindow::appendLog);
    connect(m_core, &ServerCore::started,          this, &ServerWindow::onServerStarted);
    connect(m_core, &ServerCore::clientChanged,    this, &ServerWindow::onClientChanged);
    connect(m_core, &ServerCore::statsUpdated,     this, &ServerWindow::onStats);

    m_netThread.start(QThread::TimeCriticalPriority);
    m_rigThread.start();

    auto *central = new QWidget;
    auto *root = new QVBoxLayout(central);

    auto *tabs = new QTabWidget;
    tabs->addTab(buildRigPage(),     tr("Radio"));
    tabs->addTab(buildAudioPage(),   tr("Audio"));
    tabs->addTab(buildNetworkPage(), tr("Network"));
    root->addWidget(tabs);

    // Bandeau d'état
    auto *statusBox = new QGroupBox(tr("Status"));
    auto *sl = new QHBoxLayout(statusBox);
    m_txLed = new QLabel("RX");
    m_txLed->setAlignment(Qt::AlignCenter);
    m_txLed->setMinimumWidth(64);
    m_txLed->setStyleSheet("background:#204020;color:#8f8;font-weight:bold;padding:6px;border-radius:4px;");
    m_statusLabel = new QLabel(tr("Stopped"));
    m_rigLabel    = new QLabel("—");
    m_clientLabel = new QLabel(tr("No client"));
    sl->addWidget(m_txLed);
    sl->addWidget(m_statusLabel, 1);
    sl->addWidget(m_rigLabel, 2);
    sl->addWidget(m_clientLabel, 2);
    root->addWidget(statusBox);

    m_startBtn = new QPushButton(tr("Start server"));
    m_startBtn->setMinimumHeight(38);
    connect(m_startBtn, &QPushButton::clicked, this, &ServerWindow::onStartStop);
    root->addWidget(m_startBtn);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    root->addWidget(m_log, 1);

    setCentralWidget(central);

    AudioEngine::initialiseLibrary();
    refreshDevices();
    loadSettings();
    onBackendChanged();
    refreshLocalAddresses();
    connect(m_tcpPort, &QSpinBox::valueChanged, this, &ServerWindow::refreshLocalAddresses);
}

ServerWindow::~ServerWindow()
{
    QMetaObject::invokeMethod(m_core, "stop", Qt::BlockingQueuedConnection);
    QMetaObject::invokeMethod(m_rig,  "close", Qt::BlockingQueuedConnection);
    m_netThread.quit(); m_netThread.wait(2000);
    m_rigThread.quit(); m_rigThread.wait(2000);
    delete m_core;
    delete m_rig;
    AudioEngine::terminateLibrary();
}

void ServerWindow::closeEvent(QCloseEvent *e) { saveSettings(); e->accept(); }

// ------------------------------------------------------------------ onglets
QWidget *ServerWindow::buildRigPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    m_backend = new QComboBox;
    m_backend->addItem(tr("Hamlib — full CAT"), int(RigConfig::Hamlib));
    m_backend->addItem(tr("Serial port — PTT only"), int(RigConfig::SerialPttOnly));
    m_backend->addItem(tr("None — audio only"),    int(RigConfig::None));
    if (!RigController::hamlibAvailable()) {
        m_backend->setItemData(0, false, Qt::UserRole - 1);
        m_backend->setCurrentIndex(1);
    }
    connect(m_backend, &QComboBox::currentIndexChanged, this, &ServerWindow::onBackendChanged);
    f->addRow(tr("Control"), m_backend);

    m_modelFilter = new QLineEdit;
    m_modelFilter->setPlaceholderText(tr("filter: yaesu, ic-7300, kenwood…"));
    f->addRow(tr("Search"), m_modelFilter);

    m_model = new QComboBox;
    // Hamlib expose plus d'un millier de modèles. Au-delà de quelques dizaines
    // d'entrées, le style natif remplace la liste déroulante par un menu :
    // pas d'ascenseur, et le popup se referme dès qu'il dépasse la hauteur de
    // l'écran. « combobox-popup: 0 » rétablit la vraie liste défilante.
    m_model->setStyleSheet("QComboBox { combobox-popup: 0; }");
    m_model->setMaxVisibleItems(20);
    auto *modelView = new QListView(m_model);
    modelView->setUniformItemSizes(true);          // indispensable à cette taille
    modelView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_model->setView(modelView);
    m_model->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_model->setMinimumContentsLength(34);

    m_allModels = RigController::hamlibModels();
    for (const auto &m : m_allModels)
        m_model->addItem(QString("%1  [%2]").arg(m.second).arg(m.first), m.first);
    connect(m_modelFilter, &QLineEdit::textChanged, this, [this](const QString &t) {
        const int keep = m_model->currentData().toInt();
        const QSignalBlocker block(m_model);
        m_model->clear();
        for (const auto &m : m_allModels)
            if (t.isEmpty() || m.second.contains(t, Qt::CaseInsensitive))
                m_model->addItem(QString("%1  [%2]").arg(m.second).arg(m.first), m.first);
        const int i = m_model->findData(keep);
        if (i >= 0) m_model->setCurrentIndex(i);
    });
    f->addRow(tr("Model"), m_model);

    m_catPort = new QComboBox;
    m_catPort->setEditable(true);
    m_catPort->addItems(RigController::serialPorts());
    f->addRow(tr("CAT port"), m_catPort);

    m_catBaud = new QComboBox;
    m_catBaud->addItems({"1200", "4800", "9600", "19200", "38400", "57600", "115200"});
    m_catBaud->setCurrentText("38400");
    f->addRow(tr("Speed"), m_catBaud);

    m_pttType = new QComboBox;
    m_pttType->addItems({"CAT", "RTS", "DTR", "NONE"});
    f->addRow(tr("PTT type"), m_pttType);

    m_pttPort = new QComboBox;
    m_pttPort->setEditable(true);
    m_pttPort->addItem("");
    m_pttPort->addItems(RigController::serialPorts());
    f->addRow(tr("Separate PTT port"), m_pttPort);

    m_pollMs = new QSpinBox;
    m_pollMs->setRange(50, 2000);
    m_pollMs->setValue(200);
    m_pollMs->setSuffix(" ms");
    f->addRow(tr("CAT polling"), m_pollMs);

    m_dtrAlways = new QCheckBox(tr("Keep DTR asserted (powers Digirig-style interfaces)"));
    f->addRow("", m_dtrAlways);

    return w;
}

QWidget *ServerWindow::buildAudioPage()
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
    f->addRow(tr("Input (audio from the radio)"), m_inDev);
    m_outDev = new QComboBox;
    f->addRow(tr("Output (audio to the radio)"), m_outDev);

    m_rateLabel = new QLabel("—");
    m_rateLabel->setWordWrap(true);
    f->addRow(tr("Negotiated rate"), m_rateLabel);

    auto *rescan = new QPushButton(tr("Look for devices again"));
    rescan->setToolTip(tr("Needed after plugging in a USB sound card: "
                          "the device list is read once at startup."));
    connect(rescan, &QPushButton::clicked, this, &ServerWindow::onRescanDevices);
    f->addRow("", rescan);
    connect(m_inDev,  &QComboBox::currentIndexChanged, this, &ServerWindow::updateRateLabel);
    connect(m_outDev, &QComboBox::currentIndexChanged, this, &ServerWindow::updateRateLabel);

    m_frames = new QComboBox;
    m_frames->addItem(tr("120 samples — 2.5 ms"), 120);
    m_frames->addItem(tr("240 samples — 5 ms"),   240);
    m_frames->addItem(tr("480 samples — 10 ms"),  480);
    m_frames->addItem(tr("960 samples — 20 ms"),  960);
    m_frames->setCurrentIndex(2);
    f->addRow(tr("Sound card buffer"), m_frames);

    m_rxGain = new QDoubleSpinBox;
    m_rxGain->setRange(0.1, 8.0); m_rxGain->setSingleStep(0.1); m_rxGain->setValue(1.0);
    f->addRow(tr("RX gain"), m_rxGain);
    m_txGain = new QDoubleSpinBox;
    m_txGain->setRange(0.1, 8.0); m_txGain->setSingleStep(0.1); m_txGain->setValue(1.0);
    f->addRow(tr("TX gain"), m_txGain);
    connect(m_rxGain, &QDoubleSpinBox::valueChanged, this, [this] {
        QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                                  Q_ARG(float, float(m_rxGain->value())),
                                  Q_ARG(float, float(m_txGain->value())));
    });
    connect(m_txGain, &QDoubleSpinBox::valueChanged, this, [this] {
        QMetaObject::invokeMethod(m_core, "setGains", Qt::QueuedConnection,
                                  Q_ARG(float, float(m_rxGain->value())),
                                  Q_ARG(float, float(m_txGain->value())));
    });

    m_tailMs = new QSpinBox;
    m_tailMs->setRange(0, 800); m_tailMs->setValue(120); m_tailMs->setSuffix(" ms");
    f->addRow(tr("PTT hold after transmit"), m_tailMs);

    m_rxMeter = new QProgressBar; m_rxMeter->setRange(0, 100); m_rxMeter->setTextVisible(false);
    m_txMeter = new QProgressBar; m_txMeter->setRange(0, 100); m_txMeter->setTextVisible(false);
    f->addRow(tr("RX level"), m_rxMeter);
    f->addRow(tr("TX level"), m_txMeter);

    return w;
}

QWidget *ServerWindow::buildNetworkPage()
{
    auto *w = new QWidget;
    auto *f = new QFormLayout(w);

    m_tcpPort = new QSpinBox; m_tcpPort->setRange(1, 65535); m_tcpPort->setValue(7300);
    f->addRow(tr("Control port (TCP)"), m_tcpPort);
    m_udpPort = new QSpinBox; m_udpPort->setRange(1, 65535); m_udpPort->setValue(7301);
    f->addRow(tr("Audio port (UDP)"), m_udpPort);

    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    f->addRow(tr("Password"), m_password);

    m_forceEnc = new QCheckBox(tr("Reject clients that do not encrypt"));
    f->addRow("", m_forceEnc);

    // Garde-fou de bord de bande. Le poste declare ses plages d'emission :
    // autant s'en servir pour refuser le PTT au-dehors.
    m_bandEdges = new QCheckBox(tr("Refuse transmission out of band"));
    m_bandEdges->setChecked(true);
    m_bandEdges->setToolTip(tr("Blocks the PTT when the frequency falls outside the "
                               "transmit ranges the rig declares. Turn this off for a "
                               "transverter, whose working range is not the rig's."));
    f->addRow("", m_bandEdges);

    // L'opérateur distant a besoin de cette adresse : autant la lui donner
    // ici plutôt que de le renvoyer vers ipconfig.
    m_addrLabel = new QLabel;
    m_addrLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_addrLabel->setWordWrap(true);
    m_addrLabel->setStyleSheet("font-family:monospace;");
    f->addRow(tr("This machine"), m_addrLabel);

    auto *refresh = new QPushButton(tr("Refresh addresses"));
    connect(refresh, &QPushButton::clicked, this, &ServerWindow::refreshLocalAddresses);
    f->addRow("", refresh);

    auto *hint = new QLabel(tr(
        "On a local network or a VPN tunnel you can leave encryption off: it saves a "
        "few tens of microseconds per frame.\n"
        "Exposed to the Internet, turn it on and forward both ports to this machine."));
    hint->setWordWrap(true);
    f->addRow(hint);

    return w;
}

// -------------------------------------------------------------------- logique
// Adresses IPv4 utilisables par un client, l'adresse de bouclage exclue.
void ServerWindow::refreshLocalAddresses()
{
    if (!m_addrLabel) return;

    QStringList lines;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp)) continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        const auto entries = iface.addressEntries();
        for (const QNetworkAddressEntry &e : entries) {
            const QHostAddress a = e.ip();
            if (a.protocol() != QAbstractSocket::IPv4Protocol) continue;
            lines << QString("%1:%2   (%3)")
                         .arg(a.toString())
                         .arg(m_tcpPort->value())
                         .arg(iface.humanReadableName());
        }
    }

    m_addrLabel->setText(lines.isEmpty()
        ? tr("No network interface found")
        : lines.join('\n'));
}

void ServerWindow::refreshDevices()
{
    const int api = m_hostApi ? m_hostApi->currentData().toInt() : -1;
    const QSignalBlocker b1(m_inDev), b2(m_outDev);

    m_inDev->clear();
    for (const auto &d : AudioEngine::inputDevices(api))
        m_inDev->addItem(d.name, d.index);
    if (m_inDev->count() == 0)
        m_inDev->addItem(tr("No capture device found"), -1);

    m_outDev->clear();
    for (const auto &d : AudioEngine::outputDevices(api))
        m_outDev->addItem(d.name, d.index);
    if (m_outDev->count() == 0)
        m_outDev->addItem(tr("No playback device found"), -1);
}

void ServerWindow::onRescanDevices()
{
    if (m_running) {
        appendLog(tr("Stop the server first: the device list cannot be reread "
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

void ServerWindow::updateRateLabel()
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
    m_rateLabel->setText(tr("Input: %1  ·  Output: %2").arg(describe(in), describe(out)));
}

void ServerWindow::onBackendChanged()
{
    const auto b = RigConfig::Backend(m_backend->currentData().toInt());
    const bool hamlib = (b == RigConfig::Hamlib);
    const bool serial = (b != RigConfig::None);
    m_model->setEnabled(hamlib);
    m_modelFilter->setEnabled(hamlib);
    m_pollMs->setEnabled(hamlib);
    m_catPort->setEnabled(serial);
    m_catBaud->setEnabled(serial);
    m_pttType->setEnabled(serial);
    m_pttPort->setEnabled(serial);
}

static QString portNameOf(const QString &label)
{
    return label.section("  (", 0, 0).trimmed();
}

void ServerWindow::onStartStop()
{
    if (m_running) {
        QMetaObject::invokeMethod(m_core, "stop", Qt::QueuedConnection);
        QMetaObject::invokeMethod(m_rig,  "close", Qt::QueuedConnection);
        setRunning(false);
        m_statusLabel->setText(tr("Stopped"));
        return;
    }

    RigConfig rc;
    rc.backend     = RigConfig::Backend(m_backend->currentData().toInt());
    rc.hamlibModel = m_model->currentData().toInt();
    rc.catPort     = portNameOf(m_catPort->currentText());
    rc.catBaud     = m_catBaud->currentText().toInt();
    rc.pttPort     = portNameOf(m_pttPort->currentText());
    rc.pttType     = m_pttType->currentText();
    rc.pollMs      = m_pollMs->value();
    rc.dtrOnAlways = m_dtrAlways->isChecked();
    QMetaObject::invokeMethod(m_rig, "open", Qt::QueuedConnection, Q_ARG(rr::RigConfig, rc));

    ServerConfig sc;
    sc.tcpPort  = quint16(m_tcpPort->value());
    sc.udpPort  = quint16(m_udpPort->value());
    sc.password = m_password->text();
    sc.requireEncryption = m_forceEnc->isChecked();
    sc.inputDevice  = deviceIndexOf(m_inDev);
    sc.outputDevice = deviceIndexOf(m_outDev);
    if (sc.inputDevice < 0 || sc.outputDevice < 0) {
        appendLog(tr("The station needs one capture device and one playback device. "
                     "A Raspberry Pi has no analogue input: use a USB sound card."));
        return;
    }
    sc.framesPerBuffer = m_frames->currentData().toInt();
    sc.rxGain = float(m_rxGain->value());
    sc.txGain = float(m_txGain->value());
    sc.pttTailMs = m_tailMs->value();
    sc.enforceBandEdges = m_bandEdges->isChecked();
    QMetaObject::invokeMethod(m_core, "start", Qt::QueuedConnection, Q_ARG(rr::ServerConfig, sc));
}

void ServerWindow::setRunning(bool running)
{
    m_running = running;
    m_startBtn->setText(running ? tr("Stop server") : tr("Start server"));
}

void ServerWindow::onServerStarted(bool ok, const QString &msg)
{
    appendLog(msg);
    setRunning(ok);
    m_statusLabel->setText(ok ? msg : tr("Start refused"));
}

void ServerWindow::onRigState(const RigState &st)
{
    if (st.hasCat) {
        m_rigLabel->setText(QString("%1 · VFO %2 · %3 · %4")
            .arg(st.rigName, st.vfo,
                 QLocale().toString(double((st.vfo == "B" ? st.freqB : st.freqA)) / 1e6, 'f', 5) + " MHz",
                 st.mode));
    } else {
        m_rigLabel->setText(st.rigName.isEmpty() ? "—" : st.rigName);
    }

    m_txLed->setText(st.ptt ? "TX" : "RX");
    m_txLed->setStyleSheet(st.ptt
        ? "background:#802020;color:#fdd;font-weight:bold;padding:6px;border-radius:4px;"
        : "background:#204020;color:#8f8;font-weight:bold;padding:6px;border-radius:4px;");
}

void ServerWindow::onClientChanged(const QString &peer, bool connected, bool encrypted, const QString &codec)
{
    m_clientLabel->setText(connected
        ? tr("%1 · %2 · %3").arg(peer, codec, encrypted ? tr("encrypted") : tr("clear"))
        : tr("No client"));
}

void ServerWindow::onStats(int, int lost, float rxLevel, float txLevel)
{
    m_rxMeter->setValue(int(rxLevel * 100));
    m_txMeter->setValue(int(txLevel * 100));
    Q_UNUSED(lost)
}

void ServerWindow::appendLog(const QString &msg)
{
    m_log->appendPlainText(QDateTime::currentDateTime().toString("HH:mm:ss  ") + msg);
}

// ------------------------------------------------------------------ reglages
void ServerWindow::loadSettings()
{
    QSettings s("F4JTV", "RemoteRigServer");
    const int api = m_hostApi->findText(s.value("hostApi").toString());
    if (api >= 0) { m_hostApi->setCurrentIndex(api); refreshDevices(); }
    m_backend->setCurrentIndex(s.value("backend", m_backend->currentIndex()).toInt());
    const int model = s.value("model", 0).toInt();
    if (model) { const int i = m_model->findData(model); if (i >= 0) m_model->setCurrentIndex(i); }
    m_catPort->setCurrentText(s.value("catPort").toString());
    m_catBaud->setCurrentText(s.value("catBaud", "38400").toString());
    m_pttType->setCurrentText(s.value("pttType", "RTS").toString());
    m_pttPort->setCurrentText(s.value("pttPort").toString());
    m_pollMs->setValue(s.value("pollMs", 200).toInt());
    m_dtrAlways->setChecked(s.value("dtrAlways", false).toBool());
    m_tcpPort->setValue(s.value("tcpPort", 7300).toInt());
    m_udpPort->setValue(s.value("udpPort", 7301).toInt());
    m_password->setText(s.value("password").toString());
    m_forceEnc->setChecked(s.value("forceEnc", false).toBool());
    m_bandEdges->setChecked(s.value("bandEdges", true).toBool());
    m_frames->setCurrentIndex(s.value("framesIdx", 2).toInt());
    m_rxGain->setValue(s.value("rxGain", 1.0).toDouble());
    m_txGain->setValue(s.value("txGain", 1.0).toDouble());
    m_tailMs->setValue(s.value("tailMs", 120).toInt());
    const int in = m_inDev->findText(s.value("inDev").toString());
    if (in >= 0) m_inDev->setCurrentIndex(in);
    const int out = m_outDev->findText(s.value("outDev").toString());
    if (out >= 0) m_outDev->setCurrentIndex(out);
    updateRateLabel();
}

void ServerWindow::saveSettings()
{
    QSettings s("F4JTV", "RemoteRigServer");
    s.setValue("backend", m_backend->currentIndex());
    s.setValue("model", m_model->currentData().toInt());
    s.setValue("catPort", m_catPort->currentText());
    s.setValue("catBaud", m_catBaud->currentText());
    s.setValue("pttType", m_pttType->currentText());
    s.setValue("pttPort", m_pttPort->currentText());
    s.setValue("pollMs", m_pollMs->value());
    s.setValue("dtrAlways", m_dtrAlways->isChecked());
    s.setValue("tcpPort", m_tcpPort->value());
    s.setValue("udpPort", m_udpPort->value());
    s.setValue("password", m_password->text());
    s.setValue("forceEnc", m_forceEnc->isChecked());
    s.setValue("bandEdges", m_bandEdges->isChecked());
    s.setValue("framesIdx", m_frames->currentIndex());
    s.setValue("rxGain", m_rxGain->value());
    s.setValue("txGain", m_txGain->value());
    s.setValue("tailMs", m_tailMs->value());
    s.setValue("inDev", m_inDev->currentText());
    s.setValue("outDev", m_outDev->currentText());
    s.setValue("hostApi", m_hostApi->currentText());
}

} // namespace rr
