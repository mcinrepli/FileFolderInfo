#include "common.h"
#include "strings.h"
#include <wincodec.h>
#include <wincodecsdk.h>
#include <propvarutil.h>

namespace {

std::wstring GetExtLowerLocal(const std::wstring& path)
{
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

bool IsRotatableImageExtension(const std::wstring& path)
{
    std::wstring ext = GetExtLowerLocal(path);
    return ext == L"jpg" || ext == L"jpeg" || ext == L"png" || ext == L"bmp" ||
           ext == L"tif" || ext == L"tiff" || ext == L"gif";
}

// Возвращает S_OK при успехе. При неудаче временный файл подчищается сам,
// оригинал не трогается (либо остаётся нетронутым, либо восстановлен).
HRESULT RotateImageFile(const std::wstring& path, bool rotateRight)
{
    FILETIME ftCreate, ftAccess, ftWrite;
    if (!GetFileTimes(path, &ftCreate, &ftAccess, &ftWrite))
        return E_FAIL;

    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return hr;

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                             WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) { factory->Release(); return hr; }

    UINT frameCount = 0;
    decoder->GetFrameCount(&frameCount);
    if (frameCount != 1) {
        // многокадровые (анимированные GIF/TIFF) не трогаем - велик риск испортить анимацию
        decoder->Release();
        factory->Release();
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) { decoder->Release(); factory->Release(); return hr; }

    GUID containerFormat = {};
    decoder->GetContainerFormat(&containerFormat);

    IWICBitmapFlipRotator* rotator = nullptr;
    hr = factory->CreateBitmapFlipRotator(&rotator);
    if (SUCCEEDED(hr))
        hr = rotator->Initialize(frame, rotateRight ? WICBitmapTransformRotate90 : WICBitmapTransformRotate270);

    if (FAILED(hr)) {
        if (rotator) rotator->Release();
        frame->Release(); decoder->Release(); factory->Release();
        return hr;
    }

    std::wstring tmpPath = path + L".rotating.tmp";

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(tmpPath.c_str(), GENERIC_WRITE);

    IWICBitmapEncoder* encoder = nullptr;
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(containerFormat, nullptr, &encoder);
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);

    IWICBitmapFrameEncode* frameEncode = nullptr;
    IPropertyBag2* propBag = nullptr;
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frameEncode, &propBag);

    // Для JPEG выставляем высокое качество явно — WIC перекодирует пиксели заново
    // (это не блочный lossless-поворот вроде jpegtran), так что при поворотах JPEG
    // будет небольшое дополнительное поколение сжатия; держим качество высоким,
    // чтобы эта потеря была практически незаметна.
    if (SUCCEEDED(hr) && propBag && containerFormat == GUID_ContainerFormatJpeg) {
        PROPBAG2 opt = {};
        opt.pstrName = (LPOLESTR)L"ImageQuality";
        VARIANT var;
        VariantInit(&var);
        var.vt = VT_R4;
        var.fltVal = 0.92f;
        propBag->Write(1, &opt, &var);
    }

    if (SUCCEEDED(hr)) hr = frameEncode->Initialize(propBag);

    UINT w = 0, h = 0;
    if (SUCCEEDED(hr)) hr = rotator->GetSize(&w, &h);
    if (SUCCEEDED(hr)) hr = frameEncode->SetSize(w, h);

    WICPixelFormatGUID pfRequested = {}, pf = {};
    if (SUCCEEDED(hr)) hr = rotator->GetPixelFormat(&pf);
    pfRequested = pf;
    if (SUCCEEDED(hr)) hr = frameEncode->SetPixelFormat(&pf); // pf МОЖЕТ измениться на то, что реально поддержит кодер

    IWICFormatConverter* converter = nullptr;
    IWICBitmapSource* writeSource = rotator;
    if (SUCCEEDED(hr) && !IsEqualGUID(pf, pfRequested)) {
        // Кодер не поддерживает исходный формат напрямую — нужна конвертация,
        // иначе запись пойдёт с неверной интерпретацией байт (риск как раз
        // такого раздутия/порчи файла, какое было обнаружено на реальном фото).
        hr = factory->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr))
            hr = converter->Initialize(rotator, pf, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(hr))
            writeSource = converter;
    }

    if (SUCCEEDED(hr)) hr = frameEncode->WriteSource(writeSource, nullptr);
    if (converter) converter->Release();
    if (SUCCEEDED(hr)) hr = frameEncode->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();

    if (propBag) propBag->Release();
    if (frameEncode) frameEncode->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    rotator->Release();
    frame->Release();
    decoder->Release();
    factory->Release();

    if (FAILED(hr)) {
        DeleteFileW(tmpPath.c_str());
        return hr;
    }

    // Атомарно подменяем оригинал результатом.
    if (!ReplaceFileW(path.c_str(), tmpPath.c_str(), nullptr, 0, nullptr, nullptr)) {
        DeleteFileW(path.c_str());
        if (!MoveFileW(tmpPath.c_str(), path.c_str())) {
            DeleteFileW(tmpPath.c_str());
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    // Восстанавливаем оригинальные дату изменения/создания/доступа.
    HANDLE h2 = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h2 != INVALID_HANDLE_VALUE) {
        SetFileTime(h2, &ftCreate, &ftAccess, &ftWrite);
        CloseHandle(h2);
    }

    return S_OK;
}

void DoRotateOperation(HWND owner, const SelectionInfo& sel, bool rotateRight)
{
    if (sel.paths.empty() || !sel.allFiles) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    HRESULT hrCo = CoInitialize(nullptr);
    bool needUninit = (hrCo == S_OK);

    int done = 0, skipped = 0;
    for (auto& p : sel.paths) {
        if (!IsRotatableImageExtension(p)) { skipped++; continue; }
        HRESULT hr = RotateImageFile(p, rotateRight);
        if (SUCCEEDED(hr)) done++; else skipped++;
    }

    if (needUninit) CoUninitialize();

    wchar_t msg[256];
    swprintf(msg, 256, S_MSG_ROTATE_DONE_FMT, done, skipped);
    MessageBoxW(owner, msg, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
}

} // namespace

void Cmd_RotateLeft(HWND owner, const SelectionInfo& sel) { DoRotateOperation(owner, sel, false); }
void Cmd_RotateRight(HWND owner, const SelectionInfo& sel) { DoRotateOperation(owner, sel, true); }
