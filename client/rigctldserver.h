// Serveur compatible rigctld, ecoute en local (127.0.0.1:4532).
// WSJT-X, fldigi, JS8Call ou VARA s'y connectent en "Hamlib NET rigctl"
// et pilotent le poste distant : frequence, mode et PTT.
#pragma once

#include <QObject>
#include <QTcpServer>
#include "../common/protocol.h"

namespace rr {

class RigctldServer : public QObject {
    Q_OBJECT
public:
    explicit RigctldServer(QObject *parent = nullptr);

    bool start(quint16 port = 4532, bool localOnly = true);
    void stop();
    bool running() const;
    quint16 port() const { return m_port; }

public slots:
    void updateState(const rr::RigState &st);

signals:
    void requestFrequency(quint64 hz);
    void requestMode(const QString &mode, int passband);
    void requestVfo(const QString &vfo);
    void requestPtt(bool on);
    void logMessage(const QString &msg);

private:
    QByteArray handleLine(const QString &line);
    static QString dumpState();

    QTcpServer m_server;
    RigState   m_state;
    quint16    m_port = 4532;
};

} // namespace rr
