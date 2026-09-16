// Serveur sans interface graphique.
//
// Une station deportee n'a aucune raison de faire tourner un bureau. Ce mode
// lit la configuration deja etablie par l'interface, ouvre le poste et les
// flux, et journalise sur la sortie standard, ou systemd la ramasse.
#pragma once

#include <QObject>
#include <QThread>
#include "servercore.h"
#include "rigcontroller.h"

namespace rr {

class ServerDaemon : public QObject {
    Q_OBJECT
public:
    explicit ServerDaemon(QObject *parent = nullptr);
    ~ServerDaemon() override;

    // configPath vide : les reglages ecrits par l'interface. Sinon un fichier
    // INI, pour faire tourner plusieurs stations sur la meme machine.
    bool start(const QString &configPath, bool verbose);
    void stop();

    // Le gestionnaire de signal ne fait que lever ce drapeau, qu'un minuteur
    // relit : rien d'autre n'est sur d'appeler depuis un signal.
    static void requestStop();

private slots:
    void onLog(const QString &message);
    void onStarted(bool ok, const QString &message);
    void onRigOpened(bool ok, const QString &message);
    void onClientChanged(const QString &peer, bool connected,
                         bool encrypted, const QString &codec);
    void onRigState(const rr::RigState &state);
    void checkStopRequest();

private:
    void report(const QString &line) const;
    void reportAddresses(quint16 tcpPort) const;

    QThread        m_netThread;
    QThread        m_rigThread;
    ServerCore    *m_core = nullptr;
    RigController *m_rig  = nullptr;
    bool     m_verbose = false;
    bool     m_lastPtt = false;
    QString  m_lastRigLine;
};

} // namespace rr
