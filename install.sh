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
#    --udev            also install the CM108 udev rule (needs sudo)
#    --no-udev         never install it, and do not ask
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
DO_UDEV=ask          # ask | yes | no
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
        --udev)        DO_UDEV=yes ;;
        --no-udev)     DO_UDEV=no ;;
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
UDEV_RULE="$SRC_DIR/packaging/udev/99-remoterig-cm108.rules"
UDEV_DEST=/etc/udev/rules.d/99-remoterig-cm108.rules

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

# ---------------------------------------------------------------- regle udev
# Le PTT par GPIO3 passe par /dev/hidraw*, que seul root peut ouvrir par
# defaut. La regle confie l'acces a l'utilisateur de la session en cours.
install_udev_rule() {
    [ -f "$UDEV_RULE" ] || { say "Rule not found: $UDEV_RULE"; return 1; }

    if [ "$(id -u)" -eq 0 ]; then
        mkdir -p "$(dirname "$UDEV_DEST")" || return 1
        cp "$UDEV_RULE" "$UDEV_DEST" || return 1
    elif command -v sudo >/dev/null 2>&1; then
        say "Installing the udev rule needs administrator rights."
        sudo mkdir -p "$(dirname "$UDEV_DEST")" || return 1
        sudo cp "$UDEV_RULE" "$UDEV_DEST" || return 1
        sudo udevadm control --reload-rules 2>/dev/null || true
        sudo udevadm trigger 2>/dev/null || true
        say "$UDEV_DEST"
        say "Unplug and replug the interface for it to take effect."
        return 0
    else
        say "No sudo available. Install it yourself with:"
        say "  sudo cp $UDEV_RULE $UDEV_DEST"
        say "  sudo udevadm control --reload-rules && sudo udevadm trigger"
        return 1
    fi

    udevadm control --reload-rules 2>/dev/null || true
    udevadm trigger 2>/dev/null || true
    say "$UDEV_DEST"
    say "Unplug and replug the interface for it to take effect."
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
    if [ -f "$UDEV_DEST" ]; then
        if [ "$(id -u)" -eq 0 ]; then
            rm -f "$UDEV_DEST" && say "$UDEV_DEST"
        elif command -v sudo >/dev/null 2>&1; then
            sudo rm -f "$UDEV_DEST" && say "$UDEV_DEST"
        else
            say "Left behind, remove it yourself: $UDEV_DEST"
        fi
    fi
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
# La regle est aussi deposee ici, pour la retrouver sans l'archive.
if [ -f "$UDEV_RULE" ]; then install -m 644 "$UDEV_RULE" "$DOCDIR/"; fi

refresh_caches

# ------------------------------------------------------------ PTT par GPIO3
if [ "$WANT_SERVER" -eq 1 ] && [ "$DO_UDEV" != "no" ]; then
    if [ -f "$UDEV_DEST" ]; then
        head_ "CM108 udev rule"
        say "Already installed: $UDEV_DEST"
    else
        install_it=0
        if [ "$DO_UDEV" = "yes" ]; then
            install_it=1
        elif [ -t 0 ]; then
            # Seulement si l'on peut repondre : dans un script, on s'abstient.
            head_ "CM108 udev rule"
            say "PTT through a sound card's GPIO3 line needs access to /dev/hidraw*."
            printf "  Install the rule now? [y/N] "
            read -r reply
            case "$reply" in [yYoO]*) install_it=1 ;; esac
        fi
        if [ "$install_it" -eq 1 ]; then
            head_ "CM108 udev rule"
            install_udev_rule || true
        fi
    fi
fi

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
