#!/usr/bin/env bash
# ============================================================================
#  RemoteRig - Android APK build
#
#  Configures, compiles, packages, signs and optionally installs the touch
#  client. Every check here corresponds to a real failure met while getting the
#  first APK out; the comments say which.
#
#  Usage:
#    ./build_android.sh                 build and sign
#    ./build_android.sh --install       ... then push it to the phone
#    ./build_android.sh --reinstall     ... after removing the previous install
#    ./build_android.sh --logcat        ... and follow the audio log
#    ./build_android.sh --clean         start from scratch
#    ./build_android.sh --no-build      package and sign what is already built
#    ./build_android.sh --no-sign       stop at the unsigned APK
#    ./build_android.sh --help
# ============================================================================
set -uo pipefail

SRC_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SRC_DIR/build-android"

# ------------------------------------------------------- paths, adjust freely
QT_VERSION="${QT_VERSION:-6.11.2}"
QT_ROOT="${QT_ROOT:-$HOME/Qt}"
QT_ANDROID="${QT_ANDROID:-$QT_ROOT/$QT_VERSION/android_arm64_v8a}"
QT_HOST="${QT_HOST:-$QT_ROOT/$QT_VERSION/gcc_64}"
ANDROID_SDK_ROOT="${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}"
NDK_VERSION="${NDK_VERSION:-27.2.12479018}"
SDK_PLATFORM="${SDK_PLATFORM:-36}"
BUILD_TOOLS="${BUILD_TOOLS:-36.0.0}"
KEYSTORE="${KEYSTORE:-$HOME/remoterig.keystore}"
KEY_ALIAS="${KEY_ALIAS:-remoterig}"

# La version vient du CMakeLists : un seul endroit a mettre a jour, et le nom
# de l'APK suit. Sans cela, deux compilations successives portent le meme nom.
VERSION="$(sed -n 's/^project(RemoteRig VERSION \([0-9.]*\).*/\1/p' \
           "$SRC_DIR/CMakeLists.txt" | head -1)"
[ -n "$VERSION" ] || VERSION="0.0.0"

DO_CLEAN=0
DO_BUILD=1
DO_SIGN=1
DO_INSTALL=0
DO_LOGCAT=0
DO_REINSTALL=0

say()  { printf '  %s\n' "$*"; }
step() { printf '\n== %s\n' "$*"; }
die()  { printf '\n[X] %s\n' "$*" >&2; exit 1; }

usage() {
    cat <<'HELP'
RemoteRig - Android APK build

  ./build_android.sh                 build and sign
  ./build_android.sh --install       ... then push it to the phone
  ./build_android.sh --reinstall     ... after removing the previous install
  ./build_android.sh --logcat        ... and follow the audio log
  ./build_android.sh --clean         start from scratch
  ./build_android.sh --no-build      package and sign what is already built
  ./build_android.sh --no-sign       stop at the unsigned APK
  ./build_android.sh --help          this text

Paths come from the variables at the top of the file, each overridable from
the environment: QT_VERSION, QT_ROOT, QT_ANDROID, QT_HOST, ANDROID_SDK_ROOT,
NDK_VERSION, SDK_PLATFORM, BUILD_TOOLS, KEYSTORE, KEY_ALIAS.

Set QT_ANDROID_KEYSTORE_STORE_PASS to have Qt sign during the build instead
of running apksigner afterwards.
HELP
    exit 0
}

while [ $# -gt 0 ]; do
    case "$1" in
        --clean)   DO_CLEAN=1 ;;
        --no-build) DO_BUILD=0 ;;
        --no-sign) DO_SIGN=0 ;;
        --install) DO_INSTALL=1 ;;
        --reinstall) DO_INSTALL=1; DO_REINSTALL=1 ;;
        --logcat)  DO_INSTALL=1; DO_LOGCAT=1 ;;
        -h|--help) usage ;;
        *) die "Unknown option: $1  (try --help)" ;;
    esac
    shift
done

# ============================================================ prerequisites
step "Checking prerequisites"

[ -f "$SRC_DIR/CMakeLists.txt" ] || die "Run this from the project directory."
say "Version       $VERSION"
# Le code de version est derive de la version, comme dans le CMakeLists : c'est
# lui que le Play Store compare d'une livraison a l'autre.
VERSION_CODE="$(echo "$VERSION" | awk -F. '{printf "%d", $1 * 10000 + $2 * 100 + $3}')"
say "Version code  $VERSION_CODE"

command -v cmake >/dev/null 2>&1 || die "cmake not found: sudo apt install cmake"
command -v java  >/dev/null 2>&1 || die "No JDK: sudo apt install openjdk-21-jdk"

# Qt ships two halves. The Android one holds the libraries, the desktop one
# holds androiddeployqt and qmlimportscanner, which run on the build machine.
[ -x "$QT_ANDROID/bin/qt-cmake" ] || die "Qt for Android not found at $QT_ANDROID
    aqt install-qt linux android $QT_VERSION android_arm64_v8a -O $QT_ROOT
    (no -m switch: Qt Quick is part of the base package, not an add-on)"
say "Qt Android    $QT_ANDROID"

[ -x "$QT_HOST/bin/androiddeployqt" ] || die "Host Qt not found at $QT_HOST
    aqt install-qt linux desktop $QT_VERSION linux_gcc_64 -O $QT_ROOT"
say "Qt host       $QT_HOST"

[ -d "$QT_ANDROID/lib/cmake/Qt6Quick" ] || die "Qt Quick missing from the Android install.
    Reinstall the base package; adding qtdeclarative with -m does not work."

[ -d "$ANDROID_SDK_ROOT" ] || die "Android SDK not found at $ANDROID_SDK_ROOT"

# API 36 is not a preference. Qt 6.11 drives Android Gradle Plugin 9, which
# pulls androidx.core 1.17, which refuses to compile against anything older.
if [ ! -d "$ANDROID_SDK_ROOT/platforms/android-$SDK_PLATFORM" ]; then
    die "Android platform $SDK_PLATFORM missing:
    sdkmanager \"platforms;android-$SDK_PLATFORM\" \"build-tools;$BUILD_TOOLS\""
fi
say "SDK platform  android-$SDK_PLATFORM"

ANDROID_NDK_ROOT="$ANDROID_SDK_ROOT/ndk/$NDK_VERSION"
# A mismatched NDK does not produce a readable error, only undefined symbols
# at link time. Check it up front.
[ -d "$ANDROID_NDK_ROOT" ] || die "NDK $NDK_VERSION missing:
    sdkmanager \"ndk;$NDK_VERSION\"
    It must match the NDK your Qt version was built with."
say "NDK           $NDK_VERSION"

BT_DIR="$ANDROID_SDK_ROOT/build-tools/$BUILD_TOOLS"
if [ "$DO_SIGN" -eq 1 ] && [ ! -x "$BT_DIR/apksigner" ]; then
    say "Build tools $BUILD_TOOLS absent; Gradle will fetch what it needs,"
    say "but signing then falls back to Qt. Install them to sign here:"
    say "  sdkmanager \"build-tools;$BUILD_TOOLS\""
fi

export ANDROID_SDK_ROOT ANDROID_NDK_ROOT

# ------------------------------------------------------------ signing key
SIGN_IN_CMAKE=0
if [ "$DO_SIGN" -eq 1 ]; then
    if [ ! -f "$KEYSTORE" ]; then
        say "No keystore at $KEYSTORE. Create one once with:"
        say "  keytool -genkey -v -keystore $KEYSTORE -alias $KEY_ALIAS \\"
        say "          -keyalg RSA -keysize 2048 -validity 10000"
        say "Note: the certificate fields end up readable in the APK."
        die "Signing key missing. Create it, or pass --no-sign."
    fi
    # Qt signs during the build when these are set, which avoids a second pass.
    if [ -n "${QT_ANDROID_KEYSTORE_STORE_PASS:-}" ]; then
        export QT_ANDROID_KEYSTORE_PATH="$KEYSTORE"
        export QT_ANDROID_KEYSTORE_ALIAS="$KEY_ALIAS"
        export QT_ANDROID_KEYSTORE_KEY_PASS="${QT_ANDROID_KEYSTORE_KEY_PASS:-$QT_ANDROID_KEYSTORE_STORE_PASS}"
        SIGN_IN_CMAKE=1
        say "Signing during the build, password taken from the environment"
    else
        say "Signing after the build with apksigner (it will ask for the password)"
        say "  Set QT_ANDROID_KEYSTORE_STORE_PASS to have Qt sign directly."
    fi
fi

# ================================================================== cleaning
if [ "$DO_CLEAN" -eq 1 ]; then
    step "Cleaning"
    rm -rf "$BUILD_DIR"
    say "$BUILD_DIR removed"
fi

# ================================================================ configure
if [ "$DO_BUILD" -eq 1 ]; then
    step "Configuring"
    CFG_ARGS=(
        -B "$BUILD_DIR"
        -DCMAKE_BUILD_TYPE=Release
        -DQT_HOST_PATH="$QT_HOST"
        -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT"
        -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT"
        -DRR_ANDROID_TARGET_SDK="$SDK_PLATFORM"
    )
    [ "$SIGN_IN_CMAKE" -eq 1 ] && CFG_ARGS+=(-DQT_ANDROID_SIGN_APK:BOOL=ON)

    "$QT_ANDROID/bin/qt-cmake" "${CFG_ARGS[@]}" || die "Configuration failed."

    step "Compiling"
    say "Oboe and Opus are fetched and built from source on the first run."
    cmake --build "$BUILD_DIR" -j"$(nproc)" || die "Compilation failed."
fi

# ================================================================ packaging
# androiddeployqt copies android/ into android-build/ but never deletes: an
# outdated manifest or a missing icon survives a source update. Wiping the
# directory is the only reliable way to pick up changes.
step "Packaging"
rm -rf "$BUILD_DIR/android-build"
cmake --build "$BUILD_DIR" --target apk || die "APK packaging failed."

APK_DIR="$BUILD_DIR/android-build/build/outputs/apk/release"
# Attention au motif : « unsigned » contient « signed ». Chercher *signed*.apk
# ramene l'APK non signe, et le script croit alors n'avoir rien a faire.
UNSIGNED="$(ls "$APK_DIR"/*-unsigned.apk 2>/dev/null | head -1)"
SIGNED="$(ls "$APK_DIR"/*.apk 2>/dev/null | grep -v -- '-unsigned\.apk$' | head -1)"

# ================================================================== signing
FINAL=""
if [ -n "$SIGNED" ]; then
    # Qt a signe pendant la compilation : on renomme pour que le fichier porte
    # sa version, comme dans l'autre branche.
    FINAL="$SRC_DIR/remoterig_v${VERSION}.apk"
    cp -f "$SIGNED" "$FINAL" || die "Could not copy the signed APK."
elif [ "$DO_SIGN" -eq 1 ] && [ -n "$UNSIGNED" ]; then
    step "Signing"
    [ -x "$BT_DIR/apksigner" ] || die "apksigner missing: sdkmanager \"build-tools;$BUILD_TOOLS\""
    FINAL="$SRC_DIR/remoterig_v${VERSION}.apk"
    # zipalign first, always: realigning a signed package breaks its signature.
    "$BT_DIR/zipalign" -p -f 4 "$UNSIGNED" "$BUILD_DIR/aligned.apk" \
        || die "zipalign failed."
    "$BT_DIR/apksigner" sign --ks "$KEYSTORE" --ks-key-alias "$KEY_ALIAS" \
        --out "$FINAL" "$BUILD_DIR/aligned.apk" || die "apksigner failed."
    say "$(basename "$FINAL")"
else
    FINAL="$SRC_DIR/remoterig_v${VERSION}-unsigned.apk"
    cp -f "$UNSIGNED" "$FINAL" 2>/dev/null || FINAL="$UNSIGNED"
    say "Unsigned APK. Android will refuse to install it:"
    say "  INSTALL_PARSE_FAILED_NO_CERTIFICATES"
fi

[ -n "$FINAL" ] && [ -f "$FINAL" ] || die "No APK produced in $APK_DIR"

# L'APK doit porter l'heure de cette compilation : si ce n'est pas le cas,
# c'est qu'un ancien fichier a ete ramasse quelque part.
step "Package produced"
say "File       $FINAL"
say "Built      $(date -r "$FINAL" '+%Y-%m-%d %H:%M:%S')"
say "Fingerprint $(sha256sum "$FINAL" | cut -c1-16)"

# ============================================================== installing
if [ "$DO_INSTALL" -eq 1 ]; then
    step "Installing"
    command -v adb >/dev/null 2>&1 || die "adb not found: sdkmanager \"platform-tools\""
    STATE="$(adb get-state 2>/dev/null)"
    if [ "$STATE" != "device" ]; then
        say "No phone ready (adb reports: ${STATE:-nothing})."
        say "On Samsung, Settings > Security and privacy > Auto Blocker must be"
        say "off before USB debugging can even be ticked."
        die "Phone unreachable."
    fi
    if [ "$DO_REINSTALL" -eq 1 ]; then
        say "Removing the previous install first"
        adb uninstall org.remoterig.client >/dev/null 2>&1
    fi
    adb install -r "$FINAL" || die "Installation refused.
    A different signing key from the one already on the phone gives
    INSTALL_FAILED_UPDATE_INCOMPATIBLE: rerun with --reinstall."
    # On relit ce que le telephone a reellement enregistre : c'est la seule
    # preuve que la nouvelle version est bien en place.
    INSTALLED="$(adb shell dumpsys package org.remoterig.client 2>/dev/null \
                 | grep -E 'versionName|lastUpdateTime' | tr -d '\r' | sed 's/^ *//')"
    say "installed"
    [ -n "$INSTALLED" ] && printf '%s\n' "$INSTALLED" | sed 's/^/    /'
fi

# ==================================================================== report
step "Done."
say "APK        $FINAL"
say "Size       $(du -h "$FINAL" | cut -f1)"
if [ "$DO_LOGCAT" -eq 1 ]; then
    step "Audio log, Ctrl-C to stop"
    say "Oboe reports the stream it actually obtained: rate, buffer, latency path."
    adb logcat -c
    adb shell am start -n org.remoterig.client/org.remoterig.client.RemoteRigActivity >/dev/null 2>&1
    sleep 2
    # Filtrer par tag rate ce que journalise l'application elle-meme : on suit
    # donc son processus, plus les tags audio du systeme.
    APP_PID="$(adb shell pidof org.remoterig.client 2>/dev/null | tr -d '\r')"
    if [ -n "$APP_PID" ]; then
        say "Following pid $APP_PID"
        adb logcat --pid="$APP_PID" 2>/dev/null || adb logcat | grep -E "oboe|AAudio|AudioRecord|RemoteRig|Qt"
    else
        adb logcat | grep -E "oboe|AAudio|AudioRecord|RemoteRig|Qt"
    fi
else
    say "Next:      ./build_android.sh --logcat   to watch the audio layer start"
fi
