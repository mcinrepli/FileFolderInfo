#include "common.h"
#include "strings.h"
#include <cstdio>

namespace {

std::wstring JoinLines(const std::vector<std::wstring>& lines)
{
    std::wstring out;
    for (size_t i = 0; i < lines.size(); i++) {
        out += lines[i];
        if (i + 1 < lines.size()) out += L"\r\n";
    }
    return out;
}

std::wstring FileNameOnly(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}

std::wstring DirNameOnly(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring() : path.substr(0, p);
}

std::wstring FormatFilenameDate(const SYSTEMTIME& st)
{
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d.%02d.%02d %02d-%02d-%02d",
             st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

bool GetFileMTimeLocal(const std::wstring& path, SYSTEMTIME* outLocal)
{
    FILETIME ftC, ftA, ftW;
    if (!GetFileTimes(path, &ftC, &ftA, &ftW)) return false;
    SystemTimeLocalFromFileTime(ftW, outLocal);
    return true;
}

void NotifyRenamed(const std::wstring& oldFull)
{
    // Сообщаем шеллу, что элемент переименован — иначе в открытом окне Проводника
    // список может не обновиться немедленно.
    SHChangeNotify(SHCNE_RENAMEITEM, SHCNF_PATHW, oldFull.c_str(), nullptr);
}

} // namespace

// ============================================================ форматы дат (пункты 4,5,6)

std::wstring FormatDateVariant(const SYSTEMTIME& st, int variant)
{
    wchar_t buf[160];
    switch (variant) {
    case 1:
        swprintf(buf, 160, L"%02d-%02d-%02d %d %ls %d %ls",
                 st.wHour, st.wMinute, st.wSecond, st.wDay,
                 MONTHS_GEN_LOWER[st.wMonth - 1], st.wYear, S_YEAR_LOWER);
        break;
    case 2:
        swprintf(buf, 160, L"%02d-%02d-%02d %02d.%02d.%04d",
                 st.wHour, st.wMinute, st.wSecond, st.wDay, st.wMonth, st.wYear);
        break;
    case 3:
        swprintf(buf, 160, L"%02d:%02d:%02d %d %ls %d %ls",
                 st.wHour, st.wMinute, st.wSecond, st.wDay,
                 MONTHS_GEN_LOWER[st.wMonth - 1], st.wYear, S_YEAR_LOWER);
        break;
    case 4:
        swprintf(buf, 160, L"%02d:%02d:%02d %02d.%02d.%04d",
                 st.wHour, st.wMinute, st.wSecond, st.wDay, st.wMonth, st.wYear);
        break;
    case 5:
        swprintf(buf, 160, L"%02d\u0447 %02d\u043C %02d\u0441 %d %ls %d %ls",
                 st.wHour, st.wMinute, st.wSecond, st.wDay,
                 MONTHS_GEN_UPPER[st.wMonth - 1], st.wYear, S_YEAR_UPPER);
        break;
    default:
        buf[0] = 0;
    }
    return buf;
}

// ============================================================ 1: копировать путь(и)

void Cmd_CopyPath(HWND owner, const SelectionInfo& sel)
{
    if (sel.paths.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    CopyTextToClipboard(owner, JoinLines(sel.paths));
}

// ============================================================ 2: копировать имя/имена

void Cmd_CopyName(HWND owner, const SelectionInfo& sel)
{
    if (sel.paths.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::vector<std::wstring> lines;
    lines.reserve(sel.paths.size());
    for (auto& p : sel.paths) lines.push_back(FileNameOnly(p));
    CopyTextToClipboard(owner, JoinLines(lines));
}

// ============================================================ 3: показать размер

void Cmd_ShowSize(HWND owner, const SelectionInfo& sel)
{
    if (sel.paths.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    unsigned long long total = 0;
    bool anyErr = false;
    for (auto& p : sel.paths)
        total += GetPathSizeRecursive(p, &anyErr);

    double kb = (double)total / 1024.0;
    double mb = kb / 1024.0;
    double gb = mb / 1024.0;

    std::vector<InfoRow> rows;
    rows.push_back({ S_ROW_BYTES, FormatPlainNumber(total) });
    rows.push_back({ S_ROW_KB, FormatFixed(kb, 2) });
    rows.push_back({ S_ROW_MB, FormatFixed(mb, 2) });
    rows.push_back({ S_ROW_GB, FormatFixed(gb, 2) });
    if (sel.paths.size() > 1)
        rows.push_back({ S_ROW_COUNT, FormatPlainNumber((unsigned long long)sel.paths.size()) });

    ShowInfoDialog(owner, S_DLG_TITLE_SIZE, rows);
}

// ============================================================ 4,5,6: даты

namespace {

void ShowDateInfo(HWND owner, const SelectionInfo& sel, TimeKind kind, const wchar_t* title)
{
    if (sel.paths.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (sel.paths.size() == 1) {
        FILETIME ftC, ftA, ftW;
        if (!GetFileTimes(sel.paths[0], &ftC, &ftA, &ftW)) {
            std::wstring msg = std::wstring(S_MSG_ERR_TIME_PREFIX) + L"\r\n" + FileNameOnly(sel.paths[0]);
            MessageBoxW(owner, msg.c_str(), S_MSG_TITLE, MB_OK | MB_ICONWARNING);
            return;
        }
        FILETIME ft = (kind == TimeKind::Modified) ? ftW : (kind == TimeKind::Created ? ftC : ftA);
        SYSTEMTIME stLocal;
        SystemTimeLocalFromFileTime(ft, &stLocal);

        std::vector<InfoRow> rows;
        for (int v = 1; v <= 5; v++) {
            wchar_t label[32];
            swprintf(label, 32, L"%ls %d", S_ROW_FORMAT_PREFIX, v);
            rows.push_back({ label, FormatDateVariant(stLocal, v) });
        }
        ShowInfoDialog(owner, title, rows);
    } else {
        // Несколько объектов сразу: по одной строке "имя -> компактная дата" на каждый.
        std::vector<InfoRow> rows;
        for (auto& p : sel.paths) {
            FILETIME ftC, ftA, ftW;
            std::wstring value = L"?";
            if (GetFileTimes(p, &ftC, &ftA, &ftW)) {
                FILETIME ft = (kind == TimeKind::Modified) ? ftW : (kind == TimeKind::Created ? ftC : ftA);
                SYSTEMTIME stLocal;
                SystemTimeLocalFromFileTime(ft, &stLocal);
                value = FormatDateVariant(stLocal, 4);
            }
            rows.push_back({ FileNameOnly(p), value });
        }
        ShowInfoDialog(owner, title, rows);
    }
}

} // namespace

void Cmd_DateModified(HWND owner, const SelectionInfo& sel) { ShowDateInfo(owner, sel, TimeKind::Modified, S_DLG_TITLE_MODIFIED); }
void Cmd_DateCreated(HWND owner, const SelectionInfo& sel)  { ShowDateInfo(owner, sel, TimeKind::Created,  S_DLG_TITLE_CREATED); }
void Cmd_DateAccessed(HWND owner, const SelectionInfo& sel) { ShowDateInfo(owner, sel, TimeKind::Accessed, S_DLG_TITLE_ACCESSED); }

// ============================================================ 7,8,9: переименования

namespace {

// mode: 0 = "-N" вместо "— копия"; 1 = дата вместо "— копия"; 2 = дата дописывается всегда
void DoRenameOperation(HWND owner, const SelectionInfo& sel, int mode)
{
    if (sel.paths.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    int done = 0, skipped = 0;

    for (auto& fullPath : sel.paths) {
        DWORD attrs = GetFileAttributesW(fullPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) { skipped++; continue; }
        bool isFolder = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        std::wstring dir = DirNameOnly(fullPath);
        std::wstring name = FileNameOnly(fullPath);

        std::wstring newName;

        if (mode == 0 || mode == 1) {
            CopySuffixInfo info = ParseCopySuffix(name, isFolder);
            if (info.repeats <= 0) { skipped++; continue; }

            if (mode == 0) {
                wchar_t suf[16];
                swprintf(suf, 16, L"-%d", info.repeats);
                newName = info.base + suf + info.ext;
            } else {
                SYSTEMTIME st;
                if (!GetFileMTimeLocal(fullPath, &st)) { skipped++; continue; }
                newName = info.base + L" " + FormatFilenameDate(st) + info.ext;
            }
        } else {
            // mode == 2: дописываем дату к текущему имени как есть, суффикс копии не трогаем
            SYSTEMTIME st;
            if (!GetFileMTimeLocal(fullPath, &st)) { skipped++; continue; }

            std::wstring base2 = name, ext2;
            if (!isFolder) {
                size_t dot = base2.find_last_of(L'.');
                if (dot != std::wstring::npos && dot > 0) {
                    ext2 = base2.substr(dot);
                    base2 = base2.substr(0, dot);
                }
            }
            newName = base2 + L" " + FormatFilenameDate(st) + ext2;
        }

        if (newName.empty() || newName == name) { skipped++; continue; }

        std::wstring newFull = dir.empty() ? newName : (dir + L"\\" + newName);

        if (GetFileAttributesW(newFull.c_str()) != INVALID_FILE_ATTRIBUTES) {
            skipped++; // не перезаписываем существующее
            continue;
        }

        if (MoveFileW(fullPath.c_str(), newFull.c_str())) {
            done++;
            NotifyRenamed(fullPath);
        } else {
            skipped++;
        }
    }

    wchar_t msg[256];
    swprintf(msg, 256, S_MSG_RENAME_DONE_FMT, done, skipped);
    MessageBoxW(owner, msg, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
}

} // namespace

void Cmd_RenameCopyNumber(HWND owner, const SelectionInfo& sel) { DoRenameOperation(owner, sel, 0); }
void Cmd_RenameCopyDate(HWND owner, const SelectionInfo& sel)   { DoRenameOperation(owner, sel, 1); }
void Cmd_AppendDate(HWND owner, const SelectionInfo& sel)       { DoRenameOperation(owner, sel, 2); }

// ============================================================ клонирование (копия рядом)

namespace {

// Ищем свободное имя вида "base DATE.ext", а при занятости — "base DATE (2).ext", "(3)"...
bool FindFreeCloneNameByDate(const std::wstring& dir, const std::wstring& base, const std::wstring& ext,
                              const SYSTEMTIME& st, std::wstring* outNewName)
{
    std::wstring dateSuf = FormatFilenameDate(st);
    for (int n = 1; n <= 1000; n++) {
        std::wstring candidate = (n == 1) ? (base + L" " + dateSuf + ext)
                                           : (base + L" " + dateSuf + L" (" + std::to_wstring(n) + L")" + ext);
        std::wstring full = dir.empty() ? candidate : (dir + L"\\" + candidate);
        if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
            *outNewName = candidate;
            return true;
        }
    }
    return false;
}

// Ищем свободное имя вида "base_1.ext", "base_2.ext"...
bool FindFreeCloneNameNumbered(const std::wstring& dir, const std::wstring& base, const std::wstring& ext,
                                std::wstring* outNewName)
{
    for (int n = 1; n <= 100000; n++) {
        std::wstring candidate = base + L"_" + std::to_wstring(n) + ext;
        std::wstring full = dir.empty() ? candidate : (dir + L"\\" + candidate);
        if (GetFileAttributesW(full.c_str()) == INVALID_FILE_ATTRIBUTES) {
            *outNewName = candidate;
            return true;
        }
    }
    return false;
}

// mode: 0 = дата изменения, 1 = дата создания, 2 = порядковый номер
void DoCloneOperation(HWND owner, const SelectionInfo& sel, int mode)
{
    if (sel.paths.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    HRESULT hrCo = CoInitialize(nullptr);
    bool needUninit = (hrCo == S_OK); // если уже была инициализирована (S_FALSE) - не наше дело её закрывать

    IFileOperation* pfo = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&pfo));
    if (FAILED(hr) || !pfo) {
        MessageBoxW(owner, S_MSG_CLONE_ERROR, S_MSG_TITLE, MB_OK | MB_ICONERROR);
        if (needUninit) CoUninitialize();
        return;
    }
    pfo->SetOperationFlags(FOF_NOCONFIRMATION | FOF_NOCONFIRMMKDIR | FOF_NOERRORUI);

    int queued = 0, skipped = 0;

    for (auto& fullPath : sel.paths) {
        DWORD attrs = GetFileAttributesW(fullPath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) { skipped++; continue; }
        bool isFolder = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

        std::wstring dir = DirNameOnly(fullPath);
        std::wstring name = FileNameOnly(fullPath);
        std::wstring base = name, ext;
        if (!isFolder) {
            size_t dot = base.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0) { ext = base.substr(dot); base = base.substr(0, dot); }
        }

        std::wstring newName;
        bool ok;
        if (mode == 0 || mode == 1) {
            FILETIME ftC, ftA, ftW;
            if (!GetFileTimes(fullPath, &ftC, &ftA, &ftW)) { skipped++; continue; }
            SYSTEMTIME st;
            SystemTimeLocalFromFileTime(mode == 0 ? ftW : ftC, &st);
            ok = FindFreeCloneNameByDate(dir, base, ext, st, &newName);
        } else {
            ok = FindFreeCloneNameNumbered(dir, base, ext, &newName);
        }
        if (!ok) { skipped++; continue; }

        IShellItem* pSrc = nullptr;
        if (FAILED(SHCreateItemFromParsingName(fullPath.c_str(), nullptr, IID_PPV_ARGS(&pSrc))) || !pSrc) {
            skipped++; continue;
        }
        IShellItem* pDestFolder = nullptr;
        if (FAILED(SHCreateItemFromParsingName(dir.c_str(), nullptr, IID_PPV_ARGS(&pDestFolder))) || !pDestFolder) {
            pSrc->Release(); skipped++; continue;
        }

        HRESULT hrCopy = pfo->CopyItem(pSrc, pDestFolder, newName.c_str(), nullptr);
        pSrc->Release();
        pDestFolder->Release();

        if (SUCCEEDED(hrCopy)) queued++;
        else skipped++;
    }

    if (queued > 0)
        pfo->PerformOperations();
    pfo->Release();
    if (needUninit) CoUninitialize();

    wchar_t msg[256];
    swprintf(msg, 256, S_MSG_CLONE_DONE_FMT, queued, skipped);
    MessageBoxW(owner, msg, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
}

} // namespace

void Cmd_CloneDateModified(HWND owner, const SelectionInfo& sel) { DoCloneOperation(owner, sel, 0); }
void Cmd_CloneDateCreated(HWND owner, const SelectionInfo& sel)  { DoCloneOperation(owner, sel, 1); }
void Cmd_CloneNumbered(HWND owner, const SelectionInfo& sel)     { DoCloneOperation(owner, sel, 2); }

// ============================================================ дерево путей / имён (только для папок)

namespace {

void CollectTree(const std::wstring& root, bool namesOnly, std::vector<std::wstring>* out)
{
    std::wstring base = root;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    std::wstring search = base + L"*";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        std::wstring full = base + fd.cFileName;

        if (namesOnly)
            out->push_back(fd.cFileName);
        else
            out->push_back(isDir ? (full + L"\\") : full); // папки — с завершающим "\", для наглядности дерева

        if (isDir && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            CollectTree(full, namesOnly, out); // не заходим в junction/symlink
    } while (FindNextFileW(h, &fd));

    FindClose(h);
}

void DoTreeOperation(HWND owner, const SelectionInfo& sel, bool namesOnly)
{
    if (sel.paths.empty() || !sel.allFolders) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::vector<std::wstring> lines;
    for (auto& p : sel.paths)
        CollectTree(p, namesOnly, &lines);

    if (lines.empty()) {
        MessageBoxW(owner, S_MSG_TREE_EMPTY, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    CopyTextToClipboard(owner, JoinLines(lines));
}

} // namespace

void Cmd_TreePaths(HWND owner, const SelectionInfo& sel) { DoTreeOperation(owner, sel, false); }
void Cmd_TreeNames(HWND owner, const SelectionInfo& sel) { DoTreeOperation(owner, sel, true); }
