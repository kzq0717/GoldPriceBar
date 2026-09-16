@echo off
setlocal
cd /d "%~dp0"
if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "PATH=%QT6_ROOT%\bin;%PATH%"

set "EXE="
if exist "build\Release\GoldPriceBarLite.exe" set "EXE=%cd%\build\Release\GoldPriceBarLite.exe"
if not defined EXE if exist "build\GoldPriceBarLite.exe" set "EXE=%cd%\build\GoldPriceBarLite.exe"
if not defined EXE (
  echo [ERROR] exe not found, run build.bat first
  pause
  exit /b 1
)

echo QT6_ROOT=%QT6_ROOT%
echo EXE=%EXE%
echo.
echo If window not visible: check system tray, or press Ctrl+Shift+G
echo.
start "" "%EXE%"
timeout /t 2 >nul
echo If still nothing, check log:
echo   %%APPDATA%%\GoldPriceBarLite\logs\
echo Or run from cmd to see errors:
echo   "%EXE%"
pause
