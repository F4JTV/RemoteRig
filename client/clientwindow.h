#pragma once

#include <QMainWindow>
#include <QPointer>

#include "levelmeter.h"
#include <QThread>
#include "clientcore.h"
#include "rigctldserver.h"

class QComboBox;
class QLineEdit;
class QVBoxLayout;
class QSpinBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QPlainTextEdit;
class QProgressBar;
class QGridLayout;
class QSlider;

namespace rr {

class ClientWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit ClientWindow(QWidget *parent = nullptr);
    ~ClientWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *e) override;
    void keyReleaseEvent(QKeyEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private slots:
    void onConnectClicked();
    void onConnectionChanged(bool up, const QString &msg);
    void onStateChanged(const rr::RigState &st);
    void onCapsChanged(const rr::RigCaps &caps);
    void onStats(int rttMs, int lost, int jitterMs, float rxLevel, float txLevel,
                 float gainReductionDb, bool rxClipped, bool txClipped);
    void onRetryCountdown(int secondsLeft, int attempt);
    void onPttPressed();
    void onPttReleased();
    void appendLog(const QString &msg);
    void onReceiveOnly(bool on);
    void onRescanDevices();

private:
    QWidget *buildStationPage();
    QWidget *buildAudioPage();
    QWidget *buildDataPage();
    QWidget *buildCwPage();
    QWidget *buildCatPage();
    void addCatMacroRow(const QString &label = QString(),
                        const QString &command = QString());
    void saveCatMacros();
    void setPtt(bool on);
    void setCatEnabled(bool on);
    void rebuildBands(const QList<rr::Band> &bands);
    void promptFrequency();
    void sendMorseText(const QString &text);
    void tuneBy(qint64 delta);
    void loadSettings();
    void saveSettings();
    void refreshDevices();
    void updateRateLabel();
    void pushSpeechSettings();
    void applySpeechPreset(int index);
    rr::SpeechSettings currentSpeech() const;
    void setConnectedUi(bool up);

    QThread     m_netThread;
    ClientCore *m_core = nullptr;
    RigctldServer *m_rigctld = nullptr;
    bool m_connected = false;
    bool m_ptt = false;
    bool m_retrying = false;
    bool m_rxOnly = false;
    RigState m_state;
    // QPointer plutot que QWidget* : un bouton detruit avec sa macro
    // laissait un pointeur mort dans la liste, et le premier changement
    // d'etat de connexion le dereferençait.
    QList<QPointer<QWidget>> m_catWidgets;

    // connexion
    QLineEdit *m_host = nullptr;
    QSpinBox  *m_port = nullptr;
    QSpinBox  *m_udpPort = nullptr;
    QLineEdit *m_password = nullptr;
    QCheckBox *m_encrypt = nullptr;
    QCheckBox *m_autoReconnect = nullptr;
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
    QComboBox *m_filter = nullptr;
    QString m_filterKey;
    QComboBox *m_step = nullptr;
    QProgressBar *m_sMeter = nullptr;
    QLabel *m_sLabel = nullptr;
    QWidget *m_swrRow = nullptr;
    QProgressBar *m_swrMeter = nullptr;
    QLabel *m_swrLabel = nullptr;
    QPushButton *m_pttBtn = nullptr;
    QPushButton *m_tuneBtn = nullptr;
    QWidget   *m_cwPage = nullptr;
    QLineEdit *m_myCall = nullptr;
    QLineEdit *m_cwText = nullptr;
    QLineEdit *m_catText = nullptr;
    QPlainTextEdit *m_catLog = nullptr;
    // Macros CAT : une ligne par macro, ajoutee et retiree a la volee.
    QVBoxLayout *m_catMacroBox = nullptr;
    struct CatMacro { QLineEdit *label; QLineEdit *command; QWidget *row;
                      QPushButton *send; };
    QList<CatMacro> m_catMacros;
    QSpinBox  *m_wpm = nullptr;
    QList<QLineEdit *> m_cwMacros;
    QGridLayout *m_bandGrid = nullptr;
    QList<QPushButton *> m_bandButtons;
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
    LevelMeter *m_rxMeter = nullptr;
    LevelMeter *m_txMeter = nullptr;

    // mise en forme de la modulation
    QComboBox *m_speechPreset = nullptr;
    QComboBox *m_speechHp = nullptr;
    QDoubleSpinBox *m_speechPresence = nullptr;
    QComboBox *m_speechLp = nullptr;
    QComboBox *m_speechComp = nullptr;
    QProgressBar *m_compMeter = nullptr;
    bool m_applyingPreset = false;

    // numerique
    QCheckBox *m_rigctldOn = nullptr;
    QSpinBox  *m_rigctldPort = nullptr;
    QCheckBox *m_rigctldAny = nullptr;

    QPlainTextEdit *m_log = nullptr;
};

} // namespace rr
