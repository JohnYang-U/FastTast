@echo off
setlocal

REM ===========================================================================
REM  pack.bat -- package FastTest for another computer
REM
REM    pack.bat          slim : source + headers only   (~6 MB)
REM    pack.bat full     full : + sdk\META\bin + build\bin (~210 MB)
REM
REM  Output goes to dist\FastTest_<mode>_<timestamp>.zip
REM ===========================================================================

set MODE=%~1
if "%MODE%"=="" set MODE=slim

if /I "%MODE%"=="slim" goto run
if /I "%MODE%"=="full" goto run
echo [ERROR] unknown mode: %MODE%   (use: pack.bat  or  pack.bat full)
exit /b 1

:run
echo Packaging mode: %MODE%
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\pack.ps1" -Mode %MODE%
if errorlevel 1 goto failed
exit /b 0

:failed
echo.
echo [ERROR] packaging failed
exit /b 1
