#!/usr/bin/env bash
# ============================================================================
#  RemoteRig - version bump
#
#  The version lives in one place, project(RemoteRig VERSION x.y.z) in
#  CMakeLists.txt, and flows from there to the About box, the Debian package
#  and the Android version code. The Debian changelog is the one file that
#  does not follow on its own, which is exactly where the two drift apart.
#  This script moves both at once.
#
#  Usage:
#    ./bump_version.sh patch "Fixed the thing that was broken"
#    ./bump_version.sh minor "Added the new thing"
#    ./bump_version.sh major "Changed something incompatible"
#    ./bump_version.sh --show
#
#  patch: a bug fix, nothing new.
#  minor: a new feature, nothing broken for existing users.
#  major: anything an existing setup would have to be changed for.
# ============================================================================
set -euo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CMAKE="$SRC_DIR/CMakeLists.txt"
CHANGELOG="$SRC_DIR/packaging/deb/changelog"

say()  { printf '  %s\n' "$*"; }
die()  { printf '\n[X] %s\n\n' "$*" >&2; exit 1; }

current_version() {
    sed -n 's/^project(RemoteRig VERSION \([0-9.]*\).*/\1/p' "$CMAKE" | head -1
}

[ -f "$CMAKE" ] || die "CMakeLists.txt not found next to this script."
CUR="$(current_version)"
[ -n "$CUR" ] || die "Could not read the version from CMakeLists.txt."

if [ $# -eq 0 ] || [ "${1:-}" = "--show" ]; then
    echo "$CUR"
    exit 0
fi

KIND="$1"
MESSAGE="${2:-}"
[ -n "$MESSAGE" ] || die "Give a one-line description: ./bump_version.sh $KIND \"what changed\""

IFS=. read -r MAJ MIN PAT <<< "$CUR"
case "$KIND" in
    patch) PAT=$((PAT + 1)) ;;
    minor) MIN=$((MIN + 1)); PAT=0 ;;
    major) MAJ=$((MAJ + 1)); MIN=0; PAT=0 ;;
    *)     die "Unknown kind: $KIND  (patch, minor or major)" ;;
esac
NEW="$MAJ.$MIN.$PAT"

# --- CMakeLists : la source unique
sed -i "s/^project(RemoteRig VERSION $CUR/project(RemoteRig VERSION $NEW/" "$CMAKE"
[ "$(current_version)" = "$NEW" ] || die "The CMakeLists edit did not take."
say "CMakeLists.txt  $CUR -> $NEW"

# --- changelog Debian : une entree en tete, au format attendu par dpkg
if [ -f "$CHANGELOG" ]; then
    DATE="$(date -R)"
    MAINTAINER="$(sed -n 's/^ -- \(.*\)  .*/\1/p' "$CHANGELOG" | head -1)"
    [ -n "$MAINTAINER" ] || MAINTAINER="RemoteRig <noreply@example.org>"
    TMP="$(mktemp)"
    {
        printf 'remoterig (%s) stable; urgency=medium\n\n' "$NEW"
        printf '  * %s\n\n' "$MESSAGE"
        printf ' -- %s  %s\n\n' "$MAINTAINER" "$DATE"
        cat "$CHANGELOG"
    } > "$TMP"
    mv "$TMP" "$CHANGELOG"
    say "changelog       entry added for $NEW"
fi

# --- manuels HTML : ils annoncent la version courante en clair. Seules les
#     trois formulations ci-dessous suivent ; les mentions historiques du genre
#     « corrige depuis la version 1.0.0 » doivent rester ou elles sont.
for MANUAL in "$SRC_DIR/docs/manual-fr.html" "$SRC_DIR/docs/manual-en.html"; do
    [ -f "$MANUAL" ] || continue
    sed -i \
        -e "s/\(clients, version \)$CUR\( — \)/\1$NEW\2/" \
        -e "s/remoterig_${CUR}_amd64\.deb/remoterig_${NEW}_amd64.deb/" \
        -e "s/\(RemoteRig \)$CUR\( — \)/\1$NEW\2/" \
        "$MANUAL"
    say "$(basename "$MANUAL")  updated"
done

# --- le code de version Android en decoule, on le montre pour controle
CODE=$(( MAJ * 10000 + MIN * 100 + PAT ))
say "Android version code will be $CODE"
say ""
say "Rebuild so the About box and the packages carry $NEW."
