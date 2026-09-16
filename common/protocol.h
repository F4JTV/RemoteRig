// RemoteRig - protocole commun client/serveur
// F4JTV - licence MIT
#pragma once

#include <cstdint>
#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QJsonArray>
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

// --------------------------------------------- capacites declarees par le poste
// Plage d'emission telle que Hamlib la rapporte. Le client y taille sa grille
// de bandes, au lieu d'afficher une liste figee qui ne correspond a rien.
struct BandRange {
    quint64 start = 0;
    quint64 end   = 0;
};

// Bande amateur nommee, decoupee dans les plages du poste.
struct Band {
    QString name;
    quint64 start   = 0;
    quint64 end     = 0;
    quint64 preset  = 0;   // frequence proposee par le bouton
};

struct RigCaps {
    bool hasTune  = false;             // le poste accepte un cycle d'accord
    bool hasMorse = false;             // le poste sait manipuler lui-meme
    bool hasSwr   = false;             // le poste rapporte son ROS
    int  wpmMin = 5;
    int  wpmMax = 40;
    QList<BandRange> txRanges;

    QJsonObject toJson() const;
    static RigCaps fromJson(const QJsonObject &o);
};

// Plan de bandes de reference, puis son intersection avec ce que le poste sait
// emettre. Sans plage declaree, la liste complete est rendue telle quelle.
// Spectre reellement emis, deduit de la porteuse, du mode et de la largeur du
// filtre. Le poste rapporte sa porteuse ; en USB l'emission est au-dessus, en
// LSB au-dessous. Une meme frequence peut donc etre dans la bande dans un mode
// et dehors dans l'autre.
struct EmissionSpan {
    quint64 low  = 0;
    quint64 high = 0;
};
EmissionSpan occupiedSpan(quint64 carrierHz, const QString &mode, int passbandHz);

QList<Band> standardBandPlan();
QList<Band> bandsWithin(const QList<BandRange> &ranges);

// ------------------------------------------------------------- etat du poste
struct RigState {
    bool     connected   = false;
    bool     hasCat      = false;
    bool     ptt         = false;
    bool     tuning      = false;   // cycle d'accord en cours
    bool     cw          = false;   // manipulation en cours
    // Rapport d'ondes stationnaires, 0 tant que rien n'a ete mesure.
    // La mesure n'a de sens qu'en emission : la derniere est conservee.
    float    swr         = 0.0f;
    // Faux quand la frequence courante sort des plages d'emission que le
    // poste declare. Le client grise alors le PTT, plutot que de laisser
    // l'operateur appuyer pour rien — ou pire, pour de bon.
    bool     txAllowed   = true;
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

Q_DECLARE_METATYPE(rr::RigCaps)
