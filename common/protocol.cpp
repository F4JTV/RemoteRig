#include "protocol.h"
#include "chacha20.h"

#include <QJsonDocument>
#include <QMessageAuthenticationCode>
#include <QtEndian>
#include <cstring>

namespace rr {

QJsonObject RigState::toJson() const
{
    QJsonObject o;
    o["connected"] = connected;
    o["hasCat"]    = hasCat;
    o["ptt"]       = ptt;
    o["freqA"]     = double(freqA);
    o["freqB"]     = double(freqB);
    o["vfo"]       = vfo;
    o["mode"]      = mode;
    o["passband"]  = passband;
    o["strength"]  = strength;
    o["rigName"]   = rigName;
    if (!error.isEmpty()) o["error"] = error;
    return o;
}

RigState RigState::fromJson(const QJsonObject &o)
{
    RigState s;
    s.connected = o["connected"].toBool();
    s.hasCat    = o["hasCat"].toBool();
    s.ptt       = o["ptt"].toBool();
    s.freqA     = quint64(o["freqA"].toDouble());
    s.freqB     = quint64(o["freqB"].toDouble());
    s.vfo       = o["vfo"].toString(QStringLiteral("A"));
    s.mode      = o["mode"].toString(QStringLiteral("USB"));
    s.passband  = o["passband"].toInt();
    s.strength  = o["strength"].toInt(-54);
    s.rigName   = o["rigName"].toString();
    s.error     = o["error"].toString();
    return s;
}

// Nonce deterministe : 4 octets session | 4 octets seq | 4 octets (type<<8|flags)
static void makeNonce(uint8_t n[12], uint32_t session, uint32_t seq,
                      uint8_t type, uint8_t flags)
{
    qToLittleEndian<quint32>(session, n);
    qToLittleEndian<quint32>(seq, n + 4);
    qToLittleEndian<quint32>(quint32(type) << 8 | flags, n + 8);
}

QByteArray buildPacket(PktType type, Codec codec, uint8_t flags,
                       uint32_t session, uint32_t seq, uint32_t timestamp,
                       const QByteArray &payload, const QByteArray &key)
{
    const bool enc = !key.isEmpty();
    if (enc) flags |= FLAG_ENCRYPTED;

    PktHeader h{};
    h.magic     = kMagic;
    h.version   = kVersion;
    h.type      = uint8_t(type);
    h.codec     = uint8_t(codec);
    h.flags     = flags;
    h.session   = session;
    h.seq       = seq;
    h.timestamp = timestamp;
    h.length    = uint16_t(payload.size());

    QByteArray out;
    out.resize(int(sizeof(PktHeader)) + payload.size());
    std::memcpy(out.data(), &h, sizeof(PktHeader));
    std::memcpy(out.data() + sizeof(PktHeader), payload.constData(), size_t(payload.size()));

    if (enc) {
        uint8_t nonce[12];
        makeNonce(nonce, session, seq, uint8_t(type), flags);
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(out.data()) + sizeof(PktHeader),
                            size_t(payload.size()));
    }
    return out;
}

bool parsePacket(const QByteArray &datagram, const QByteArray &key,
                 PktHeader *hdrOut, QByteArray *payloadOut)
{
    if (datagram.size() < int(sizeof(PktHeader))) return false;

    PktHeader h{};
    std::memcpy(&h, datagram.constData(), sizeof(PktHeader));
    if (h.magic != kMagic || h.version != kVersion) return false;
    if (h.length > kMaxPayload) return false;
    if (datagram.size() < int(sizeof(PktHeader)) + int(h.length)) return false;

    QByteArray payload = datagram.mid(int(sizeof(PktHeader)), int(h.length));

    if (h.flags & FLAG_ENCRYPTED) {
        if (key.isEmpty()) return false;
        uint8_t nonce[12];
        makeNonce(nonce, h.session, h.seq, h.type, h.flags);
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(payload.data()), size_t(payload.size()));
    }

    if (hdrOut)     *hdrOut = h;
    if (payloadOut) *payloadOut = payload;
    return true;
}

// ------------------------------------------------------------- canal TCP
QByteArray frameJson(const QJsonObject &obj, const QByteArray &key, uint64_t counter)
{
    QByteArray body = QJsonDocument(obj).toJson(QJsonDocument::Compact);

    if (!key.isEmpty()) {
        uint8_t nonce[12] = {0};
        qToLittleEndian<quint64>(counter, nonce);
        nonce[11] = 0xC7;   // domaine "controle"
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(body.data()), size_t(body.size()));
        QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
        mac.addData(body);
        body.append(mac.result().left(16));
    }

    QByteArray out;
    out.resize(4);
    qToBigEndian<quint32>(quint32(body.size()), reinterpret_cast<uchar *>(out.data()));
    out.append(body);
    return out;
}

bool parseJsonFrame(const QByteArray &frame, const QByteArray &key,
                    uint64_t counter, QJsonObject *out)
{
    QByteArray body = frame;

    if (!key.isEmpty()) {
        if (body.size() < 17) return false;
        QByteArray tag = body.right(16);
        body.chop(16);
        QMessageAuthenticationCode mac(QCryptographicHash::Sha256, key);
        mac.addData(body);
        if (mac.result().left(16) != tag) return false;

        uint8_t nonce[12] = {0};
        qToLittleEndian<quint64>(counter, nonce);
        nonce[11] = 0xC7;
        ChaCha20::xorBuffer(reinterpret_cast<const uint8_t *>(key.constData()), nonce, 1,
                            reinterpret_cast<uint8_t *>(body.data()), size_t(body.size()));
    }

    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    if (out) *out = doc.object();
    return true;
}

} // namespace rr
