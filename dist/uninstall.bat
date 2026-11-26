@echo off
setlocal
chcp 65001 >nul
set "DLL=%~dp0FileFolderInfo.dll"
set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if defined PROCESSOR_ARCHITEW6432 set "REGSVR=%SystemRoot%\Sysnative\regsvr32.exe"

if not exist "%DLL%" (
    echo FileFolderInfo.dll was not found next to this script.
    echo Keep the DLL in place until it has been unregistered.
    pause
    exit /b 1
)
if not exist "%REGSVR%" (
    echo 64-bit regsvr32.exe was not found: "%REGSVR%"
    pause
    exit /b 1
)

echo Removing FileFolderInfo.dll registration from HKLM/HKCU.
echo Run this script as Administrator.
"%REGSVR%" /u /s "%DLL%"
if errorlevel 1 goto :unregister_failed

echo.
echo Unregistration succeeded.
choice /C YN /N /M "Restart Explorer now so the menu is refreshed? Y/N "
if errorlevel 2 goto :restart_skipped

taskkill /f /im explorer.exe >nul 2>&1
start "" "%SystemRoot%\explorer.exe"
echo Explorer restarted.
goto :done

:restart_skipped
echo Explorer was not restarted. Restart it manually to remove any cached menu entry.
goto :done

:unregister_failed
echo.
echo Unregistration failed. If this is an access-denied error, run uninstall.bat as Administrator.
"%REGSVR%" /u "%DLL%"
pause
exit /b 1

:done
echo.
echo Done. The extension is unregistered; the DLL file was not deleted.
echo You may delete the extracted folder after Explorer has released the DLL.
pause
exit /b 0
