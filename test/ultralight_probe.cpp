#include <windows.h>
#include <windowsx.h>
#include <cstdio>

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
} ul;

#define LOAD(mod, name) \
    do { ul.name = (Fn_ul##name)GetProcAddress(mod, "ul" #name); \
         if (!ul.name) { wchar_t b[128]; swprintf(b, 128, L"GetProcAddress не нашёл ul%hs", #name); \
                          MessageBoxW(nullptr, b, L"probe", MB_OK | MB_ICONERROR); ExitProcess(1); } \
    } while (0)

static ULRenderer g_renderer = nullptr;
static ULView g_view = nullptr;
static HWND g_hwnd = nullptr;

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

// Не ждём следующего тика таймера (16мс) — сразу после события ввода
// прогоняем Update+Render+Blit, чтобы отклик был мгновенным, а не с
// задержкой до ближайшего WM_TIMER.
void PumpAndBlit()
{
    ul.Update(g_renderer);
    ul.Render(g_renderer);
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
        PumpAndBlit();
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
        PumpAndBlit();
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
        PumpAndBlit();
        return 0;
    }
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wp); // обычно ±120 за "щелчок" колеса
        {
            FILE* f = fopen("ultralight_probe_input.log", "a");
            if (f) { fprintf(f, "WM_MOUSEWHEEL delta=%d focus=%p this_hwnd=%p\n",
                              delta, (void*)GetFocus(), (void*)hwnd); fclose(f); }
        }
        // пиксели на "щелчок" — эмпирически удобное для Leaflet значение
        int pixelDelta = (delta * 100) / WHEEL_DELTA;
        ULScrollEvent ev = ul.CreateScrollEvent(kScrollEventType_ScrollByPixel, 0, pixelDelta);
        ul.ViewFireScrollEvent(g_view, ev);
        ul.DestroyScrollEvent(ev);
        PumpAndBlit();
        return 0;
    }
    case WM_TIMER:
        ul.Update(g_renderer);
        ul.Render(g_renderer);
        BlitViewToWindow();
        return 0;
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

    g_renderer = ul.CreateRenderer(config);
    ul.DestroyConfig(config);

    ULViewConfig vc = ul.CreateViewConfig();
    ul.ViewConfigSetIsAccelerated(vc, false); // CPU-рендер — без DirectX
    g_view = ul.CreateView(g_renderer, 800, 600, vc, nullptr);
    ul.DestroyViewConfig(vc);

    ULString url = ul.CreateString("file:///leaflet_page/page.html");
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

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ul.DestroyView(g_view);
    ul.DestroyRenderer(g_renderer);
    return 0;
}
