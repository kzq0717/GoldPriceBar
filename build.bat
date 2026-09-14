@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion

REM ============================================================
REM  GoldPriceBarLite v1.0 一键编译 (Windows)
REM  编译: goldsdk + GoldPriceBarLite.exe
REM
REM  用法:
REM    build.bat              配置并编译 Release
REM    build.bat clean        删除 build 后重新配置编译
REM    build.bat run          编译后启动
REM    build.bat deploy       编译后 windeployqt
REM    build.bat ninja        强制使用 Ninja（需已在 VS 开发者命令行）
REM    build.bat vs           强制使用 Visual Studio 生成器
REM    build.bat clean ninja  清理后用 Ninja 编译
REM
REM  环境变量:
REM    set QT6_ROOT=D:\path\to\Qt\6.7.3\msvc2022_64
REM ============================================================

cd /d "%~dp0"

if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "BUILD_DIR=build"
set "CONFIG=Release"
set "ARCH=x64"

set "DO_CLEAN=0"
set "DO_RUN=0"
set "DO_DEPLOY=0"
set "FORCE_NINJA=0"
set "FORCE_VS=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="clean"  set "DO_CLEAN=1"
if /i "%~1"=="run"    set "DO_RUN=1"
if /i "%~1"=="deploy" set "DO_DEPLOY=1"
if /i "%~1"=="ninja"  set "FORCE_NINJA=1"
if /i "%~1"=="vs"     set "FORCE_VS=1"
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
    echo        set QT6_ROOT=你的Qt\msvc2022_64目录
    exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
    echo [错误] 未找到 cmake，请加入 PATH
    exit /b 1
)

REM ---------- 读取已有 CMakeCache 中的生成器 ----------
set "CACHE_GEN="
if exist "%BUILD_DIR%\CMakeCache.txt" (
    for /f "tokens=2 delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt" 2^>nul') do set "CACHE_GEN=%%A"
)

REM ---------- 选择生成器 ----------
set "GENERATOR="
set "USE_NINJA=0"

if "%FORCE_NINJA%"=="1" (
    where ninja >nul 2>&1
    if errorlevel 1 (
        echo [错误] 未找到 ninja.exe，请安装或改用: build.bat vs
        exit /b 1
    )
    where cl >nul 2>&1
    if errorlevel 1 (
        echo [错误] 未找到 cl.exe，请在 "x64 Native Tools Command Prompt for VS" 中运行
        exit /b 1
    )
    set "GENERATOR=Ninja"
    set "USE_NINJA=1"
    goto gen_selected
)

if "%FORCE_VS%"=="1" goto pick_vs

REM 默认: 若已有缓存，沿用原生成器，避免 Ninja/VS 混用报错
if defined CACHE_GEN (
    echo [信息] 检测到已有构建缓存生成器: %CACHE_GEN%
    if /i "%CACHE_GEN%"=="Ninja" (
        set "GENERATOR=Ninja"
        set "USE_NINJA=1"
        goto gen_selected
    )
    set "GENERATOR=%CACHE_GEN%"
    set "USE_NINJA=0"
    goto gen_selected
)

REM 无缓存: 有 cl+ninja 时可用 Ninja，否则 VS
where cl >nul 2>&1
if not errorlevel 1 (
    where ninja >nul 2>&1
    if not errorlevel 1 (
        set "GENERATOR=Ninja"
        set "USE_NINJA=1"
        goto gen_selected
    )
)

:pick_vs
REM VS 生成器探测（2026 优先，再 2022）
cmake -G "Visual Studio 18 2026" -h >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Visual Studio 18 2026"
    goto gen_selected
)
cmake -G "Visual Studio 17 2022" -h >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Visual Studio 17 2022"
    goto gen_selected
)

echo [错误] 未找到 Visual Studio CMake 生成器
echo        请安装 VS C++ 工作负载，或: build.bat ninja
exit /b 1

:gen_selected
echo [信息] 使用生成器: %GENERATOR%
if "%USE_NINJA%"=="0" echo [信息] 平台: %ARCH%
echo.

REM 若用户指定的生成器与缓存不一致，必须清理
if defined CACHE_GEN (
    if /i not "%CACHE_GEN%"=="%GENERATOR%" (
        echo [警告] 缓存生成器 "%CACHE_GEN%" 与当前 "%GENERATOR%" 不一致
        echo [清理] 自动删除 %BUILD_DIR% 以避免 CMake 报错
        set "DO_CLEAN=1"
    )
)

if "%DO_CLEAN%"=="1" (
    echo [清理] 删除 %BUILD_DIR% ...
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    set "CACHE_GEN="
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/2] CMake 配置（含 sdk/goldsdk）...
if "%USE_NINJA%"=="1" (
    cmake -S . -B "%BUILD_DIR%" -G "Ninja" -DCMAKE_BUILD_TYPE=%CONFIG% -DQT6_ROOT_DIR="%QT6_ROOT%" -DCMAKE_PREFIX_PATH="%QT6_ROOT%"
) else (
    cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH% -DQT6_ROOT_DIR="%QT6_ROOT%" -DCMAKE_PREFIX_PATH="%QT6_ROOT%"
)
if errorlevel 1 (
    echo [错误] CMake 配置失败
    echo 可尝试:  build.bat clean
    echo 或强制:  build.bat clean vs
    echo 或:      build.bat clean ninja
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
if not defined EXE (
    echo   编译完成，但未找到 GoldPriceBarLite.exe
    exit /b 1
)
echo   编译成功
echo   可执行文件: %EXE%
echo ========================================

if "%DO_DEPLOY%"=="1" (
    echo.
    echo [deploy] windeployqt ...
    if exist "%QT6_ROOT%\bin\windeployqt.exe" (
        "%QT6_ROOT%\bin\windeployqt.exe" --release --no-translations "%EXE%"
    ) else (
        echo [警告] 未找到 windeployqt.exe
    )
) else (
    echo.
    echo [提示] 缺 DLL 时: build.bat deploy
)

if "%DO_RUN%"=="1" (
    echo [运行] %EXE%
    start "" "%EXE%"
)

exit /b 0
