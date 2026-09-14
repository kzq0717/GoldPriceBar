@echo off
setlocal EnableExtensions
chcp 65001 >nul

cd /d "%~dp0"

REM GoldPriceBarLite v1.0 build script
REM Usage: build.bat
REM        build.bat clean
REM        build.bat clean vs
REM        build.bat clean ninja
REM        build.bat deploy
REM        build.bat start

if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "BUILD_DIR=build"
set "CONFIG=Release"
set "ARCH=x64"

set "DO_CLEAN=0"
set "DO_START=0"
set "DO_DEPLOY=0"
set "FORCE_NINJA=0"
set "FORCE_VS=0"

echo %* | findstr /i /c:"clean" >nul && set "DO_CLEAN=1"
echo %* | findstr /i /c:"deploy" >nul && set "DO_DEPLOY=1"
echo %* | findstr /i /c:"ninja" >nul && set "FORCE_NINJA=1"
echo %* | findstr /i /c:" vs" >nul && set "FORCE_VS=1"
echo %* | findstr /i /c:"start" >nul && set "DO_START=1"
REM also accept legacy "run" as start
echo %* | findstr /i /c:"run" >nul && set "DO_START=1"

echo.
echo ========================================
echo   GoldPriceBarLite build v1.0
echo ========================================
echo   QT6_ROOT = %QT6_ROOT%
echo ========================================
echo.

if not exist "%QT6_ROOT%\lib\cmake\Qt6\Qt6Config.cmake" (
  echo [ERROR] Qt6 not found: %QT6_ROOT%
  exit /b 1
)

where cmake >nul 2>&1
if errorlevel 1 (
  echo [ERROR] cmake not in PATH
  exit /b 1
)

set "CACHE_GEN="
if exist "%BUILD_DIR%\CMakeCache.txt" (
  for /f "tokens=2 delims==" %%A in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" "%BUILD_DIR%\CMakeCache.txt"') do set "CACHE_GEN=%%A"
)

set "GENERATOR="
set "USE_NINJA=0"

if "%FORCE_NINJA%"=="1" (
  where ninja >nul 2>&1
  if errorlevel 1 (
    echo [ERROR] ninja.exe not found
    exit /b 1
  )
  where cl >nul 2>&1
  if errorlevel 1 (
    echo [ERROR] cl.exe not found. Use x64 Native Tools Prompt.
    exit /b 1
  )
  set "GENERATOR=Ninja"
  set "USE_NINJA=1"
  goto have_gen
)

if "%FORCE_VS%"=="1" goto pick_vs

if defined CACHE_GEN (
  echo [INFO] reuse cache generator: %CACHE_GEN%
  if /i "%CACHE_GEN%"=="Ninja" (
    set "GENERATOR=Ninja"
    set "USE_NINJA=1"
  ) else (
    set "GENERATOR=%CACHE_GEN%"
    set "USE_NINJA=0"
  )
  goto have_gen
)

where cl >nul 2>&1
if not errorlevel 1 (
  where ninja >nul 2>&1
  if not errorlevel 1 (
    set "GENERATOR=Ninja"
    set "USE_NINJA=1"
    goto have_gen
  )
)

:pick_vs
cmake -G "Visual Studio 18 2026" -h >nul 2>&1
if not errorlevel 1 (
  set "GENERATOR=Visual Studio 18 2026"
  goto have_gen
)
cmake -G "Visual Studio 17 2022" -h >nul 2>&1
if not errorlevel 1 (
  set "GENERATOR=Visual Studio 17 2022"
  goto have_gen
)
echo [ERROR] No VS generator found. Try: build.bat clean ninja
exit /b 1

:have_gen
echo [INFO] generator = %GENERATOR%

if defined CACHE_GEN (
  if /i not "%CACHE_GEN%"=="%GENERATOR%" (
    echo [WARN] generator mismatch, cleaning build dir
    set "DO_CLEAN=1"
  )
)

if "%DO_CLEAN%"=="1" (
  echo [CLEAN] removing %BUILD_DIR%
  if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

echo [1/2] cmake configure...
if "%USE_NINJA%"=="1" (
  cmake -S . -B "%BUILD_DIR%" -G "Ninja" -DCMAKE_BUILD_TYPE=%CONFIG% -DQT6_ROOT_DIR="%QT6_ROOT%" -DCMAKE_PREFIX_PATH="%QT6_ROOT%"
) else (
  cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A %ARCH% -DQT6_ROOT_DIR="%QT6_ROOT%" -DCMAKE_PREFIX_PATH="%QT6_ROOT%"
)
if errorlevel 1 (
  echo [ERROR] cmake configure failed
  echo Try: build.bat clean vs
  exit /b 1
)

echo [2/2] cmake build...
if "%USE_NINJA%"=="1" (
  cmake --build "%BUILD_DIR%" --parallel
) else (
  cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel
)
if errorlevel 1 (
  echo [ERROR] build failed
  exit /b 1
)

set "EXE="
if exist "%BUILD_DIR%\%CONFIG%\GoldPriceBarLite.exe" set "EXE=%BUILD_DIR%\%CONFIG%\GoldPriceBarLite.exe"
if not defined EXE if exist "%BUILD_DIR%\GoldPriceBarLite.exe" set "EXE=%BUILD_DIR%\GoldPriceBarLite.exe"

if not defined EXE (
  echo [ERROR] exe not found
  exit /b 1
)

echo.
echo BUILD OK: %EXE%

if "%DO_DEPLOY%"=="1" (
  if exist "%QT6_ROOT%\bin\windeployqt.exe" (
    "%QT6_ROOT%\bin\windeployqt.exe" --release --no-translations "%EXE%"
  )
)

if "%DO_START%"=="1" (
  echo starting %EXE%
  start "" "%EXE%"
)

exit /b 0
