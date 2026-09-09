# RemoteRig

Station radio déportée : un serveur tourne à côté du poste, un client tourne
là où vous êtes. Audio bidirectionnel, PTT, et pilotage CAT complet quand le
poste le permet.

C++17 / Qt6 Widgets / PortAudio / Opus / Hamlib. Windows et Linux, même code.

*English version: [README.md](README.md)*

---

## Architecture

```
       CLIENT                                        SERVEUR
  ┌──────────────────┐                        ┌────────────────────┐
  │ micro / câble    │──── UDP audio TX ─────▶│ sortie carte son   │──▶ MIC du poste
  │ virtuel          │                        │                    │
  │                  │◀─── UDP audio RX ──────│ entrée carte son   │◀── AF du poste
  │ rigctld :4532    │                        │                    │
  │  ↑ WSJT-X, VARA  │◀─── TCP contrôle ─────▶│ Hamlib / RTS / DTR │──▶ CAT + PTT
  └──────────────────┘   (JSON encadré)       └────────────────────┘
```

Deux canaux séparés, pour une raison précise : l'audio ne doit jamais attendre
une retransmission TCP. Le PTT part sur les deux à la fois — trois copies UDP
pour l'immédiateté, une commande TCP pour la certitude. Le serveur applique la
première qui arrive.

Chaque programme utilise trois threads : interface, réseau (priorité temps
critique), et pour le serveur un troisième dédié au dialogue série, pour qu'un
poste CAT lent ne retarde jamais l'audio.

## Langue de l'interface

Les deux programmes sont bilingues anglais/français. Au premier lancement, la
langue suit celle du système : française si la locale commence par `fr`,
anglaise sinon. Le menu **Langue** de chaque fenêtre permet de forcer l'une ou
l'autre ; le choix est mémorisé et le programme propose de redémarrer pour
l'appliquer.

Les chaînes sources sont en anglais, la traduction française vit dans
`i18n/remoterig_fr.ts` et le `.qm` compilé est embarqué dans les exécutables :
rien à installer à côté du binaire.

Pour retoucher une formulation :

```bash
lupdate -locations none -no-obsolete common server client -ts i18n/remoterig_fr.ts
linguist i18n/remoterig_fr.ts      # ou un éditeur de texte
```

CMake recompile le `.qm` tout seul si les outils Linguist sont présents. S'ils
manquent, le `.qm` livré avec les sources est utilisé tel quel et la compilation
aboutit quand même. Ajouter une troisième langue tient en deux lignes : un
`remoterig_xx.ts`, une entrée dans `i18n/translations.qrc`.

## Débit d'échantillonnage

Le protocole est figé à 48 kHz mono : c'est le seul débit qu'Opus accepte
nativement, celui de PulseAudio et PipeWire par défaut, et celui qu'attendent
WSJT-X, fldigi et VARA. Ajouter 44,1 kHz au protocole reviendrait à
rééchantillonner deux fois pour rien.

Le point délicat n'est pas ce choix, c'est que la carte son peut refuser le
48 kHz — typiquement une carte USB figée par le panneau son de Windows, ou une
carte ALSA sans plug. Le moteur audio traite ce cas à la frontière :

1. `Pa_IsFormatSupported` teste le 48 kHz, puis le débit natif de la carte,
   puis 44,1 / 96 / 32 / 24 / 16 / 8 kHz.
2. Sous Windows en WASAPI, `paWinWasapiAutoConvert` est armé : Windows convertit
   lui-même et le 48 kHz passe presque toujours du premier coup.
3. Si la carte impose malgré tout autre chose, un rééchantillonneur polyphase
   rationnel s'intercale dans le callback. Sinc fenêtré Blackman, 16 coefficients
   par phase (2 560 au total pour 44,1 → 48 kHz), gain unité à 0,1 % près,
   repliement sous -50 dB. Il coûte quelques microsecondes par trame et n'ajoute
   aucune latence perceptible.

L'onglet Audio affiche le débit négocié pour chaque périphérique et indique s'il
y a rééchantillonnage. Un sélecteur d'interface audio (WASAPI, MME, DirectSound,
ALSA, JACK…) permet de forcer le backend : sous Windows, préférez WASAPI, seul
capable de tenir des tampons de 240 échantillons.

## Budget de latence

| Étage | Opus 10 ms | PCM 16 bits |
|---|---|---|
| Capture (tampon 480 éch.) | 10 ms | 10 ms |
| Encodage | ~2,5 ms | 0 |
| Réseau LAN | 1–3 ms | 1–3 ms |
| Tampon de gigue | 40 ms (réglable 10–300) | idem |
| Restitution | 10 ms | 10 ms |
| **Total bouche-à-oreille** | **~65 ms** | **~62 ms** |

Sur réseau local, descendre le tampon de gigue à 20 ms et le tampon carte son à
240 échantillons ramène l'ensemble autour de 35 ms. Sur Internet, remontez le
tampon de gigue jusqu'à ce que le compteur de trames perdues cesse de grimper.

Débit : Opus 48 kbit/s en phonie, PCM 768 kbit/s. Le PCM ne se justifie qu'en
réseau local ou en numérique.

## Choix du codec

Opus en `RESTRICTED_LOWDELAY` est excellent en phonie et médiocre sur les
tonalités étroites : FT8, PSK31 et VARA perdent des décodages. Le sélecteur du
client bascule les deux extrémités à chaud, sans couper la liaison. Règle
simple : **Opus en phonie, PCM en numérique.**

## Sécurité

- Authentification défi/réponse : PBKDF2-HMAC-SHA256 (60 000 tours) puis HMAC
  sur deux aléas. Le mot de passe ne circule jamais.
- Chiffrement facultatif ChaCha20 sur les deux canaux, MAC tronqué sur le
  contrôle. Implémentation autonome, aucune dépendance TLS à installer.
- Les datagrammes PTT portent un jeton dérivé de la session : un tiers ne peut
  pas mettre le poste en émission, même en connaissant les ports.

Sur réseau local ou tunnel VPN, laissez le chiffrement décoché : il coûte
quelques dizaines de microsecondes par trame. Exposé sur Internet, cochez-le des
deux côtés, et cochez côté serveur « refuser les clients qui ne chiffrent pas ».

## Postes sans CAT

Choisissez « Port série — PTT seul » : le programme n'ouvre le port que pour
basculer RTS ou DTR. Le client grise alors tout ce qui exigerait le CAT —
bandes, modes, VFO, pas d'accord, S-mètre — et affiche « pas de CAT » à la place
de la fréquence. L'audio et le PTT fonctionnent normalement. L'option
« maintenir DTR actif » alimente les interfaces qui tirent leur courant de la
ligne, comme les Digirig.

## Modes numériques

Le client publie une interface **rigctld** sur 127.0.0.1:4532.

1. Onglet Numérique : cochez « publier une interface rigctld ».
2. Onglet Audio : choisissez le câble virtuel des deux côtés
   (VB-Audio Cable sous Windows ; `pactl load-module module-null-sink` ou un
   nœud PipeWire sous Linux).
3. Dans WSJT-X / fldigi / JS8Call : radio « Hamlib NET rigctl », 127.0.0.1:4532,
   PTT « CAT ». Audio : l'autre extrémité du câble virtuel.
4. Passez le codec sur PCM 16 bits.

VARA se configure pareil : PTT par rigctld, audio par le câble.

---

# Compilation sous Debian et dérivés

Testé sous Ubuntu 24.04 (Qt 6.4.2, Hamlib 4.5.5). Debian 12 « bookworm » et
suivantes, Ubuntu 22.04 et suivantes, Linux Mint et Raspberry Pi OS portent les
mêmes noms de paquets.

### 1. Dépendances

```bash
sudo apt update
sudo apt install build-essential cmake git \
  qt6-base-dev qt6-serialport-dev \
  portaudio19-dev libopus-dev libhamlib-dev
```

Facultatif, pour régénérer les traductions depuis le `.ts` :

```bash
sudo apt install qt6-tools-dev qt6-l10n-tools
```

Sans eux la compilation fonctionne quand même et utilise le `.qm` livré.

| Paquet | À quoi il sert |
|---|---|
| `qt6-base-dev` | Core, Gui, Widgets, Network |
| `qt6-serialport-dev` | PTT RTS/DTR pour les postes sans CAT |
| `portaudio19-dev` | capture et restitution audio |
| `libopus-dev` | codec voix faible latence (facultatif) |
| `libhamlib-dev` | pilotage CAT (facultatif) |

### 2. Compilation

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Les deux exécutables sortent dans `build/` :

```bash
./build/remoterig-server    # côté poste
./build/remoterig-client    # côté opérateur
```

### 3. Droits sur le port série

Votre utilisateur doit appartenir au groupe propriétaire du port série, sinon
Hamlib ne pourra pas l'ouvrir :

```bash
sudo usermod -a -G dialout $USER      # plugdev sur certaines distributions
```

Il faut se déconnecter et se reconnecter pour que ça prenne effet.

### 4. Installation système facultative

```bash
sudo cmake --install build            # dans /usr/local/bin
```

### Désactiver une dépendance facultative

```bash
cmake -B build -DWITH_HAMLIB=OFF      # PTT série uniquement
cmake -B build -DWITH_OPUS=OFF        # PCM 16 bits uniquement
```

---

# Compilation sous Windows

Deux points à connaître avant de commencer :

- **vcpkg fournit PortAudio et Opus, mais pas Hamlib pour MSVC.** Le port
  `hamlib` déclare `"supports": "!windows | mingw"`. Hamlib vient donc du paquet
  binaire Windows officiel, et il faut fabriquer une bibliothèque d'import à
  partir du fichier `.def` fourni. C'est une seule commande.
- Le client n'a besoin ni de Hamlib ni d'un port série. Si vous ne compilez que
  le client, sautez entièrement l'étape 4.

### 1. Outils de compilation

Installez **Visual Studio 2026** ou **2022** (l'édition Community suffit) ou,
plus léger, les **Build Tools** correspondants, en cochant la charge de travail
*Développement Desktop en C++*. Cela apporte le compilateur MSVC, le SDK
Windows, CMake et `lib.exe`.

Les deux conviennent. Deux points à connaître si vous partez sur les Build
Tools 2026 :

- Le générateur CMake `Visual Studio 18 2026` **exige CMake 4.2 ou plus
  récent**. Le CMake livré avec VS 2026 l'est ; un CMake installé séparément et
  plus ancien ne l'est pas. Si `cmake -B build` proteste sur le générateur,
  utilisez celui fourni, ou ajoutez `-G Ninja` depuis la Developer Command
  Prompt : Ninja fonctionne avec n'importe quelle version de CMake puisqu'il
  reprend `cl.exe` dans l'environnement.
- **Prenez un vcpkg récent.** Les clones anciens détectent Visual Studio via
  `vswhere` et ignorent la version 18. Un `git pull` suivi de
  `bootstrap-vcpkg.bat` suffit.

Qt n'a pas besoin de correspondre : MSVC 14.51, le toolset par défaut de
VS 2026, conserve la compatibilité binaire avec tout ce qui est compilé depuis
Visual Studio 2015. Les paquets Qt `msvc2022_64` se lient donc sans problème
sous VS 2026.

Installez aussi **Git pour Windows** (<https://git-scm.com/download/win>),
nécessaire à vcpkg.

### 2. Qt 6

Téléchargez l'installateur en ligne de Qt sur
<https://www.qt.io/download-qt-installer>. Un compte gratuit est demandé. Dans
le sélecteur de composants, sous la dernière version de Qt 6, cochez :

- **MSVC 2022 64-bit**
- **Qt Serial Port** (dans *Additional Libraries*)

Notez le chemin d'installation, par exemple `C:\Qt\6.8.1\msvc2022_64`.

### 3. vcpkg, PortAudio et Opus

Ouvrez une **Developer Command Prompt for VS 2022** (menu Démarrer) :

```bat
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
vcpkg install portaudio:x64-windows opus:x64-windows
```

La première compilation prend plusieurs minutes : vcpkg construit les deux
bibliothèques depuis les sources. Le résultat atterrit dans
`C:\vcpkg\installed\x64-windows`.

Le PortAudio de vcpkg est compilé avec WASAPI, DirectSound et MME, ce qui est
exactement ce qu'il faut — WASAPI est le seul backend qui tienne de petits
tampons.

### 4. Hamlib (serveur seulement)

Téléchargez `hamlib-w64-4.7.2.zip` depuis
<https://github.com/Hamlib/Hamlib/releases> et décompressez-le dans
`C:\hamlib`, de sorte que `C:\hamlib\include\hamlib\rig.h` existe.

Le paquet est compilé en croisé avec MinGW et ne contient aucune bibliothèque
d'import MSVC, seulement le fichier `.def` qui permet de la fabriquer. Dans la
**x64 Native Tools Command Prompt for VS 2022** :

```bat
cd C:\hamlib\lib\msvc
lib /def:libhamlib-4.def /machine:x64 /out:hamlib.lib
```

Vous obtenez `C:\hamlib\lib\msvc\hamlib.lib`. Le code a été vérifié contre les
en-têtes de Hamlib 4.5.5 et 4.7.2.

Rien d'autre à installer. `hamlib/rig.h` inclut `<pthread.h>` sans condition, ce
que MSVC ne fournit pas — le commentaire de Hamlib lui-même suggère d'aller
chercher le paquet NuGet pthreads. Ce n'est pas nécessaire ici :
`compat/msvc/pthread.h` fournit les deux seuls types auxquels les en-têtes font
référence, `pthread_t` et `pthread_mutex_t`, aux largeurs exactes de
winpthreads. Ces deux types sont membres de `struct rig_state`, donc les
largeurs comptent : elles ont été vérifiées en compilant deux fois les en-têtes
Hamlib sous MinGW, une fois contre le vrai winpthreads et une fois contre le
shim. Les deux donnent `struct rig_state` à 31 424 octets et `struct s_rig` à
47 752 octets : la disposition est identique à celle de la DLL officielle. CMake
n'ajoute ce dossier que lors d'une compilation MSVC.

### 5. Tout d'un coup

Une fois les étapes 1 à 4 faites, `build_all.bat` à la racine du projet enchaîne
l'ensemble : configuration, compilation, déploiement des bibliothèques
d'exécution et fabrication de l'installateur. Ouvrez-le et ajustez les trois
chemins en tête du fichier :

```bat
set "QT_DIR=C:\Qt\6.11.2\msvc2022_64"
set "VCPKG_ROOT=C:\vcpkg"
set "HAMLIB_DIR=C:\hamlib"
```

Puis, depuis une **x64 Native Tools Command Prompt**, à la racine du projet :

```bat
build_all.bat
```

Options : `/clean` efface d'abord le dossier de compilation, `/nobuild` se
contente de redéployer et réempaqueter, `/noinstaller` s'arrête après la
préparation.

Le script vérifie ses prérequis avant d'agir et indique celui qui manque. Hamlib
est traité comme facultatif : sans lui, il compile le client seul et le signale,
et si seule la bibliothèque d'import manque, il affiche la commande `lib /def:`
à lancer.

Les étapes ci-dessous décrivent la même chose à la main, si vous avez besoin
d'intervenir à un endroit précis.

### 6. Configuration et compilation

Toujours dans la **x64 Native Tools Command Prompt**, depuis le dossier du
projet :

```bat
cmake -B build ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.1/msvc2022_64 ^
  -DHAMLIB_INCLUDE_DIR=C:/hamlib/include ^
  -DHAMLIB_LIBRARY=C:/hamlib/lib/msvc/hamlib.lib

cmake --build build --config Release
```

Adaptez le chemin de Qt à votre version. Les barres obliques normales
fonctionnent partout dans CMake et évitent les problèmes d'échappement.

Pour ne compiler que le client, sans Hamlib :

```bat
cmake -B build ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.1/msvc2022_64 ^
  -DWITH_HAMLIB=OFF
cmake --build build --config Release --target remoterig-client
```

### 7. Rassembler les DLL

Les exécutables sortent dans `build\Release\`. Il leur faut les bibliothèques
d'exécution de Qt, de vcpkg et de Hamlib à côté d'eux :

```bat
C:\Qt\6.8.1\msvc2022_64\bin\windeployqt.exe build\Release\remoterig-client.exe
C:\Qt\6.8.1\msvc2022_64\bin\windeployqt.exe build\Release\remoterig-server.exe

copy C:\vcpkg\installed\x64-windows\bin\portaudio.dll build\Release\
copy C:\vcpkg\installed\x64-windows\bin\opus.dll      build\Release\

rem serveur uniquement
copy C:\hamlib\bin\libhamlib-4.dll      build\Release\
copy C:\hamlib\bin\libusb-1.0.dll       build\Release\
copy C:\hamlib\bin\libgcc_s_seh-1.dll   build\Release\
copy C:\hamlib\bin\libwinpthread-1.dll  build\Release\
```

Les trois dernières viennent de la compilation MinGW de Hamlib et sont exigées
par `libhamlib-4.dll`. Les oublier produit au démarrage une boîte de dialogue
« l'exécution du code ne peut pas se poursuivre ».

Le dossier est alors autonome et peut être copié sur une autre machine.

### 8. Fabriquer l'installateur à la main

`installer\RemoteRig.iss` produit un installateur bilingue anglais/français qui
laisse choisir les deux programmes, le serveur seul, ou le client seul.

Installez **Inno Setup 6** (<https://jrsoftware.org/isdl.php>). `build_all.bat`
le trouve tout seul aux emplacements habituels ; pour l'appeler directement, il
faut d'abord rassembler les fichiers dans `installer\dist` — c'est exactement ce
que fait `build_all.bat /nobuild /noinstaller` — puis :

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\RemoteRig.iss
```

L'installateur sort dans `installer\output\RemoteRig-1.0.0-setup.exe`. Il
propose :

- une boîte de dialogue de langue au démarrage, puis tout en anglais ou en
  français ;
- quatre types d'installation — les deux programmes, serveur seul, client seul,
  personnalisé ;
- des raccourcis Bureau facultatifs, un par programme ;
- une règle de pare-feu facultative ouvrant TCP 7300 et UDP 7301, proposée
  seulement si le serveur est retenu, et supprimée à la désinstallation.

Décocher les deux programmes est refusé, plutôt que d'installer silencieusement
des bibliothèques et rien d'autre.

### Câble audio virtuel

Pour les modes numériques, installez **VB-Audio Virtual Cable**
(<https://vb-audio.com/Cable/>). Il crée un périphérique de lecture « CABLE
Input » et un périphérique d'enregistrement « CABLE Output ». Dans RemoteRig,
choisissez « CABLE Input » comme sortie d'écoute ; dans WSJT-X, choisissez
« CABLE Output » comme entrée, et l'inverse pour le chemin d'émission.

---

## Ports à ouvrir

| Port | Protocole | Rôle |
|---|---|---|
| 7300 | TCP | contrôle, état, commandes CAT |
| 7301 | UDP | audio et PTT |
| 4532 | TCP | rigctld, **local uniquement** par défaut |

L'onglet Réseau du serveur liste les adresses IPv4 de la machine, port de
contrôle déjà accolé : vous lisez directement ce que l'opérateur distant doit
saisir, sans passer par `ipconfig`.

Le client apprend l'adresse de retour du premier datagramme reçu : un seul NAT
à traverser, côté serveur. Le maintien de session part toutes les secondes.

## Réglages de niveau

Commencez à 1,0 des deux côtés. Côté serveur, montez le gain RX jusqu'à ce que
le vumètre batte vers 60–70 % sur un signal moyen. Côté client, réglez le niveau
d'émission pour que l'ALC du poste bouge à peine — le contrôle final reste le
gain micro du poste.

## Organisation des sources

```
RemoteRig/
├── CMakeLists.txt
├── build_all.bat     compilation Windows d'un bloc : build, déploiement, paquet
├── LICENSE.txt
├── common/           protocole, crypto, codec, moteur audio, rééchantillonneur, i18n
├── compat/msvc/      shim pthread.h, MSVC uniquement
├── server/           pilotage Hamlib, cœur réseau, fenêtre, appicon.rc
├── client/           cœur réseau, interface rigctld, fenêtre, appicon.rc
├── icons/            icônes des applications (.png et .ico)
├── i18n/             remoterig_fr.ts, remoterig_fr.qm, translations.qrc
└── installer/        script Inno Setup
```

## État du code

Compile et se lie sans le moindre avertissement, `-Wall -Wextra` compris, sous
Ubuntu 24.04 avec Qt 6.4.2, Hamlib 4.5.5, PortAudio 19 et Opus. Les deux exécutables démarrent et tiennent
leur boucle événementielle en locale française comme anglaise.
`server/rigcontroller.cpp` compile également sans erreur contre les en-têtes de
Hamlib 4.7.2, et les 21 symboles Hamlib utilisés sont tous exportés par le
fichier `.def` MSVC du paquet Windows officiel.

Le rééchantillonneur est vérifié isolément : comptes d'échantillons exacts, gain
unité, et un ton à 10 kHz décimé vers 16 kHz ressort à -53 dB. Les 147 chaînes
traduisibles sont toutes traduites et vérifiées à l'exécution.

Rien n'a encore été compilé sous Windows, ni testé sur du matériel radio réel.
Trois points à vérifier lors du premier essai :

- La compilation Windows a été menée par un utilisateur sous Visual Studio Build
  Tools 2022 (MSVC 14.44) avec Qt 6.11.2. Deux problèmes sont apparus et sont
  corrigés : `M_PI`, que MSVC ne définit pas sans `_USE_MATH_DEFINES` préalable,
  et le `<pthread.h>` manquant qu'entraînent les en-têtes de Hamlib.

- La réponse `\dump_state` de l'interface rigctld est volontairement générique.
  Elle couvre 30 kHz – 470 MHz et tous les modes ; certaines versions de Hamlib
  peuvent réclamer des champs supplémentaires. Si WSJT-X refuse la connexion,
  comparez avec la sortie de `rigctld -m 2` de votre version.
- `rig_set_conf` est utilisé plutôt que l'accès direct aux champs de la
  structure `RIG`, parce que ces champs bougent entre les versions 4.x de
  Hamlib. L'accès à `rig->caps` pour le nom du poste reste direct ; c'est encore
  un simple membre en 4.7.2, mais Hamlib expose un commutateur
  `RIGCAPS_NOT_CONST` qui laisse penser que cela pourrait changer.

---

F4JTV — licence MIT.
