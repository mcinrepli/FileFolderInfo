#include "common.h"
#include "shellext.h"
#include "strings.h"
#include <strsafe.h>
#include <cwctype>

CShellExt::CShellExt() : m_cRef(1), m_pDataObj(nullptr)
{
    InterlockedIncrement(&g_cDllRef);
}

CShellExt::~CShellExt()
{
    if (m_pDataObj) m_pDataObj->Release();
    InterlockedDecrement(&g_cDllRef);
}

// ---------------------------------------------------------- IUnknown

IFACEMETHODIMP CShellExt::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (riid == IID_IUnknown || riid == IID_IShellExtInit) {
        *ppv = static_cast<IShellExtInit*>(this);
    } else if (riid == IID_IContextMenu) {
        *ppv = static_cast<IContextMenu*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    reinterpret_cast<IUnknown*>(*ppv)->AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) CShellExt::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) CShellExt::Release()
{
    LONG r = InterlockedDecrement(&m_cRef);
    if (r == 0) delete this;
    return r;
}

// ---------------------------------------------------------- IShellExtInit

IFACEMETHODIMP CShellExt::Initialize(LPCITEMIDLIST /*pidlFolder*/, IDataObject* pDataObj, HKEY /*hKeyProgID*/)
{
    if (m_pDataObj) { m_pDataObj->Release(); m_pDataObj = nullptr; }
    if (pDataObj) { pDataObj->AddRef(); m_pDataObj = pDataObj; }
    return S_OK;
}

// ---------------------------------------------------------- IContextMenu

IFACEMETHODIMP CShellExt::QueryContextMenu(HMENU hMenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags)
{
    if (uFlags & CMF_DEFAULTONLY)
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

    if ((idCmdLast - idCmdFirst) < CMD_COUNT + 4)
        return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, 0);

    SelectionInfo sel = GetSelectionInfo(m_pDataObj);
    bool multi = sel.paths.size() > 1;
    bool foldersOnly = sel.allFolders && !sel.paths.empty();

    auto addItemTo = [&](HMENU h, UINT idOffset, const wchar_t* text) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE;
        mii.fType = MFT_STRING;
        mii.fState = MFS_ENABLED;
        mii.wID = idCmdFirst + idOffset;
        mii.dwTypeData = (LPWSTR)text;
        InsertMenuItemW(h, (UINT)-1, TRUE, &mii);
    };
    auto addSepTo = [&](HMENU h) {
        InsertMenuW(h, (UINT)-1, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    };
    auto addPopupTo = [&](HMENU parent, UINT containerId, const wchar_t* text, HMENU child) {
        MENUITEMINFOW mii = { sizeof(mii) };
        mii.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_ID;
        mii.fType = MFT_STRING;
        mii.wID = idCmdFirst + containerId; // контейнер, никогда не инвокается
        mii.dwTypeData = (LPWSTR)text;
        mii.hSubMenu = child;
        InsertMenuItemW(parent, (UINT)-1, TRUE, &mii);
    };

    const wchar_t* pathText =
        sel.allFolders ? (multi ? S_ITEM_COPY_PATH_FOLDER_N : S_ITEM_COPY_PATH_FOLDER_1) :
        sel.allFiles   ? (multi ? S_ITEM_COPY_PATH_FILE_N   : S_ITEM_COPY_PATH_FILE_1) :
                         S_ITEM_COPY_PATH_MIXED;
    const wchar_t* nameText =
        sel.allFolders ? (multi ? S_ITEM_COPY_NAME_FOLDER_N : S_ITEM_COPY_NAME_FOLDER_1) :
        sel.allFiles   ? (multi ? S_ITEM_COPY_NAME_FILE_N   : S_ITEM_COPY_NAME_FILE_1) :
                         S_ITEM_COPY_NAME_MIXED;

    // ---- подменю "Скопировать" ----
    HMENU hCopySub = CreatePopupMenu();
    addItemTo(hCopySub, CMD_COPY_PATH, pathText);
    addItemTo(hCopySub, CMD_COPY_NAME, nameText);
    addSepTo(hCopySub);
    addItemTo(hCopySub, CMD_COPY_DATE_MODIFIED, S_ITEM_DATE_MODIFIED);
    addItemTo(hCopySub, CMD_COPY_DATE_CREATED, S_ITEM_DATE_CREATED);
    addItemTo(hCopySub, CMD_COPY_DATE_ACCESSED, S_ITEM_DATE_ACCESSED);
    if (foldersOnly) {
        addSepTo(hCopySub);
        addItemTo(hCopySub, CMD_TREE_PATHS, S_ITEM_TREE_PATHS);
        addItemTo(hCopySub, CMD_TREE_NAMES, S_ITEM_TREE_NAMES);
    }

    // ---- подменю "Клонировать" ----
    HMENU hCloneSub = CreatePopupMenu();
    addItemTo(hCloneSub, CMD_CLONE_DATE_MODIFIED, S_ITEM_CLONE_DATE_MODIFIED);
    addItemTo(hCloneSub, CMD_CLONE_DATE_CREATED, S_ITEM_CLONE_DATE_CREATED);
    addItemTo(hCloneSub, CMD_CLONE_NUMBERED, S_ITEM_CLONE_NUMBERED);

    // ---- подменю "Повернуть" (только если ВСЕ выбранные — распознанные статичные изображения) ----
    bool allRotatable = sel.allFiles && !sel.paths.empty();
    if (allRotatable) {
        static const wchar_t* rotExts[] = { L"jpg", L"jpeg" };
        for (auto& p : sel.paths) {
            size_t dot = p.find_last_of(L'.');
            bool ok = false;
            if (dot != std::wstring::npos) {
                std::wstring ext = p.substr(dot + 1);
                for (auto& c : ext) c = (wchar_t)towlower(c);
                for (auto e : rotExts) if (ext == e) { ok = true; break; }
            }
            if (!ok) { allRotatable = false; break; }
        }
    }
    HMENU hRotateSub = nullptr;
    if (allRotatable) {
        hRotateSub = CreatePopupMenu();
        addItemTo(hRotateSub, CMD_ROTATE_LEFT, S_ITEM_ROTATE_LEFT);
        addItemTo(hRotateSub, CMD_ROTATE_RIGHT, S_ITEM_ROTATE_RIGHT);
    }

    // ---- верхний уровень нашего пункта ----
    HMENU hSub = CreatePopupMenu();
    if (!hSub) return E_FAIL;

    addPopupTo(hSub, CMD_COUNT + 2, S_ITEM_COPY_SUBMENU, hCopySub);
    addSepTo(hSub);
    addItemTo(hSub, CMD_SHOW_SIZE, S_ITEM_SHOW_SIZE);
    addItemTo(hSub, CMD_METADATA, S_ITEM_METADATA);
    if (!multi && sel.allFiles && !sel.paths.empty()) {
        size_t dot = sel.paths[0].find_last_of(L'.');
        if (dot != std::wstring::npos) {
            std::wstring ext = sel.paths[0].substr(dot + 1);
            for (auto& c : ext) c = (wchar_t)towlower(c);
            if (ext == L"exe" || ext == L"dll" || ext == L"sys" || ext == L"ocx")
                addItemTo(hSub, CMD_DEPENDENCIES, S_ITEM_DEPENDENCIES);
        }
    }
    addSepTo(hSub);
    addItemTo(hSub, CMD_RENAME_COPY_NUMBER, S_ITEM_RENAME_NUMBER);
    addItemTo(hSub, CMD_RENAME_COPY_DATE, S_ITEM_RENAME_DATE);
    addItemTo(hSub, CMD_APPEND_DATE, S_ITEM_APPEND_DATE);
    addSepTo(hSub);
    addPopupTo(hSub, CMD_COUNT + 1, S_ITEM_CLONE_SUBMENU, hCloneSub);
    if (hRotateSub)
        addPopupTo(hSub, CMD_COUNT + 3, S_ITEM_ROTATE_SUBMENU, hRotateSub);

    MENUITEMINFOW miiTop = { sizeof(miiTop) };
    miiTop.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_ID;
    miiTop.fType = MFT_STRING;
    miiTop.wID = idCmdFirst + CMD_COUNT; // контейнер, никогда не инвокается напрямую
    miiTop.dwTypeData = (LPWSTR)S_MENU_TOP;
    miiTop.hSubMenu = hSub;

    InsertMenuItemW(hMenu, indexMenu, TRUE, &miiTop);

    return MAKE_HRESULT(SEVERITY_SUCCESS, FACILITY_NULL, CMD_COUNT + 4);
}

IFACEMETHODIMP CShellExt::InvokeCommand(LPCMINVOKECOMMANDINFO pici)
{
    if (((ULONG_PTR)pici->lpVerb >> 16) != 0)
        return E_INVALIDARG; // строковые verb-ы не поддерживаем

    UINT cmd = LOWORD((ULONG_PTR)pici->lpVerb);
    SelectionInfo sel = GetSelectionInfo(m_pDataObj);
    HWND hwnd = pici->hwnd;

    switch (cmd) {
    case CMD_COPY_PATH:          Cmd_CopyPath(hwnd, sel); break;
    case CMD_COPY_NAME:          Cmd_CopyName(hwnd, sel); break;
    case CMD_SHOW_SIZE:          Cmd_ShowSize(hwnd, sel); break;
    case CMD_METADATA:           Cmd_ShowMetadata(hwnd, sel); break;
    case CMD_DEPENDENCIES:       Cmd_ShowDependencies(hwnd, sel); break;
    case CMD_COPY_DATE_MODIFIED: Cmd_DateModified(hwnd, sel); break;
    case CMD_COPY_DATE_CREATED:  Cmd_DateCreated(hwnd, sel); break;
    case CMD_COPY_DATE_ACCESSED: Cmd_DateAccessed(hwnd, sel); break;
    case CMD_TREE_PATHS:         Cmd_TreePaths(hwnd, sel); break;
    case CMD_TREE_NAMES:         Cmd_TreeNames(hwnd, sel); break;
    case CMD_RENAME_COPY_NUMBER: Cmd_RenameCopyNumber(hwnd, sel); break;
    case CMD_RENAME_COPY_DATE:   Cmd_RenameCopyDate(hwnd, sel); break;
    case CMD_APPEND_DATE:        Cmd_AppendDate(hwnd, sel); break;
    case CMD_CLONE_DATE_MODIFIED: Cmd_CloneDateModified(hwnd, sel); break;
    case CMD_CLONE_DATE_CREATED:  Cmd_CloneDateCreated(hwnd, sel); break;
    case CMD_CLONE_NUMBERED:      Cmd_CloneNumbered(hwnd, sel); break;
    case CMD_ROTATE_LEFT:         Cmd_RotateLeft(hwnd, sel); break;
    case CMD_ROTATE_RIGHT:        Cmd_RotateRight(hwnd, sel); break;
    default: return E_INVALIDARG;
    }
    return S_OK;
}

IFACEMETHODIMP CShellExt::GetCommandString(UINT_PTR idCmd, UINT uType, UINT* /*pReserved*/, LPSTR pszName, UINT cchMax)
{
    static const wchar_t* helpTexts[CMD_COUNT] = {
        S_HELP_COPY_PATH, S_HELP_COPY_NAME,
        S_HELP_DATE, S_HELP_DATE, S_HELP_DATE,
        S_HELP_TREE_PATHS, S_HELP_TREE_NAMES,
        S_HELP_SHOW_SIZE, S_HELP_METADATA, S_HELP_DEPENDENCIES,
        S_HELP_RENAME_NUMBER, S_HELP_RENAME_DATE, S_HELP_APPEND_DATE,
        S_HELP_CLONE_DATE_MODIFIED, S_HELP_CLONE_DATE_CREATED, S_HELP_CLONE_NUMBERED,
        S_HELP_ROTATE_LEFT, S_HELP_ROTATE_RIGHT
    };

    if (idCmd >= CMD_COUNT) return E_INVALIDARG;

    if (uType == GCS_HELPTEXTW) {
        StringCchCopyW((LPWSTR)pszName, cchMax, helpTexts[idCmd]);
        return S_OK;
    }
    if (uType == GCS_VERBW) {
        wchar_t buf[32];
        StringCchPrintfW(buf, 32, L"ffi_cmd_%u", (unsigned)idCmd);
        StringCchCopyW((LPWSTR)pszName, cchMax, buf);
        return S_OK;
    }
    return E_NOTIMPL;
}
