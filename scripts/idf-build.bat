@echo off
REM ponytail: build only (no flash), same env trick as idf-build-flash.bat.
setlocal
set "MSYSTEM="
call C:\esp\v5.5.4\esp-idf\export.bat || exit /b 1
cd /d %~dp0..
idf.py build
