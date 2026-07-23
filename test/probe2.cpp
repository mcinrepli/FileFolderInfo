#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>

static const CLSID CLSID_FileFolderInfoExt =
{ 0xf4de0d56, 0xc976, 0x41e6, { 0x80, 0x1c, 0xd8, 0x3b, 0x57, 0xc6, 0x84, 0x6b } };

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        wprintf(L"Ispolzovanie: probe2.exe <put-k-suschestvuyuschemu-faylu-ili-papke>\n");
        return 1;
    }

    wchar_t fullPath[MAX_PATH];
    if (!GetFullPathNameW(argv[1], MAX_PATH, fullPath, nullptr)) {
        wprintf(L"GetFullPathNameW ne srabotal\n");
        return 1;
    }
    wprintf(L"Test s putem: %s\n", fullPath);
    wprintf(L"------------------------------------------------------------------\n");

    HRESULT hr = CoInitialize(nullptr);
    wprintf(L"CoInitialize:                     0x%08lx\n", (long)hr);

    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(fullPath);
    if (!pidl) { wprintf(L"ILCreateFromPathW ne srabotal - proverjte put'\n"); return 1; }

    PIDLIST_ABSOLUTE pidlParent = ILCloneFull(pidl);
    ILRemoveLastID(pidlParent);
    PCUITEMID_CHILD pidlChild = ILFindLastID(pidl);

    IShellFolder* pDesktop = nullptr;
    hr = SHGetDesktopFolder(&pDesktop);
    wprintf(L"SHGetDesktopFolder:               0x%08lx\n", (long)hr);

    IShellFolder* pParentFolder = nullptr;
    hr = pDesktop->BindToObject(pidlParent, nullptr, IID_IShellFolder, (void**)&pParentFolder);
    wprintf(L"BindToObject(roditel'skaya papka): 0x%08lx\n", (long)hr);
    if (FAILED(hr) || !pParentFolder) { wprintf(L"Ne mogu prodolzhit' bez roditel'skoy papki.\n"); return 1; }

    IDataObject* pDataObj = nullptr;
    PCUITEMID_CHILD apidl[1] = { pidlChild };
    hr = pParentFolder->GetUIObjectOf(nullptr, 1, (LPCITEMIDLIST*)apidl, IID_IDataObject, nullptr, (void**)&pDataObj);
    wprintf(L"GetUIObjectOf(IDataObject):       0x%08lx  ptr=%p\n", (long)hr, (void*)pDataObj);

    IShellExtInit* pInit = nullptr;
    hr = CoCreateInstance(CLSID_FileFolderInfoExt, nullptr, CLSCTX_INPROC_SERVER, IID_IShellExtInit, (void**)&pInit);
    wprintf(L"CoCreateInstance(nash obyekt):    0x%08lx  ptr=%p\n", (long)hr, (void*)pInit);

    if (SUCCEEDED(hr) && pInit && pDataObj) {
        hr = pInit->Initialize(pidlParent, pDataObj, nullptr);
        wprintf(L"IShellExtInit::Initialize:        0x%08lx\n", (long)hr);

        IContextMenu* pOurMenu = nullptr;
        HRESULT hrQI = pInit->QueryInterface(IID_IContextMenu, (void**)&pOurMenu);
        wprintf(L"QueryInterface(IContextMenu):     0x%08lx\n", (long)hrQI);

        if (SUCCEEDED(hrQI) && pOurMenu) {
            HMENU hMenu = CreatePopupMenu();
            HRESULT hrQCM = pOurMenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);
            wprintf(L"QueryContextMenu:                 0x%08lx (kod ispolzovannyh ID = %d)\n",
                    (long)hrQCM, (int)(hrQCM & 0xFFFF));

            int count = GetMenuItemCount(hMenu);
            wprintf(L"GetMenuItemCount posle vyzova:    %d\n", count);
            for (int i = 0; i < count; i++) {
                wchar_t buf[256] = {0};
                GetMenuStringW(hMenu, i, buf, 256, MF_BYPOSITION);
                wprintf(L"  punkt[%d] = '%s'\n", i, buf);
            }
            DestroyMenu(hMenu);
            pOurMenu->Release();
        }
        pInit->Release();
    } else {
        wprintf(L"Propuskayu Initialize/QueryContextMenu - net obyekta ili net IDataObject.\n");
    }

    if (pDataObj) pDataObj->Release();
    pParentFolder->Release();
    pDesktop->Release();
    ILFree(pidl);
    ILFree(pidlParent);

    CoUninitialize();
    wprintf(L"------------------------------------------------------------------\n");
    wprintf(L"Gotovo. Skopiruj VESJ vyvod celikom i prishli ego.\n");
    wprintf(L"Enter dlya vyhoda...\n");
    getwchar();
    return 0;
}
