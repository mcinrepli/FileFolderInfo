@echo off
setlocal
cd /d "%~dp0"
if errorlevel 1 goto :bad_directory
set "FFI_DIAG=1"
echo Starting Ultralight probe with diagnostics.
echo Close the map window to return here and see the exit code.
echo.
"%~dp0ultralight_probe.exe"
set "PROBE_EXIT_CODE=%ERRORLEVEL%"
echo.
echo ultralight_probe.exe exited with code %PROBE_EXIT_CODE%.
echo Logs, if created:
echo   %~dp0ultralight_probe_input.log
echo   %~dp0ultralight_probe.log
pause
exit /b %PROBE_EXIT_CODE%

:bad_directory
echo Could not switch to the probe directory: "%~dp0"
pause
exit /b 1
