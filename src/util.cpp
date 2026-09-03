#include "common.h"
#include "strings.h"
#include <cstdio>

// ============================================================ выборка (HDROP)

SelectionInfo GetSelectionInfo(IDataObject* pDataObj)
{
    SelectionInfo info;
    if (!pDataObj) return info;

    FORMATETC fmte = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stg;
    if (FAILED(pDataObj->GetData(&fmte, &stg)))
        return info;

    HDROP hDrop = (HDROP)stg.hGlobal;
    UINT count = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    info.paths.reserve(count);

    for (UINT i = 0; i < count; i++) {
        UINT need = DragQueryFileW(hDrop, i, nullptr, 0);
        if (need == 0) continue;
        std::wstring p(need, L'\0');
        DragQueryFileW(hDrop, i, &p[0], need + 1);

        DWORD attrs = GetFileAttributesW(p.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
            info.allFiles = false;
        else
            info.allFolders = false;

        info.paths.push_back(std::move(p));
    }

    ReleaseStgMedium(&stg);

    if (info.paths.empty()) { info.allFiles = false; info.allFolders = false; }
    return info;
}

// ============================================================ буфер обмена

void CopyTextToClipboard(HWND hwnd, const std::wstring& text)
{
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* p = GlobalLock(hMem);
        if (p) {
            memcpy(p, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem); // владение переходит системе
        }
    }
    CloseClipboard();
}

// ============================================================ рекурсивный размер

unsigned long long GetPathSizeRecursive(const std::wstring& path, bool* pAnyError)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (pAnyError) *pAnyError = true;
        return 0;
    }

    if (!(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
            ULARGE_INTEGER sz;
            sz.HighPart = fad.nFileSizeHigh;
            sz.LowPart = fad.nFileSizeLow;
            return sz.QuadPart;
        }
        if (pAnyError) *pAnyError = true;
        return 0;
    }

    // reparse point (симлинк/junction) — не рекурсируем, чтобы не зациклиться
    if (attrs & FILE_ATTRIBUTE_REPARSE_POINT)
        return 0;

    std::wstring base = path;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    std::wstring search = base + L"*";

    unsigned long long total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) {
        if (pAnyError) *pAnyError = true;
        return 0;
    }

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;

        std::wstring child = base + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                continue;
            total += GetPathSizeRecursive(child, pAnyError);
        } else {
            ULARGE_INTEGER sz;
            sz.HighPart = fd.nFileSizeHigh;
            sz.LowPart = fd.nFileSizeLow;
            total += sz.QuadPart;
        }
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    return total;
}

// ============================================================ форматирование чисел (без разделителей разрядов!)

std::wstring FormatPlainNumber(unsigned long long value)
{
    wchar_t buf[32];
    swprintf(buf, 32, L"%llu", value);
    return buf;
}

std::wstring FormatFixed(double value, int decimals)
{
    wchar_t fmt[16];
    swprintf(fmt, 16, L"%%.%df", decimals);
    wchar_t buf[64];
    swprintf(buf, 64, fmt, value);
    return buf;
}

std::wstring RussianMonthGenitive(int m)
{
    if (m < 1 || m > 12) return L"";
    return MONTHS_GEN_LOWER[m - 1];
}

// ============================================================ время файлов

bool GetFileTimes(const std::wstring& path, FILETIME* ftCreate, FILETIME* ftAccess, FILETIME* ftWrite)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    DWORD flags = FILE_FLAG_BACKUP_SEMANTICS; // необходимо, чтобы открыть папку через CreateFile
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
        flags |= FILE_FLAG_OPEN_REPARSE_POINT; // время самой точки, не цели

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, flags, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    BOOL ok = GetFileTime(h, ftCreate, ftAccess, ftWrite);
    CloseHandle(h);
    return ok != 0;
}

void SystemTimeLocalFromFileTime(const FILETIME& ft, SYSTEMTIME* stLocal)
{
    FILETIME ftLocal;
    FileTimeToLocalFileTime(&ft, &ftLocal);
    FileTimeToSystemTime(&ftLocal, stLocal);
}

bool FileTimeUtcFromSystemTimeLocal(const SYSTEMTIME& stLocal, FILETIME* ftUtc)
{
    FILETIME ftLocal;
    if (!SystemTimeToFileTime(&stLocal, &ftLocal)) return false; // это пока не UTC, а те же числа в виде FILETIME
    return LocalFileTimeToFileTime(&ftLocal, ftUtc) != 0; // а вот теперь конвертация локальное -> UTC
}

// ============================================================ разбор суффикса "— копия"

CopySuffixInfo ParseCopySuffix(const std::wstring& name, bool isFolder)
{
    CopySuffixInfo info;
    std::wstring base = name;
    std::wstring ext;

    if (!isFolder) {
        size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) {
            ext = base.substr(dot);
            base = base.substr(0, dot);
        }
    }

    const std::wstring suffix = S_COPY_SUFFIX; // " — копия"
    int repeats = 0;
    while (base.size() >= suffix.size() &&
           base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0) {
        base.resize(base.size() - suffix.size());
        repeats++;
    }

    info.base = base;
    info.ext = ext;
    info.repeats = repeats;
    return info;
}

// ============================================================ окно с копируемыми строками

namespace {

struct RowCtrl { std::wstring value; int action = 0; };

struct RowWnd {
    HWND label = nullptr;
    HWND edit = nullptr;
    HWND copyBtn = nullptr;
    HWND openBtn = nullptr; // может быть nullptr
    int logicalY = 0;
    int height = 0;
};

std::vector<RowCtrl>* g_activeRows = nullptr;
int* g_pResultCode = nullptr;   // 0=закрыто, 1=далее, -1=назад
bool* g_pDoneFlag = nullptr;
bool g_dlgClassRegistered = false;

std::vector<RowWnd>* g_rowWnds = nullptr;
bool g_needsScroll = false;
int g_rowsAreaTop = 0;
int g_rowsAreaH = 0;
int g_fullRowsH = 0;
int g_scrollPos = 0;
int g_labelX = 0, g_editX = 0, g_copyX = 0, g_openX = 0;

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    WNDPROC orig = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (msg == WM_SETFOCUS) {
        LRESULT r = orig ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
        SendMessageW(hwnd, EM_SETSEL, 0, -1);
        return r;
    }
    return orig ? CallWindowProcW(orig, hwnd, msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

// Ручное перепозиционирование ВСЕХ строк под текущий scrollPos — надёжно работает
// для любого количества строк и любой дистанции скролла, в отличие от
// ScrollWindowEx(SW_SCROLLCHILDREN), который двигает только окна, пересекающиеся
// с областью скролла НА МОМЕНТ вызова (и потому "теряет" далёкие строки).
void RepositionRows()
{
    if (!g_rowWnds) return;
    int count = (int)g_rowWnds->size();
    HDWP hdwp = BeginDeferWindowPos(count * 4);

    int visTop = g_rowsAreaTop;
    int visBottom = g_rowsAreaTop + g_rowsAreaH;

    for (auto& rw : *g_rowWnds) {
        int y = g_rowsAreaTop + rw.logicalY - g_scrollPos;
        bool visible = (y + rw.height > visTop) && (y < visBottom);

        UINT flags = SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE;
        if (hdwp) hdwp = DeferWindowPos(hdwp, rw.label, nullptr, g_labelX, y + 4, 0, 0, flags);
        if (hdwp) hdwp = DeferWindowPos(hdwp, rw.edit, nullptr, g_editX, y, 0, 0, flags);
        if (hdwp) hdwp = DeferWindowPos(hdwp, rw.copyBtn, nullptr, g_copyX, y, 0, 0, flags);
        if (rw.openBtn && hdwp) hdwp = DeferWindowPos(hdwp, rw.openBtn, nullptr, g_openX, y, 0, 0, flags);

        ShowWindow(rw.label, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        ShowWindow(rw.edit, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        ShowWindow(rw.copyBtn, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (rw.openBtn) ShowWindow(rw.openBtn, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
    }

    if (hdwp) EndDeferWindowPos(hdwp);
}

void DoScroll(HWND hwnd, int newPos)
{
    int maxPos = g_fullRowsH - g_rowsAreaH;
    if (maxPos < 0) maxPos = 0;
    if (newPos < 0) newPos = 0;
    if (newPos > maxPos) newPos = maxPos;
    if (newPos == g_scrollPos) return;

    g_scrollPos = newPos;

    SCROLLINFO si = { sizeof(si) };
    si.fMask = SIF_POS;
    si.nPos = g_scrollPos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

    RepositionRows();
}

LRESULT CALLBACK InfoDlgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id == 2000) { if (g_pResultCode) *g_pResultCode = 0; DestroyWindow(hwnd); return 0; }
        if (id == 2001) { if (g_pResultCode) *g_pResultCode = -1; DestroyWindow(hwnd); return 0; }
        if (id == 2002) { if (g_pResultCode) *g_pResultCode = 1; DestroyWindow(hwnd); return 0; }
        if (id >= 3000 && id < 4000 && g_activeRows) {
            size_t idx = (size_t)(id - 3000);
            if (idx < g_activeRows->size()) {
                auto& row = (*g_activeRows)[idx];
                if (row.action == 2) {
                    std::wstring params = L"/select,\"" + row.value + L"\"";
                    ShellExecuteW(hwnd, L"open", L"explorer.exe", params.c_str(), nullptr, SW_SHOWNORMAL);
                } else {
                    ShellExecuteW(hwnd, L"open", row.value.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                }
            }
            return 0;
        }
        if (id >= 1000 && id < 2000 && g_activeRows) {
            size_t idx = (size_t)(id - 1000);
            if (idx < g_activeRows->size())
                CopyTextToClipboard(hwnd, (*g_activeRows)[idx].value);
            return 0;
        }
        break;
    }
    case WM_VSCROLL: {
        if (!g_needsScroll) break;
        int pos = g_scrollPos;
        switch (LOWORD(wp)) {
        case SB_LINEUP:      pos -= 24; break;
        case SB_LINEDOWN:    pos += 24; break;
        case SB_PAGEUP:      pos -= g_rowsAreaH; break;
        case SB_PAGEDOWN:    pos += g_rowsAreaH; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = { sizeof(si) };
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, SB_VERT, &si);
            pos = si.nTrackPos;
            break;
        }
        default: return 0;
        }
        DoScroll(hwnd, pos);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!g_needsScroll) break;
        short wheel = GET_WHEEL_DELTA_WPARAM(wp);
        DoScroll(hwnd, g_scrollPos - (wheel / WHEEL_DELTA) * 48);
        return 0;
    }
    case WM_CLOSE:
        if (g_pResultCode) *g_pResultCode = 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g_pDoneFlag) *g_pDoneFlag = true;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int ShowInfoDialog(HWND owner, const std::wstring& title, const std::vector<InfoRow>& rows,
                    bool showNav, bool enablePrev, bool enableNext)
{
    if (!g_dlgClassRegistered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = InfoDlgProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"FFI_InfoDlgClass";
        RegisterClassW(&wc);
        g_dlgClassRegistered = true;
    }

    const int marginX = 12, marginY = 12;
    const int labelW = 170, editW = 300, copyBtnW = 90, openBtnW = 120, gap = 8;
    const int baseRowH = 24, lineIncrement = 16;
    const int footerBtnH = 26;

    int n = (int)rows.size();
    bool hasAnyLink = false;
    std::vector<int> rowHeights(n);
    std::vector<int> rowYs(n);
    int fullRowsH = 0;
    for (int i = 0; i < n; i++) {
        if (rows[i].action != 0) hasAnyLink = true;
        rowHeights[i] = baseRowH + (rows[i].extraLines > 0 ? rows[i].extraLines * lineIncrement : 0);
        rowYs[i] = fullRowsH;
        fullRowsH += rowHeights[i] + gap;
    }

    int clientW = marginX * 2 + labelW + editW + copyBtnW + gap * 2 + (hasAnyLink ? (openBtnW + gap) : 0);

    RECT workArea = { 0, 0, 1024, 768 };
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);
    int screenH = workArea.bottom - workArea.top;
    int maxRowsAreaH = screenH - 170; // запас на заголовок окна, футер и отступы от краёв экрана
    if (maxRowsAreaH < 120) maxRowsAreaH = 120;

    bool needsScroll = fullRowsH > maxRowsAreaH;
    int rowsAreaH = needsScroll ? maxRowsAreaH : fullRowsH;

    int footerY = marginY + rowsAreaH + gap;
    int clientH = footerY + footerBtnH + marginY;

    RECT rc = { 0, 0, clientW, clientH };
    DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU | (needsScroll ? WS_VSCROLL : 0);
    AdjustWindowRect(&rc, style, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    if (winH > screenH - 20) winH = screenH - 20;

    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT ownerRc;
    if (owner && GetWindowRect(owner, &ownerRc)) {
        x = ownerRc.left + ((ownerRc.right - ownerRc.left) - winW) / 2;
        y = ownerRc.top + ((ownerRc.bottom - ownerRc.top) - winH) / 2;
        if (y < workArea.top) y = workArea.top;
        if (x < workArea.left) x = workArea.left;
    }

    if (owner) EnableWindow(owner, FALSE);

    HWND hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"FFI_InfoDlgClass", title.c_str(),
                                 style, x, y, winW, winH, owner, nullptr, g_hInst, nullptr);
    if (!hwnd) {
        if (owner) EnableWindow(owner, TRUE);
        return 0;
    }

    std::vector<RowCtrl> ctrls;
    ctrls.reserve(n);
    std::vector<RowWnd> rowWnds;
    rowWnds.reserve(n);
    HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    int labelX = marginX;
    int editX = marginX + labelW + gap;
    int copyX = editX + editW + gap;
    int openX = copyX + copyBtnW + gap;

    for (int i = 0; i < n; i++) {
        int y0 = marginY + rowYs[i]; // изначальное создание — на полной логической позиции, потом сразу пересчитаем

        HWND hLabel = CreateWindowExW(0, L"STATIC", rows[i].label.c_str(),
            WS_CHILD | (rows[i].label.empty() ? 0 : WS_VISIBLE), labelX, y0 + 4, labelW, rowHeights[i],
            hwnd, nullptr, g_hInst, nullptr);
        SendMessageW(hLabel, WM_SETFONT, (WPARAM)hFont, TRUE);

        DWORD editStyle = WS_CHILD | WS_VISIBLE | ES_READONLY;
        editStyle |= (rows[i].extraLines > 0) ? (ES_MULTILINE | ES_AUTOVSCROLL) : ES_AUTOHSCROLL;

        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", rows[i].value.c_str(),
            editStyle, editX, y0, editW, rowHeights[i],
            hwnd, nullptr, g_hInst, nullptr);
        SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);

        WNDPROC origProc = (WNDPROC)SetWindowLongPtrW(hEdit, GWLP_WNDPROC, (LONG_PTR)EditProc);
        SetWindowLongPtrW(hEdit, GWLP_USERDATA, (LONG_PTR)origProc);

        HWND hBtn = CreateWindowExW(0, L"BUTTON", S_BTN_COPY,
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            copyX, y0, copyBtnW, baseRowH,
            hwnd, (HMENU)(INT_PTR)(1000 + i), g_hInst, nullptr);
        SendMessageW(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hOpenBtn = nullptr;
        if (rows[i].action != 0) {
            const wchar_t* btnText = (rows[i].action == 2) ? S_BTN_REVEAL : S_BTN_OPEN;
            hOpenBtn = CreateWindowExW(0, L"BUTTON", btnText,
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                openX, y0, openBtnW, baseRowH,
                hwnd, (HMENU)(INT_PTR)(3000 + i), g_hInst, nullptr);
            SendMessageW(hOpenBtn, WM_SETFONT, (WPARAM)hFont, TRUE);
        }

        ctrls.push_back({ rows[i].value, rows[i].action });
        rowWnds.push_back({ hLabel, hEdit, hBtn, hOpenBtn, rowYs[i], rowHeights[i] });
    }

    // Футер (Закрыть/Пред/След) — фиксирован, лежит НИЖЕ прокручиваемой области и в неё не входит.
    int footerBtnW = 100;
    int fx = clientW - marginX - footerBtnW;
    HWND hClose = CreateWindowExW(0, L"BUTTON", S_BTN_CLOSE,
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_DEFPUSHBUTTON,
        fx, footerY, footerBtnW, footerBtnH,
        hwnd, (HMENU)(INT_PTR)2000, g_hInst, nullptr);
    SendMessageW(hClose, WM_SETFONT, (WPARAM)hFont, TRUE);

    if (showNav) {
        int navW = 90;
        HWND hNext = CreateWindowExW(0, L"BUTTON", L"\u25B6", // ▶
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | (enableNext ? 0 : WS_DISABLED),
            fx - gap - navW, footerY, navW, footerBtnH,
            hwnd, (HMENU)(INT_PTR)2002, g_hInst, nullptr);
        SendMessageW(hNext, WM_SETFONT, (WPARAM)hFont, TRUE);

        HWND hPrev = CreateWindowExW(0, L"BUTTON", L"\u25C0", // ◀
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | (enablePrev ? 0 : WS_DISABLED),
            fx - gap - navW - gap - navW, footerY, navW, footerBtnH,
            hwnd, (HMENU)(INT_PTR)2001, g_hInst, nullptr);
        SendMessageW(hPrev, WM_SETFONT, (WPARAM)hFont, TRUE);
    }

    g_rowWnds = &rowWnds;
    g_needsScroll = needsScroll;
    g_rowsAreaTop = marginY;
    g_rowsAreaH = rowsAreaH;
    g_fullRowsH = fullRowsH;
    g_scrollPos = 0;
    g_labelX = labelX; g_editX = editX; g_copyX = copyX; g_openX = openX;

    if (needsScroll) {
        SCROLLINFO si = { sizeof(si) };
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = fullRowsH;
        si.nPage = rowsAreaH;
        si.nPos = 0;
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    }

    RepositionRows(); // приводим все строки к их правильной видимой/скрытой позиции сразу, до показа окна

    bool done = false;
    int resultCode = 0;
    g_activeRows = &ctrls;
    g_pDoneFlag = &done;
    g_pResultCode = &resultCode;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetFocus(hClose);

    MSG msg;
    while (!done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && IsChild(hwnd, msg.hwnd)) {
            resultCode = 0;
            DestroyWindow(hwnd);
            continue;
        }
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    g_activeRows = nullptr;
    g_pDoneFlag = nullptr;
    g_pResultCode = nullptr;
    g_rowWnds = nullptr;
    g_needsScroll = false;

    if (owner) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }

    return resultCode;
}
