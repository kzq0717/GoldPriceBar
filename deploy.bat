@echo off
chcp 65001 >nul
setlocal EnableExtensions

cd /d "%~dp0"

if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"

set "EXE="
if exist "build\Release\GoldPriceBarLite.exe" set "EXE=build\Release\GoldPriceBarLite.exe"
if not defined EXE if exist "build\GoldPriceBarLite.exe" set "EXE=build\GoldPriceBarLite.exe"

if not defined EXE (
    echo [错误] 未找到可执行文件，请先运行 build.bat
    exit /b 1
)

if not exist "%QT6_ROOT%\bin\windeployqt.exe" (
    echo [错误] 未找到 windeployqt: %QT6_ROOT%\bin\windeployqt.exe
    exit /b 1
)

echo 部署 Qt 运行库到: %EXE%
"%QT6_ROOT%\bin\windeployqt.exe" --release --no-translations "%EXE%"
if errorlevel 1 (
    echo [错误] windeployqt 失败
    exit /b 1
)

echo 部署完成。可直接运行: %EXE%
exit /b 0
