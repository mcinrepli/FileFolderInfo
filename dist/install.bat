@echo off
setlocal
chcp 65001 >nul
set "DLL=%~dp0FileFolderInfo.dll"
set "REGSVR=%SystemRoot%\System32\regsvr32.exe"
if defined PROCESSOR_ARCHITEW6432 set "REGSVR=%SystemRoot%\Sysnative\regsvr32.exe"

if not exist "%DLL%" (
    echo FileFolderInfo.dll was not found next to this script.
    echo Extract the complete test archive before running install.bat.
    pause
    exit /b 1
)
if not exist "%REGSVR%" (
    echo 64-bit regsvr32.exe was not found: "%REGSVR%"
    pause
    exit /b 1
)

echo Registering FileFolderInfo.dll.
echo This extension writes its CLSID and ContextMenuHandlers entries to HKLM.
echo Run this script as Administrator; Explorer context-menu registration is machine-wide.
"%REGSVR%" /s "%DLL%"
if errorlevel 1 goto :register_failed

echo.
echo Registration succeeded.
choice /C YN /N /M "Restart Explorer now so the menu is refreshed? Y/N "
if errorlevel 2 goto :restart_skipped

taskkill /f /im explorer.exe >nul 2>&1
start "" "%SystemRoot%\explorer.exe"
echo Explorer restarted.
goto :done

:restart_skipped
echo Explorer was not restarted. Restart it manually before testing the context menu.
goto :done

:register_failed
echo.
echo Registration failed. If this is an access-denied error, run install.bat as Administrator.
"%REGSVR%" "%DLL%"
pause
exit /b 1

:done
echo.
echo Done. Right-click a file or folder and select the File/Folder Info menu.
pause
exit /b 0
