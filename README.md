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

## Microphone shaping

A boom microphone worn close to the mouth suffers from the proximity effect:
everything below 300 Hz is lifted by 10 to 15 dB, and the voice sounds dark and
muffled at the far end while the intelligibility band gets masked. The Audio tab
carries a shaping chain to correct that, on the transmit path, before encoding.

Four presets: **none**, **headset boom microphone**, **desk microphone**, and
**custom**, which exposes the four controls — high-pass, presence, low-pass and
compression — plus a gain reduction meter to set the level by eye.

The headset preset measures as follows, high-pass 300 Hz, presence +6 dB at
2 kHz, low-pass 3.2 kHz:

| Frequency | 80 Hz | 150 Hz | 300 Hz | 600 Hz | 1 kHz | 2 kHz | 3 kHz | 3.5 kHz |
|---|---|---|---|---|---|---|---|---|
| Response | -23 dB | -12 dB | -3 dB | +0.5 dB | +2 dB | **+5.4 dB** | +1 dB | -1 dB |

**Latency is exactly zero.** Only recursive biquads and a compressor with no
lookahead: not a single sample is held back, which an impulse test confirms —
the output starts on the very sample the impulse arrives.

Distortion stays inaudible across the working range, measured on a 1 kHz tone:

| Input | -30 dBFS | -20 dBFS | -12 dBFS | -6 dBFS | -3 dBFS |
|---|---|---|---|---|---|
| Gain reduction | 0 dB | 0 dB | 3.6 dB | 7.6 dB | 9.6 dB |
| THD+N | 0.053 % | 0.018 % | 0.009 % | 0.004 % | 0.006 % |

Two design points keep it that way: the compressor knee is quadratic, so the
slope never breaks when compression starts, and the gain itself is smoothed in
the dB domain with a 5 ms attack and a 150 ms release. Above -1 dBFS a soft
clipper takes over as a last resort, gently — 0.97 % THD, still no hard edges.

**Switch the preset to "none" for data modes.** A compressor destroys FT8, PSK
and VARA tones. The filters are also reset at each transition to transmit, so
the first syllable is never coloured by leftover state.

## Bands and antenna tuner

The band buttons are not a fixed list. On connection the server reads the rig's
transmit ranges from Hamlib — `tx_range_list`, normalised at `rig_open` for the
ITU region the rig reports — and sends them to the client, which intersects them
with a reference band plan running from 2200 m to 23 cm. A rig therefore shows
its own bands, with a preset frequency clamped inside what it can actually
transmit. Without CAT, the full plan is shown as a reference.

A **Tune** button appears when `rig_has_vfo_op` reports `RIG_OP_TUNE`; it stays
hidden on rigs that do not support it rather than failing silently. A tuning
cycle puts the rig on air for several seconds, so the server locks the PTT for
the duration: the indicator reads TUNE, the transmit button is disabled, and a
PTT request arriving meanwhile is ignored. The lock clears when the rig drops
its own PTT, with a 1.5 s grace so it is not released before the cycle starts,
and a 15 s hard limit in case the rig never reports back.

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

### Extra packages for the touch client

The Qt Quick interface needs the QML runtime modules on top of the packages
above. They are separate on Debian and Ubuntu, and their absence shows up as a
window that opens empty rather than as a build error:

```bash
sudo apt install qt6-declarative-dev \
  qml6-module-qtquick qml6-module-qtquick-controls \
  qml6-module-qtquick-layouts qml6-module-qtquick-templates \
  qml6-module-qtquick-window qml6-module-qtqml-workerscript
```

Then add `-DWITH_QML_CLIENT=ON` to the cmake line. A third program comes out,
`remoterig-client-qml`.

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

### Debian package

`make_deb.sh` produces a `.deb` for the machine it runs on:

```bash
./make_deb.sh            # server, desktop client, touch client
./make_deb.sh --no-qml   # without the Qt Quick client
./make_deb.sh --check    # and run lintian on the result
```

Install it with `apt`, not `dpkg -i`, so the dependencies come along:

```bash
sudo apt install build-deb/remoterig_1.0.0_amd64.deb
```

**A `.deb` carries compiled code, so one architecture is one package**: `amd64`
for a PC, `arm64` for a 64-bit Raspberry Pi, `armhf` for a 32-bit one. Build on
each machine; there is no universal package.

Dependencies are not written by hand. `dpkg-shlibdeps` reads the libraries
actually linked into the binaries and names the packages providing them, which
gives the right answer on Ubuntu and on Raspberry Pi OS despite their different
Qt versions. The six `qml6-module-*` packages are the exception: the QML engine
loads them at runtime and no tool can see that, so they are declared explicitly.
Without them the touch client opens an empty window and says nothing.

The package installs the three programs, their desktop entries, icons at nine
sizes, man pages, and refreshes the desktop caches on install. `lintian` reports
nothing.

### 3. Installing

`install.sh` builds if needed, then puts the programs, their icons and their
desktop entries where the desktop expects them.

```bash
./install.sh                  # for you alone, into ~/.local, no root needed
sudo ./install.sh --system    # for everyone, into /usr/local
```

The entries then appear in the menu under Internet or Audio, each with its own
icon, in English or French according to the session language. Icons are
installed at nine sizes, from 16 to 512 px, so the panel, the menu and the
task switcher all find one that fits.

Other options: `--client-only` and `--server-only` to install just one program,
`--no-build` to reuse the binaries already in `build/`, `--prefix DIR` to
install somewhere else, and `--uninstall` to remove everything. Settings under
`~/.config/F4JTV` are always left alone.

### 4. Serial port permissions

Your user must belong to the group that owns the serial port, otherwise Hamlib
cannot open it:

```bash
sudo usermod -a -G dialout $USER      # plugdev on some distributions
```

Log out and back in for it to take effect.

### 5. Manual install

`install.sh` is the easy route. To place only the binaries, without icons or
menu entries:

```bash
sudo cmake --install build            # into /usr/local/bin
```

### Turning off an optional dependency

```bash
cmake -B build -DWITH_HAMLIB=OFF      # serial PTT only
cmake -B build -DWITH_OPUS=OFF        # 16-bit PCM only
```

## Raspberry Pi 4 and 5

Everything builds and runs on a Pi. The Debian section above applies as is —
Raspberry Pi OS Bookworm is Debian 12, and carries `qt6-base-dev`,
`qt6-serialport-dev`, `portaudio19-dev`, `libopus-dev` and `libhamlib-dev` for
both arm64 and armhf.

```bash
sudo apt install build-essential cmake git \
  qt6-base-dev qt6-serialport-dev \
  portaudio19-dev libopus-dev libhamlib-dev
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

Prefer the **64-bit** system: it is the default on Pi 4 and 5, and gives a
noticeably faster build. The 32-bit one works too, checked below.

### What was verified

The pure C++ core was cross-compiled for `aarch64` and `armhf` and run under
qemu. Results are identical to x86-64, byte for byte:

| | x86-64 | aarch64 | armhf |
|---|---|---|---|
| `PktHeader` size and field offsets | 24 / 12 / 20 | 24 / 12 / 20 | 24 / 12 / 20 |
| ChaCha20, RFC 8439 vector | matches | matches | matches |
| Resampler, sample counts and gain | reference | identical | identical |
| Ring buffer wraparound | ok | ok | ok |

Worth knowing: `char` is **unsigned** on ARM and signed on x86. No part of the
code depends on that, which the identical results confirm. The protocol header
uses only fixed-width types and explicit little-endian conversions, so a Pi and
a PC exchange the same bytes.

Every source file was also syntax-checked with the `aarch64` and `armhf`
compilers, warnings enabled, with no complaint.

### CPU load

Measured on an x86-64 core, per 10 ms audio frame:

| | Time | Share of one core |
|---|---|---|
| Resampling 44.1 → 48 kHz | 9.7 µs | 0.10 % |
| Opus encode + decode | 42.4 µs | 0.42 % |

A Pi 4 core is several times slower, but the margin is wide: even ten times
slower, the whole audio path stays around 5 % of one core. A Pi 4 is comfortable
for either program, a Pi 5 more so.

### Practical points

- **A USB sound card is required on the server.** The Pi 4 headphone jack is
  output only, and the Pi 5 has no analogue jack at all. Any USB CODEC works —
  a Digirig, a CM108 interface, or a plain USB dongle.
- **If a USB microphone does not show up in the list**, PortAudio has not been
  able to enumerate it — usually because PipeWire is holding the card. Run
  `remoterig-client --list-audio` to see exactly what PortAudio sees. The fix is
  to declare a named PCM in `~/.asoundrc`, pointing at the card that `arecord -l`
  reports:

  ```
  pcm.rr_micro {
      type plug
      slave.pcm "hw:3,0"
  }
  ```

  `rr_micro` then appears in the microphone list. The `plug` type takes care of
  format and rate conversion, so a headset limited to 16 kHz mono works too.
- Many USB CODECs are locked to 44.1 or 48 kHz. The engine negotiates the rate
  and resamples if needed; the Audio tab tells you which rate it got.
- On Raspberry Pi OS Lite there is no PulseAudio or PipeWire, so PortAudio talks
  straight to ALSA. That is the lowest-latency path, and also the strictest one
  about sample rates.
- Both programs have a graphical interface, so the server wants a desktop
  session — Raspberry Pi OS Desktop, or Lite plus VNC.
- Serial port access needs the group: `sudo usermod -a -G dialout $USER`, then
  log out and back in.
- The network thread asks for time-critical priority. Without privileges Linux
  quietly ignores the request and the thread runs at normal priority, which is
  fine; to actually grant it, add a line to `/etc/security/limits.conf`:
  `@audio - rtprio 95` and put your user in the `audio` group.


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
├── build_android.sh  one-shot Android build: compile, package, sign, install
├── LICENSE.txt
├── common/           protocol, crypto, codec, audio engine, resampler, speech, i18n
├── compat/msvc/      pthread.h shim, MSVC only
├── server/           Hamlib control, network core, window, appicon.rc
├── client/           network core, rigctld interface, window, appicon.rc
│   └── qml/          touch interface: bridge, Main.qml, Android service glue
├── install.sh        Linux install: build, icons, desktop entries
├── make_deb.sh       Debian package for the current architecture
├── icons/            application icons, .ico and hicolor .png tree
├── desktop/          freedesktop desktop entries, bilingual
├── i18n/             remoterig_fr.ts, remoterig_fr.qm, translations.qrc
├── installer/        Inno Setup script
└── android/          manifest, launcher icons, foreground service
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

# Android build (work in progress)

The client runs the same C++ on Android. The audio layer is done; the touch
interface is not.

## What is in place

`common/audioengine_oboe.cpp` implements the whole `AudioEngine` interface on
**Oboe**, Google's low-latency audio library. PortAudio has no Android host API
upstream — the OpenSL ES ticket opened in 2011 never went anywhere — and Google
recommends Oboe, which calls AAudio when available and falls back to OpenSL ES
otherwise.

Nothing else changed. `clientcore.cpp`, the protocol, the codec, the speech
shaping and the jitter buffer compile untouched: the gain, peak metering,
resampling and ring buffer code was moved into `audioengine_shared.cpp`, which
both backends call. CMake picks the backend on its own and says which one:

```
-- Audio backend: PortAudio      (desktop)
-- Audio backend: Oboe           (Android, fetched automatically)
```

Deliberate choices in the Oboe layer:

- **`InputPreset::VoiceRecognition`** — turns off echo cancellation and
  automatic gain control, so the voice arrives untouched and our own shaping
  chain does the work. Android's AGC would fight the compressor.
- **Oboe does the rate conversion**, at `High` quality, so the application
  always sees 48 kHz mono whatever the phone's hardware runs at. Our own
  resampler stays bypassed.
- **`onErrorAfterClose` reopens the stream.** Unplugging a headset closes the
  stream from underneath the app; without this the audio never comes back.
- The server is not built on Android: no serial port, no Hamlib.

## The touch interface

`client/qml/Main.qml` is a Qt Quick interface built for a thumb: the PTT takes
the bottom quarter of the screen, the frequency is readable at arm's length, and
everything else lives in a drawer. `client/qml/clientbridge.cpp` exposes the
core's state as QML properties — `clientcore.cpp` itself is reused untouched,
and runs in the same worker thread as on the desktop.

The bridge is registered as a **QML singleton** rather than a context property:
Qt 6 discourages the latter, which defeats ahead-of-time QML compilation and
leaves the tooling with nothing to type-check.

### Previewing on the desktop

You can judge the ergonomics without a phone. The same interface builds on the
desktop:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DWITH_QML_CLIENT=ON
cmake --build build -j$(nproc)
./build/remoterig-client-qml
```

Three programs come out: the server, the desktop client, and the touch client.
On Android only the last one is built, and it is named `remoterig-client`.

## Staying alive with the screen off

`android/src/org/remoterig/client/RemoteRigService.java` is a foreground
service, started when the link comes up and stopped when it goes down. Without
it Android suspends the app as soon as the screen turns off and the audio dies
mid-contact. It also holds two locks:

- a **WifiLock** in high-performance mode, because Wi-Fi power saving otherwise
  punches holes in the stream;
- a **partial WakeLock**, so the processor keeps handling audio.

The service declares `foregroundServiceType="microphone"`, which Android 14
requires of any service that captures sound. It is driven from C++ through
`QJniObject`, no Java glue on the application side.

## Themes

The touch client carries four palettes, each for a real operating situation:
**Dark**, **Red** which preserves night vision, **Contrast** for bright
sunlight, and **Light**. The choice sits at the bottom of the drawer and is
remembered. Qt Quick Controls follows the same palette as the custom drawing, so
the switch is immediate and complete — no restart.

## Audio device, PTT key and rigctld

The Audio section of the drawer lists the real devices, enumerated through
`AudioManager.getDevices()` over JNI: built-in microphone, wired headset, USB
CODEC, Bluetooth. The identifier goes straight to Oboe's `setDeviceId`, so a USB
interface can be targeted rather than whatever the system decides. **Rescan**
picks up a headset plugged in after launch.

**PTT on volume-down** turns the physical key into a transmit button. The filter
sits on the whole application rather than on a widget, because volume keys do
not follow the keyboard focus; the event is consumed, so the volume does not
move while transmitting. The switch is off by default, since it takes the key
over.

**The rigctld interface** can be published on 127.0.0.1:4532 from the same
section. A data-mode application on the phone then drives the remote radio
through Hamlib NET rigctl, exactly as on the desktop. It is off by default.

## What is still missing

- Nothing identified. Report what you find.

## Building the APK on Ubuntu 24.04

Nothing here comes from the distribution: Ubuntu packages Qt for the desktop
only. Everything below installs into your home directory and touches nothing
system-wide except the JDK.

### Everything at once

`build_android.sh` chains the whole thing and checks each prerequisite before
touching anything:

```bash
./build_android.sh --logcat
```

It configures, builds, wipes `android-build` before packaging, signs, installs
over `adb` and then follows Oboe's log. Each check corresponds to a failure met
while getting the first APK out, and each error message carries the command that
fixes it. Every path is overridable from the environment — `QT_VERSION`,
`NDK_VERSION`, `SDK_PLATFORM`, `KEYSTORE` and the rest; `--help` lists them.

Setting `QT_ANDROID_KEYSTORE_STORE_PASS` makes Qt sign during the build rather
than running `apksigner` afterwards.

The steps below are the same thing by hand.

### 1. JDK and the usual tools

```bash
sudo apt update
sudo apt install openjdk-21-jdk unzip curl cmake ninja-build python3-pip
export JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64
```

Qt's documentation for the current release asks for JDK 21. Older Qt 6 releases
wanted JDK 17, which Ubuntu also carries as `openjdk-17-jdk` — check the
"Supported Configurations" table on your own Qt version's Android page if the
build complains.

### 2. Android SDK command-line tools

Take the current Linux "command line tools only" link from
<https://developer.android.com/studio#command-tools>; the build number in the
file name changes every few months.

```bash
mkdir -p ~/Android/Sdk/cmdline-tools
cd ~/Android/Sdk/cmdline-tools
curl -O https://dl.google.com/android/repository/commandlinetools-linux-11076708_latest.zip
unzip -q commandlinetools-linux-*.zip
mv cmdline-tools latest        # sdkmanager insists on this layout
export ANDROID_SDK_ROOT=$HOME/Android/Sdk
export PATH="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$PATH"
```

### 3. SDK platform, build tools and NDK

```bash
yes | sdkmanager --licenses
sdkmanager "platform-tools" "platforms;android-36" \
           "build-tools;36.0.0" "ndk;27.2.12479018"
export ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
```

**API 36, not 35.** Qt 6.11 drives Gradle 9 and Android Gradle Plugin 9, which
pull in `androidx.core` 1.17. That library refuses to be compiled against
anything older than API 36. Installing only `android-35` gets you as far as
Gradle and then stops on `checkReleaseAarMetadata`. The CMake cache variables
`RR_ANDROID_TARGET_SDK` (36) and `RR_ANDROID_MIN_SDK` (26) let you change this
without editing the project.

**Match the NDK to your Qt version.** Qt's own libraries are built with one
specific NDK, and mixing them produces missing-symbol errors at link time rather
than anything readable. Recent Qt 6 releases use r27c (27.2.12479018); Qt 6.8
and 6.9 also accepted r26b (26.1.10909125). The table on your Qt version's
Android page is the authority.

### 4. Qt for Android

The Qt online installer works, but `aqtinstall` scripts the whole thing. Both
the Android build **and** a matching desktop build are needed: the host one
provides `androiddeployqt` and `qmlimportscanner`.

```bash
pip install --user aqtinstall
export PATH="$HOME/.local/bin:$PATH"

# host tools first: the Android install refuses to work without them
aqt install-qt linux desktop 6.11.2 linux_gcc_64 -O ~/Qt

# the Android target
aqt install-qt linux android 6.11.2 android_arm64_v8a -O ~/Qt
```

**No `-m` switch here.** In Qt 6, Qt Quick (`qtdeclarative`) and
`qtshadertools` are part of the base package, not optional add-ons, so asking
for them by name fails with *"The packages ['qtdeclarative'] were not found
while parsing XML of package information"*. The `-m` list holds only the true
add-ons — Qt Charts, Qt Multimedia, Qt WebEngine and the like:

```bash
aqt list-qt linux android --modules 6.11.2 android_arm64_v8a
```

To confirm Qt Quick did land, check that this directory exists once the install
finishes:

```bash
ls ~/Qt/6.11.2/android_arm64_v8a/lib/cmake/Qt6Quick
```

### 5. Configure and build

```bash
cd /path/to/RemoteRig
~/Qt/6.11.2/android_arm64_v8a/bin/qt-cmake -B build-android \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=$HOME/Qt/6.11.2/gcc_64 \
    -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
    -DANDROID_NDK_ROOT=$ANDROID_NDK_ROOT

cmake --build build-android -j$(nproc)
cmake --build build-android --target apk
```

The first run takes a while: CMake fetches and builds **Oboe** and **Opus** from
source, since neither exists as an Android package. `qt-cmake` sets the
toolchain, the ABI and the Qt paths on its own — do not pass
`CMAKE_TOOLCHAIN_FILE` yourself.

The APK lands in:

```
build-android/android-build/build/outputs/apk/debug/android-build-debug.apk
```

### 6. Install on the phone

Enable developer mode and USB debugging on the device, then:

```bash
adb devices                 # the phone must show up as "device", not "unauthorized"
adb install -r build-android/android-build/build/outputs/apk/debug/android-build-debug.apk
adb logcat -s RemoteRig:V Qt:V oboe:V
```

The last line is the one that matters on the first run: Oboe logs the stream it
actually obtained — rate, buffer size, whether it got the low-latency path.

### 7. A signed release

A Release build produces an **unsigned** APK, and Android refuses to install
it: `INSTALL_PARSE_FAILED_NO_CERTIFICATES`. Signing is not optional, even for
your own phone.

First create a key, once and for all:

```bash
keytool -genkey -v -keystore ~/remoterig.keystore -alias remoterig \
        -keyalg RSA -keysize 2048 -validity 10000
```

Keep the password: losing it means never updating the app under the same
identity again. The certificate details you type end up visible to anyone who
inspects the APK, so pick what you are happy to publish.

Then let Qt sign at build time. `QT_ANDROID_SIGN_APK` is a CMake variable, the
rest are environment variables read by `androiddeployqt`:

```bash
export QT_ANDROID_KEYSTORE_PATH=$HOME/remoterig.keystore
export QT_ANDROID_KEYSTORE_ALIAS=remoterig
export QT_ANDROID_KEYSTORE_STORE_PASS=yourpassword
export QT_ANDROID_KEYSTORE_KEY_PASS=yourpassword

~/Qt/6.11.2/android_arm64_v8a/bin/qt-cmake -B build-android \
    -DCMAKE_BUILD_TYPE=Release \
    -DQT_HOST_PATH=$HOME/Qt/6.11.2/gcc_64 \
    -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
    -DANDROID_NDK_ROOT=$ANDROID_NDK_ROOT \
    -DQT_ANDROID_SIGN_APK:BOOL=ON

cmake --build build-android --target apk
ls build-android/android-build/build/outputs/apk/release/
```

**If you would rather not reconfigure**, sign the APK you already have, with the
tools from the SDK:

```bash
BT=$ANDROID_SDK_ROOT/build-tools/36.0.0
OUT=build-android/android-build/build/outputs/apk/release

$BT/zipalign -p -f 4 $OUT/android-build-release-unsigned.apk /tmp/aligned.apk
$BT/apksigner sign --ks ~/remoterig.keystore --ks-key-alias remoterig \
    --out ~/remoterig-1.0.0.apk /tmp/aligned.apk

adb install -r ~/remoterig-1.0.0.apk
```

`zipalign` must run before `apksigner`, never after: realigning a signed
package breaks its signature.

### If something goes wrong

- **`fatal: invalid reference: 1.10.x`** when fetching Oboe — the tag does not
  exist. Oboe numbers its tags without a `v` prefix and its `Version.h` runs
  ahead of the last released tag. `git ls-remote --tags
  https://github.com/google/oboe.git` lists what actually exists.
- **`Could NOT find Qt6TaskTree`** — harmless. It comes from an optional Qt QML
  plugin whose dependency is not shipped in the Android package; configuration
  carries on past it.
- **`Target "rr_common" links to oboe::oboe but the target was not found`** —
  Oboe declares a plain `oboe` target and no namespaced alias. The build accepts
  both, so this only bites an older copy of the CMakeLists.
- **`unknown type name 'QJniObject'; did you mean 'QObject'?`** — the include
  order. `Q_OS_ANDROID` is defined by `<QtGlobal>`, so any `#ifdef Q_OS_ANDROID`
  placed before it is silently false. The includes get skipped while the code
  that needs them still compiles.
- **`AAPT: error: resource drawable/icon not found`** — the launcher icons are
  missing from `android/res/drawable-*/`. They ship with the project; a partial
  copy of the source tree is the usual cause.
- **`checkReleaseAarMetadata` fails on `androidx.core:core` requiring API 36** —
  `platforms;android-36` is not installed. See step 3.
- **`QML import could not be resolved in any of the import paths: RemoteRig`** —
  harmless. The `RemoteRig` namespace is registered from C++ at startup with
  `qmlRegisterSingletonInstance`, which the QML import scanner cannot see ahead
  of time. The import resolves at runtime.
- **`The specified Android SDK Build Tools version (35.0.0) is ignored`** —
  harmless too. Android Gradle Plugin 9 picks its own build tools and installs
  them on the spot.
- **`INSTALL_PARSE_FAILED_NO_CERTIFICATES`** — the APK is unsigned. See step 7.
- **`androiddeployqt: No such file or directory`** in the Android Qt — it is a
  host tool. It lives in the desktop Qt: `~/Qt/6.11.2/gcc_64/bin/`.
- **`Could not find Qt6Quick`** — the Android install is incomplete. Do not
  try to add `qtdeclarative` with `-m`; reinstall the base package instead.
- **Undefined symbols at link time** — the NDK does not match the one Qt was
  built with. Step 3.
- **The app opens, the meters move, but transmit is silent** — the microphone
  permission was denied. Settings, Apps, RemoteRig, Permissions.
- **Audio dies when the screen goes off** — the foreground service did not
  start. `adb logcat` filtered on `RemoteRigService` will say why.

## What was verified, and what was not

`audioengine_oboe.cpp` compiles without a warning against the real Oboe 1.10.2
headers, for x86-64 and for `aarch64`, the architecture of phones. All three
programs build with `-Wall -Wextra` and no warning, and the Qt Quick interface
was loaded and run for real on the desktop — `qmllint` reports no unqualified
access and no error. The Java service is syntactically valid; its 59 compiler
complaints all trace to the missing Android SDK, none to the code.

Nothing has run on a phone. Expect 80 to 150 ms of total latency depending on
the device, rather than the 65 ms of the desktop. A **Bluetooth** headset drops
to 8 or 16 kHz over SCO and will sound poor whatever the shaping does — wired
or USB-C only.

Known upstream bug worked around: Oboe 1.10.2 forgets `<cstring>` in
`FullDuplexStream.h`, which the NDK toolchain happens to hide. The include is
added before the Oboe headers.

---

F4JTV — MIT licence.
