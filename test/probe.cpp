#include <windows.h>
#include <shlobj.h>
#include <cstdio>

// {F4DE0D56-C976-41E6-801C-D83B57C6846B}
static const CLSID CLSID_FileFolderInfoExt =
{ 0xf4de0d56, 0xc976, 0x41e6, { 0x80, 0x1c, 0xd8, 0x3b, 0x57, 0xc6, 0x84, 0x6b } };

int wmain()
{
    wprintf(L"Probe: proverka registracii i zagruzki FileFolderInfoExt COM-obyekta\n");
    wprintf(L"------------------------------------------------------------------\n");

    HRESULT hr = CoInitialize(nullptr);
    wprintf(L"CoInitialize:                          0x%08lx\n", (long)hr);

    IShellExtInit* pInit = nullptr;
    hr = CoCreateInstance(CLSID_FileFolderInfoExt, nullptr, CLSCTX_INPROC_SERVER,
                           IID_IShellExtInit, (void**)&pInit);
    wprintf(L"CoCreateInstance -> IShellExtInit:     0x%08lx  ptr=%p\n", (long)hr, (void*)pInit);

    if (hr == REGDB_E_CLASSNOTREG)
        wprintf(L"  => CLSID NE NAJDEN v reestre (registraciya ne primenilas').\n");
    else if (FAILED(hr))
        wprintf(L"  => oshibka zagruzki DLL ili sozdaniya obyekta (sm. kod vyshe).\n");
    else
        wprintf(L"  => OK, obyekt sozdan uspeshno.\n");

    if (SUCCEEDED(hr) && pInit) {
        IContextMenu* pMenu = nullptr;
        HRESULT hr2 = pInit->QueryInterface(IID_IContextMenu, (void**)&pMenu);
        wprintf(L"QueryInterface -> IContextMenu:        0x%08lx  ptr=%p\n", (long)hr2, (void*)pMenu);
        if (pMenu) pMenu->Release();
        pInit->Release();
    }

    CoUninitialize();
    wprintf(L"------------------------------------------------------------------\n");
    wprintf(L"Gotovo. Skopiruj VESJ vyvod i prishli ego.\n");
    wprintf(L"Nazhmi Enter dlya vyhoda...\n");
    getwchar();
    return 0;
}
