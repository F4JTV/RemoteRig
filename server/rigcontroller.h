// Pilotage du poste : Hamlib pour le CAT complet, ou simple bascule
// RTS/DTR sur port serie pour les postes sans CAT.
// Tout le dialogue serie se fait dans son propre thread : le CAT ne doit
// jamais bloquer le chemin audio.
#pragma once

#include <QObject>
#include <QMutex>
#include <QString>
#include <QStringList>
#include "../common/protocol.h"

class QTimer;
class QSerialPort;

namespace rr {

struct RigConfig {
    enum Backend { Hamlib, SerialPttOnly, None };

    Backend backend      = SerialPttOnly;
    int     hamlibModel  = 0;              // numero de modele Hamlib
    QString catPort;                       // ex. COM4 ou /dev/ttyUSB0
    int     catBaud      = 38400;
    QString pttPort;                       // vide = meme port que le CAT
    QString pttType      = QStringLiteral("RTS");  // CAT | RTS | DTR | NONE
    int     pollMs       = 200;
    int     pttTailMs    = 120;            // maintien du PTT apres la fin de l'audio
    bool    dtrOnAlways  = false;          // alimentation d'interfaces type Digirig
};

class RigController : public QObject {
    Q_OBJECT
public:
    explicit RigController(QObject *parent = nullptr);
    ~RigController() override;

    static QList<QPair<int, QString>> hamlibModels();   // (modele, "Marque Type")
    static QStringList serialPorts();
    // Chemin que Hamlib attend : « /dev/ttyUSB0 » sous Unix, « COM3 » sous
    // Windows. Un nom nu comme « ttyUSB0 » serait pris pour un nom d'hote.
    static QString serialDevicePath(const QString &portName);
    static QStringList supportedModes();
    static bool hamlibAvailable();

    RigState state() const;
    RigCaps caps() const;

public slots:
    // Le type doit etre qualifie : moc enregistre la signature telle qu'elle
    // est ecrite, et invokeMethod par nom compare ces chaines sur Qt 6.4.
    // Sans « rr:: », l'appel echoue silencieusement sur Ubuntu 24.04.
    void open(const rr::RigConfig &cfg);
    void close();
    void setPtt(bool on);
    void setFrequency(quint64 hz);
    void setMode(const QString &mode, int passband);
    void setVfo(const QString &vfo);
    void poll();

public slots:
    void startTune();
    void sendMorse(const QString &text);
    void stopMorse();
    void setKeySpeed(int wpm);

signals:
    void stateChanged(const rr::RigState &st);
    void capsChanged(const rr::RigCaps &caps);
    void logMessage(const QString &msg);
    void opened(bool ok, const QString &message);

private:
    bool openHamlib();
    bool openSerialPtt();
    void applySerialPtt(bool on);
    void emitState();

    RigConfig    m_cfg;
    RigState     m_state;
    RigCaps      m_caps;
    mutable QMutex m_mutex;
    QTimer      *m_timer  = nullptr;

    int          m_wpm = 20;   // memorisee pour information
    int          m_readFailures = 0;  // lectures consecutives en echec
    QSerialPort *m_serial = nullptr;
    void        *m_rig    = nullptr;   // RIG* de Hamlib
    bool         m_pttWanted = false;
};

} // namespace rr

Q_DECLARE_METATYPE(rr::RigConfig)
