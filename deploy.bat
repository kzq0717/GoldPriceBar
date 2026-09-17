@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

REM ============================================================
REM  Deploy Qt6 runtime DLLs next to GoldPriceBarLite.exe
REM  Usage:
REM    deploy.bat
REM    deploy.bat "D:\path\to\GoldPriceBarLite.exe"
REM  Env:
REM    set QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64
REM ============================================================

cd /d "%~dp0"

if not defined QT6_ROOT set "QT6_ROOT=D:\InstallDir\Qt6.7\6.7.3\msvc2022_64"

set "EXE=%~1"
if defined EXE goto have_exe

set "EXE="
if exist "build\debug\GoldPriceBarLite.exe" set "EXE=%cd%\build\debug\GoldPriceBarLite.exe"
if not defined EXE if exist "build\release\GoldPriceBarLite.exe" set "EXE=%cd%\build\release\GoldPriceBarLite.exe"
if not defined EXE if exist "build\Release\GoldPriceBarLite.exe" set "EXE=%cd%\build\Release\GoldPriceBarLite.exe"
if not defined EXE if exist "build\RelWithDebInfo\GoldPriceBarLite.exe" set "EXE=%cd%\build\RelWithDebInfo\GoldPriceBarLite.exe"
if not defined EXE if exist "build\Debug\GoldPriceBarLite.exe" set "EXE=%cd%\build\Debug\GoldPriceBarLite.exe"
if not defined EXE if exist "build\GoldPriceBarLite.exe" set "EXE=%cd%\build\GoldPriceBarLite.exe"

:have_exe
if not defined EXE (
  echo [ERROR] GoldPriceBarLite.exe not found. Build first: build.bat
  exit /b 1
)
if not exist "%EXE%" (
  echo [ERROR] File not found: %EXE%
  exit /b 1
)

for %%I in ("%EXE%") do set "EXEDIR=%%~dpI"
set "EXEDIR=%EXEDIR:~0,-1%"

set "WDEPLOY=%QT6_ROOT%\bin\windeployqt.exe"
if not exist "%WDEPLOY%" (
  echo [ERROR] windeployqt not found:
  echo         %WDEPLOY%
  echo Set QT6_ROOT to your Qt msvc kit, e.g.
  echo   set QT6_ROOT=D:\Qt\6.7.3\msvc2022_64
  exit /b 1
)

echo.
echo ========================================
echo   Deploy Qt dependencies
echo ========================================
echo   EXE     : %EXE%
echo   OutDir  : %EXEDIR%
echo   Qt      : %QT6_ROOT%
echo ========================================
echo.

REM Detect debug vs release from path
set "WD_MODE=--release"
set "DLL_SUFFIX="
echo %EXE% | findstr /i "\\Debug\\" >nul && (
  set "WD_MODE=--debug"
  set "DLL_SUFFIX=d"
)
echo %EXE% | findstr /i "\\debug\\" >nul && (
  set "WD_MODE=--debug"
  set "DLL_SUFFIX=d"
)

echo [1/2] windeployqt %WD_MODE% ...
"%WDEPLOY%" %WD_MODE% --compiler-runtime --no-translations --force ^
  --network --sql --charts --widgets --gui --core --qml --quick ^
  --qmldir "%cd%\qml" ^
  "%EXE%"
if errorlevel 1 (
  echo [WARN] windeployqt with module flags failed, retry minimal...
  "%WDEPLOY%" %WD_MODE% --compiler-runtime --no-translations --force "%EXE%"
  if errorlevel 1 (
    echo [ERROR] windeployqt failed
    exit /b 1
  )
)

echo.
echo [2/2] verify key DLLs ...
set "MISS=0"
for %%D in (Qt6Core Qt6Gui Qt6Widgets Qt6Network Qt6Charts Qt6Sql Qt6Qml Qt6Quick) do (
  if not exist "%EXEDIR%\%%D%DLL_SUFFIX%.dll" if not exist "%EXEDIR%\%%D.dll" (
    echo   MISSING: %%D%DLL_SUFFIX%.dll
    set "MISS=1"
  ) else (
    echo   OK: %%D
  )
)
if not exist "%EXEDIR%\platforms\qwindows%DLL_SUFFIX%.dll" if not exist "%EXEDIR%\platforms\qwindows.dll" (
  echo   MISSING: platforms\qwindows%DLL_SUFFIX%.dll
  set "MISS=1"
) else (
  echo   OK: platforms\qwindows
)

echo.
if "%MISS%"=="1" (
  echo [WARN] Some DLLs still missing. You can also run with PATH:
  echo   set PATH=%QT6_ROOT%\bin;%%PATH%%
  echo   "%EXE%"
  exit /b 2
)

echo Deploy OK. Run:
echo   "%EXE%"
echo.
echo Tip: double-click the exe in:
echo   %EXEDIR%
exit /b 0
