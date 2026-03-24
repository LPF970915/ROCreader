@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "PROJECT_WIN=%SCRIPT_DIR:~0,-1%"
set "ROOT_WIN=%PROJECT_WIN%"
set "BASH_EXE="

if not "%MSYS2_ROOT%"=="" (
  if exist "%MSYS2_ROOT%\usr\bin\bash.exe" set "BASH_EXE=%MSYS2_ROOT%\usr\bin\bash.exe"
)

if "%BASH_EXE%"=="" if exist "D:\Program Files\MSYS2\usr\bin\bash.exe" set "BASH_EXE=D:\Program Files\MSYS2\usr\bin\bash.exe"
if "%BASH_EXE%"=="" if exist "C:\msys64\usr\bin\bash.exe" set "BASH_EXE=C:\msys64\usr\bin\bash.exe"
if "%BASH_EXE%"=="" if exist "D:\msys64\usr\bin\bash.exe" set "BASH_EXE=D:\msys64\usr\bin\bash.exe"
if "%BASH_EXE%"=="" if exist "C:\tools\msys64\usr\bin\bash.exe" set "BASH_EXE=C:\tools\msys64\usr\bin\bash.exe"
if "%BASH_EXE%"=="" if exist "%LOCALAPPDATA%\Programs\msys64\usr\bin\bash.exe" set "BASH_EXE=%LOCALAPPDATA%\Programs\msys64\usr\bin\bash.exe"
if "%BASH_EXE%"=="" if exist "C:\Program Files\msys64\usr\bin\bash.exe" set "BASH_EXE=C:\Program Files\msys64\usr\bin\bash.exe"

if "%BASH_EXE%"=="" (
  echo [ERROR] MSYS2 bash not found.
  echo Please install MSYS2 or set:
  echo   set MSYS2_ROOT=C:\msys64
  pause
  exit /b 1
)

echo [INFO] bash: "%BASH_EXE%"
echo [INFO] project: "%PROJECT_WIN%"
if exist "%ROOT_WIN%\.venv\Scripts\python.exe" (
  set "ROCREADER_PYTHON=%ROOT_WIN%\.venv\Scripts\python.exe"
  echo [INFO] ROCREADER_PYTHON="%ROCREADER_PYTHON%"
)

set "MSYSTEM=UCRT64"
set "CHERE_INVOKING=1"

"%BASH_EXE%" -lc "export PATH=/ucrt64/bin:/usr/bin:$PATH; PROJECT_UNIX=$(cygpath -u \"$PROJECT_WIN\"); cd \"$PROJECT_UNIX\"; export ROCREADER_ROOT=\"$PROJECT_UNIX\"; if command -v make >/dev/null 2>&1; then MK=make; elif command -v mingw32-make >/dev/null 2>&1; then MK=mingw32-make; else echo '[ERROR] make not found'; exit 10; fi; if ! command -v g++ >/dev/null 2>&1; then echo '[ERROR] g++ not found (install mingw-w64-ucrt-x86_64-toolchain)'; exit 11; fi; if ! pkg-config --exists sdl2; then echo '[ERROR] SDL2 dev package missing'; exit 12; fi; if ! pkg-config --exists SDL2_mixer; then echo '[WARN] SDL2_mixer dev package missing, will fallback to SDL audio backend.'; echo '[INFO] Optional install:'; echo '  pacman -S --needed mingw-w64-ucrt-x86_64-SDL2_mixer'; fi; echo '[INFO] build (REQUIRE_MUPDF=1)'; $MK clean; $MK -j4 REQUIRE_MUPDF=1; RC=$?; if [ $RC -ne 0 ]; then exit $RC; fi; echo '[INFO] launch'; ROCREADER_WINDOWED=1 ./build/rocreader_sdl.exe"

set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" (
  echo [ERROR] Preview failed, exit code %RC%.
  pause
  exit /b %RC%
)

echo [INFO] Preview exited normally.
pause
exit /b 0
