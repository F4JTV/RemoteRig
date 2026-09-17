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
    o["tuning"]    = tuning;
    o["cw"]        = cw;
    o["swr"]       = double(swr);
    o["txAllowed"] = txAllowed;
    o["pbWide"]    = pbWide;
    o["pbNormal"]  = pbNormal;
    o["pbNarrow"]  = pbNarrow;
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
    s.tuning    = o["tuning"].toBool();
    s.cw        = o["cw"].toBool();
    s.swr       = float(o["swr"].toDouble());
    s.txAllowed = o.contains("txAllowed") ? o["txAllowed"].toBool() : true;
    s.pbWide    = o["pbWide"].toInt();
    s.pbNormal  = o["pbNormal"].toInt();
    s.pbNarrow  = o["pbNarrow"].toInt();
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

QJsonObject RigCaps::toJson() const
{
    QJsonArray ranges;
    for (const BandRange &r : txRanges)
        ranges.append(QJsonObject{{"s", double(r.start)}, {"e", double(r.end)}});
    return QJsonObject{{"hasTune", hasTune}, {"hasMorse", hasMorse},
                       {"hasSwr", hasSwr}, {"hasVfoSet", hasVfoSet},
                       {"modes", QJsonArray::fromStringList(modes)},
                       {"wpmMin", wpmMin}, {"wpmMax", wpmMax}, {"tx", ranges}};
}

RigCaps RigCaps::fromJson(const QJsonObject &o)
{
    RigCaps c;
    c.hasTune  = o["hasTune"].toBool();
    c.hasMorse = o["hasMorse"].toBool();
    c.hasSwr   = o["hasSwr"].toBool();
    c.hasVfoSet = o.contains("hasVfoSet") ? o["hasVfoSet"].toBool() : true;
    const QJsonArray modes = o["modes"].toArray();
    for (const QJsonValue &v : modes) c.modes << v.toString();

    c.wpmMin   = o.value("wpmMin").toInt(5);
    c.wpmMax   = o.value("wpmMax").toInt(40);
    const QJsonArray ranges = o["tx"].toArray();
    for (const QJsonValue &v : ranges) {
        const QJsonObject r = v.toObject();
        c.txRanges.append({quint64(r["s"].toDouble()), quint64(r["e"].toDouble())});
    }
    return c;
}

EmissionSpan occupiedSpan(quint64 carrierHz, const QString &mode, int passbandHz)
{
    const QString m = mode.toUpper();

    // Largeurs de repli quand le poste ne rapporte pas son filtre.
    int width = passbandHz;
    if (width <= 0) {
        if (m.contains(QLatin1String("CW")))      width = 200;
        else if (m.startsWith(QLatin1String("AM"))) width = 6000;
        else if (m.startsWith(QLatin1String("FM"))) width = 12000;
        else                                        width = 2700;
    }

    EmissionSpan s;

    // Bande laterale superieure : l'emission est au-dessus de la porteuse.
    // PKTUSB, DIGU et RTTYR sont batis sur la meme laterale.
    const bool upper = m.contains(QLatin1String("USB")) || m.contains(QLatin1String("DIGU"))
                       || m == QLatin1String("RTTYR") || m == QLatin1String("PKTUSB");
    const bool lower = m.contains(QLatin1String("LSB")) || m.contains(QLatin1String("DIGL"))
                       || m == QLatin1String("RTTY")  || m == QLatin1String("PKTLSB");

    if (upper) {
        s.low  = carrierHz;
        s.high = carrierHz + quint64(width);
    } else if (lower) {
        s.low  = carrierHz > quint64(width) ? carrierHz - quint64(width) : 0;
        s.high = carrierHz;
    } else if (m.contains(QLatin1String("CW"))) {
        // En telegraphie, le signal est centre sur la frequence affichee.
        const quint64 half = quint64(width) / 2;
        s.low  = carrierHz > half ? carrierHz - half : 0;
        s.high = carrierHz + half;
    } else {
        // AM, FM et le reste : porteuse centree.
        const quint64 half = quint64(width) / 2;
        s.low  = carrierHz > half ? carrierHz - half : 0;
        s.high = carrierHz + half;
    }
    return s;
}

// Bandes amateur, region 1. Les bornes servent a reconnaitre ce que le poste
// sait faire ; la frequence proposee est un point de depart usuel en phonie.
QList<Band> standardBandPlan()
{
    return {
        {QStringLiteral("2200 m"),     135700,     137800,     136000},
        {QStringLiteral("630 m"),      472000,     479000,     474200},
        {QStringLiteral("160 m"),     1810000,    2000000,    1840000},
        {QStringLiteral("80 m"),      3500000,    3800000,    3650000},
        {QStringLiteral("60 m"),      5351500,    5366500,    5354000},
        {QStringLiteral("40 m"),      7000000,    7200000,    7100000},
        {QStringLiteral("30 m"),     10100000,   10150000,   10130000},
        {QStringLiteral("20 m"),     14000000,   14350000,   14200000},
        {QStringLiteral("17 m"),     18068000,   18168000,   18130000},
        {QStringLiteral("15 m"),     21000000,   21450000,   21250000},
        {QStringLiteral("12 m"),     24890000,   24990000,   24950000},
        {QStringLiteral("10 m"),     28000000,   29700000,   28400000},
        {QStringLiteral("6 m"),      50000000,   52000000,   50200000},
        {QStringLiteral("4 m"),      70000000,   70500000,   70200000},
        {QStringLiteral("2 m"),     144000000,  148000000,  145500000},
        {QStringLiteral("70 cm"),   430000000,  440000000,  433500000},
        {QStringLiteral("23 cm"),  1240000000, 1300000000, 1296200000},
    };
}

QList<Band> bandsWithin(const QList<BandRange> &ranges)
{
    const QList<Band> plan = standardBandPlan();
    if (ranges.isEmpty()) return plan;

    QList<Band> kept;
    for (const Band &b : plan) {
        for (const BandRange &r : ranges) {
            const quint64 lo = qMax(b.start, r.start);
            const quint64 hi = qMin(b.end, r.end);
            if (lo >= hi) continue;          // aucun recouvrement

            Band band = b;
            // La frequence proposee doit tomber dans ce que le poste emet.
            band.preset = qBound(lo, b.preset, hi);
            kept.append(band);
            break;
        }
    }
    return kept;
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
