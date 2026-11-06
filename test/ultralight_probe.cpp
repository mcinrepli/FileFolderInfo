#include <windows.h>
#include <windowsx.h>
#include <cstdio>
#include <cstdarg>

// Официальные заголовки — только ради типов (ULView, ULRenderer, структуры)
// и сигнатур для typedef'ов. Сами функции берём через GetProcAddress, не
// линкуемся на Ultralight.lib/AppCore.lib напрямую (это .lib от MSVC/Clang,
// с MinGW напрямую не дружат — та же история, что и с WebView2Loader).
#include "Ultralight/CAPI.h"
#include "AppCore/CAPI.h"

// ---- сигнатуры нужных функций (минимальный набор для offscreen-рендера) ----
typedef void          (STDAPICALLTYPE *Fn_ulEnablePlatformFontLoader)();
typedef void          (STDAPICALLTYPE *Fn_ulEnablePlatformFileSystem)(ULString);
typedef void           (STDAPICALLTYPE *Fn_ulEnableDefaultLogger)(ULString);
typedef ULString      (STDAPICALLTYPE *Fn_ulCreateString)(const char*);
typedef void           (STDAPICALLTYPE *Fn_ulDestroyString)(ULString);
typedef ULConfig       (STDAPICALLTYPE *Fn_ulCreateConfig)();
typedef void           (STDAPICALLTYPE *Fn_ulDestroyConfig)(ULConfig);
typedef void           (STDAPICALLTYPE *Fn_ulConfigSetResourcePathPrefix)(ULConfig, ULString);
typedef ULRenderer     (STDAPICALLTYPE *Fn_ulCreateRenderer)(ULConfig);
typedef void           (STDAPICALLTYPE *Fn_ulDestroyRenderer)(ULRenderer);
typedef ULViewConfig   (STDAPICALLTYPE *Fn_ulCreateViewConfig)();
typedef void           (STDAPICALLTYPE *Fn_ulDestroyViewConfig)(ULViewConfig);
typedef void           (STDAPICALLTYPE *Fn_ulViewConfigSetIsAccelerated)(ULViewConfig, bool);
typedef ULView         (STDAPICALLTYPE *Fn_ulCreateView)(ULRenderer, unsigned int, unsigned int, ULViewConfig, ULSession);
typedef void           (STDAPICALLTYPE *Fn_ulDestroyView)(ULView);
typedef void           (STDAPICALLTYPE *Fn_ulViewLoadHTML)(ULView, ULString);
typedef void           (STDAPICALLTYPE *Fn_ulViewLoadURL)(ULView, ULString);
typedef void           (STDAPICALLTYPE *Fn_ulUpdate)(ULRenderer);
typedef void           (STDAPICALLTYPE *Fn_ulRender)(ULRenderer);
typedef ULSurface      (STDAPICALLTYPE *Fn_ulViewGetSurface)(ULView);
typedef ULBitmap       (STDAPICALLTYPE *Fn_ulBitmapSurfaceGetBitmap)(ULSurface);
typedef void*          (STDAPICALLTYPE *Fn_ulBitmapLockPixels)(ULBitmap);
typedef void           (STDAPICALLTYPE *Fn_ulBitmapUnlockPixels)(ULBitmap);
typedef unsigned int   (STDAPICALLTYPE *Fn_ulBitmapGetWidth)(ULBitmap);
typedef unsigned int   (STDAPICALLTYPE *Fn_ulBitmapGetHeight)(ULBitmap);
typedef unsigned int   (STDAPICALLTYPE *Fn_ulBitmapGetRowBytes)(ULBitmap);
typedef ULMouseEvent   (STDAPICALLTYPE *Fn_ulCreateMouseEvent)(ULMouseEventType, int, int, ULMouseButton);
typedef void           (STDAPICALLTYPE *Fn_ulDestroyMouseEvent)(ULMouseEvent);
typedef void           (STDAPICALLTYPE *Fn_ulViewFireMouseEvent)(ULView, ULMouseEvent);
typedef ULScrollEvent  (STDAPICALLTYPE *Fn_ulCreateScrollEvent)(ULScrollEventType, int, int);
typedef void           (STDAPICALLTYPE *Fn_ulDestroyScrollEvent)(ULScrollEvent);
typedef void           (STDAPICALLTYPE *Fn_ulViewFireScrollEvent)(ULView, ULScrollEvent);
typedef void           (STDAPICALLTYPE *Fn_ulViewResize)(ULView, unsigned int, unsigned int);
typedef void           (STDAPICALLTYPE *Fn_ulViewSetNeedsPaint)(ULView, bool);
typedef bool           (STDAPICALLTYPE *Fn_ulViewGetNeedsPaint)(ULView);
typedef void           (STDAPICALLTYPE *Fn_ulConfigSetForceRepaint)(ULConfig, bool);
typedef ULString       (STDAPICALLTYPE *Fn_ulViewEvaluateScript)(ULView, ULString, ULString*);
typedef char*          (STDAPICALLTYPE *Fn_ulStringGetData)(ULString);

struct UL {
    Fn_ulEnablePlatformFontLoader        EnablePlatformFontLoader;
    Fn_ulEnablePlatformFileSystem        EnablePlatformFileSystem;
    Fn_ulEnableDefaultLogger             EnableDefaultLogger;
    Fn_ulCreateString                    CreateString;
    Fn_ulDestroyString                   DestroyString;
    Fn_ulCreateConfig                    CreateConfig;
    Fn_ulDestroyConfig                   DestroyConfig;
    Fn_ulConfigSetResourcePathPrefix     ConfigSetResourcePathPrefix;
    Fn_ulCreateRenderer                  CreateRenderer;
    Fn_ulDestroyRenderer                 DestroyRenderer;
    Fn_ulCreateViewConfig                CreateViewConfig;
    Fn_ulDestroyViewConfig               DestroyViewConfig;
    Fn_ulViewConfigSetIsAccelerated      ViewConfigSetIsAccelerated;
    Fn_ulCreateView                      CreateView;
    Fn_ulDestroyView                     DestroyView;
    Fn_ulViewLoadHTML                    ViewLoadHTML;
    Fn_ulViewLoadURL                     ViewLoadURL;
    Fn_ulUpdate                          Update;
    Fn_ulRender                          Render;
    Fn_ulViewGetSurface                  ViewGetSurface;
    Fn_ulBitmapSurfaceGetBitmap          BitmapSurfaceGetBitmap;
    Fn_ulBitmapLockPixels                BitmapLockPixels;
    Fn_ulBitmapUnlockPixels              BitmapUnlockPixels;
    Fn_ulBitmapGetWidth                  BitmapGetWidth;
    Fn_ulBitmapGetHeight                 BitmapGetHeight;
    Fn_ulBitmapGetRowBytes               BitmapGetRowBytes;
    Fn_ulCreateMouseEvent                CreateMouseEvent;
    Fn_ulDestroyMouseEvent               DestroyMouseEvent;
    Fn_ulViewFireMouseEvent              ViewFireMouseEvent;
    Fn_ulCreateScrollEvent               CreateScrollEvent;
    Fn_ulDestroyScrollEvent              DestroyScrollEvent;
    Fn_ulViewFireScrollEvent             ViewFireScrollEvent;
    Fn_ulViewResize                      ViewResize;
    Fn_ulViewSetNeedsPaint               ViewSetNeedsPaint;
    Fn_ulViewGetNeedsPaint               ViewGetNeedsPaint;
    Fn_ulConfigSetForceRepaint           ConfigSetForceRepaint;
    Fn_ulViewEvaluateScript              ViewEvaluateScript;
    Fn_ulStringGetData                   StringGetData;
} ul;

#define LOAD(mod, name) \
    do { ul.name = (Fn_ul##name)GetProcAddress(mod, "ul" #name); \
         if (!ul.name) { wchar_t b[128]; swprintf(b, 128, L"GetProcAddress не нашёл ul%hs", #name); \
                          MessageBoxW(nullptr, b, L"probe", MB_OK | MB_ICONERROR); ExitProcess(1); } \
    } while (0)

static ULRenderer g_renderer = nullptr;
static ULView g_view = nullptr;
static HWND g_hwnd = nullptr;

// --- диагностика -----------------------------------------------------------
// FFI_FORCE_REPAINT=1  — включить Config::force_repaint (перерисовывать всё
//                        каждый кадр, независимо от dirty-флага).
// FFI_NOANIM=1         — после загрузки страницы выключить zoomAnimation у
//                        Leaflet (проверка гипотезы про CSS-transform-слой).
// FFI_DIAG=1           — писать в лог хеш битмапа и счётчики из JS.
static bool g_forceRepaint = false;
static bool g_noAnim = false;
static bool g_diag = false;

static bool GetEnvFlag(const wchar_t* name)
{
    wchar_t buf[8] = {};
    DWORD n = GetEnvironmentVariableW(name, buf, 8);
    return n > 0 && buf[0] == L'1';
}

void BlitViewToWindow()
{
    ULSurface surface = ul.ViewGetSurface(g_view);
    ULBitmap bitmap = ul.BitmapSurfaceGetBitmap(surface);
    unsigned int w = ul.BitmapGetWidth(bitmap);
    unsigned int h = ul.BitmapGetHeight(bitmap);
    unsigned int stride = ul.BitmapGetRowBytes(bitmap);
    void* pixels = ul.BitmapLockPixels(bitmap);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = (LONG)w;
    bmi.bmiHeader.biHeight = -(LONG)h; // отрицательная высота — top-down DIB, как и у Ultralight
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC hdc = GetDC(g_hwnd);
    StretchDIBits(hdc, 0, 0, (int)w, (int)h, 0, 0, (int)w, (int)h,
                  pixels, &bmi, DIB_RGB_COLORS, SRCCOPY);
    ReleaseDC(g_hwnd, hdc);

    ul.BitmapUnlockPixels(bitmap);
    (void)stride;
}

// Быстрый FNV-1a по пикселям — показывает, ИЗМЕНИЛАСЬ ли картинка физически.
// Если после зума хеш не меняется, значит рендер не отрисовал новый кадр
// (проблема в dirty-флаге), а не в блите на экран.
static unsigned int HashBitmap()
{
    ULSurface surface = ul.ViewGetSurface(g_view);
    ULBitmap bitmap = ul.BitmapSurfaceGetBitmap(surface);
    unsigned int h = ul.BitmapGetHeight(bitmap);
    unsigned int stride = ul.BitmapGetRowBytes(bitmap);
    unsigned char* p = (unsigned char*)ul.BitmapLockPixels(bitmap);
    unsigned int hash = 2166136261u;
    if (p) {
        // Сэмплим не каждый байт — 1 из 16 достаточно для детекции изменений
        // и не тормозит рендер на 800x600x4.
        size_t total = (size_t)stride * h;
        for (size_t i = 0; i < total; i += 16) {
            hash ^= p[i];
            hash *= 16777619u;
        }
    }
    ul.BitmapUnlockPixels(bitmap);
    return hash;
}

// Не ждём следующего тика таймера (16мс) — сразу после события ввода
// прогоняем Update+Render+Blit, чтобы отклик был мгновенным, а не с
// задержкой до ближайшего WM_TIMER.
static void ProbeLog(const char* fmt, ...)
{
    FILE* f = fopen("ultralight_probe_input.log", "a");
    if (!f) return;
    fprintf(f, "[%lu] ", (unsigned long)GetTickCount());
    va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
    fputc('\n', f);
    fclose(f);
}

// Выполнить JS и вернуть результат строкой (для диагностики из лога).
static void EvalLog(const char* label, const char* js)
{
    ULString script = ul.CreateString(js);
    ULString exc = nullptr;
    ULString res = ul.ViewEvaluateScript(g_view, script, &exc);
    ul.DestroyString(script);
    if (exc) {
        char* ed = ul.StringGetData(exc);
        ProbeLog("%s EXC: %s", label, ed ? ed : "?");
        ul.DestroyString(exc);
        return;
    }
    if (res) {
        char* rd = ul.StringGetData(res);
        ProbeLog("%s = %s", label, rd ? rd : "null");
        ul.DestroyString(res);
    } else {
        ProbeLog("%s = <null>", label);
    }
}

// ГИПОТЕЗА (по наблюдениям пользователя): зум от колеса в JS происходит сразу,
// но флаг "нужна перерисовка" у view не ставится (CSS-transform-анимации Leaflet),
// поэтому картинка не обновляется, пока что-то другое (hover по кнопке, попап
// маркера) не поменяет DOM и не выставит флаг. Лог пишет, был ли флаг выставлен
// движком САМ, затем форсируем перерисовку вручную.
void PumpAndBlit(const char* reason)
{
    ul.Update(g_renderer);
    bool flagBefore = ul.ViewGetNeedsPaint(g_view);
    ul.ViewSetNeedsPaint(g_view, true);
    ul.Render(g_renderer);
    if (g_diag) {
        static unsigned int lastHash = 0;
        unsigned int h = HashBitmap();
        ProbeLog("%s: needsPaintBefore=%d hash=%08x %s",
                 reason ? reason : "?", (int)flagBefore, h,
                 (h == lastHash) ? "(unchanged)" : "(CHANGED)");
        lastHash = h;
    }
    BlitViewToWindow();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KILLFOCUS: {
        FILE* f = fopen("ultralight_probe_input.log", "a");
        if (f) { fprintf(f, "WM_KILLFOCUS — \u0444\u043E\u043A\u0443\u0441 \u0443\u0448\u0451\u043B \u043A %p\n", (void*)wp); fclose(f); }
        return 0;
    }
    case WM_MOUSEMOVE: {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        ULMouseButton btn = (wp & MK_LBUTTON) ? kMouseButton_Left
                          : (wp & MK_RBUTTON) ? kMouseButton_Right
                          : (wp & MK_MBUTTON) ? kMouseButton_Middle : kMouseButton_None;
        ULMouseEvent ev = ul.CreateMouseEvent(kMouseEventType_MouseMoved, x, y, btn);
        ul.ViewFireMouseEvent(g_view, ev);
        ul.DestroyMouseEvent(ev);
        PumpAndBlit("mousemove");
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN: {
        SetCapture(hwnd);
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        ULMouseButton btn = (msg == WM_LBUTTONDOWN) ? kMouseButton_Left
                           : (msg == WM_RBUTTONDOWN) ? kMouseButton_Right : kMouseButton_Middle;
        ULMouseEvent ev = ul.CreateMouseEvent(kMouseEventType_MouseDown, x, y, btn);
        ul.ViewFireMouseEvent(g_view, ev);
        ul.DestroyMouseEvent(ev);
        PumpAndBlit("mousedown");
        return 0;
    }
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP: {
        ReleaseCapture();
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        ULMouseButton btn = (msg == WM_LBUTTONUP) ? kMouseButton_Left
                           : (msg == WM_RBUTTONUP) ? kMouseButton_Right : kMouseButton_Middle;
        ULMouseEvent ev = ul.CreateMouseEvent(kMouseEventType_MouseUp, x, y, btn);
        ul.ViewFireMouseEvent(g_view, ev);
        ul.DestroyMouseEvent(ev);
        PumpAndBlit("mouseup");
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp); // обычно ±120 за "щелчок" колеса
        {
            FILE* f = fopen("ultralight_probe_input.log", "a");
            if (f) { fprintf(f, "WM_MOUSEWHEEL delta=%d focus=%p this_hwnd=%p\n",
                              delta, (void*)GetFocus(), (void*)hwnd); fclose(f); }
        }
        if (g_diag) {
            EvalLog("wheel: zoomBefore", "map.getZoom()");
            EvalLog("wheel: deltaY-js", "String(window.__lastWheelDeltaY)");
        }

        // пиксели на "щелчок" — эмпирически удобное для Leaflet значение
        int pixelDelta = (delta * 100) / WHEEL_DELTA;

        // Координаты в WM_MOUSEWHEEL — ЭКРАННЫЕ, не клиентские (в отличие от
        // WM_MOUSEMOVE/кнопок). Движку для скролла/зума важно ВНУТРЕННЕЕ
        // состояние "что под курсором", обновляемое именно MouseMoved —
        // scroll-событие такой позиции не несёт вообще. Освежаем перед прокруткой.
        POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
        ULMouseEvent moveEv = ul.CreateMouseEvent(kMouseEventType_MouseMoved, pt.x, pt.y, kMouseButton_None);
        ul.ViewFireMouseEvent(g_view, moveEv);
        ul.DestroyMouseEvent(moveEv);
        ULScrollEvent ev = ul.CreateScrollEvent(kScrollEventType_ScrollByPixel, 0, pixelDelta);
        ul.ViewFireScrollEvent(g_view, ev);
        ul.DestroyScrollEvent(ev);
        PumpAndBlit("wheel-now");

        // ВАЖНО: Leaflet применяет зум колесом НЕ сразу, а через
        // setTimeout(_performZoom, wheelDebounceTime=40мс) + CSS-transition
        // (~250мс). Поэтому мгновенный PumpAndBlit выше физически не может
        // показать результат. Планируем отложенные кадры, чтобы поймать
        // момент, когда зум уже применён (и записать хеш картинки).
        SetTimer(hwnd, 2, 60, nullptr);   // после debounce (~40мс)
        SetTimer(hwnd, 3, 320, nullptr);  // после transition (~250мс)
        return 0;
    }
    case WM_TIMER:
        if (wp == 99) {
            KillTimer(hwnd, 99);
            ProbeLog("=== baseline (page loaded) ===");
            EvalLog("baseline zoom", "map.getZoom()");
            EvalLog("baseline wheel", "window.__wheelCount");
            EvalLog("baseline raf", "window.__rafCount");
            EvalLog("baseline anim", "map.options.zoomAnimation");
            PumpAndBlit("baseline");
            return 0;
        }
        if (wp == 2 || wp == 3) {
            KillTimer(hwnd, (UINT_PTR)wp);   // одноразовый
            const char* which = (wp == 2) ? "wheel+60ms" : "wheel+320ms";
            if (g_diag) {
                EvalLog("  zoomAfter", "map.getZoom()");
            }
            PumpAndBlit(which);
            return 0;
        }
        ul.Update(g_renderer);
        ul.ViewSetNeedsPaint(g_view, true);
        ul.Render(g_renderer);
        BlitViewToWindow();
        return 0;
    case WM_SIZE: {
        // Раньше вьюпорт был зафиксирован 800x600 и не следил за окном —
        // при увеличении окна карта оставалась прежнего размера.
        unsigned int w = LOWORD(lp), h = HIWORD(lp);
        if (g_view && w > 0 && h > 0) {
            ul.ViewResize(g_view, w, h);
            PumpAndBlit("resize");
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        BlitViewToWindow();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    g_forceRepaint = GetEnvFlag(L"FFI_FORCE_REPAINT");
    g_noAnim       = GetEnvFlag(L"FFI_NOANIM");
    g_diag         = GetEnvFlag(L"FFI_DIAG");

    // Загружаем обе DLL из папки, где лежит сама проба (SetDllDirectory не
    // нужен — Windows и так первой проверяет каталог exe).
    HMODULE hUltralight = LoadLibraryW(L"Ultralight.dll");
    HMODULE hAppCore = LoadLibraryW(L"AppCore.dll");
    if (!hUltralight || !hAppCore) {
        MessageBoxW(nullptr, L"Не найдены Ultralight.dll / AppCore.dll рядом с probe.exe", L"probe", MB_OK | MB_ICONERROR);
        return 1;
    }

    LOAD(hAppCore, EnablePlatformFontLoader);
    LOAD(hAppCore, EnablePlatformFileSystem);
    LOAD(hAppCore, EnableDefaultLogger);
    LOAD(hUltralight, CreateString);
    LOAD(hUltralight, DestroyString);
    LOAD(hUltralight, CreateConfig);
    LOAD(hUltralight, DestroyConfig);
    LOAD(hUltralight, ConfigSetResourcePathPrefix);
    LOAD(hUltralight, CreateRenderer);
    LOAD(hUltralight, DestroyRenderer);
    LOAD(hUltralight, CreateViewConfig);
    LOAD(hUltralight, DestroyViewConfig);
    LOAD(hUltralight, ViewConfigSetIsAccelerated);
    LOAD(hUltralight, CreateView);
    LOAD(hUltralight, DestroyView);
    LOAD(hUltralight, ViewLoadHTML);
    LOAD(hUltralight, ViewLoadURL);
    LOAD(hUltralight, Update);
    LOAD(hUltralight, Render);
    LOAD(hUltralight, ViewGetSurface);
    LOAD(hUltralight, BitmapSurfaceGetBitmap);
    LOAD(hUltralight, BitmapLockPixels);
    LOAD(hUltralight, BitmapUnlockPixels);
    LOAD(hUltralight, BitmapGetWidth);
    LOAD(hUltralight, BitmapGetHeight);
    LOAD(hUltralight, BitmapGetRowBytes);
    LOAD(hUltralight, CreateMouseEvent);
    LOAD(hUltralight, DestroyMouseEvent);
    LOAD(hUltralight, ViewFireMouseEvent);
    LOAD(hUltralight, CreateScrollEvent);
    LOAD(hUltralight, DestroyScrollEvent);
    LOAD(hUltralight, ViewFireScrollEvent);
    LOAD(hUltralight, ViewResize);
    LOAD(hUltralight, ViewSetNeedsPaint);
    LOAD(hUltralight, ViewGetNeedsPaint);
    LOAD(hUltralight, ConfigSetForceRepaint);
    LOAD(hUltralight, ViewEvaluateScript);
    LOAD(hUltralight, StringGetData);

    ul.EnablePlatformFontLoader();

    ULString baseDir = ul.CreateString(".");
    ul.EnablePlatformFileSystem(baseDir);
    ul.DestroyString(baseDir);

    ULString logPath = ul.CreateString("./ultralight_probe.log");
    ul.EnableDefaultLogger(logPath);
    ul.DestroyString(logPath);

    ULConfig config = ul.CreateConfig();
    ULString resPath = ul.CreateString("./resources/");
    ul.ConfigSetResourcePathPrefix(config, resPath);
    ul.DestroyString(resPath);
    if (g_forceRepaint) {
        ul.ConfigSetForceRepaint(config, true);
        ProbeLog("force_repaint=ON");
    }

    g_renderer = ul.CreateRenderer(config);
    ul.DestroyConfig(config);

    ULViewConfig vc = ul.CreateViewConfig();
    ul.ViewConfigSetIsAccelerated(vc, false); // CPU-рендер — без DirectX
    g_view = ul.CreateView(g_renderer, 800, 600, vc, nullptr);
    ul.DestroyViewConfig(vc);

    const char* pageUrl = g_noAnim
        ? "file:///leaflet_page/page.html?noanim=1"
        : "file:///leaflet_page/page.html";
    ULString url = ul.CreateString(pageUrl);
    ul.ViewLoadURL(g_view, url);
    ul.DestroyString(url);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"UltralightProbeWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    g_hwnd = CreateWindowW(L"UltralightProbeWnd", L"Ultralight probe",
                           WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 820, 640,
                           nullptr, nullptr, hInst, nullptr);
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    SetTimer(g_hwnd, 1, 16, nullptr); // ~60 fps апдейт/рендер
    if (g_diag) SetTimer(g_hwnd, 99, 1500, nullptr); // эталонное состояние до колеса

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ul.DestroyView(g_view);
    ul.DestroyRenderer(g_renderer);
    return 0;
}
