@echo off
REM ponytail: capture the serial log non-interactively (idf.py monitor needs a
REM tty). Usage: idf-monitor.bat [PORT] [SECONDS]   defaults COM3 / 30.
setlocal
set "MSYSTEM="
set PORT=%1
if "%PORT%"=="" set PORT=COM3
set SECS=%2
if "%SECS%"=="" set SECS=30
call C:\esp\v5.5.4\esp-idf\export.bat >nul || exit /b 1
cd /d %~dp0..
python scripts\serial_tail.py %PORT% %SECS%
