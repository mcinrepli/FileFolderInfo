#include <windows.h>
#include "WebView2.h"
#include <cstdio>

typedef HRESULT (STDAPICALLTYPE *CreateEnvFn)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

static ICoreWebView2Controller* g_controller = nullptr;

// --- минимальные реализации completion-handler'ов: QueryInterface нарочно
// упрощён (принимаем IUnknown и "свой" интерфейс, остальное не важно, т.к.
// единственный вызывающий — сам рантайм WebView2, а не произвольный код) ---

struct EnvCreatedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    ULONG refs = 1;
    HWND hwnd;
    EnvCreatedHandler(HWND h) : hwnd(h) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        *ppv = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override;
};

struct ControllerCreatedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    ULONG refs = 1;
    HWND hwnd;
    ControllerCreatedHandler(HWND h) : hwnd(h) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** ppv) override {
        *ppv = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        wchar_t buf[256];
        swprintf(buf, 256, L"ControllerCreatedHandler::Invoke hr=0x%08X controller=%p", (unsigned)result, (void*)controller);
        OutputDebugStringW(buf);
        if (FAILED(result) || !controller) {
            MessageBoxW(hwnd, L"Не удалось создать WebView2 controller", L"probe", MB_OK | MB_ICONERROR);
            return S_OK;
        }
        g_controller = controller;
        g_controller->AddRef();

        RECT bounds; GetClientRect(hwnd, &bounds);
        controller->put_Bounds(bounds);

        ICoreWebView2* webview = nullptr;
        controller->get_CoreWebView2(&webview);
        if (webview) {
            webview->NavigateToString(
                L"<html><body style='font-family:sans-serif;background:#eef'>"
                L"<h1>WebView2 embed OK</h1><p>\u0421\u0432\u044F\u0437\u0430\u043D\u043E \u0431\u0435\u0437 NuGet-\u0437\u0430\u0433\u0440\u0443\u0437\u0447\u0438\u043A\u0430.</p>"
                L"</body></html>");
            webview->Release();
        }
        return S_OK;
    }
};

HRESULT STDMETHODCALLTYPE EnvCreatedHandler::Invoke(HRESULT result, ICoreWebView2Environment* env)
{
    wchar_t buf[256];
    swprintf(buf, 256, L"EnvCreatedHandler::Invoke hr=0x%08X env=%p", (unsigned)result, (void*)env);
    OutputDebugStringW(buf);
    if (FAILED(result) || !env) {
        MessageBoxW(hwnd, L"Не удалось создать WebView2 environment", L"probe", MB_OK | MB_ICONERROR);
        return S_OK;
    }
    env->CreateCoreWebView2Controller(hwnd, new ControllerCreatedHandler(hwnd));
    return S_OK;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE:
        if (g_controller) {
            RECT r; GetClientRect(hwnd, &r);
            g_controller->put_Bounds(r);
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WV2ProbeWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"WV2ProbeWnd", L"WebView2 probe",
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                              nullptr, nullptr, hInst, nullptr);

    HMODULE hLoader = LoadLibraryW(L"WebView2Loader.dll");
    if (!hLoader) {
        MessageBoxW(hwnd, L"WebView2Loader.dll не найден — нужен WebView2 Runtime (обычно уже есть в Windows 10/11 с Edge)", L"probe", MB_OK | MB_ICONERROR);
        return 1;
    }
    auto createEnv = (CreateEnvFn)GetProcAddress(hLoader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!createEnv) {
        MessageBoxW(hwnd, L"CreateCoreWebView2EnvironmentWithOptions не найдена в WebView2Loader.dll", L"probe", MB_OK | MB_ICONERROR);
        return 1;
    }

    HRESULT hr = createEnv(nullptr, nullptr, nullptr, new EnvCreatedHandler(hwnd));
    if (FAILED(hr)) {
        wchar_t buf[128];
        swprintf(buf, 128, L"CreateCoreWebView2EnvironmentWithOptions вернула ошибку 0x%08X", (unsigned)hr);
        MessageBoxW(hwnd, buf, L"probe", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(hwnd, SW_SHOW);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
