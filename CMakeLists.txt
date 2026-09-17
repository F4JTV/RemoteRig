cmake_minimum_required(VERSION 3.19)
project(RemoteRig VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
# rcc compresse en zstd par defaut et pose alors une garde sur
# qResourceFeatureZstd, un symbole que QtCore n'exporte que s'il a ete compile
# avec zstd. Un binaire lie a un Qt qui l'a, execute avec un Qt qui ne l'a pas,
# refuse de demarrer sur « point d'entree introuvable ». zlib est present dans
# toutes les compilations de Qt : la garde disparait.
set(CMAKE_AUTORCC_OPTIONS "--compress-algo;zlib")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(WITH_QML_CLIENT "Also build the touch client with Qt Quick" OFF)

if(ANDROID)
    # No serial port and no Hamlib on Android: the touch client only.
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Network Quick QuickControls2)
    set(RR_BUILD_SERVER OFF)
    set(RR_BUILD_WIDGETS OFF)
    set(WITH_QML_CLIENT ON)
else()
    find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network SerialPort)
    set(RR_BUILD_SERVER ON)
    set(RR_BUILD_WIDGETS ON)
    if(WITH_QML_CLIENT)
        find_package(Qt6 REQUIRED COMPONENTS Quick QuickControls2)
    endif()
endif()

# -------------------------------------------------------------- translations
# The .qm ships with the sources, so the project builds without the Linguist
# tools. When they are available the .qm is regenerated from the .ts.
set(RR_TS ${CMAKE_CURRENT_SOURCE_DIR}/i18n/remoterig_fr.ts)
set(RR_QM ${CMAKE_CURRENT_SOURCE_DIR}/i18n/remoterig_fr.qm)
set(RR_QRC ${CMAKE_CURRENT_SOURCE_DIR}/i18n/translations.qrc)
set(RR_ICONS_QRC ${CMAKE_CURRENT_SOURCE_DIR}/icons/icons.qrc)
# Le manuel est embarque dans les deux interfaces Widgets : toujours
# disponible, quelle que soit l'installation.
set(RR_DOCS_QRC ${CMAKE_CURRENT_SOURCE_DIR}/docs/docs.qrc)

# Executable icon and version metadata, Windows only.
if(WIN32)
    set(RR_SERVER_RC ${CMAKE_CURRENT_SOURCE_DIR}/server/appicon.rc)
    set(RR_CLIENT_RC ${CMAKE_CURRENT_SOURCE_DIR}/client/appicon.rc)
else()
    set(RR_SERVER_RC "")
    set(RR_CLIENT_RC "")
endif()

find_package(Qt6 QUIET COMPONENTS LinguistTools)
if(Qt6LinguistTools_FOUND)
    add_custom_command(OUTPUT ${RR_QM}
        COMMAND Qt6::lrelease ${RR_TS} -qm ${RR_QM}
        DEPENDS ${RR_TS}
        COMMENT "Compiling the French translation"
        VERBATIM)
    add_custom_target(rr_translations DEPENDS ${RR_QM})
else()
    message(STATUS "Linguist tools not found: using the bundled remoterig_fr.qm as is")
    add_custom_target(rr_translations)
endif()

# ------------------------------------------------------------ audio backend
# PortAudio has no Android host API upstream, so Android uses Oboe instead.
# Both sit behind the same AudioEngine interface; nothing else changes.
if(ANDROID)
    include(FetchContent)
    # Oboe etiquette ses versions sans prefixe : 1.10.0, pas v1.10.0. La
    # branche principale annonce deja 1.10.2 dans Version.h, mais ce numero
    # n'est pas encore etiquete.
    FetchContent_Declare(oboe
        GIT_REPOSITORY https://github.com/google/oboe.git
        GIT_TAG        1.10.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(oboe)
    set(RR_AUDIO_SOURCES common/audioengine_oboe.cpp)
    message(STATUS "Audio backend: Oboe")
else()
    find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
    find_library(PORTAUDIO_LIBRARY NAMES portaudio portaudio_x64 portaudio_static)
    if(NOT PORTAUDIO_INCLUDE_DIR OR NOT PORTAUDIO_LIBRARY)
        message(FATAL_ERROR "PortAudio not found. Debian: apt install portaudio19-dev. Windows: vcpkg install portaudio:x64-windows")
    endif()
    set(RR_AUDIO_SOURCES common/audioengine.cpp)
    message(STATUS "Audio backend: PortAudio")
endif()

# --------------------------------------------------------------------- Opus
option(WITH_OPUS "Compress audio with Opus" ON)
set(OPUS_FROM_SOURCE FALSE)

if(WITH_OPUS AND ANDROID)
    # No system Opus on Android: build it from source, like Oboe.
    # Without this the APK would fall back to raw PCM, 768 kbit/s over mobile.
    include(FetchContent)
    # Opus, lui, prefixe ses etiquettes d'un v.
    FetchContent_Declare(opus
        GIT_REPOSITORY https://github.com/xiph/opus.git
        GIT_TAG        v1.5.2
        GIT_SHALLOW    TRUE)
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(opus)
    set(HAVE_OPUS TRUE)
    set(OPUS_FROM_SOURCE TRUE)
    message(STATUS "Opus: built from source (v1.5.2)")
else()
    find_path(OPUS_INCLUDE_DIR opus/opus.h)
    find_library(OPUS_LIBRARY NAMES opus)
    if(WITH_OPUS AND OPUS_INCLUDE_DIR AND OPUS_LIBRARY)
        set(HAVE_OPUS TRUE)
        message(STATUS "Opus: system library")
    else()
        set(HAVE_OPUS FALSE)
        message(WARNING "Opus not found: only 16-bit PCM will be available")
    endif()
endif()

# ------------------------------------------------------------------- Hamlib
option(WITH_HAMLIB "CAT control through Hamlib" ON)
find_path(HAMLIB_INCLUDE_DIR hamlib/rig.h)
find_library(HAMLIB_LIBRARY NAMES hamlib libhamlib hamlib-4)
if(WITH_HAMLIB AND HAMLIB_INCLUDE_DIR AND HAMLIB_LIBRARY)
    set(HAVE_HAMLIB TRUE)
else()
    set(HAVE_HAMLIB FALSE)
    message(WARNING "Hamlib not found: the server will be limited to serial PTT")
endif()

# ------------------------------------------------------------------- common
add_library(rr_common STATIC
    common/protocol.cpp
    common/chacha20.cpp
    common/crypto.cpp
    common/audiocodec.cpp
    common/resampler.cpp
    common/speechproc.cpp
    common/audioengine_shared.cpp
    ${RR_AUDIO_SOURCES}
)
target_include_directories(rr_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(rr_common PUBLIC RR_VERSION="${PROJECT_VERSION}")
target_link_libraries(rr_common PUBLIC Qt6::Core Qt6::Network)
if(ANDROID)
    # Oboe 1.10.0 ne declare que la cible "oboe", sans alias oboe::oboe.
    # On accepte les deux, au cas ou une version future en ajouterait un.
    if(TARGET oboe::oboe)
        target_link_libraries(rr_common PUBLIC oboe::oboe)
    else()
        target_link_libraries(rr_common PUBLIC oboe)
    endif()
    # Son repertoire d'en-tetes est declare en chemin relatif : on l'ajoute
    # explicitement pour ne pas dependre de la facon dont CMake le resout.
    target_include_directories(rr_common PUBLIC ${oboe_SOURCE_DIR}/include)
else()
    target_include_directories(rr_common PUBLIC ${PORTAUDIO_INCLUDE_DIR})
    target_link_libraries(rr_common PUBLIC ${PORTAUDIO_LIBRARY})
endif()
if(HAVE_OPUS)
    if(OPUS_FROM_SOURCE)
        target_link_libraries(rr_common PUBLIC Opus::opus)
        # The source tree lays the headers out flat, unlike a distribution
        # package, so audiocodec.cpp includes <opus.h> instead.
        target_compile_definitions(rr_common PUBLIC RR_OPUS_FLAT_HEADERS)
    else()
        target_include_directories(rr_common PUBLIC ${OPUS_INCLUDE_DIR})
        target_link_libraries(rr_common PUBLIC ${OPUS_LIBRARY})
    endif()
    target_compile_definitions(rr_common PUBLIC RR_HAVE_OPUS)
endif()

# ------------------------------------------------------------------- server
if(RR_BUILD_SERVER)
add_executable(remoterig-server WIN32
    server/main.cpp
    server/serverwindow.cpp
    server/servercore.cpp
    server/rigcontroller.cpp
    server/serverdaemon.cpp
    common/i18n.cpp
    common/i18n_menu.cpp
    common/about.cpp
    ${RR_QRC}
    ${RR_ICONS_QRC}
    ${RR_DOCS_QRC}
    ${RR_SERVER_RC}
)
add_dependencies(remoterig-server rr_translations)
target_link_libraries(remoterig-server PRIVATE rr_common Qt6::Widgets Qt6::SerialPort)
if(HAVE_HAMLIB)
    target_include_directories(remoterig-server PRIVATE ${HAMLIB_INCLUDE_DIR})
    target_link_libraries(remoterig-server PRIVATE ${HAMLIB_LIBRARY})
    target_compile_definitions(remoterig-server PRIVATE RR_HAVE_HAMLIB)

    # hamlib/rig.h includes <pthread.h> unconditionally and MSVC ships none.
    # compat/msvc supplies the two types the headers need, with the exact
    # widths winpthreads uses, so the structure layout matches the official
    # MinGW-built DLL. See compat/msvc/pthread.h.
    if(MSVC)
        target_include_directories(remoterig-server BEFORE PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/compat/msvc)
    endif()
endif()
endif()

# -------------------------------------------------------------------- client
# Deux interfaces sur le meme coeur : Widgets pour le bureau, Qt Quick pour le
# tactile. clientcore.cpp est partage sans modification.
if(RR_BUILD_WIDGETS)
    add_executable(remoterig-client WIN32
        client/main.cpp
        client/clientwindow.cpp
        client/levelmeter.cpp
        client/clientcore.cpp
        client/rigctldserver.cpp
        common/i18n.cpp
        common/i18n_menu.cpp
        common/about.cpp
        ${RR_QRC}
        ${RR_ICONS_QRC}
        ${RR_DOCS_QRC}
        ${RR_CLIENT_RC}
    )
    add_dependencies(remoterig-client rr_translations)
    target_link_libraries(remoterig-client PRIVATE rr_common Qt6::Widgets)
endif()

if(WITH_QML_CLIENT)
    set(RR_QML_SOURCES
        client/qml/main_qml.cpp
        client/qml/clientbridge.cpp
        client/qml/androidservice.cpp
        client/clientcore.cpp
        client/rigctldserver.cpp
        common/i18n.cpp
        ${RR_QRC}
        ${RR_ICONS_QRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/client/qml/qml.qrc)

    if(ANDROID)
        qt_add_executable(remoterig-client ${RR_QML_SOURCES})
        set(RR_QML_TARGET remoterig-client)
        set_property(TARGET remoterig-client PROPERTY
            QT_ANDROID_PACKAGE_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/android)
        # Le plugin Gradle d'Android 9 tire androidx.core 1.17, qui exige de
        # compiler contre l'API 36 ou plus recente. Il faut donc que
        # "platforms;android-36" soit installe dans le SDK.
        set(RR_ANDROID_MIN_SDK 26 CACHE STRING "Oldest Android release supported")
        set(RR_ANDROID_TARGET_SDK 36 CACHE STRING "Android API the app is compiled against")
        set_property(TARGET remoterig-client PROPERTY QT_ANDROID_MIN_SDK_VERSION ${RR_ANDROID_MIN_SDK})
        set_property(TARGET remoterig-client PROPERTY QT_ANDROID_TARGET_SDK_VERSION ${RR_ANDROID_TARGET_SDK})
        # AGP 9 ignore l'attribut package du manifeste : l'identifiant doit
        # venir d'ici, sinon l'application herite du nom par defaut de Qt.
        set_property(TARGET remoterig-client PROPERTY
            QT_ANDROID_PACKAGE_NAME "org.remoterig.client")
        set_property(TARGET remoterig-client PROPERTY
            QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}")
        # Android exige un entier croissant. On le derive de la version pour
        # n'avoir qu'un seul endroit a mettre a jour : 1.0.0 donne 10000,
        # 1.2.3 donne 10203. Un versionCode fige a 1 interdirait toute mise a
        # jour ulterieure.
        math(EXPR RR_ANDROID_VERSION_CODE
             "${PROJECT_VERSION_MAJOR} * 10000 + ${PROJECT_VERSION_MINOR} * 100 + ${PROJECT_VERSION_PATCH}")
        set_property(TARGET remoterig-client PROPERTY
            QT_ANDROID_VERSION_CODE ${RR_ANDROID_VERSION_CODE})
        message(STATUS "Android version ${PROJECT_VERSION} (code ${RR_ANDROID_VERSION_CODE})")
        # androiddeployqt scrute les imports QML pour savoir quels greffons
        # embarquer. Sans ce chemin il peut ne rien trouver, et l'application
        # demarre alors sur « module QtQuick.Controls plugin not found ».
        # Ne pas declarer la cible en module QML : cela fait taire le scanner.
        set_property(TARGET remoterig-client PROPERTY
            QT_QML_ROOT_PATH ${CMAKE_CURRENT_SOURCE_DIR}/client/qml)
    else()
        # Apercu sur le bureau : juger l'ergonomie sans passer par un telephone.
        add_executable(remoterig-client-qml ${RR_QML_SOURCES})
        set(RR_QML_TARGET remoterig-client-qml)
    endif()

    add_dependencies(${RR_QML_TARGET} rr_translations)
    target_link_libraries(${RR_QML_TARGET} PRIVATE
        rr_common Qt6::Quick Qt6::QuickControls2)
endif()

# ------------------------------------------------------------- installation
include(GNUInstallDirs)

if(RR_BUILD_SERVER)
    install(TARGETS remoterig-server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(RR_BUILD_WIDGETS)
    install(TARGETS remoterig-client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(WITH_QML_CLIENT AND NOT ANDROID)
    install(TARGETS remoterig-client-qml RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(NOT ANDROID)
    # Les raccourcis portent @BINDIR@ : on y met le chemin reel d'installation,
    # qui n'est pas le meme selon le prefixe.
    set(BINDIR ${CMAKE_INSTALL_FULL_BINDIR})
    set(RR_DESKTOP_FILES "")
    if(RR_BUILD_SERVER)
        list(APPEND RR_DESKTOP_FILES remoterig-server)
    endif()
    if(RR_BUILD_WIDGETS)
        list(APPEND RR_DESKTOP_FILES remoterig-client)
    endif()
    foreach(app IN LISTS RR_DESKTOP_FILES)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/desktop/${app}.desktop
                       ${CMAKE_CURRENT_BINARY_DIR}/${app}.desktop @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${app}.desktop
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    endforeach()

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons)

    # Unite systemd utilisateur : « systemctl --user enable remoterig-server ».
    if(UNIX AND NOT APPLE AND RR_BUILD_SERVER)
        # Chemin en dur, pas CMAKE_INSTALL_LIBDIR : celui-ci est qualifie par
        # l'architecture sur Debian, or systemd ne regarde que lib/systemd/user.
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/systemd/remoterig-server.service
                DESTINATION lib/systemd/user)

        # Regle udev pour le PTT par GPIO3, posee la ou udev la lit. Un
        # administrateur qui n'en veut pas la neutralise en deposant un fichier
        # vide du meme nom dans /etc/udev/rules.d, qui a la priorite.
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/udev/99-remoterig-cm108.rules
                DESTINATION lib/udev/rules.d)
        # Et une copie sous la documentation, pour la retrouver facilement.
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/packaging/udev/99-remoterig-cm108.rules
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/remoterig)
    endif()

    # Debian attend le nom du paquet, en minuscules, pas celui du projet.
    install(FILES README.md README.fr.md LICENSE.txt
                  docs/manual-fr.html docs/manual-en.html
            DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/remoterig)

    # Debian reclame un fichier de droits et un journal des versions compresse.
    if(UNIX AND NOT APPLE)
        # Paquet natif : le journal s'appelle changelog.gz, sans « .Debian ».
        set(RR_CHANGELOG_GZ ${CMAKE_CURRENT_BINARY_DIR}/changelog.gz)
        execute_process(
            COMMAND gzip -9 -n -c ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/changelog
            OUTPUT_FILE ${RR_CHANGELOG_GZ})
        install(FILES ${RR_CHANGELOG_GZ} ${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/copyright
                DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/doc/remoterig)

        # Pages de manuel, compressees comme Debian l'attend.
        set(RR_MAN_PAGES "")
        if(RR_BUILD_SERVER)
            list(APPEND RR_MAN_PAGES remoterig-server)
        endif()
        if(RR_BUILD_WIDGETS)
            list(APPEND RR_MAN_PAGES remoterig-client)
        endif()
        if(WITH_QML_CLIENT)
            list(APPEND RR_MAN_PAGES remoterig-client-qml)
        endif()
        foreach(page IN LISTS RR_MAN_PAGES)
            execute_process(
                COMMAND gzip -9 -n -c ${CMAKE_CURRENT_SOURCE_DIR}/packaging/man/${page}.1
                OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/${page}.1.gz)
            install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${page}.1.gz
                    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
        endforeach()
    endif()
endif()

# ------------------------------------------------------------- paquet Debian
# Les dependances ne sont pas ecrites a la main : dpkg-shlibdeps les deduit des
# bibliotheques reellement liees, ce qui donne la bonne liste sur Ubuntu comme
# sur Raspberry Pi OS, dont les versions de Qt different.
if(NOT ANDROID)
    set(CPACK_PACKAGE_NAME "remoterig")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    # lintian exige la forme « Nom <adresse> » ; a remplacer par la votre.
    set(CPACK_PACKAGE_CONTACT "RemoteRig Project <noreply@remoterig.invalid>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Remote amateur radio station: audio, PTT and CAT over the network")
    set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/")
    set(CPACK_PACKAGE_DESCRIPTION
"Operate an amateur radio transceiver over the network. A server runs next to\n\
 the rig and shares its audio, PTT and CAT control; a client runs wherever the\n\
 operator is. Opus or 16-bit PCM audio, sub-100 ms latency on a local network,\n\
 optional encryption, and a rigctld interface for data-mode software.")
    # Les binaires non allegés font trois fois leur taille utile.
    set(CPACK_STRIP_FILES ON)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

    set(CPACK_GENERATOR "DEB")
    set(CPACK_DEBIAN_PACKAGE_SECTION "hamradio")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pipewire | pulseaudio")

    # Les modules QML sont charges a l'execution par le moteur, jamais lies :
    # dpkg-shlibdeps ne peut pas les voir. Sans eux, le client tactile ouvre
    # une fenetre vide, sans message d'erreur.
    if(WITH_QML_CLIENT)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript")
    endif()
    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postinst;${CMAKE_CURRENT_SOURCE_DIR}/packaging/deb/postrm")
    include(CPack)
endif()
