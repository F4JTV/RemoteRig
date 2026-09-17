#pragma once

#include <QMainWindow>
#include <QThread>
#include "servercore.h"
#include "rigcontroller.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QProgressBar;

namespace rr {

class ServerWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ServerWindow(QWidget *parent = nullptr);
    ~ServerWindow() override;

protected:
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onStartStop();
    void onBackendChanged();
    void onRigState(const rr::RigState &st);
    void onServerStarted(bool ok, const QString &msg);
    void onClientChanged(const QString &peer, bool connected, bool encrypted, const QString &codec);
    void onStats(int rttMs, int lost, float rxLevel, float txLevel);
    void appendLog(const QString &msg);
    void onRescanDevices();

private:
    QWidget *buildRigPage();
    QWidget *buildAudioPage();
    QWidget *buildNetworkPage();
    void loadSettings();
    void saveSettings();
    void refreshDevices();
    void refreshLocalAddresses();
    void updateRateLabel();
    void setRunning(bool running);

    // threads
    QThread        m_netThread;
    QThread        m_rigThread;
    ServerCore    *m_core = nullptr;
    RigController *m_rig  = nullptr;
    bool           m_running = false;

    // poste
    QComboBox *m_backend  = nullptr;
    QLineEdit *m_modelFilter = nullptr;
    QComboBox *m_model    = nullptr;
    QComboBox *m_catPort  = nullptr;
    QComboBox *m_catBaud  = nullptr;
    QComboBox *m_pttType  = nullptr;
    QComboBox *m_pttPort  = nullptr;
    QSpinBox  *m_pollMs   = nullptr;
    QCheckBox *m_dtrAlways = nullptr;

    // audio
    QComboBox *m_hostApi = nullptr;
    QLabel    *m_rateLabel = nullptr;
    QComboBox *m_inDev  = nullptr;
    QComboBox *m_outDev = nullptr;
    QComboBox *m_frames = nullptr;
    QDoubleSpinBox *m_rxGain = nullptr;
    QDoubleSpinBox *m_txGain = nullptr;
    QSpinBox  *m_tailMs = nullptr;
    QProgressBar *m_rxMeter = nullptr;
    QProgressBar *m_txMeter = nullptr;

    // reseau
    QSpinBox  *m_tcpPort = nullptr;
    QSpinBox  *m_udpPort = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_forceEnc = nullptr;
    QCheckBox *m_bandEdges = nullptr;
    QLabel    *m_catPortLabel = nullptr;
    QLineEdit *m_cm108Path = nullptr;
    QSpinBox  *m_cm108Gpio = nullptr;
    QCheckBox *m_pttTone = nullptr;
    QSpinBox  *m_pttToneHz = nullptr;
    QLabel    *m_addrLabel = nullptr;

    // etat
    QPushButton *m_startBtn = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_rigLabel = nullptr;
    QLabel *m_clientLabel = nullptr;
    QLabel *m_txLed = nullptr;
    QPlainTextEdit *m_log = nullptr;

    QList<QPair<int, QString>> m_allModels;
};

} // namespace rr
