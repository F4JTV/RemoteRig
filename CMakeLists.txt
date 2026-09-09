cmake_minimum_required(VERSION 3.19)
project(RemoteRig VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network SerialPort)

# -------------------------------------------------------------- translations
# The .qm ships with the sources, so the project builds without the Linguist
# tools. When they are available the .qm is regenerated from the .ts.
set(RR_TS ${CMAKE_CURRENT_SOURCE_DIR}/i18n/remoterig_fr.ts)
set(RR_QM ${CMAKE_CURRENT_SOURCE_DIR}/i18n/remoterig_fr.qm)
set(RR_QRC ${CMAKE_CURRENT_SOURCE_DIR}/i18n/translations.qrc)
set(RR_ICONS_QRC ${CMAKE_CURRENT_SOURCE_DIR}/icons/icons.qrc)

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

# ---------------------------------------------------------------- PortAudio
find_path(PORTAUDIO_INCLUDE_DIR portaudio.h)
find_library(PORTAUDIO_LIBRARY NAMES portaudio portaudio_x64 portaudio_static)
if(NOT PORTAUDIO_INCLUDE_DIR OR NOT PORTAUDIO_LIBRARY)
    message(FATAL_ERROR "PortAudio not found. Debian: apt install portaudio19-dev. Windows: vcpkg install portaudio:x64-windows")
endif()

# --------------------------------------------------------------------- Opus
option(WITH_OPUS "Compress audio with Opus" ON)
find_path(OPUS_INCLUDE_DIR opus/opus.h)
find_library(OPUS_LIBRARY NAMES opus)
if(WITH_OPUS AND OPUS_INCLUDE_DIR AND OPUS_LIBRARY)
    set(HAVE_OPUS TRUE)
else()
    set(HAVE_OPUS FALSE)
    message(WARNING "Opus not found: only 16-bit PCM will be available")
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
    common/audioengine.cpp
)
target_include_directories(rr_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${PORTAUDIO_INCLUDE_DIR})
target_link_libraries(rr_common PUBLIC Qt6::Core Qt6::Network ${PORTAUDIO_LIBRARY})
if(HAVE_OPUS)
    target_include_directories(rr_common PUBLIC ${OPUS_INCLUDE_DIR})
    target_link_libraries(rr_common PUBLIC ${OPUS_LIBRARY})
    target_compile_definitions(rr_common PUBLIC RR_HAVE_OPUS)
endif()

# ------------------------------------------------------------------- server
add_executable(remoterig-server WIN32
    server/main.cpp
    server/serverwindow.cpp
    server/servercore.cpp
    server/rigcontroller.cpp
    common/i18n.cpp
    ${RR_QRC}
    ${RR_ICONS_QRC}
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

# -------------------------------------------------------------------- client
add_executable(remoterig-client WIN32
    client/main.cpp
    client/clientwindow.cpp
    client/clientcore.cpp
    client/rigctldserver.cpp
    common/i18n.cpp
    ${RR_QRC}
    ${RR_ICONS_QRC}
    ${RR_CLIENT_RC}
)
add_dependencies(remoterig-client rr_translations)
target_link_libraries(remoterig-client PRIVATE rr_common Qt6::Widgets)

install(TARGETS remoterig-server remoterig-client RUNTIME DESTINATION bin)
