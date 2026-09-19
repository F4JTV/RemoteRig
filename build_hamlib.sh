#!/usr/bin/env bash
# ============================================================================
#  RemoteRig - build and install Hamlib from source
#
#  Ubuntu 24.04 and Debian 12 ship Hamlib 4.5.5. Its Yaesu driver sends only
#  the first character of a CW message, read as a memory number, so free text
#  keys one of the rig's stored memories instead of being sent. Hamlib 4.6
#  fixed that. A station therefore behaves differently on Windows, which ships
#  a recent Hamlib, and on Linux with the distribution's own.
#
#  This builds the same version on both. It installs into /usr/local, leaving
#  the distribution package alone: /usr/local comes first for the compiler and
#  for the runtime linker, so RemoteRig picks up the new one once rebuilt.
#
#  Usage:
#    ./build_hamlib.sh                 build and install 4.7.2 into /usr/local
#    ./build_hamlib.sh --version 4.6.2 another version
#    ./build_hamlib.sh --prefix DIR    install somewhere else
#    ./build_hamlib.sh --src FILE.tar.gz   use a tarball already downloaded
#    ./build_hamlib.sh --check         only report the versions in place
# ============================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VERSION=4.7.2
PREFIX=/usr/local
TARBALL=""
CHECK_ONLY=0
WORK="${TMPDIR:-/tmp}/remoterig-hamlib-build"

head_() { printf '\n== %s\n' "$*"; }
say()   { printf '  %s\n' "$*"; }
die()   { printf '\n[X] %s\n\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --version) shift; [ $# -gt 0 ] || die "--version needs a number"; VERSION="$1" ;;
        --prefix)  shift; [ $# -gt 0 ] || die "--prefix needs a directory"; PREFIX="$1" ;;
        --src)     shift; [ $# -gt 0 ] || die "--src needs a file"; TARBALL="$1" ;;
        --check)   CHECK_ONLY=1 ;;
        -h|--help) sed -n '2,24p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *)         die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done

report_versions() {
    head_ "Hamlib in place"
    local distro="" local_="" running=""
    distro="$(dpkg-query -W -f='${Version}' libhamlib4t64 2>/dev/null \
              || dpkg-query -W -f='${Version}' libhamlib4 2>/dev/null || true)"
    [ -n "$distro" ] && say "distribution package: $distro"
    if [ -x "$PREFIX/bin/rigctl" ]; then
        local_="$("$PREFIX/bin/rigctl" --version 2>/dev/null | head -1)"
        say "in $PREFIX: ${local_:-unknown}"
    else
        say "in $PREFIX: none"
    fi
    running="$(pkg-config --modversion hamlib 2>/dev/null || true)"
    [ -n "$running" ] && say "pkg-config reports: $running"
    say ""
    say "Free CW text needs 4.6 or later."
}

[ "$CHECK_ONLY" -eq 1 ] && { report_versions; exit 0; }

# ------------------------------------------------------------- dependances
head_ "Build dependencies"
DEPS="build-essential autoconf automake libtool pkg-config libusb-1.0-0-dev"
MISSING=""
for pkg in $DEPS; do
    dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed" || MISSING="$MISSING $pkg"
done
if [ -n "$MISSING" ]; then
    say "Missing:$MISSING"
    SUDO=""; [ "$(id -u)" -eq 0 ] || SUDO="sudo"
    # shellcheck disable=SC2086
    $SUDO apt-get update && $SUDO apt-get install -y $MISSING || die "Could not install them."
else
    say "All present."
fi

# ------------------------------------------------------------------ sources
head_ "Sources"
rm -rf "$WORK"; mkdir -p "$WORK"; cd "$WORK"
if [ -n "$TARBALL" ]; then
    [ -f "$TARBALL" ] || die "No such file: $TARBALL"
    cp "$TARBALL" hamlib.tar.gz
    say "using $TARBALL"
else
    URL="https://codeload.github.com/Hamlib/Hamlib/tar.gz/refs/tags/$VERSION"
    say "downloading Hamlib $VERSION"
    curl -fsSL -o hamlib.tar.gz "$URL" || die "Download failed. Check the version number, or pass --src."
fi
tar xzf hamlib.tar.gz || die "The archive could not be opened."
cd "$(find . -maxdepth 1 -type d -name 'Hamlib-*' | head -1)" || die "Unexpected archive layout."

# ---------------------------------------------------------------- compilation
# L'archive d'un depot git n'a pas de script configure : il faut le produire.
head_ "Building"
say "generating the build system"
./bootstrap > "$WORK/bootstrap.log" 2>&1 || { tail -5 "$WORK/bootstrap.log"; die "bootstrap failed."; }

say "configuring for $PREFIX"
./configure --prefix="$PREFIX" --disable-static --without-cxx-binding \
    > "$WORK/configure.log" 2>&1 || { tail -10 "$WORK/configure.log"; die "configure failed."; }

JOBS=1
if [ -f "$SRC_DIR/packaging/build-deps.sh" ]; then
    # shellcheck source=packaging/build-deps.sh
    . "$SRC_DIR/packaging/build-deps.sh"
    JOBS="$(rr_build_jobs)"
else
    JOBS="$(nproc 2>/dev/null || echo 1)"
fi
say "compiling with $JOBS job(s) — this takes a while"
make -j"$JOBS" > "$WORK/make.log" 2>&1 || { tail -15 "$WORK/make.log"; die "Compilation failed."; }

# ---------------------------------------------------------------- installation
head_ "Installing into $PREFIX"
SUDO=""
[ -w "$PREFIX" ] || { [ "$(id -u)" -eq 0 ] || SUDO="sudo"; }
$SUDO make install > "$WORK/install.log" 2>&1 || { tail -5 "$WORK/install.log"; die "Installation failed."; }
# Sans cela le lieur dynamique continuerait de trouver l'ancienne.
$SUDO ldconfig 2>/dev/null || true
say "done"

report_versions

head_ "Next"
say "Rebuild RemoteRig so it links against this one:"
say "  rm -rf build && ./install.sh"
say ""
say "Check what it actually linked against:"
say "  ldd \$(command -v remoterig-server) | grep hamlib"
say ""
say "The distribution package is left in place. Both versions carry the same"
say "library name, so the one in $PREFIX wins only while it comes first in the"
say "linker's path. RemoteRig reads the version it is running against and says"
say "so in its log if it is older than 4.6."
