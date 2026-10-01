@echo off
REM ponytail: thin wrapper so idf.py runs with its env (won't run in Git Bash).
REM Usage: idf-build-flash.bat [PORT]   (default COM3). Flash only, no monitor.
setlocal
set "MSYSTEM="
set PORT=%1
if "%PORT%"=="" set PORT=COM3
call C:\esp\v5.5.4\esp-idf\export.bat || exit /b 1
cd /d %~dp0..
idf.py -p %PORT% build || exit /b 1
REM Which flash: `idf.py flash` writes plaintext, which a board with Flash
REM Encryption burned in mis-decrypts — it boot-loops on "partition 0 invalid
REM magic number" and needs the encrypted path to recover. So ask the build
REM which kind it is instead of trusting whoever typed the command.
findstr /b /c:"CONFIG_SECURE_FLASH_ENC_ENABLED=y" sdkconfig >nul
if errorlevel 1 (
    idf.py -p %PORT% flash
) else (
    echo Flash Encryption is on - pre-encrypting on the host ^(README "Routine reflash"^).
    python tools\secure_flash.py --port %PORT% --baud 921600
)
