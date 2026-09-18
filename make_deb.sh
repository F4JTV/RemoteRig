#!/usr/bin/env bash
# ============================================================================
#  RemoteRig - Debian package
#
#  Produces a .deb for the machine it runs on: amd64 on a PC, arm64 on a
#  64-bit Raspberry Pi, armhf on a 32-bit one. A .deb carries compiled code,
#  so one architecture equals one package; there is no universal build.
#
#  Dependencies are not written by hand: dpkg-shlibdeps reads the libraries
#  actually linked into the binaries and names the packages that provide them.
#  The same source therefore yields correct dependencies on Ubuntu and on
#  Raspberry Pi OS, whose Qt versions differ.
#
#  Usage:
#    ./make_deb.sh              server, desktop client, touch client if present
#    ./make_deb.sh --no-qml     skip the Qt Quick client
#    ./make_deb.sh --check      run lintian on the result
#    ./make_deb.sh --deps       install the build dependencies first (needs sudo)
#    ./make_deb.sh --no-deps    never check them, and do not ask
#    ./make_deb.sh --help
# ============================================================================
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-deb"
WITH_QML=ON
DO_CHECK=0
DO_DEPS=ask          # ask | yes | no

say()  { printf '  %s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\n[X] %s\n' "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
    case "$1" in
        --no-qml)  WITH_QML=OFF ;;
        --check)   DO_CHECK=1 ;;
        --deps)    DO_DEPS=yes ;;
        --no-deps) DO_DEPS=no ;;
        -h|--help)
            sed -n '2,20p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
            exit 0 ;;
        *) die "Unknown option: $1" ;;
    esac
    shift
done

step "Checking prerequisites"
[ -f "$SRC_DIR/CMakeLists.txt" ] || die "Run this from the project directory."
# ------------------------------------------------------------- dependances
# Liste et logique partagees avec install.sh : un seul endroit a tenir a jour.
# shellcheck source=packaging/build-deps.sh
. "$SRC_DIR/packaging/build-deps.sh"

RR_DEPS_MODE="$DO_DEPS"
RR_EXTRA_DEPS="dpkg-dev"     # dpkg-shlibdeps, pour calculer les dependances
rr_install_deps || true

command -v cmake >/dev/null 2>&1 || die "cmake missing: sudo apt install cmake"
command -v cpack >/dev/null 2>&1 || die "cpack missing: it ships with cmake"
command -v dpkg-shlibdeps >/dev/null 2>&1 || \
    die "dpkg-shlibdeps missing: sudo apt install dpkg-dev"

ARCH="$(dpkg --print-architecture)"
say "Architecture  $ARCH"
say "Qt Quick      $WITH_QML"

# Les scripts de maintenance doivent etre executables dans le paquet ; le bit
# se perd a la copie sur certains systemes de fichiers.
chmod 755 "$SRC_DIR/packaging/deb/postinst" "$SRC_DIR/packaging/deb/postrm" 2>/dev/null || true

step "Building"
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr \
      -DWITH_QML_CLIENT="$WITH_QML" >/dev/null || die "Configuration failed."
cmake --build "$BUILD_DIR" -j"$(rr_build_jobs)" || die "Compilation failed."

step "Packaging"
# cpack est lance depuis le dossier de build : shlibdeps y cherche les binaires.
( cd "$BUILD_DIR" && cpack -G DEB ) || die "Packaging failed."

DEB="$(ls -t "$BUILD_DIR"/*.deb 2>/dev/null | head -1)"
[ -n "$DEB" ] || die "No .deb produced."

step "Result"
say "File          $DEB"
say "Size          $(du -h "$DEB" | cut -f1)"
say "Dependencies:"
dpkg-deb -f "$DEB" Depends | tr ',' '\n' | sed 's/^ */    /'

if [ "$DO_CHECK" -eq 1 ]; then
    step "lintian"
    if ! command -v lintian >/dev/null 2>&1; then
        if [ "$DO_DEPS" != "no" ] && command -v apt-get >/dev/null 2>&1; then
            say "Installing lintian..."
            { [ "$(id -u)" -eq 0 ] && apt-get install -y lintian; } \
                || sudo apt-get install -y lintian \
                || die "lintian missing: sudo apt install lintian"
        else
            die "lintian missing: sudo apt install lintian"
        fi
    fi
    lintian --no-tag-display-limit "$DEB" || true
fi

step "Install with"
say "sudo apt install $DEB"
say "(apt, not dpkg -i: it pulls the dependencies on its own)"
