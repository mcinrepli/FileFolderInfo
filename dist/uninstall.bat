@echo off
chcp 65001 >nul
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Trebuyutsya prava administratora dlya polnoy ochistki HKLM.
    echo Pravoy knopkoy po uninstall.bat -^> "Zapustit ot imeni administratora".
    pause
    exit /b 1
)
echo Unregistering FileFolderInfo.dll...
regsvr32 /u /s "%~dp0FileFolderInfo.dll"
echo Restarting Explorer...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start explorer.exe
echo Done. The extension is unregistered.
pause
