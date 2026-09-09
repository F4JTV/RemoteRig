# RemoteRig

Remote amateur radio station. A server runs next to the transceiver, a client
runs wherever you are. Two-way audio, PTT, and full CAT control on radios that
support it.

C++17 / Qt6 Widgets / PortAudio / Opus / Hamlib. Same code on Windows and Linux.

*Version française : [README.fr.md](README.fr.md)*

---

## Architecture

```
       CLIENT                                        SERVER
  ┌──────────────────┐                        ┌────────────────────┐
  │ mic / virtual    │──── UDP TX audio ─────▶│ sound card output  │──▶ radio MIC
  │ cable            │                        │                    │
  │                  │◀─── UDP RX audio ──────│ sound card input   │◀── radio AF
  │ rigctld :4532    │                        │                    │
  │  ↑ WSJT-X, VARA  │◀─── TCP control ──────▶│ Hamlib / RTS / DTR │──▶ CAT + PTT
  └──────────────────┘   (framed JSON)        └────────────────────┘
```

Two separate channels, for one specific reason: audio must never wait for a TCP
retransmission. PTT goes out on both at once — three UDP copies for immediacy, a
TCP command for certainty. The server acts on whichever arrives first.

Each program uses three threads: user interface, network (time-critical
priority), and on the server a third one dedicated to serial dialogue, so a slow
CAT radio never delays audio.

## Interface language

Both programs are bilingual English/French. On first launch the language follows
the system: French when the locale starts with `fr`, English otherwise. The
**Language** menu in each window forces one or the other; the choice is
remembered and the program offers to restart to apply it.

Source strings are English, the French translation lives in
`i18n/remoterig_fr.ts`, and the compiled `.qm` is embedded in the executables —
nothing to install alongside the binary.

To reword something:

```bash
lupdate -locations none -no-obsolete common server client -ts i18n/remoterig_fr.ts
linguist i18n/remoterig_fr.ts      # or any text editor
```

CMake recompiles the `.qm` on its own when the Linguist tools are present. When
they are missing, the bundled `.qm` is used as is and the build still succeeds.
Adding a third language takes two lines: a `remoterig_xx.ts` file and an entry
in `i18n/translations.qrc`.

## Sample rate

The protocol is fixed at 48 kHz mono: the only rate Opus accepts natively, the
default for PulseAudio and PipeWire, and what WSJT-X, fldigi and VARA expect.
Adding 44.1 kHz to the protocol would mean resampling twice for nothing.

The tricky part is not that choice, it is that a sound card may refuse 48 kHz —
typically a USB card pinned by the Windows sound panel, or an ALSA card without
plug. The audio engine handles that at the boundary:

1. `Pa_IsFormatSupported` tries 48 kHz, then the card's native rate, then
   44.1 / 96 / 32 / 24 / 16 / 8 kHz.
2. On Windows with WASAPI, `paWinWasapiAutoConvert` is armed: Windows converts
   on its own and 48 kHz almost always goes through on the first try.
3. If the card still imposes something else, a rational polyphase resampler sits
   in the callback. Blackman-windowed sinc, 16 taps per phase (2,560 total for
   44.1 → 48 kHz), unity gain within 0.1 %, aliasing below -50 dB. It costs a few
   microseconds per frame and adds no perceptible latency.

The Audio tab shows the negotiated rate for each device and whether resampling
is happening. An audio interface selector (WASAPI, MME, DirectSound, ALSA,
JACK…) lets you force the backend: on Windows, prefer WASAPI, the only one that
holds 240-sample buffers.

## Latency budget

| Stage | Opus 10 ms | 16-bit PCM |
|---|---|---|
| Capture (480-sample buffer) | 10 ms | 10 ms |
| Encoding | ~2.5 ms | 0 |
| LAN network | 1–3 ms | 1–3 ms |
| Jitter buffer | 40 ms (10–300 adjustable) | same |
| Playback | 10 ms | 10 ms |
| **Total mouth to ear** | **~65 ms** | **~62 ms** |

On a local network, dropping the jitter buffer to 20 ms and the sound card
buffer to 240 samples brings the whole thing near 35 ms. Over the Internet,
raise the jitter buffer until the lost-frame counter stops climbing.

Bandwidth: Opus 48 kbit/s for voice, PCM 768 kbit/s. PCM only makes sense on a
local network or for data modes.

## Choosing the codec

Opus in `RESTRICTED_LOWDELAY` is excellent for voice and poor on narrow tones:
FT8, PSK31 and VARA lose decodes. The client's selector switches both ends on
the fly, without dropping the link. Simple rule: **Opus for voice, PCM for data
modes.**

## Security

- Challenge/response authentication: PBKDF2-HMAC-SHA256 (60,000 rounds) then an
  HMAC over two nonces. The password never travels.
- Optional ChaCha20 encryption on both channels, truncated MAC on control.
  Self-contained implementation, no TLS dependency to install.
- PTT datagrams carry a token derived from the session: a third party cannot key
  the transmitter, even knowing the ports.

On a local network or VPN tunnel, leave encryption off: it costs a few tens of
microseconds per frame. Exposed to the Internet, turn it on at both ends, and on
the server tick "reject clients that do not encrypt".

## Radios without CAT

Pick "Serial port — PTT only": the program opens the port solely to toggle RTS
or DTR. The client then greys out everything CAT would be needed for — bands,
modes, VFO, tuning steps, S-meter — and shows "no CAT" instead of the frequency.
Audio and PTT work normally. The "keep DTR asserted" option powers interfaces that draw
current from the line, such as Digirig.

## Data modes

The client publishes a **rigctld** interface on 127.0.0.1:4532.

1. Data modes tab: tick "publish a rigctld interface".
2. Audio tab: pick the virtual cable on both sides (VB-Audio Cable on Windows;
   `pactl load-module module-null-sink` or a PipeWire node on Linux).
3. In WSJT-X / fldigi / JS8Call: radio "Hamlib NET rigctl", 127.0.0.1:4532,
   PTT "CAT". Audio: the other end of the virtual cable.
4. Switch the codec to 16-bit PCM.

VARA is set up the same way: PTT through rigctld, audio through the cable.

---

# Building on Debian and derivatives

Tested on Ubuntu 24.04 (Qt 6.4.2, Hamlib 4.5.5). Debian 12 "bookworm" and later,
Ubuntu 22.04 and later, Linux Mint and Raspberry Pi OS all carry the same
package names.

### 1. Dependencies

```bash
sudo apt update
sudo apt install build-essential cmake git \
  qt6-base-dev qt6-serialport-dev \
  portaudio19-dev libopus-dev libhamlib-dev
```

Optional, to regenerate translations from the `.ts` file:

```bash
sudo apt install qt6-tools-dev qt6-l10n-tools
```

Without them the build still works and uses the bundled `.qm`.

| Package | Why |
|---|---|
| `qt6-base-dev` | Core, Gui, Widgets, Network |
| `qt6-serialport-dev` | RTS/DTR PTT on radios without CAT |
| `portaudio19-dev` | audio capture and playback |
| `libopus-dev` | low-latency voice codec (optional) |
| `libhamlib-dev` | CAT control (optional) |

### 2. Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Two executables land in `build/`:

```bash
./build/remoterig-server    # on the radio side
./build/remoterig-client    # on the operator side
```

### 3. Serial port permissions

Your user must belong to the group that owns the serial port, otherwise Hamlib
cannot open it:

```bash
sudo usermod -a -G dialout $USER      # plugdev on some distributions
```

Log out and back in for it to take effect.

### 4. Optional system-wide install

```bash
sudo cmake --install build            # into /usr/local/bin
```

### Turning off an optional dependency

```bash
cmake -B build -DWITH_HAMLIB=OFF      # serial PTT only
cmake -B build -DWITH_OPUS=OFF        # 16-bit PCM only
```

---

# Building on Windows

Two things to know before starting:

- **vcpkg provides PortAudio and Opus, but not Hamlib for MSVC.** The `hamlib`
  port declares `"supports": "!windows | mingw"`. Hamlib comes from the official
  Windows binary package instead, and an import library has to be generated from
  the supplied `.def` file. It takes one command.
- The client needs neither Hamlib nor a serial port. If you only build the
  client, skip step 4 entirely.

### 1. Build tools

Install **Visual Studio 2026** or **2022** (Community edition is fine) or,
lighter, the matching **Build Tools**, ticking the *Desktop development with
C++* workload. That brings in the MSVC compiler, the Windows SDK, CMake and
`lib.exe`.

Both work. Two things to know if you go with the 2026 Build Tools:

- The `Visual Studio 18 2026` CMake generator **requires CMake 4.2 or later**.
  The CMake bundled with VS 2026 is recent enough; a separately installed older
  CMake is not. If `cmake -B build` complains about the generator, either use
  the bundled CMake, or add `-G Ninja` from the Developer Command Prompt, which
  works with any CMake version because it picks `cl.exe` up from the
  environment.
- **Use a recent vcpkg checkout.** Older clones detect Visual Studio through
  `vswhere` and do not know about version 18. `git pull` then
  `bootstrap-vcpkg.bat` is enough.

Qt does not need to match: MSVC 14.51, the default toolset of VS 2026, keeps
binary compatibility with everything built since Visual Studio 2015, so the
`msvc2022_64` Qt packages link fine under VS 2026.

Also install **Git for Windows** (<https://git-scm.com/download/win>), needed by
vcpkg.

### 2. Qt 6

Download the Qt online installer from <https://www.qt.io/download-qt-installer>.
A free account is required. In the component selector pick, under the latest
Qt 6 version:

- **MSVC 2022 64-bit**
- **Qt Serial Port** (in *Additional Libraries*)

Note the installation path, for example `C:\Qt\6.8.1\msvc2022_64`.

### 3. vcpkg, PortAudio and Opus

Open a **Developer Command Prompt for VS 2022** (Start menu) and run:

```bat
cd C:\
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat
vcpkg install portaudio:x64-windows opus:x64-windows
```

The first build takes several minutes: vcpkg compiles both libraries from
source. The result lands in `C:\vcpkg\installed\x64-windows`.

PortAudio from vcpkg is built with WASAPI, DirectSound and MME support, which is
what you want — WASAPI is the only backend that holds small buffers.

### 4. Hamlib (server only)

Download `hamlib-w64-4.7.2.zip` from
<https://github.com/Hamlib/Hamlib/releases> and extract it to `C:\hamlib`, so
that `C:\hamlib\include\hamlib\rig.h` exists.

The package is cross-compiled with MinGW and ships no MSVC import library, only
the `.def` file needed to build one. In the **x64 Native Tools Command Prompt
for VS 2022**:

```bat
cd C:\hamlib\lib\msvc
lib /def:libhamlib-4.def /machine:x64 /out:hamlib.lib
```

That produces `C:\hamlib\lib\msvc\hamlib.lib`. The code has been checked against
the headers of both Hamlib 4.5.5 and 4.7.2.

Nothing else to install. `hamlib/rig.h` includes `<pthread.h>` unconditionally,
which MSVC does not ship — Hamlib's own comment suggests fetching the NuGet
pthreads package. That is not needed here: `compat/msvc/pthread.h` supplies the
only two types the headers refer to, `pthread_t` and `pthread_mutex_t`, with the
exact widths winpthreads uses. Both are members of `struct rig_state`, so the
widths matter: they were verified by compiling the Hamlib headers twice under
MinGW, once against real winpthreads and once against the shim. Both give
`struct rig_state` at 31,424 bytes and `struct s_rig` at 47,752 bytes, so the
layout matches the official DLL exactly. CMake adds that directory only when
building with MSVC.

### 5. Everything at once

Once steps 1 to 4 are done, `build_all.bat` at the project root chains the whole
thing: configure, compile, deploy the runtime libraries, and build the
installer. Open the three paths at the top of the file and set them to match
your machine:

```bat
set "QT_DIR=C:\Qt\6.11.2\msvc2022_64"
set "VCPKG_ROOT=C:\vcpkg"
set "HAMLIB_DIR=C:\hamlib"
```

Then, from a **x64 Native Tools Command Prompt**, in the project root:

```bat
build_all.bat
```

Options: `/clean` wipes the build directory first, `/nobuild` only redeploys and
repackages, `/noinstaller` stops after staging.

The script checks its prerequisites before doing anything and says which one is
missing. Hamlib is treated as optional: without it, it builds the client alone
and tells you so, and if only the import library is missing it prints the `lib
/def:` command to run.

The steps below describe the same thing by hand, should you need to intervene at
one particular point.

### 6. Configure and build

Still in the **x64 Native Tools Command Prompt**, from the project directory:

```bat
cmake -B build ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.1/msvc2022_64 ^
  -DHAMLIB_INCLUDE_DIR=C:/hamlib/include ^
  -DHAMLIB_LIBRARY=C:/hamlib/lib/msvc/hamlib.lib

cmake --build build --config Release
```

Adjust the Qt path to your version. Forward slashes work everywhere in CMake and
avoid backslash-escaping problems.

Building the client alone, without Hamlib:

```bat
cmake -B build ^
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DCMAKE_PREFIX_PATH=C:/Qt/6.8.1/msvc2022_64 ^
  -DWITH_HAMLIB=OFF
cmake --build build --config Release --target remoterig-client
```

### 7. Collect the DLLs

The executables land in `build\Release\`. They need the Qt, vcpkg and Hamlib
runtime libraries next to them:

```bat
C:\Qt\6.8.1\msvc2022_64\bin\windeployqt.exe build\Release\remoterig-client.exe
C:\Qt\6.8.1\msvc2022_64\bin\windeployqt.exe build\Release\remoterig-server.exe

copy C:\vcpkg\installed\x64-windows\bin\portaudio.dll build\Release\
copy C:\vcpkg\installed\x64-windows\bin\opus.dll      build\Release\

rem server only
copy C:\hamlib\bin\libhamlib-4.dll      build\Release\
copy C:\hamlib\bin\libusb-1.0.dll       build\Release\
copy C:\hamlib\bin\libgcc_s_seh-1.dll   build\Release\
copy C:\hamlib\bin\libwinpthread-1.dll  build\Release\
```

The last three come from the MinGW build of Hamlib and are required by
`libhamlib-4.dll`. Forgetting them produces a "the code execution cannot
proceed" dialog on startup.

That folder is then self-contained and can be copied to another machine.

### 8. Building the installer by hand

`installer\RemoteRig.iss` produces a bilingual English/French installer that
lets the user pick both programs, the server only, or the client only.

Install **Inno Setup 6** (<https://jrsoftware.org/isdl.php>). `build_all.bat`
finds it on its own in the usual locations; to invoke it directly, gather the
files into `installer\dist` first — that is exactly what `build_all.bat
/nobuild /noinstaller` does — then:

```bat
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\RemoteRig.iss
```

The installer lands in `installer\output\RemoteRig-1.0.0-setup.exe`. It offers:

- a language dialog at startup, then English or French throughout;
- four install types — both programs, server only, client only, custom;
- optional desktop shortcuts, one per program;
- an optional firewall rule opening TCP 7300 and UDP 7301, offered only when the
  server is selected, and removed on uninstall.

Unticking both programs is refused rather than silently installing runtime
libraries and nothing else.

### Virtual audio cable

For data modes, install **VB-Audio Virtual Cable**
(<https://vb-audio.com/Cable/>). It creates a "CABLE Input" playback device and
a "CABLE Output" recording device. In RemoteRig pick "CABLE Input" as the
monitor output; in WSJT-X pick "CABLE Output" as the input, and the reverse for
the transmit path.

---

## Ports to open

| Port | Protocol | Purpose |
|---|---|---|
| 7300 | TCP | control, status, CAT commands |
| 7301 | UDP | audio and PTT |
| 4532 | TCP | rigctld, **local only** by default |

The server's Network tab lists this machine's IPv4 addresses with the control
port already appended, so you can read off what the remote operator should type
instead of digging through `ipconfig`.

The client learns the return address from the first datagram received: a single
NAT to traverse, on the server side. Keepalives go out once a second.

## Level setting

Start at 1.0 on both sides. On the server, raise the RX gain until the meter
swings to 60–70 % on an average signal. On the client, set the transmit level so
the radio's ALC barely moves — the final control is still the radio's mic gain.

## Source layout

```
RemoteRig/
├── CMakeLists.txt
├── build_all.bat     one-shot Windows build: compile, deploy, package
├── LICENSE.txt
├── common/           protocol, crypto, codec, audio engine, resampler, i18n
├── compat/msvc/      pthread.h shim, MSVC only
├── server/           Hamlib control, network core, window, appicon.rc
├── client/           network core, rigctld interface, window, appicon.rc
├── icons/            application icons (.png and .ico)
├── i18n/             remoterig_fr.ts, remoterig_fr.qm, translations.qrc
└── installer/        Inno Setup script
```

## State of the code

Builds and links without a warning, `-Wall -Wextra` included, on Ubuntu 24.04
with Qt 6.4.2, Hamlib 4.5.5, PortAudio 19 and Opus. Both executables start and hold their event loop under
French and English locales. `server/rigcontroller.cpp` also compiles cleanly
against the Hamlib 4.7.2 headers, and the 21 Hamlib symbols it uses are all
exported by the MSVC `.def` file of the official Windows package.

The resampler is verified in isolation: exact sample counts, unity gain, and a
10 kHz tone decimated to 16 kHz comes out at -53 dB. All 147 translatable
strings are translated and checked at runtime.

Nothing has been compiled on Windows yet, nor run against real radio hardware.
Three things to check on the first try:

- The Windows build has been carried through by a user on Visual Studio Build
  Tools 2022 (MSVC 14.44) with Qt 6.11.2. Two problems came up and are fixed:
  `M_PI`, which MSVC does not define unless `_USE_MATH_DEFINES` comes first, and
  the missing `<pthread.h>` pulled in by the Hamlib headers.

- The `\dump_state` reply of the rigctld interface is deliberately generic. It
  covers 30 kHz – 470 MHz and every mode; some Hamlib versions may want extra
  fields. If WSJT-X refuses the connection, compare with the output of
  `rigctld -m 2` from your version.
- `rig_set_conf` is used rather than direct access to the fields of the `RIG`
  structure, because those fields move between Hamlib 4.x releases. Access to
  `rig->caps` for the radio name stays direct; it is still a plain member in
  4.7.2, but Hamlib exposes a `RIGCAPS_NOT_CONST` switch that suggests it may
  change.

---

F4JTV — MIT licence.
