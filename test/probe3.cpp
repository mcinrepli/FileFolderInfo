#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <cstdio>

int wmain(int argc, wchar_t** argv)
{
    if (argc < 2) {
        wprintf(L"Ispolzovanie: probe3.exe <put-k-faylu-ili-papke>\n");
        return 1;
    }

    wchar_t fullPath[MAX_PATH];
    GetFullPathNameW(argv[1], MAX_PATH, fullPath, nullptr);
    wprintf(L"Test s putem: %ls\n", fullPath);
    wprintf(L"------------------------------------------------------------------\n");

    CoInitialize(nullptr);

    PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(fullPath);
    if (!pidl) { wprintf(L"ILCreateFromPathW ne srabotal - proverte put'\n"); return 1; }

    PIDLIST_ABSOLUTE pidlParent = ILCloneFull(pidl);
    ILRemoveLastID(pidlParent);
    PCUITEMID_CHILD pidlChild = ILFindLastID(pidl);

    IShellFolder* pDesktop = nullptr;
    SHGetDesktopFolder(&pDesktop);

    IShellFolder* pParentFolder = nullptr;
    HRESULT hr = pDesktop->BindToObject(pidlParent, nullptr, IID_IShellFolder, (void**)&pParentFolder);
    wprintf(L"BindToObject(roditel'):                          0x%08lx\n", (long)hr);
    if (FAILED(hr) || !pParentFolder) return 1;

    // ГЛАВНОЕ: просим IID_IContextMenu у РОДИТЕЛЬСКОЙ ПАПКИ для элемента —
    // это ровно тот же вызов, что делает сам explorer.exe. Внутри него шелл
    // сам перебирает все ContextMenuHandlers и агрегирует их меню в одно.
    PCUITEMID_CHILD apidl[1] = { pidlChild };
    IContextMenu* pMenu = nullptr;
    hr = pParentFolder->GetUIObjectOf(nullptr, 1, (LPCITEMIDLIST*)apidl, IID_IContextMenu, nullptr, (void**)&pMenu);
    wprintf(L"GetUIObjectOf(IID_IContextMenu, ot papki):        0x%08lx  ptr=%p\n", (long)hr, (void*)pMenu);

    if (SUCCEEDED(hr) && pMenu) {
        HMENU hMenu = CreatePopupMenu();
        HRESULT hrQ = pMenu->QueryContextMenu(hMenu, 0, 1, 0x7FFF, CMF_NORMAL);
        wprintf(L"QueryContextMenu (agregirovannyj, ot Provodnika): 0x%08lx\n", (long)hrQ);

        int count = GetMenuItemCount(hMenu);
        wprintf(L"Vsego punktov v itogovom menju:                   %d\n", count);

        bool found = false;
        for (int i = 0; i < count; i++) {
            wchar_t buf[256] = {0};
            MENUITEMINFOW mii = { sizeof(mii) };
            mii.fMask = MIIM_STRING | MIIM_ID | MIIM_SUBMENU;
            mii.dwTypeData = buf;
            mii.cch = 256;
            GetMenuItemInfoW(hMenu, i, TRUE, &mii);
            wprintf(L"  [%2d] id=%-6u submenu=%-14p '%ls'\n", i, mii.wID, (void*)mii.hSubMenu, buf);
            if (wcsstr(buf, L"\u0418\u043D\u0444\u043E") != nullptr) // "Инфо..."
                found = true;
        }
        wprintf(L"------------------------------------------------------------------\n");
        wprintf(L"Nash punkt naiden v agregirovannom menju: %ls\n", found ? L"DA" : L"NET");

        DestroyMenu(hMenu);
        pMenu->Release();
    } else {
        wprintf(L"Ne poluchilos' poluchit' agregirovannyj IContextMenu ot papki voobsche.\n");
    }

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
