@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

REM ============================================================
REM  GoldPriceBarLite v1.0 一键编译 (Windows)
REM  目标：goldsdk（无 Qt 静态库）+ GoldPriceBarLite.exe（Qt6 UI）
REM
REM  用法：
REM    build.bat              配置并编译 Release
REM    build.bat clean        清理后重新配置并编译
REM    build.bat run          编译后启动
REM    build.bat clean run    清理、编译并启动
REM    build.bat deploy       编译后执行 windeployqt
REM
REM  环境变量（可选覆盖）：
REM    set QT6_ROOT=D:\path\to\Qt\6.7.3\msvc2022_64
REM ============================================================

cd /d "%~dp0"

REM ---------- 默认 Qt 路径（可改 / 可被环境变量覆盖）----------
if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "BUILD_DIR=build"
set "CONFIG=Release"
set "ARCH=x64"
REM -----------------------------------------------------------

set "DO_CLEAN=0"
set "DO_RUN=0"
set "DO_DEPLOY=0"
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="clean"  set "DO_CLEAN=1"
if /i "%~1"=="run"    set "DO_RUN=1"
if /i "%~1"=="deploy" set "DO_DEPLOY=1"
shift
goto parse_args
:args_done

echo.
echo ========================================
echo   GoldPriceBarLite 自动编译  v1.0
echo ========================================
echo   Qt6 路径 : %QT6_ROOT%
echo   输出目录 : %BUILD_DIR% ^| %CONFIG%
echo   组件     : goldsdk + UI
echo ========================================
echo.

if not exist "%QT6_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [错误] 未找到 Qt6Config.cmake
    echo        请设置 QT6_ROOT 为 Qt 的 msvc 套件目录，例如：
    echo        set QT6_ROOT=D:\Qt\6.7.3\msvc2022_64
    echo        或编辑本脚本中的默认 QT6_ROOT
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到 cmake，请安装 CMake 并加入 PATH
    exit /b 1
)

REM ---------- 探测 CMake 生成器 ----------
set "GENERATOR="
set "USE_NINJA=0"

where ninja >nul 2>&1
if not errorlevel 1 (
    REM 若已在 VS 开发者命令行中（有 cl），优先 Ninja 更快
    where cl >nul 2>&1
    if not errorlevel 1 (
        set "GENERATOR=Ninja"
        set "USE_NINJA=1"
    )
)

if not defined GENERATOR (
    REM Visual Studio：优先 2022，其次尝试 2026 命名
    cmake -G "Visual Studio 17 2022" -h >nul 2>&1
    if not errorlevel 1 (
        set "GENERATOR=Visual Studio 17 2022"
    ) else (
        cmake -G "Visual Studio 18 2026" -h >nul 2>&1
        if not errorlevel 1 (
            set "GENERATOR=Visual Studio 18 2026"
        )
    )
)

if not defined GENERATOR (
    echo [错误] 未找到可用生成器。请安装：
    echo   - Visual Studio 2022（含“使用 C++ 的桌面开发”）
    echo   或 Ninja + 在 “x64 Native Tools Command Prompt” 中运行本脚本
    exit /b 1
)

echo [信息] 生成器: %GENERATOR%
if "%USE_NINJA%"=="0" echo [信息] 平台  : %ARCH%
echo.

if "%DO_CLEAN%"=="1" (
    echo [清理] 删除 %BUILD_DIR% ...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/2] CMake 配置（含 sdk/goldsdk）...
if "%USE_NINJA%"=="1" (
    cmake -S . -B "%BUILD_DIR%" ^
        -G "Ninja" ^
        -DCMAKE_BUILD_TYPE=%CONFIG% ^
        -DQT6_ROOT_DIR="%QT6_ROOT%" ^
        -DCMAKE_PREFIX_PATH="%QT6_ROOT%"
) else (
    cmake -S . -B "%BUILD_DIR%" ^
        -G "%GENERATOR%" -A %ARCH% ^
        -DQT6_ROOT_DIR="%QT6_ROOT%" ^
        -DCMAKE_PREFIX_PATH="%QT6_ROOT%"
)
if errorlevel 1 (
    echo [错误] CMake 配置失败
    echo 提示: 首次请在 “x64 Native Tools Command Prompt for VS” 中运行，
    echo       或确认已安装 C++ 工作负载与 Qt 6 Charts/Sql 组件。
    exit /b 1
)

echo.
echo [2/2] 编译 goldsdk + GoldPriceBarLite (%CONFIG%) ...
if "%USE_NINJA%"=="1" (
    cmake --build "%BUILD_DIR%" --parallel
) else (
    cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
)
if errorlevel 1 (
    echo [错误] 编译失败
    exit /b 1
)

set "EXE="
if exist "%BUILD_DIR%\%CONFIG%\GoldPriceBarLite.exe" set "EXE=%BUILD_DIR%\%CONFIG%\GoldPriceBarLite.exe"
if not defined EXE if exist "%BUILD_DIR%\GoldPriceBarLite.exe" set "EXE=%BUILD_DIR%\GoldPriceBarLite.exe"
if not defined EXE if exist "%BUILD_DIR%\Release\GoldPriceBarLite.exe" set "EXE=%BUILD_DIR%\Release\GoldPriceBarLite.exe"

echo.
echo ========================================
if defined EXE (
    echo   编译成功
    echo   可执行文件: %EXE%
) else (
    echo   编译完成，但未找到 GoldPriceBarLite.exe
    echo   请在 %BUILD_DIR% 下手动查找
    exit /b 1
)
echo ========================================

if "%DO_DEPLOY%"=="1" (
    echo.
    echo [deploy] windeployqt ...
    if exist "%QT6_ROOT%\bin\windeployqt.exe" (
        "%QT6_ROOT%\bin\windeployqt.exe" --%CONFIG% --no-translations "%EXE%"
        if errorlevel 1 (
            echo [警告] windeployqt 失败，可稍后手动执行
        ) else (
            echo [deploy] 完成
        )
    ) else (
        echo [警告] 未找到 windeployqt.exe
    )
) else (
    echo.
    echo [提示] 缺 DLL 时执行:  build.bat deploy
    echo         或: "%QT6_ROOT%\bin\windeployqt.exe" "%EXE%"
)

if "%DO_RUN%"=="1" (
    echo [运行] %EXE%
    start "" "%EXE%"
)

exit /b 0
