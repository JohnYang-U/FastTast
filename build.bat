@echo off
setlocal

REM ===========================================================================
REM  METAQuickCheck one-click build  (Qt5 32-bit + MSVC x86)
REM
REM  Usage:
REM    build.bat                          use default Qt path
REM    build.bat D:\Qt5\5.14.2\msvc2017   specify Qt kit
REM    build.bat D:\Qt5\5.14.2\msvc2017 clean    clean rebuild
REM
REM  Generator: tries "Visual Studio 16 2019" first; if that generator is not
REM  installed it falls back to "Visual Studio 17 2022" with the v142 toolset.
REM  Both produce Win32/x86, which is mandatory: every META DLL is 32-bit.
REM  (The first probe is silent on purpose -- a missing generator is a normal
REM   situation here, not an error worth printing.)
REM ===========================================================================

set QT_DIR=%~1
if "%QT_DIR%"=="" set QT_DIR=D:\Qt5\5.14.2\msvc2017

set DO_CLEAN=0
if /I "%~2"=="clean" set DO_CLEAN=1

set HERE=%~dp0
set BUILD_DIR=%~dp0build
set PROBE_LOG=%TEMP%\mqc_configure_probe.log

echo ============================================================
echo  METAQuickCheck build
echo    Qt      : %QT_DIR%
echo    Source  : %HERE%
echo    Output  : %BUILD_DIR%\bin
echo ============================================================
echo.

if not exist "%QT_DIR%\bin\qmake.exe" goto noqt

REM Put our Qt5 bin FIRST: a Qt6 kit on PATH confuses Qt5 windeployqt
REM (it resolves qmake via PATH, then looks for Qt5*.dll inside the Qt6 dir).
set PATH=%QT_DIR%\bin;%PATH%

set CMAKE_EXE=
where cmake >nul 2>&1
if not errorlevel 1 set CMAKE_EXE=cmake
if not defined CMAKE_EXE if exist "D:\Cmake\bin\cmake.exe" set CMAKE_EXE=D:\Cmake\bin\cmake.exe
if not defined CMAKE_EXE if exist "D:\VisualStudio2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set CMAKE_EXE=D:\VisualStudio2022\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
if not defined CMAKE_EXE if exist "D:\VS2019Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set CMAKE_EXE=D:\VS2019Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
if not defined CMAKE_EXE if exist "C:\Program Files\CMake\bin\cmake.exe" set CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe
if not defined CMAKE_EXE goto nocmake
echo Using CMake: %CMAKE_EXE%

if not "%DO_CLEAN%"=="1" goto skipclean
echo Cleaning build dir ...
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
:skipclean

echo.
echo [1/2] Configure ...
"%CMAKE_EXE%" -S "%HERE%." -B "%BUILD_DIR%" -G "Visual Studio 16 2019" -A Win32 -DCMAKE_PREFIX_PATH="%QT_DIR%" > "%PROBE_LOG%" 2>&1
if not errorlevel 1 goto configured
echo        VS 2019 generator not found -- falling back to VS 2022 -T v142
if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
"%CMAKE_EXE%" -S "%HERE%." -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A Win32 -T v142 -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 goto failed

:configured
echo.
echo [2/2] Building (Release) ...
"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release
if errorlevel 1 goto failed

echo.
echo ============================================================
echo  BUILD OK
echo    exe : %BUILD_DIR%\bin\METAQuickCheck.exe
echo.
echo  Self test : %BUILD_DIR%\bin\METAQuickCheck.exe --selftest
echo  GUI       : run the exe, then set "META runtime dir" and click "Re-check"
echo ============================================================
exit /b 0

:noqt
echo [ERROR] Qt not found: %QT_DIR%\bin\qmake.exe
echo         Pass the path explicitly, e.g.:
echo         build.bat D:\Qt5\5.14.2\msvc2017
exit /b 1

:nocmake
echo [ERROR] cmake.exe not found (not in PATH, not in VS dirs)
exit /b 1

:failed
echo.
echo ============================================================
echo  BUILD FAILED - see errors above
echo ============================================================
exit /b 1
