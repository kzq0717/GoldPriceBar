@echo off
chcp 65001 >nul
setlocal

REM 将 Qt 依赖拷贝到 Release 目录，生成可独立运行的目录

cd /d "%~dp0"

set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "EXE=build\Release\GoldPriceBarLite.exe"

if not exist "%EXE%" (
    echo [错误] 未找到 %EXE%，请先运行 build.bat
    exit /b 1
)

if not exist "%QT6_ROOT%\bin\windeployqt.exe" (
    echo [错误] 未找到 windeployqt：%QT6_ROOT%\bin\windeployqt.exe
    exit /b 1
)

echo 正在部署 Qt 运行库到 build\Release ...
"%QT6_ROOT%\bin\windeployqt.exe" --release --no-translations "%EXE%"
if errorlevel 1 (
    echo [错误] windeployqt 失败
    exit /b 1
)

echo.
echo 部署完成。可直接运行: %EXE%
exit /b 0
