@echo off
rem ===========================================================================
rem  RemoteRig - complete Windows build
rem
rem  Configures, compiles, deploys the runtime libraries and builds the
rem  installer, in one go.
rem
rem  Run it from the project root, in a x64 Native Tools Command Prompt
rem  for VS 2022 or VS 2026.
rem
rem  Options:
rem    /clean        wipe the build directory first
rem    /nobuild      skip configure and compile, deploy and package only
rem    /noinstaller  stop after staging, do not run Inno Setup
rem    /help         print the options
rem
rem  Note: these comments deliberately avoid the help switch and the
rem  characters cmd treats specially. A rem line carrying the help switch
rem  makes cmd print the help for REM and then reject the words after it.
rem ===========================================================================

setlocal enabledelayedexpansion
title RemoteRig - complete build

rem ------------------------------------------------------- paths to adjust
set "QT_DIR=C:\Qt\6.11.2\msvc2022_64"
set "VCPKG_ROOT=C:\vcpkg"
set "HAMLIB_DIR=C:\hamlib"

set "BUILD_DIR=build"
set "CONFIG=Release"
set "DIST=installer\dist"

rem ------------------------------------------------------------- arguments
set DO_CLEAN=0
set DO_BUILD=1
set DO_INSTALLER=1

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="/clean" ( set "DO_CLEAN=1" & shift & goto parse )
if /i "%~1"=="/nobuild" ( set "DO_BUILD=0" & shift & goto parse )
if /i "%~1"=="/noinstaller" ( set "DO_INSTALLER=0" & shift & goto parse )
if /i "%~1"=="/?" goto usage
if /i "%~1"=="/help" goto usage
if /i "%~1"=="-h" goto usage
echo Unknown option: %~1
goto usage
:parsed

if not exist "CMakeLists.txt" (
    echo [X] Run this script from the project root, next to CMakeLists.txt.
    goto fail
)

rem ---------------------------------------------------------- prerequisites
echo.
echo === Checking prerequisites ===

where cmake >nul 2>&1
if errorlevel 1 (
    echo [X] cmake not found in PATH. Open a "x64 Native Tools Command Prompt".
    goto fail
)

where cl >nul 2>&1
if errorlevel 1 (
    echo [X] cl.exe not found. This must run in a "x64 Native Tools Command Prompt".
    goto fail
)

if not exist "%QT_DIR%\bin\windeployqt.exe" (
    echo [X] Qt not found at %QT_DIR%
    echo     Edit QT_DIR at the top of this script.
    goto fail
)
echo [ok] Qt          %QT_DIR%

set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not exist "%VCPKG_TOOLCHAIN%" (
    echo [X] vcpkg not found at %VCPKG_ROOT%
    goto fail
)
set "VCPKG_BIN=%VCPKG_ROOT%\installed\x64-windows\bin"
if not exist "%VCPKG_BIN%\portaudio.dll" (
    echo [X] portaudio.dll missing. Run:
    echo     "%VCPKG_ROOT%\vcpkg.exe" install portaudio:x64-windows opus:x64-windows
    goto fail
)
echo [ok] vcpkg       %VCPKG_ROOT%

rem Hamlib is optional. Without it only the client is built.
set HAVE_HAMLIB=1
if not exist "%HAMLIB_DIR%\include\hamlib\rig.h"   set HAVE_HAMLIB=0
if not exist "%HAMLIB_DIR%\lib\msvc\hamlib.lib"    set HAVE_HAMLIB=0
if "%HAVE_HAMLIB%"=="1" (
    echo [ok] Hamlib      %HAMLIB_DIR%
) else (
    echo [--] Hamlib not usable, the client alone will be built.
    if exist "%HAMLIB_DIR%\lib\msvc\libhamlib-4.def" (
        echo     The import library is missing. Generate it once with:
        echo       cd /d %HAMLIB_DIR%\lib\msvc
        echo       lib /def:libhamlib-4.def /machine:x64 /out:hamlib.lib
    )
)

rem Version de l'application, lue dans CMakeLists.txt : le script Inno Setup
rem portait la sienne en dur, et l'installateur restait en 1.0.0 quoi qu'on
rem fasse. Cette ligne reste au niveau superieur : la chaine cherchee contient
rem une parenthese, qui casserait un bloc parenthese, comme plus bas pour ISCC.
set "APPVER="
for /f "tokens=3 delims= " %%V in ('findstr /b /c:"project(RemoteRig VERSION" CMakeLists.txt') do set "APPVER=%%V"

rem Inno Setup, looked up in the usual places then in PATH.
rem Careful: the ProgramFiles x86 variable carries parentheses, which break
rem a parenthesised block. These lines stay at top level on purpose.
set "ISCC="
set "PF86=%ProgramFiles(x86)%"
if not defined PF86 set "PF86=C:\Program Files (x86)"
if exist "%PF86%\Inno Setup 6\ISCC.exe" set "ISCC=%PF86%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%PF86%\Inno Setup 7\ISCC.exe" set "ISCC=%PF86%\Inno Setup 7\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 7\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 7\ISCC.exe"
if not defined ISCC for /f "delims=" %%P in ('where ISCC 2^>nul') do if not defined ISCC set "ISCC=%%P"

if defined APPVER echo [ok] Version     !APPVER!

if "%DO_INSTALLER%"=="1" (
    if defined ISCC (
        echo [ok] Inno Setup  !ISCC!
    ) else (
        echo [--] Inno Setup not found, the installer step will be skipped.
        set DO_INSTALLER=0
    )
)

rem ------------------------------------------------------------------ clean
if "%DO_CLEAN%"=="1" (
    echo.
    echo === Cleaning ===
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%DIST%"      rmdir /s /q "%DIST%"
    if exist "installer\output" rmdir /s /q "installer\output"
)

rem -------------------------------------------------------------- configure
if "%DO_BUILD%"=="1" (
    echo.
    echo === Configuring ===
    if "%HAVE_HAMLIB%"=="1" (
        cmake -B "%BUILD_DIR%" ^
            -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
            -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
            -DHAMLIB_INCLUDE_DIR="%HAMLIB_DIR%/include" ^
            -DHAMLIB_LIBRARY="%HAMLIB_DIR%/lib/msvc/hamlib.lib"
    ) else (
        cmake -B "%BUILD_DIR%" ^
            -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%" ^
            -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
            -DWITH_HAMLIB=OFF
    )
    if errorlevel 1 (
        echo [X] Configuration failed.
        goto fail
    )

    echo.
    echo === Compiling %CONFIG% ===
    cmake --build "%BUILD_DIR%" --config %CONFIG%
    if errorlevel 1 (
        echo [X] Compilation failed.
        goto fail
    )
)

set "OUT=%BUILD_DIR%\%CONFIG%"
if not exist "%OUT%\remoterig-client.exe" (
    echo [X] %OUT%\remoterig-client.exe missing. Compile without /nobuild first.
    goto fail
)

rem ------------------------------------------------------------------ stage
echo.
echo === Gathering into %DIST% ===
if exist "%DIST%" rmdir /s /q "%DIST%"
mkdir "%DIST%" 2>nul

copy /y "%OUT%\remoterig-client.exe" "%DIST%\" >nul
if exist "%OUT%\remoterig-server.exe" copy /y "%OUT%\remoterig-server.exe" "%DIST%\" >nul

echo   Qt runtime...
"%QT_DIR%\bin\windeployqt.exe" --release --no-system-d3d-compiler --no-opengl-sw ^
    "%DIST%\remoterig-client.exe" >nul
if errorlevel 1 (
    echo [X] windeployqt failed on the client.
    goto fail
)
if exist "%DIST%\remoterig-server.exe" (
    "%QT_DIR%\bin\windeployqt.exe" --release --no-system-d3d-compiler --no-opengl-sw ^
        "%DIST%\remoterig-server.exe" >nul
    if errorlevel 1 (
        echo [X] windeployqt failed on the server.
        goto fail
    )
)

echo   PortAudio and Opus...
copy /y "%VCPKG_BIN%\portaudio.dll" "%DIST%\" >nul
copy /y "%VCPKG_BIN%\opus.dll"      "%DIST%\" >nul

if "%HAVE_HAMLIB%"=="1" (
    echo   Hamlib runtime...
    for %%D in (libhamlib-4.dll libusb-1.0.dll libgcc_s_seh-1.dll libwinpthread-1.dll) do (
        if exist "%HAMLIB_DIR%\bin\%%D" (
            copy /y "%HAMLIB_DIR%\bin\%%D" "%DIST%\" >nul
        ) else (
            echo   [warn] %%D missing from %HAMLIB_DIR%\bin
        )
    )
)

rem -------------------------------------------------------------- installer
if "%DO_INSTALLER%"=="1" (
    echo.
    echo === Building the installer ===
    if not defined APPVER (
        echo [--] Version not found in CMakeLists.txt, the installer keeps its default.
        "%ISCC%" /Q "installer\RemoteRig.iss"
    ) else (
        echo [ok] Installer version !APPVER!
        "%ISCC%" /Q "/DAppVersion=!APPVER!" "installer\RemoteRig.iss"
    )
    if errorlevel 1 (
        echo [X] Inno Setup failed.
        goto fail
    )
)

rem ----------------------------------------------------------------- report
echo.
echo ===========================================================
echo  Done.
echo.
echo  Programs        %OUT%
echo  Ready to run    %DIST%
if "%DO_INSTALLER%"=="1" (
    rem No quotes around the pattern: cmd only expands an unquoted wildcard.
    for %%F in (installer\output\*.exe) do echo  Installer       installer\output\%%~nxF   %%~zF bytes
)
if "%HAVE_HAMLIB%"=="0" echo.
if "%HAVE_HAMLIB%"=="0" echo  Note: built without Hamlib, so without the station server.
echo ===========================================================
endlocal
exit /b 0

:usage
echo.
echo Usage: build_all.bat [/clean] [/nobuild] [/noinstaller]
echo.
echo   /clean        wipe the build directory first
echo   /nobuild      skip configure and compile, deploy and package only
echo   /noinstaller  stop after staging, do not run Inno Setup
echo.
echo Adjust QT_DIR, VCPKG_ROOT and HAMLIB_DIR at the top of the file.
endlocal
exit /b 0

:fail
echo.
echo Build aborted.
endlocal
exit /b 1
