#include "rigctldserver.h"

#include <QTcpSocket>
#include <QRegularExpression>

namespace rr {

RigctldServer::RigctldServer(QObject *parent) : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, [this] {
        QTcpSocket *s = m_server.nextPendingConnection();
        if (!s) return;
        s->setSocketOption(QAbstractSocket::LowDelayOption, 1);
        auto *buf = new QByteArray;

        connect(s, &QTcpSocket::readyRead, this, [this, s, buf] {
            buf->append(s->readAll());
            int nl;
            while ((nl = buf->indexOf('\n')) >= 0) {
                const QString line = QString::fromLatin1(buf->left(nl)).trimmed();
                buf->remove(0, nl + 1);
                if (line == "q" || line == "Q") { s->disconnectFromHost(); return; }
                const QByteArray reply = handleLine(line);
                if (!reply.isEmpty()) s->write(reply);
            }
        });
        connect(s, &QTcpSocket::disconnected, this, [s, buf] {
            delete buf;
            s->deleteLater();
        });
        emit logMessage(tr("Local application connected to the rigctld port"));
    });
}

bool RigctldServer::start(quint16 port, bool localOnly)
{
    stop();
    m_port = port;
    const bool ok = m_server.listen(localOnly ? QHostAddress::LocalHost : QHostAddress::Any, port);
    emit logMessage(ok ? tr("rigctld interface listening on %1:%2")
                             .arg(localOnly ? "127.0.0.1" : "0.0.0.0").arg(port)
                       : tr("rigctld port %1 unavailable").arg(port));
    return ok;
}

void RigctldServer::stop() { if (m_server.isListening()) m_server.close(); }
bool RigctldServer::running() const { return m_server.isListening(); }

void RigctldServer::updateState(const RigState &st) { m_state = st; }

QByteArray RigctldServer::handleLine(const QString &raw)
{
    // Hamlib peut préfixer la commande par un séparateur en mode étendu ;
    // on répond en format court, accepté par WSJT-X et fldigi.
    QString line = raw;
    if (!line.isEmpty() && (line[0] == '+' || line[0] == ';' || line[0] == '|' || line[0] == ','))
        line = line.mid(1);
    line = line.trimmed();
    if (line.isEmpty()) return {};

    const QStringList a = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const QString cmd = a.value(0);
    const quint64 freq = (m_state.vfo == "B") ? m_state.freqB : m_state.freqA;

    if (cmd == "\\dump_state")  return dumpState().toLatin1();
    if (cmd == "\\chk_vfo")     return "CHKVFO 0\n";
    if (cmd == "\\get_powerstat" || cmd == "get_powerstat") return "1\n";

    if (cmd == "f" || cmd == "get_freq")
        return QByteArray::number(qulonglong(freq)) + "\n";

    if (cmd == "F" || cmd == "set_freq") {
        const quint64 hz = quint64(a.value(1).toDouble());
        if (hz > 0) emit requestFrequency(hz);
        return "RPRT 0\n";
    }

    if (cmd == "m" || cmd == "get_mode") {
        const int pb = m_state.passband > 0 ? m_state.passband : 2400;
        return (m_state.mode + "\n" + QString::number(pb) + "\n").toLatin1();
    }

    if (cmd == "M" || cmd == "set_mode") {
        const QString mode = a.value(1);
        const int pb = a.value(2).toInt();
        if (!mode.isEmpty()) emit requestMode(mode, pb);
        return "RPRT 0\n";
    }

    if (cmd == "t" || cmd == "get_ptt")
        return m_state.ptt ? "1\n" : "0\n";

    if (cmd == "T" || cmd == "set_ptt") {
        emit requestPtt(a.value(1).toInt() != 0);
        return "RPRT 0\n";
    }

    if (cmd == "v" || cmd == "get_vfo")
        return (m_state.vfo == "B" ? "VFOB\n" : "VFOA\n");

    if (cmd == "V" || cmd == "set_vfo") {
        emit requestVfo(a.value(1).contains("B") ? "B" : "A");
        return "RPRT 0\n";
    }

    if (cmd == "s" || cmd == "get_split_vfo")
        return "0\nVFOA\n";

    if (cmd == "S" || cmd == "set_split_vfo")
        return "RPRT 0\n";

    if (cmd == "l" || cmd == "get_level") {
        if (a.value(1) == "STRENGTH") return QByteArray::number(m_state.strength) + "\n";
        return "0\n";
    }

    if (cmd == "L" || cmd == "set_level" || cmd == "U" || cmd == "set_func")
        return "RPRT 0\n";

    return "RPRT -11\n";   // RIG_ENAVAIL
}

QString RigctldServer::dumpState()
{
    // Description volontairement générique : toutes bandes, tous modes.
    // WSJT-X et fldigi n'utilisent que la version de protocole et les listes.
    const QString modes = "0xdfffffff";
    QString s;
    s += "0\n";      // version du protocole
    s += "2\n";      // modèle (NET rigctl)
    s += "1\n";      // région ITU
    // rx_range : debut fin modes low_power high_power vfo ant
    s += "30000.000000 470000000.000000 " + modes + " -1 -1 0x3 0x3\n";
    s += "0 0 0 0 0 0 0\n";
    // tx_range
    s += "30000.000000 470000000.000000 " + modes + " 1000 100000 0x3 0x3\n";
    s += "0 0 0 0 0 0 0\n";
    // pas d'accord : modes pas
    s += modes + " 1\n";
    s += modes + " 10\n";
    s += modes + " 100\n";
    s += "0 0\n";
    // filtres : modes largeur
    s += "0x2 2400\n";      // SSB
    s += "0x1 500\n";       // CW
    s += "0x20 6000\n";     // AM
    s += "0x40 15000\n";    // FM
    s += "0 0\n";
    s += "0\n";             // max_rit
    s += "0\n";             // max_xit
    s += "0\n";             // max_ifshift
    s += "0\n";             // announces
    s += "0\n";             // preamplis
    s += "0\n";             // attenuateurs
    s += "0x0\n";           // has_get_func
    s += "0x0\n";           // has_set_func
    s += "0x40000000\n";    // has_get_level : STRENGTH
    s += "0x0\n";           // has_set_level
    s += "0x0\n";           // has_get_parm
    s += "0x0\n";           // has_set_parm
    return s;
}

} // namespace rr
