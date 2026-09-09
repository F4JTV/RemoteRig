#pragma once

#include <QMainWindow>
#include <QThread>
#include "clientcore.h"
#include "rigctldserver.h"

class QComboBox;
class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QSlider;

namespace rr {

class ClientWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ClientWindow(QWidget *parent = nullptr);
    ~ClientWindow() override;

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onConnectClicked();
    void onConnectionChanged(bool up, const QString &msg);
    void onStateChanged(const rr::RigState &st);
    void onStats(int rttMs, int lost, int jitterMs, float rxLevel, float txLevel);
    void onPttPressed();
    void onPttReleased();
    void appendLog(const QString &msg);

private:
    QWidget *buildStationPage();
    QWidget *buildAudioPage();
    QWidget *buildDataPage();
    void setPtt(bool on);
    void setCatEnabled(bool on);
    void tuneBy(qint64 delta);
    void loadSettings();
    void saveSettings();
    void refreshDevices();
    void updateRateLabel();
    void setConnectedUi(bool up);

    QThread     m_netThread;
    ClientCore *m_core = nullptr;
    RigctldServer *m_rigctld = nullptr;
    bool m_connected = false;
    bool m_ptt = false;
    RigState m_state;
    QList<QWidget *> m_catWidgets;   // désactivés quand la station n'a pas de CAT

    // connexion
    QLineEdit *m_host = nullptr;
    QSpinBox  *m_port = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_encrypt = nullptr;
    QComboBox *m_codec = nullptr;
    QSpinBox  *m_bitrate = nullptr;
    QPushButton *m_connectBtn = nullptr;

    // station
    QLabel *m_freqLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_txLed = nullptr;
    QPushButton *m_vfoA = nullptr;
    QPushButton *m_vfoB = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_step = nullptr;
    QProgressBar *m_sMeter = nullptr;
    QLabel *m_sLabel = nullptr;
    QPushButton *m_pttBtn = nullptr;
    QLabel *m_statsLabel = nullptr;

    // audio
    QComboBox *m_hostApi = nullptr;
    QLabel    *m_rateLabel = nullptr;
    QComboBox *m_inDev = nullptr;
    QComboBox *m_outDev = nullptr;
    QComboBox *m_frames = nullptr;
    QSpinBox  *m_jitter = nullptr;
    QDoubleSpinBox *m_rxGain = nullptr;
    QDoubleSpinBox *m_txGain = nullptr;
    QProgressBar *m_rxMeter = nullptr;
    QProgressBar *m_txMeter = nullptr;

    // numerique
    QCheckBox *m_rigctldOn = nullptr;
    QSpinBox  *m_rigctldPort = nullptr;
    QCheckBox *m_rigctldAny = nullptr;

    QPlainTextEdit *m_log = nullptr;
};

} // namespace rr
