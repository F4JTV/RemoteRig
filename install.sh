#!/usr/bin/env bash
# ============================================================================
#  RemoteRig - Linux installer
#
#  Builds if needed, then installs the programs, their icons and their desktop
#  entries. Two modes:
#
#    ./install.sh              for the current user, into ~/.local  (no root)
#    sudo ./install.sh --system   for everyone, into /usr/local
#
#  Options:
#    --system          install into /usr/local, needs root
#    --prefix DIR      install somewhere else
#    --no-build        use the binaries already in build/
#    --client-only     install the operator client alone
#    --server-only     install the station server alone
#    --uninstall       remove what was installed
#    -h, --help        this help
# ============================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# USER n'est pas toujours exporte, et sudo le remplace.
TARGET_USER="${SUDO_USER:-${USER:-$(id -un)}}"
BUILD_DIR="$SRC_DIR/build"

PREFIX="$HOME/.local"
DO_BUILD=1
DO_UNINSTALL=0
WANT_SERVER=1
WANT_CLIENT=1
PREFIX_SET=0

say()  { printf '  %s\n' "$*"; }
head_() { printf '\n== %s\n' "$*"; }
die()  { printf '\n[X] %s\n' "$*" >&2; exit 1; }

usage() { sed -n '2,25p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0; }

while [ $# -gt 0 ]; do
    case "$1" in
        --system)      PREFIX=/usr/local; PREFIX_SET=1 ;;
        --prefix)      shift; [ $# -gt 0 ] || die "--prefix needs a directory"; PREFIX="$1"; PREFIX_SET=1 ;;
        --prefix=*)    PREFIX="${1#*=}"; PREFIX_SET=1 ;;
        --no-build)    DO_BUILD=0 ;;
        --client-only) WANT_SERVER=0 ;;
        --server-only) WANT_CLIENT=0 ;;
        --uninstall)   DO_UNINSTALL=1 ;;
        -h|--help)     usage ;;
        *)             die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done

BINDIR="$PREFIX/bin"
ICONDIR="$PREFIX/share/icons/hicolor"
APPDIR="$PREFIX/share/applications"
DOCDIR="$PREFIX/share/doc/remoterig"

# Writing outside the home directory needs root.
case "$PREFIX" in
    "$HOME"/*) ;;
    *) [ "$(id -u)" -eq 0 ] || die "Installing into $PREFIX needs root. Use sudo, or drop --system to install into ~/.local." ;;
esac

refresh_caches() {
    command -v update-desktop-database >/dev/null 2>&1 && \
        update-desktop-database "$APPDIR" 2>/dev/null || true
    command -v gtk-update-icon-cache >/dev/null 2>&1 && \
        gtk-update-icon-cache -f -t "$ICONDIR" 2>/dev/null || true
}

# ------------------------------------------------------------------ uninstall
if [ "$DO_UNINSTALL" -eq 1 ]; then
    head_ "Removing from $PREFIX"
    for app in remoterig-server remoterig-client; do
        rm -f "$BINDIR/$app" && say "$BINDIR/$app"
        rm -f "$APPDIR/$app.desktop"
        find "$ICONDIR" -name "$app.png" -delete 2>/dev/null || true
    done
    rm -rf "$DOCDIR"
    refresh_caches
    say "Settings under ~/.config/F4JTV were left alone."
    head_ "Done."
    exit 0
fi

# ---------------------------------------------------------------------- build
if [ "$DO_BUILD" -eq 1 ]; then
    head_ "Building"
    command -v cmake >/dev/null 2>&1 || die "cmake not found. Install: sudo apt install build-essential cmake"
    targets=()
    if [ "$WANT_SERVER" -eq 1 ]; then targets+=(remoterig-server); fi
    if [ "$WANT_CLIENT" -eq 1 ]; then targets+=(remoterig-client); fi
    cmake -S "$SRC_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release >/dev/null
    cmake --build "$BUILD_DIR" -j"$(nproc)" --target "${targets[@]}"
fi

# Attention : sous set -e, une construction "[ cond ] && action" fait sortir
# le script des que la condition est fausse. D'ou les if explicites partout.
if [ "$WANT_SERVER" -eq 1 ] && [ ! -x "$BUILD_DIR/remoterig-server" ]; then
    die "$BUILD_DIR/remoterig-server missing. Build first, or drop --no-build."
fi
if [ "$WANT_CLIENT" -eq 1 ] && [ ! -x "$BUILD_DIR/remoterig-client" ]; then
    die "$BUILD_DIR/remoterig-client missing. Build first, or drop --no-build."
fi

# -------------------------------------------------------------------- install
head_ "Installing into $PREFIX"
install -d "$BINDIR" "$APPDIR" "$DOCDIR"

install_one() {
    local app="$1"
    install -m 755 "$BUILD_DIR/$app" "$BINDIR/$app"
    say "$BINDIR/$app"

    # Icons, at every size the desktop may ask for.
    local n=0
    for png in "$SRC_DIR"/icons/hicolor/*/apps/"$app".png; do
        [ -f "$png" ] || continue
        local size; size="$(basename "$(dirname "$(dirname "$png")")")"
        install -d "$ICONDIR/$size/apps"
        install -m 644 "$png" "$ICONDIR/$size/apps/$app.png"
        n=$((n + 1))
    done
    say "$n icon sizes"

    # Desktop entry, with the real path substituted in.
    sed "s|@BINDIR@|$BINDIR|g" "$SRC_DIR/desktop/$app.desktop" > "$APPDIR/$app.desktop"
    chmod 644 "$APPDIR/$app.desktop"
    say "$APPDIR/$app.desktop"
}

if [ "$WANT_SERVER" -eq 1 ]; then install_one remoterig-server; fi
if [ "$WANT_CLIENT" -eq 1 ]; then install_one remoterig-client; fi

for doc in README.md README.fr.md LICENSE.txt; do
    if [ -f "$SRC_DIR/$doc" ]; then install -m 644 "$SRC_DIR/$doc" "$DOCDIR/$doc"; fi
done

refresh_caches

# --------------------------------------------------------------------- report
head_ "Done."
case ":$PATH:" in
    *":$BINDIR:"*) ;;
    *) say "Note: $BINDIR is not in your PATH."
       say "      Add it with: echo 'export PATH=\"\$PATH:$BINDIR\"' >> ~/.profile" ;;
esac

if [ "$WANT_SERVER" -eq 1 ] && ! id -nG "$TARGET_USER" 2>/dev/null | grep -qw dialout; then
    say "Note: $TARGET_USER is not in the dialout group, so Hamlib cannot open"
    say "      the serial port. Fix it with: sudo usermod -a -G dialout $TARGET_USER"
    say "      then log out and back in."
fi

say "The entries appear in the menu under Internet or Audio."
if [ "$PREFIX_SET" -eq 1 ]; then
    say "To remove everything: $0 --uninstall --prefix $PREFIX"
else
    say "To remove everything: $0 --uninstall"
fi
