@echo off
chcp 65001 >nul
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Trebuyutsya prava administratora ^(dlya zapisi treh ukazateley
    echo ContextMenuHandlers v HKLM - eto trebovanie samogo Provodnika,
    echo sam COM-obyekt ostaetsya v HKCU^).
    echo.
    echo Pravoy knopkoy po install.bat -^> "Zapustit ot imeni administratora".
    pause
    exit /b 1
)

echo Registering FileFolderInfo.dll (CLSID stays per-user in HKCU;
echo only the ContextMenuHandlers pointers go into HKLM)...
regsvr32 /s "%~dp0FileFolderInfo.dll"
if %errorlevel% neq 0 (
    echo.
    echo Something went wrong. Re-running without /s to show the real error message:
    regsvr32 "%~dp0FileFolderInfo.dll"
    pause
    exit /b 1
)
echo Registered OK.
echo Restarting Explorer so the new context menu item appears right away...
taskkill /f /im explorer.exe >nul 2>&1
timeout /t 1 /nobreak >nul
start explorer.exe
echo.
echo Done. Right-click any file or folder and look for
echo "Informaciya o faile/papke" (Cyrillic label, see README).
pause
