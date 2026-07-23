#include "common.h"
#include "shellext.h"

// {F4DE0D56-C976-41E6-801C-D83B57C6846B}
const CLSID CLSID_FileFolderInfoExt =
{ 0xf4de0d56, 0xc976, 0x41e6, { 0x80, 0x1c, 0xd8, 0x3b, 0x57, 0xc6, 0x84, 0x6b } };

LONG g_cDllRef = 0;
HINSTANCE g_hInst = nullptr;

// ---------------------------------------------------------------- ClassFactory

class CClassFactory : public IClassFactory
{
public:
    CClassFactory() : m_cRef(1) { InterlockedIncrement(&g_cDllRef); }

    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv)
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    IFACEMETHODIMP_(ULONG) AddRef() { return InterlockedIncrement(&m_cRef); }

    IFACEMETHODIMP_(ULONG) Release()
    {
        LONG r = InterlockedDecrement(&m_cRef);
        if (r == 0) delete this;
        return r;
    }

    IFACEMETHODIMP CreateInstance(IUnknown* pUnkOuter, REFIID riid, void** ppv)
    {
        *ppv = nullptr;
        if (pUnkOuter) return CLASS_E_NOAGGREGATION;

        CShellExt* p = new (std::nothrow) CShellExt();
        if (!p) return E_OUTOFMEMORY;

        HRESULT hr = p->QueryInterface(riid, ppv);
        p->Release();
        return hr;
    }

    IFACEMETHODIMP LockServer(BOOL fLock)
    {
        if (fLock) InterlockedIncrement(&g_cDllRef);
        else InterlockedDecrement(&g_cDllRef);
        return S_OK;
    }

private:
    virtual ~CClassFactory() { InterlockedDecrement(&g_cDllRef); }
    LONG m_cRef;
};

// ---------------------------------------------------------------- exports

extern "C" BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID)
{
    if (dwReason == DLL_PROCESS_ATTACH) {
        g_hInst = hInstance;
        DisableThreadLibraryCalls(hInstance);
    }
    return TRUE;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv)
{
    *ppv = nullptr;
    if (rclsid != CLSID_FileFolderInfoExt)
        return CLASS_E_CLASSNOTAVAILABLE;

    CClassFactory* pFactory = new (std::nothrow) CClassFactory();
    if (!pFactory) return E_OUTOFMEMORY;

    HRESULT hr = pFactory->QueryInterface(riid, ppv);
    pFactory->Release();
    return hr;
}

extern "C" __declspec(dllexport) HRESULT WINAPI DllCanUnloadNow(void)
{
    return (g_cDllRef == 0) ? S_OK : S_FALSE;
}
