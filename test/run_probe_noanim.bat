@echo off
setlocal
cd /d "%~dp0"
set "FFI_DIAG=1"
set "FFI_NOANIM=1"
start "" /D "%~dp0" "%~dp0ultralight_probe.exe"
