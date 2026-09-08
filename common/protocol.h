// RemoteRig - protocole commun client/serveur
// F4JTV - licence MIT
#pragma once

#include <cstdint>
#include <QByteArray>
#include <QJsonObject>
#include <QMetaType>

namespace rr {

// ---------------------------------------------------------------- constantes
constexpr uint32_t kMagic       = 0x50545252u;  // 'RRTP'
constexpr uint8_t  kVersion     = 1;
constexpr int      kSampleRate  = 48000;
constexpr int      kChannels    = 1;
constexpr int      kFrameSamples= 480;          // 10 ms @ 48 kHz
constexpr int      kFrameBytes  = kFrameSamples * 2;
constexpr int      kMaxPayload  = 4096;
constexpr int      kPttTokenLen = 8;

// ------------------------------------------------------------------- entetes
enum PktType : uint8_t {
    PKT_AUDIO     = 0,   // charge utile = audio encode
    PKT_PTT       = 1,   // charge utile = token(8) + etat(1)
    PKT_KEEPALIVE = 2,   // charge utile = vide (maintien NAT)
};

enum Codec : uint8_t {
    CODEC_PCM16 = 0,
    CODEC_OPUS  = 1,
};

enum Flags : uint8_t {
    FLAG_ENCRYPTED = 0x01,
    FLAG_TX        = 0x02,   // client -> serveur (audio micro)
    FLAG_DATA      = 0x04,   // liaison en mode donnees (pas de traitement)
};

#pragma pack(push, 1)
struct PktHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  type;
    uint8_t  codec;
    uint8_t  flags;
    uint32_t session;
    uint32_t seq;
    uint32_t timestamp;   // en echantillons
    uint16_t length;      // octets de charge utile
    uint16_t reserved;
};
#pragma pack(pop)
static_assert(sizeof(PktHeader) == 24, "PktHeader doit faire 24 octets");

// ------------------------------------------------------------- etat du poste
struct RigState {
    bool     connected   = false;
    bool     hasCat      = false;
    bool     ptt         = false;
    quint64  freqA       = 0;
    quint64  freqB       = 0;
    QString  vfo         = QStringLiteral("A");
    QString  mode        = QStringLiteral("USB");
    int      passband    = 0;
    int      strength    = -54;   // dB par rapport a S9
    QString  rigName;
    QString  error;

    QJsonObject toJson() const;
    static RigState fromJson(const QJsonObject &o);
};

// -------------------------------------------------------------- construction
// Construit un datagramme complet. `key` vide => pas de chiffrement.
QByteArray buildPacket(PktType type, Codec codec, uint8_t flags,
                       uint32_t session, uint32_t seq, uint32_t timestamp,
                       const QByteArray &payload, const QByteArray &key);

// Decode un datagramme. Renvoie false si invalide.
bool parsePacket(const QByteArray &datagram, const QByteArray &key,
                 PktHeader *hdrOut, QByteArray *payloadOut);

// Trame TCP : longueur (4 octets BE) + JSON compact, chiffre + MAC si `key`.
QByteArray frameJson(const QJsonObject &obj, const QByteArray &key, uint64_t counter);
bool       parseJsonFrame(const QByteArray &frame, const QByteArray &key,
                          uint64_t counter, QJsonObject *out);


} // namespace rr

Q_DECLARE_METATYPE(rr::RigState)
