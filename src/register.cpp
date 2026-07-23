#include "common.h"
#include <strsafe.h>

static const wchar_t* kHandlerName = L"FileFolderInfoExt";
static const wchar_t* kFriendlyName = L"File/Folder Info shell extension";

static std::wstring GuidToString(const GUID& g)
{
    wchar_t buf[64] = {};
    StringFromGUID2(g, buf, 64);
    return buf; // формат "{XXXXXXXX-XXXX-...}"
}

static bool SetStringValue(HKEY hRoot, const std::wstring& subKey, const wchar_t* valueName, const std::wstring& data)
{
    HKEY hKey;
    LONG r = RegCreateKeyExW(hRoot, subKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr);
    if (r != ERROR_SUCCESS) return false;
    r = RegSetValueExW(hKey, valueName, 0, REG_SZ,
                        (const BYTE*)data.c_str(), (DWORD)((data.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

static void DeleteTreeQuiet(HKEY hRoot, const std::wstring& subKey)
{
    SHDeleteKeyW(hRoot, subKey.c_str());
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void);

extern "C" __declspec(dllexport) HRESULT WINAPI DllRegisterServer(void)
{
    wchar_t dllPath[MAX_PATH];
    if (!GetModuleFileNameW(g_hInst, dllPath, MAX_PATH))
        return E_UNEXPECTED;

    std::wstring clsid = GuidToString(CLSID_FileFolderInfoExt);

    // Полностью в HKLM — ровно так же, как зарегистрированы 7-Zip/MEGA/WinHex
    // у пользователя. Экспериментально подтвердилось, что смесь HKCU (CLSID)
    // + HKLM (ContextMenuHandlers) не работает: внутренний механизм
    // активации, которым Проводник поднимает найденный обработчик, CLSID
    // в HKCU не видит, хотя обычный CoCreateInstance его прекрасно находит.
    bool ok = true;
    ok &= SetStringValue(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID\\" + clsid, nullptr, kFriendlyName);
    ok &= SetStringValue(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", nullptr, dllPath);
    ok &= SetStringValue(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID\\" + clsid + L"\\InprocServer32", L"ThreadingModel", L"Apartment");

    const wchar_t* points[] = { L"*", L"Directory", L"AllFilesystemObjects" };
    for (auto pt : points) {
        ok &= SetStringValue(HKEY_LOCAL_MACHINE,
            std::wstring(L"Software\\Classes\\") + pt + L"\\shellex\\ContextMenuHandlers\\" + kHandlerName,
            nullptr, clsid);
    }

    if (!ok) {
        DllUnregisterServer();
        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
    }

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllUnregisterServer(void)
{
    std::wstring clsid = GuidToString(CLSID_FileFolderInfoExt);
    const wchar_t* points[] = { L"*", L"Directory", L"AllFilesystemObjects" };
    for (auto pt : points) {
        DeleteTreeQuiet(HKEY_LOCAL_MACHINE,
            std::wstring(L"Software\\Classes\\") + pt + L"\\shellex\\ContextMenuHandlers\\" + kHandlerName);
        // подчищаем и старые HKCU-записи от прошлых версий на всякий случай
        DeleteTreeQuiet(HKEY_CURRENT_USER,
            std::wstring(L"Software\\Classes\\") + pt + L"\\shellex\\ContextMenuHandlers\\" + kHandlerName);
    }
    DeleteTreeQuiet(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID\\" + clsid);
    DeleteTreeQuiet(HKEY_CURRENT_USER, L"Software\\Classes\\CLSID\\" + clsid);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    return S_OK;
}
