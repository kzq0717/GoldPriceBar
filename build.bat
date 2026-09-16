@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

cd /d "%~dp0"

REM GoldPriceBarLite v1.0 build
REM Usage: build.bat [clean] [vs|ninja] [debug|release] [deploy] [start]
REM   debug   = CMAKE_BUILD_TYPE/Debug (VS: --config Debug)

if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"
set "BUILD_DIR=build"
set "CONFIG=Release"
echo %* | findstr /i /c:"debug" >nul && set "CONFIG=Debug"
echo %* | findstr /i /c:"release" >nul && set "CONFIG=Release"
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
echo %*| findstr /i /r /c:"\<vs\>" >nul && set "FORCE_VS=1"
echo %* | findstr /i /c:"start" >nul && set "DO_START=1"
echo %* | findstr /i /c:"run" >nul && set "DO_START=1"

echo.
echo ========================================
echo   GoldPriceBarLite build v1.0
echo ========================================
echo   QT6_ROOT = %QT6_ROOT%
echo   CONFIG   = %CONFIG%
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

REM ---------- Load MSVC environment (fixes C1083 type_traits) ----------
call :ensure_msvc_env
if errorlevel 1 (
  echo [ERROR] MSVC environment not available.
  echo         Open "x64 Native Tools Command Prompt for VS" and retry,
  echo         or install "Desktop development with C++".
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
    echo [ERROR] cl.exe still not found after loading VS env
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

REM Prefer VS generator when available (more reliable include paths on CI/desktop)
goto pick_vs

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

where ninja >nul 2>&1
if not errorlevel 1 (
  where cl >nul 2>&1
  if not errorlevel 1 (
    set "GENERATOR=Ninja"
    set "USE_NINJA=1"
    goto have_gen
  )
)

echo [ERROR] No CMake generator found
exit /b 1

:have_gen
echo [INFO] generator = %GENERATOR%
echo [INFO] INCLUDE (first): 
echo %INCLUDE% | more +0 2>nul | findstr /i "MSVC" >nul && echo   MSVC headers OK || echo   [WARN] INCLUDE may be incomplete

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
  echo If C1090 PDB API failed: close Visual Studio, then:
  echo   build.bat clean debug
  echo or delete build\*.pdb and rebuild.
  echo.
  echo If error is C1083 type_traits / iostream:
  echo   1. Use: build.bat clean vs
  echo   2. Or open "x64 Native Tools Command Prompt for VS" then:
  echo      build.bat clean ninja
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
  call "%~dp0deploy.bat" "%EXE%"
)

if "%DO_START%"=="1" (
  echo starting %EXE%
  start "" "%EXE%"
)

exit /b 0

REM ============================================================
:ensure_msvc_env
REM If cl already works with STL headers, keep current env.
where cl >nul 2>&1
if not errorlevel 1 (
  if defined INCLUDE (
    echo %INCLUDE% | findstr /i "MSVC" >nul
    if not errorlevel 1 (
      echo [INFO] MSVC env already active
      exit /b 0
    )
  )
)

set "VCVARS="
if defined VSINSTALLDIR (
  if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
)

REM vswhere (VS 2022 / 2026)
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCVARS if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
  )
)

REM Common install paths
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars64.bat"

if not defined VCVARS (
  echo [WARN] vcvars64.bat not found
  exit /b 1
)

echo [INFO] loading VS env: %VCVARS%
call "%VCVARS%" >nul
where cl >nul 2>&1
if errorlevel 1 (
  echo [ERROR] cl.exe not available after vcvars
  exit /b 1
)
echo [INFO] cl.exe ready
exit /b 0
