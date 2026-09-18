# ============================================================================
#  Dependances de compilation, partagees par install.sh et make_deb.sh.
#
#  Un seul endroit ou les tenir a jour : les deux scripts compilent les memes
#  sources, et une liste dupliquee finit toujours par diverger.
#
#  Le fichier attend que l'appelant ait defini say(), step() et die(), et qu'il
#  ait pose WITH_QML a ON ou OFF. Il n'est pas executable seul.
#
#  Les noms sont ceux de Debian et d'Ubuntu, donc aussi ceux de Raspberry Pi OS.
# ============================================================================

RR_BUILD_DEPS="build-essential cmake qt6-base-dev qt6-serialport-dev
               portaudio19-dev libopus-dev libhamlib-dev"
RR_QML_DEPS="qt6-declarative-dev qml6-module-qtquick qml6-module-qtquick-controls
             qml6-module-qtquick-layouts qml6-module-qtquick-templates
             qml6-module-qtquick-window qml6-module-qtqml-workerscript"

# Rend la liste de ceux qui ne sont pas installes, vide si tout est la.
rr_missing_packages() {
    local want="$1" miss=""
    for pkg in $want; do
        dpkg-query -W -f='${Status}' "$pkg" 2>/dev/null | grep -q "ok installed" \
            || miss="$miss $pkg"
    done
    printf '%s' "$miss"
}

# Installe ce qui manque. RR_DEPS_MODE vaut ask, yes ou no ; RR_EXTRA_DEPS
# permet a l'appelant d'ajouter ses propres paquets, dpkg-dev par exemple.
# Rend 0 si l'on peut compiler, 1 sinon.
rr_install_deps() {
    [ "${RR_DEPS_MODE:-ask}" = "no" ] && return 0

    command -v apt-get >/dev/null 2>&1 || {
        say "Not a Debian-based system; install the dependencies yourself."
        return 1
    }
    command -v dpkg-query >/dev/null 2>&1 || return 1

    local want="$RR_BUILD_DEPS ${RR_EXTRA_DEPS:-}"
    [ "${WITH_QML:-OFF}" = "ON" ] && want="$want $RR_QML_DEPS"

    local miss
    miss="$(rr_missing_packages "$want")"
    if [ -z "$miss" ]; then
        say "All build dependencies are already installed."
        return 0
    fi

    step "Build dependencies"
    say "Missing:$miss"

    if [ "${RR_DEPS_MODE:-ask}" = "ask" ]; then
        # Sans terminal pour repondre, on ne bloque pas : on dit quoi faire.
        if [ ! -t 0 ]; then
            say "Install them with: sudo apt install$miss"
            return 1
        fi
        printf "  Install them now? [Y/n] "
        read -r reply
        case "$reply" in [nN]*) say "Skipped."; return 1 ;; esac
    fi

    local sudo_cmd=""
    [ "$(id -u)" -eq 0 ] || sudo_cmd="sudo"
    if [ -n "$sudo_cmd" ] && ! command -v sudo >/dev/null 2>&1; then
        say "No sudo available. Install them with: apt install$miss"
        return 1
    fi

    $sudo_cmd apt-get update || say "apt-get update failed, carrying on anyway"
    # shellcheck disable=SC2086
    $sudo_cmd apt-get install -y $miss || die "Installing the dependencies failed."
    say "Done."
    return 0
}

# Nombre de taches de compilation, borne par la memoire et non par les seuls
# coeurs : compiler du Qt demande environ 700 Mio par tache, et un Raspberry Pi
# a 2 Gio qui en lancerait quatre se ferait tuer par le noyau en chemin.
rr_build_jobs() {
    local jobs mem_kb by_mem
    jobs="$(nproc 2>/dev/null || echo 1)"
    mem_kb="$(awk '/MemTotal/ {print $2}' /proc/meminfo 2>/dev/null || echo 0)"
    if [ "$mem_kb" -gt 0 ]; then
        by_mem=$(( mem_kb / 700000 ))
        [ "$by_mem" -lt 1 ] && by_mem=1
        if [ "$by_mem" -lt "$jobs" ]; then
            say "Limiting to $by_mem parallel job(s): $(( mem_kb / 1024 )) MiB of RAM" >&2
            jobs="$by_mem"
        fi
    fi
    printf '%s' "$jobs"
}
