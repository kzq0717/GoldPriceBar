@echo off
setlocal
cd /d "%~dp0"
if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "PATH=%QT6_ROOT%\bin;%PATH%"

set "EXE="
if exist "build\Release\GoldPriceBarLite.exe" set "EXE=build\Release\GoldPriceBarLite.exe"
if not defined EXE if exist "build\GoldPriceBarLite.exe" set "EXE=build\GoldPriceBarLite.exe"
if not defined EXE (
  echo [ERROR] exe not found, run build.bat first
  exit /b 1
)
echo Using PATH Qt: %QT6_ROOT%\bin
echo Starting %EXE%
start "" "%EXE%"
