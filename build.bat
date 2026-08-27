@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

REM ============================================================
REM  GoldPriceBarLite 一键编译脚本 (Windows / Qt 6.7 + MSVC 2022)
REM  用法：
REM    双击运行  或  在工程根目录执行 build.bat
REM    build.bat clean     清理后重新配置并编译
REM    build.bat run       编译成功后启动程序
REM ============================================================

cd /d "%~dp0"

REM ---------- 可按本机环境修改 ----------
set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "BUILD_DIR=build"
set "CONFIG=Release"
set "GENERATOR=Visual Studio 17 2022"
set "ARCH=x64"
REM ------------------------------------

echo.
echo ========================================
echo   GoldPriceBarLite 自动编译
echo ========================================
echo   Qt6 路径 : %QT6_ROOT%
echo   生成器   : %GENERATOR% ^| %ARCH%
echo   配置     : %CONFIG%
echo ========================================
echo.

REM 检查 Qt
if not exist "%QT6_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
    if not exist "%QT6_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
        echo [错误] 未找到 Qt6，请检查 QT6_ROOT：
        echo        %QT6_ROOT%
        echo 可在本脚本开头修改 set "QT6_ROOT=..."
        exit /b 1
    )
)

REM 检查 cmake
where cmake >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到 cmake，请安装 CMake 并加入 PATH
    exit /b 1
)

REM clean
if /i "%~1"=="clean" (
    echo [清理] 删除 %BUILD_DIR% ...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/2] CMake 配置...
cmake -S . -B "%BUILD_DIR%" ^
    -G "%GENERATOR%" -A %ARCH% ^
    -DQT6_ROOT_DIR="%QT6_ROOT%" ^
    -DCMAKE_PREFIX_PATH="%QT6_ROOT%"
if errorlevel 1 (
    echo [错误] CMake 配置失败
    exit /b 1
)

echo.
echo [2/2] 编译 %CONFIG% ...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
if errorlevel 1 (
    echo [错误] 编译失败
    exit /b 1
)

set "EXE=%BUILD_DIR%\%CONFIG%\GoldPriceBarLite.exe"
if not exist "%EXE%" (
    REM Ninja / 单配置生成器时可能在 build 根目录
    set "EXE=%BUILD_DIR%\GoldPriceBarLite.exe"
)

echo.
echo ========================================
echo   编译成功
echo   可执行文件: %EXE%
echo ========================================

REM 可选：复制 Qt 运行库到输出目录（便于直接运行）
if exist "%EXE%" (
    echo.
    echo [提示] 若直接运行报缺 DLL，可在「x64 Native Tools」中执行：
    echo   "%QT6_ROOT%\bin\windeployqt.exe" "%EXE%"
)

if /i "%~1"=="run" (
    if exist "%EXE%" (
        echo [运行] %EXE%
        start "" "%EXE%"
    )
)

if /i "%~2"=="run" (
    if exist "%EXE%" (
        echo [运行] %EXE%
        start "" "%EXE%"
    )
)

exit /b 0
