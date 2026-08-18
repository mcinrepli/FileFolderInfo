// INITGUID нужен ровно в одном .cpp, чтобы PKEY_* константы получили
// реальные значения GUID здесь же, без отдельной библиотеки propsys.lib.
#define INITGUID
#include "common.h"
#include "strings.h"
#include <propkey.h>
#include <propvarutil.h>
#include <propsys.h>
#include <map>
#include <algorithm>
#include <cstdarg>

// ------------------------------------------------------------------
// MinGW-w64 не включает в propkey.h фото/GPS/изображенческие ключи
// (только Audio/Music/Media/Calendar/Contact и т.п.) — досоздаю их
// сам тем же макросом DEFINE_PROPERTYKEY, значения сверены по двум
// независимым зеркалам оригинального Microsoft propkey.h (wine-mirror
// и открытые SDK-миррор-репозитории на GitHub).
// ------------------------------------------------------------------
DEFINE_PROPERTYKEY(PKEY_Photo_DateTaken,          0x14B81DA1,0x0135,0x4D31,0x96,0xD9,0x6C,0xBF,0xC9,0x67,0x1A,0x99, 36867);
DEFINE_PROPERTYKEY(PKEY_Photo_CameraManufacturer, 0x14B81DA1,0x0135,0x4D31,0x96,0xD9,0x6C,0xBF,0xC9,0x67,0x1A,0x99, 271);
DEFINE_PROPERTYKEY(PKEY_Photo_CameraModel,        0x14B81DA1,0x0135,0x4D31,0x96,0xD9,0x6C,0xBF,0xC9,0x67,0x1A,0x99, 272);
DEFINE_PROPERTYKEY(PKEY_Photo_FNumber,            0x14B81DA1,0x0135,0x4D31,0x96,0xD9,0x6C,0xBF,0xC9,0x67,0x1A,0x99, 33437);
DEFINE_PROPERTYKEY(PKEY_Photo_ExposureTime,       0x14B81DA1,0x0135,0x4D31,0x96,0xD9,0x6C,0xBF,0xC9,0x67,0x1A,0x99, 33434);
DEFINE_PROPERTYKEY(PKEY_Photo_ISOSpeed,           0x14B81DA1,0x0135,0x4D31,0x96,0xD9,0x6C,0xBF,0xC9,0x67,0x1A,0x99, 34855);
DEFINE_PROPERTYKEY(PKEY_Photo_FocalLength,        0x14B81DA1,0x0135,0x4D31,0x96,0xD9,0x6C,0xBF,0xC9,0x67,0x1A,0x99, 37386);
DEFINE_PROPERTYKEY(PKEY_Image_HorizontalSize,     0x6444048F,0x4C8B,0x11D1,0x8B,0x70,0x08,0x00,0x36,0xB1,0x1A,0x03, 3);
DEFINE_PROPERTYKEY(PKEY_Image_VerticalSize,       0x6444048F,0x4C8B,0x11D1,0x8B,0x70,0x08,0x00,0x36,0xB1,0x1A,0x03, 4);
DEFINE_PROPERTYKEY(PKEY_GPS_LatitudeDecimal,      0x0F55CDE2,0x4F49,0x450D,0x92,0xC1,0xDC,0xD1,0x63,0x01,0xB1,0xB7, 100);
DEFINE_PROPERTYKEY(PKEY_GPS_LongitudeDecimal,     0x4679C1B5,0x844D,0x4590,0xBA,0xF5,0xF3,0x22,0x23,0x1F,0x1B,0x81, 100);

namespace {

std::wstring FileNameOnlyLocal(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? path : path.substr(p + 1);
}

// PSFormatForDisplayAlloc принимает сам PROPERTYKEY напрямую (не IPropertyDescription*) —
// форматирует так же, как это делает сам Проводник (единицы, дроби выдержки и т.п.)
std::wstring FormatPropertyForDisplay(const PROPERTYKEY& key, const PROPVARIANT& pv)
{
    std::wstring result;
    PWSTR psz = nullptr;
    if (SUCCEEDED(PSFormatForDisplayAlloc(key, pv, PDFF_DEFAULT, &psz)) && psz) {
        result = psz;
        CoTaskMemFree(psz);
    }
    return result;
}

struct MetaProp { const PROPERTYKEY* key; const wchar_t* label; };

std::wstring FormatSizeAdaptive(unsigned long long bytes)
{
    wchar_t buf[64];
    if (bytes >= 1024ULL * 1024 * 1024)
        swprintf(buf, 64, L"%.2f \u0433\u0431", bytes / (1024.0 * 1024 * 1024));
    else if (bytes >= 1024ULL * 1024)
        swprintf(buf, 64, L"%.2f \u043C\u0431", bytes / (1024.0 * 1024));
    else if (bytes >= 1024)
        swprintf(buf, 64, L"%.2f \u043A\u0431", bytes / 1024.0);
    else
        swprintf(buf, 64, L"%llu \u0431", bytes);
    return buf;
}

struct ExtStat { unsigned long long count = 0; unsigned long long totalSize = 0; };

void AggregateFolderRecursive(const std::wstring& folder, unsigned long long* folderCount,
                               std::map<std::wstring, ExtStat>* byExt)
{
    std::wstring base = folder;
    if (!base.empty() && base.back() != L'\\') base += L'\\';
    std::wstring search = base + L"*";

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring full = base + fd.cFileName;
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        if (isDir) {
            (*folderCount)++;
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                AggregateFolderRecursive(full, folderCount, byExt);
        } else {
            std::wstring name = fd.cFileName;
            size_t dot = name.find_last_of(L'.');
            std::wstring ext = (dot != std::wstring::npos && dot > 0) ? name.substr(dot + 1) : L"";
            for (auto& c : ext) c = (wchar_t)towlower(c);
            if (ext.empty()) ext = S_EXT_NONE;

            ULARGE_INTEGER sz; sz.HighPart = fd.nFileSizeHigh; sz.LowPart = fd.nFileSizeLow;
            auto& st = (*byExt)[ext];
            st.count++;
            st.totalSize += sz.QuadPart;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void ShowFolderAggregateInfo(HWND owner, const SelectionInfo& sel)
{
    unsigned long long folderCount = 0;
    std::map<std::wstring, ExtStat> byExt;

    for (auto& p : sel.paths)
        AggregateFolderRecursive(p, &folderCount, &byExt);

    std::vector<InfoRow> rows;
    if (folderCount > 0)
        rows.push_back({ S_META_SUBFOLDERS, FormatPlainNumber(folderCount) + L" \u0448\u0442." });

    std::vector<std::pair<std::wstring, ExtStat>> sorted(byExt.begin(), byExt.end());
    std::sort(sorted.begin(), sorted.end(),
              [](auto& a, auto& b) { return a.second.totalSize > b.second.totalSize; });

    for (auto& kv : sorted) {
        std::wstring value = FormatPlainNumber(kv.second.count) + L" \u0448\u0442. - " + FormatSizeAdaptive(kv.second.totalSize);
        rows.push_back({ kv.first, value });
    }

    if (rows.empty()) {
        MessageBoxW(owner, S_MSG_TREE_EMPTY, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShowInfoDialog(owner, S_DLG_TITLE_METADATA, rows);
}

std::wstring GetExtLower(const std::wstring& path)
{
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos) return L"";
    std::wstring ext = path.substr(dot + 1);
    for (auto& c : ext) c = (wchar_t)towlower(c);
    return ext;
}

bool IsTextLikeExtension(const std::wstring& path)
{
    static const wchar_t* exts[] = {
        L"txt", L"bat", L"cmd", L"ps1", L"psm1", L"py", L"js", L"mjs", L"ts",
        L"json", L"xml", L"html", L"htm", L"css", L"c", L"h", L"cpp", L"hpp",
        L"cc", L"cxx", L"cs", L"java", L"sh", L"bash", L"ini", L"cfg", L"conf",
        L"yml", L"yaml", L"md", L"markdown", L"log", L"csv", L"tsv", L"sql",
        L"reg", L"vbs", L"ahk", L"lua", L"rb", L"php", L"go", L"rs", L"toml",
        L"gitignore", L"editorconfig", L"gitattributes", L"nfo", L"srt", L"sub"
    };
    std::wstring ext = GetExtLower(path);
    for (auto e : exts) if (ext == e) return true;
    return false;
}

// Определяем кодировку по началу файла (не читаем весь файл, вдруг он на несколько ГБ)
std::wstring DetectTextEncoding(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";

    const DWORD kSample = 8192;
    std::vector<unsigned char> buf(kSample);
    DWORD read = 0;
    BOOL ok = ReadFile(h, buf.data(), kSample, &read, nullptr);
    CloseHandle(h);
    if (!ok) return L"";
    if (read == 0) return S_ENC_EMPTY;

    if (read >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF)
        return S_ENC_UTF8_BOM;
    if (read >= 4 && buf[0] == 0xFF && buf[1] == 0xFE && buf[2] == 0x00 && buf[3] == 0x00)
        return S_ENC_UTF32_LE;
    if (read >= 4 && buf[0] == 0x00 && buf[1] == 0x00 && buf[2] == 0xFE && buf[3] == 0xFF)
        return S_ENC_UTF32_BE;
    if (read >= 2 && buf[0] == 0xFF && buf[1] == 0xFE)
        return S_ENC_UTF16_LE;
    if (read >= 2 && buf[0] == 0xFE && buf[1] == 0xFF)
        return S_ENC_UTF16_BE;

    // Без BOM: проверяем, валидна ли последовательность как UTF-8
    int wlen = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (LPCCH)buf.data(), (int)read, nullptr, 0);
    if (wlen > 0) {
        bool allAscii = true;
        for (DWORD i = 0; i < read; i++) {
            if (buf[i] >= 0x80) { allAscii = false; break; }
        }
        return allAscii ? S_ENC_ASCII : S_ENC_UTF8_NOBOM;
    }

    return S_ENC_UNKNOWN;
}

} // namespace

// --- ВРЕМЕННАЯ диагностика: ищем, что именно тормозит/виснет в цепочке
// SHCreateItemFromParsingName/GetProperty при разборе метаданных ЦЕЛИ
// ярлыка. Пишет метки времени в %TEMP%\FileFolderInfo_diag.log через
// чистый WinAPI (CreateFileW/WriteFile), без зависимости от режимов
// открытия CRT-файлов. Убрать после того, как найдём причину.
void DiagLog(const wchar_t* fmt, ...)
{
    wchar_t msg[512];
    va_list args;
    va_start(args, fmt);
    vswprintf(msg, 512, fmt, args);
    va_end(args);

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t line[600];
    swprintf(line, 600, L"[%02u:%02u:%02u.%03u] %ls\r\n",
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, msg);

    wchar_t path[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, path)) return;
    wcsncat(path, L"FileFolderInfo_diag.log", MAX_PATH - wcslen(path) - 1);

    int n = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return;
    std::vector<char> utf8((size_t)n);
    WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), n, nullptr, nullptr);

    HANDLE h = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(h, utf8.data(), (DWORD)(n - 1), &written, nullptr); // n-1: без завершающего нуля
    CloseHandle(h);
}

// RAII-обёртка: логирует вход и выход из функции (с временем) через ЛЮБОЙ
// return, не трогая при этом сами return-ы — их в ResolveShortcut несколько.
struct ScopeDiag {
    const wchar_t* label;
    ULONGLONG t0;
    ScopeDiag(const wchar_t* lbl, const std::wstring& info) : label(lbl), t0(GetTickCount64()) {
        DiagLog(L"%ls ENTER %ls", label, info.c_str());
    }
    ~ScopeDiag() {
        DiagLog(L"%ls EXIT (%llu ms)", label, GetTickCount64() - t0);
    }
};

std::vector<InfoRow> BuildRegularFileMetadataRows(const std::wstring& targetPath)
{
    std::vector<InfoRow> rows;
    ULONGLONG diagT0 = GetTickCount64();
    DiagLog(L"START target=%ls", targetPath.c_str());

    if (IsTextLikeExtension(targetPath)) {
        std::wstring enc = DetectTextEncoding(targetPath);
        if (!enc.empty())
            rows.push_back({ S_META_ENCODING, enc, false, 2 });
    }

    {
        std::wstring ext = GetExtLower(targetPath);
        if (ext == L"exe" || ext == L"dll" || ext == L"sys" || ext == L"ocx") {
            std::wstring arch, dotNetVer;
            if (GetPeArchAndDotNetInfo(targetPath, &arch, &dotNetVer)) {
                if (!arch.empty()) rows.push_back({ S_META_ARCH, arch });
                if (!dotNetVer.empty()) rows.push_back({ S_META_DOTNET_VERSION, dotNetVer });
            }
        }
    }

    ULONGLONG diagTCo = GetTickCount64();
    HRESULT hrCo = CoInitialize(nullptr);
    DiagLog(L"  CoInitialize -> 0x%08X (%llu ms)", (unsigned)hrCo, GetTickCount64() - diagTCo);
    bool needUninit = (hrCo == S_OK);

    IShellItem2* psi = nullptr;
    ULONGLONG diagTSh = GetTickCount64();
    HRESULT hr = SHCreateItemFromParsingName(targetPath.c_str(), nullptr, IID_PPV_ARGS(&psi));
    DiagLog(L"  SHCreateItemFromParsingName -> 0x%08X (%llu ms)", (unsigned)hr, GetTickCount64() - diagTSh);

    if (SUCCEEDED(hr) && psi) {
        static const MetaProp props[] = {
            { &PKEY_Photo_DateTaken,          S_META_DATE_TAKEN },
            { &PKEY_GPS_LatitudeDecimal,      S_META_GPS_LAT },
            { &PKEY_GPS_LongitudeDecimal,     S_META_GPS_LON },
            { &PKEY_Photo_CameraManufacturer, S_META_CAMERA_MAKER },
            { &PKEY_Photo_CameraModel,        S_META_CAMERA_MODEL },
            { &PKEY_Photo_FNumber,            S_META_FNUMBER },
            { &PKEY_Photo_ExposureTime,       S_META_EXPOSURE },
            { &PKEY_Photo_ISOSpeed,           S_META_ISO },
            { &PKEY_Photo_FocalLength,        S_META_FOCAL },
            { &PKEY_Media_Duration,           S_META_DURATION },
            { &PKEY_Music_Artist,             S_META_ARTIST },
            { &PKEY_Music_AlbumTitle,         S_META_ALBUM },
            { &PKEY_Media_Year,               S_META_YEAR },
        };

        ULONGLONG diagTProps = GetTickCount64();
        for (auto& p : props) {
            ULONGLONG diagTOne = GetTickCount64();
            PROPVARIANT pv;
            PropVariantInit(&pv);
            HRESULT hrProp = psi->GetProperty(*p.key, &pv);
            ULONGLONG diagOneMs = GetTickCount64() - diagTOne;
            if (diagOneMs >= 50) // логируем только заметно медленные, чтобы не засорять лог
                DiagLog(L"    GetProperty(one key) -> 0x%08X (%llu ms)", (unsigned)hrProp, diagOneMs);
            if (SUCCEEDED(hrProp) && pv.vt != VT_EMPTY) {
                std::wstring text = FormatPropertyForDisplay(*p.key, pv);
                if (!text.empty())
                    rows.push_back({ p.label, text });
            }
            PropVariantClear(&pv);
        }
        DiagLog(L"  props-loop (%d keys) done (%llu ms)",
                (int)(sizeof(props) / sizeof(props[0])), GetTickCount64() - diagTProps);

        // Разрешение одной строкой "ШxВ" — сначала пробуем фото-свойства, если нет — видео
        {
            PROPVARIANT pvW, pvH;
            UINT w = 0, h = 0;
            bool have = false;

            PropVariantInit(&pvW); PropVariantInit(&pvH);
            if (SUCCEEDED(psi->GetProperty(PKEY_Image_HorizontalSize, &pvW)) && pvW.vt == VT_UI4 &&
                SUCCEEDED(psi->GetProperty(PKEY_Image_VerticalSize, &pvH)) && pvH.vt == VT_UI4) {
                w = pvW.ulVal; h = pvH.ulVal; have = true;
            }
            PropVariantClear(&pvW); PropVariantClear(&pvH);

            if (!have) {
                PropVariantInit(&pvW); PropVariantInit(&pvH);
                if (SUCCEEDED(psi->GetProperty(PKEY_Video_FrameWidth, &pvW)) && pvW.vt == VT_UI4 &&
                    SUCCEEDED(psi->GetProperty(PKEY_Video_FrameHeight, &pvH)) && pvH.vt == VT_UI4) {
                    w = pvW.ulVal; h = pvH.ulVal; have = true;
                }
                PropVariantClear(&pvW); PropVariantClear(&pvH);
            }

            if (have && w > 0 && h > 0) {
                wchar_t buf[32];
                swprintf(buf, 32, L"%ux%u", w, h);
                rows.push_back({ S_META_RESOLUTION, buf });
            }
        }

        // Тип битрейта — булево значение, форматируем сами понятным текстом
        {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(psi->GetProperty(PKEY_Audio_IsVariableBitRate, &pv)) && pv.vt == VT_BOOL) {
                rows.push_back({ S_META_BITRATE_TYPE, (pv.boolVal != VARIANT_FALSE) ? S_META_VBR : S_META_CBR });
            }
            PropVariantClear(&pv);
        }
        {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(psi->GetProperty(PKEY_Audio_EncodingBitrate, &pv)) && pv.vt != VT_EMPTY) {
                std::wstring text = FormatPropertyForDisplay(PKEY_Audio_EncodingBitrate, pv);
                if (!text.empty()) rows.push_back({ S_META_AUDIO_BITRATE, text });
            }
            PropVariantClear(&pv);
        }
        {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(psi->GetProperty(PKEY_Video_EncodingBitrate, &pv)) && pv.vt != VT_EMPTY) {
                std::wstring text = FormatPropertyForDisplay(PKEY_Video_EncodingBitrate, pv);
                if (!text.empty()) rows.push_back({ S_META_VIDEO_BITRATE, text });
            }
            PropVariantClear(&pv);
        }
        {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(psi->GetProperty(PKEY_Video_TotalBitrate, &pv)) && pv.vt != VT_EMPTY) {
                std::wstring text = FormatPropertyForDisplay(PKEY_Video_TotalBitrate, pv);
                if (!text.empty()) rows.push_back({ S_META_TOTAL_BITRATE, text });
            }
            PropVariantClear(&pv);
        }

        // Ссылки на карты — нужны СЫРЫЕ числа (не отформатированная строка), чтобы собрать URL.
        // ВАЖНО: GetProperty может вернуть S_OK даже для файла, где свойства нет вообще
        // (просто с пустым PROPVARIANT), а PropVariantToDouble на пустом значении молча
        // даёт 0.0 вместо ошибки — поэтому проверяем именно pv.vt == VT_R8, а не только HRESULT,
        // и дополнительно отбрасываем (0,0) как «нулевой остров» — GPS так не бывает у реальных фото.
        {
            PROPVARIANT pvLat, pvLon;
            PropVariantInit(&pvLat);
            PropVariantInit(&pvLon);
            double lat = 0, lon = 0;

            bool haveLat = SUCCEEDED(psi->GetProperty(PKEY_GPS_LatitudeDecimal, &pvLat)) &&
                           pvLat.vt == VT_R8 &&
                           SUCCEEDED(PropVariantToDouble(pvLat, &lat));
            bool haveLon = SUCCEEDED(psi->GetProperty(PKEY_GPS_LongitudeDecimal, &pvLon)) &&
                           pvLon.vt == VT_R8 &&
                           SUCCEEDED(PropVariantToDouble(pvLon, &lon));
            PropVariantClear(&pvLat);
            PropVariantClear(&pvLon);

            bool isNullIsland = (lat > -0.0001 && lat < 0.0001 && lon > -0.0001 && lon < 0.0001);

            if (haveLat && haveLon && !isNullIsland) {
                wchar_t latBuf[32], lonBuf[32];
                swprintf(latBuf, 32, L"%.6f", lat);
                swprintf(lonBuf, 32, L"%.6f", lon);

                wchar_t urlOsm[256], urlGoogle[256], urlYandex[256], urlPastvu[320];
                swprintf(urlOsm, 256, L"https://www.openstreetmap.org/?mlat=%ls&mlon=%ls#map=17/%ls/%ls",
                         latBuf, lonBuf, latBuf, lonBuf);
                swprintf(urlGoogle, 256, L"https://www.google.com/maps/search/?api=1&query=%ls,%ls",
                         latBuf, lonBuf);
                swprintf(urlYandex, 256, L"https://yandex.ru/maps/?ll=%ls,%ls&z=17&pt=%ls,%ls",
                         lonBuf, latBuf, lonBuf, latBuf); // у Яндекса порядок "долгота,широта"
                swprintf(urlPastvu, 320,
                         L"https://pastvu.com/?g=%ls,%ls&z=17&s=osm&t=openstreetmap&type=1&comments=1",
                         latBuf, lonBuf);

                rows.push_back({ S_META_MAP_OSM, urlOsm, 1 });
                rows.push_back({ S_META_MAP_GOOGLE, urlGoogle, 1 });
                rows.push_back({ S_META_MAP_YANDEX, urlYandex, 1 });
                rows.push_back({ S_META_MAP_PASTVU, urlPastvu, 1 });
            }
        }

        psi->Release();
    }

    if (needUninit) CoUninitialize();
    DiagLog(L"END target=%ls total=%llu ms", targetPath.c_str(), GetTickCount64() - diagT0);
    return rows;
}

// Ручной разбор .lnk по спецификации MS-SHLLINK — без единого вызова COM.
// Причина смены подхода: IShellLinkW/IPersistFile давали нестабильный результат
// (то же самое из раза в раз то срабатывало, то нет, без видимой закономерности,
// в том числе на совершенно обычных путях без ничего экзотического — то есть
// дело было не в целях ярлыков, а в самом механизме COM-вызова внутри
// расширения). Формат .lnk стабильный и документированный — разбор проверен
// на трёх реальных присланных файлах (обычный exe, обычная папка, и ярлык на
// Панель управления — у него, в отличие от первых двух, есть только
// LinkTargetIDList и нет LinkInfo вообще, это системное расположение, а не
// путь к файлу).
ShortcutInfo ResolveShortcut(const std::wstring& lnkPath)
{
    ScopeDiag _diag(L"ResolveShortcut", lnkPath);
    ShortcutInfo info;
    if (GetExtLower(lnkPath) != L"lnk") return info;
    info.isShortcut = true;

    HANDLE hFile = CreateFileW(lnkPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return info;

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(hFile, &sz) || sz.QuadPart < 76 || sz.QuadPart > 4 * 1024 * 1024) {
        CloseHandle(hFile);
        return info;
    }
    DWORD size = (DWORD)sz.QuadPart;

    std::vector<BYTE> data(size);
    DWORD read = 0;
    BOOL ok = ReadFile(hFile, data.data(), size, &read, nullptr);
    CloseHandle(hFile);
    if (!ok || read != size) return info;

    auto rd16 = [&](DWORD off) -> WORD {
        return (WORD)(data[off] | (data[off + 1] << 8));
    };
    auto rd32 = [&](DWORD off) -> DWORD {
        return (DWORD)data[off] | ((DWORD)data[off + 1] << 8) |
               ((DWORD)data[off + 2] << 16) | ((DWORD)data[off + 3] << 24);
    };

    if (rd32(0) != 0x4C) return info; // HeaderSize должен быть ровно 76 (0x4C)

    DWORD linkFlags = rd32(20);
    bool hasIDList = (linkFlags & 0x1) != 0;
    bool hasLinkInfo = (linkFlags & 0x2) != 0;

    info.resolved = true; // файл прочитан и заголовок опознан как ShellLink

    DWORD pos = 76;
    if (hasIDList) {
        if (pos + 2 > size) return info;
        WORD idListSize = rd16(pos);
        pos += 2 + idListSize;
        if (pos > size) return info;
    }

    if (hasLinkInfo) {
        if (pos + 20 > size) return info;
        DWORD liStart = pos;
        DWORD liSize = rd32(pos);
        DWORD liFlags = rd32(pos + 8);
        DWORD localBaseOff = rd32(pos + 16);

        if ((liFlags & 0x1) && localBaseOff && liStart + localBaseOff < size) {
            DWORD strStart = liStart + localBaseOff;
            DWORD strEnd = strStart;
            while (strEnd < size && data[strEnd] != 0) strEnd++;
            // LocalBasePath — всегда ANSI-строка (текущая кодовая страница), независимо от IsUnicode
            std::string ansi(reinterpret_cast<char*>(&data[strStart]), strEnd - strStart);
            int wlen = MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), -1, nullptr, 0);
            if (wlen > 0) {
                std::wstring wpath(wlen, L'\0');
                MultiByteToWideChar(CP_ACP, 0, ansi.c_str(), -1, &wpath[0], wlen);
                if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();
                info.targetPath = wpath;
                info.hasLocalPath = !wpath.empty();
            }
        }

        if (liSize == 0 || liStart + liSize < liStart) return info; // защита от переполнения
        pos = liStart + liSize;
    } else if (hasIDList) {
        info.isVirtualLocation = true; // только IDList — системное расположение (Панель управления и т.п.)
    }

    if (info.hasLocalPath) {
        DWORD attrs = GetFileAttributesW(info.targetPath.c_str());
        info.targetExists = (attrs != INVALID_FILE_ATTRIBUTES);
        info.targetIsFolder = info.targetExists && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    return info;
}

std::vector<InfoRow> BuildFileMetadataRows(const std::wstring& targetPath)
{
    ScopeDiag _diag(L"BuildFileMetadataRows", targetPath);
    ShortcutInfo sc = ResolveShortcut(targetPath);
    if (!sc.isShortcut)
        return BuildRegularFileMetadataRows(targetPath);

    std::vector<InfoRow> rows;

    if (!sc.resolved) {
        rows.push_back({ S_META_SHORTCUT_STATUS, S_SHORTCUT_UNREADABLE });
        return rows;
    }

    if (sc.isVirtualLocation) {
        rows.push_back({ S_META_SHORTCUT_STATUS, S_SHORTCUT_VIRTUAL });
        return rows;
    }

    if (!sc.hasLocalPath) {
        rows.push_back({ S_META_SHORTCUT_STATUS, S_SHORTCUT_UNREADABLE });
        return rows;
    }

    rows.push_back({ S_META_SHORTCUT_TARGET, sc.targetPath, 0, 1 });

    if (!sc.targetExists) {
        rows.push_back({ S_META_SHORTCUT_STATUS, S_SHORTCUT_DEAD });
        return rows;
    }

    rows.push_back({ S_META_SHORTCUT_STATUS, S_SHORTCUT_ALIVE });
    rows.push_back({ S_META_SHORTCUT_REVEAL_LABEL, sc.targetPath, 2 });

    if (!sc.targetIsFolder) {
        std::vector<InfoRow> targetRows = BuildRegularFileMetadataRows(sc.targetPath);
        rows.insert(rows.end(), targetRows.begin(), targetRows.end());
    }

    return rows;
}

void Cmd_ShowMetadata(HWND owner, const SelectionInfo& sel)
{
    DiagLog(L"Cmd_ShowMetadata ENTER, %u path(s), allFolders=%d allFiles=%d",
            (unsigned)sel.paths.size(), (int)sel.allFolders, (int)sel.allFiles);
    if (sel.paths.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (sel.allFolders) {
        ShowFolderAggregateInfo(owner, sel);
        return;
    }

    if (!sel.allFiles) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    size_t count = sel.paths.size();
    size_t idx = 0;
    bool showedAny = false;

    while (true) {
        std::vector<InfoRow> rows = BuildFileMetadataRows(sel.paths[idx]);

        if (rows.empty()) {
            // Пустая страница — молча пропускаем (переходим дальше/назад), а не бросаем всё листание.
            // Но если ВСЕ страницы пустые — покажем сообщение один раз в конце.
            if (count == 1) {
                MessageBoxW(owner, S_MSG_NO_METADATA, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
                return;
            }
        } else {
            showedAny = true;
            std::wstring title = S_DLG_TITLE_METADATA;
            if (count > 1) {
                title += L": " + FileNameOnlyLocal(sel.paths[idx]) + L" (" +
                         std::to_wstring(idx + 1) + L" \u0438\u0437 " + std::to_wstring(count) + L")";
            }

            int result = ShowInfoDialog(owner, title, rows,
                                         count > 1, idx > 0, idx + 1 < count);
            if (result == 1 && idx + 1 < count) { idx++; continue; }
            if (result == -1 && idx > 0) { idx--; continue; }
            return; // закрыли — выходим
        }

        // страница была пустой: просто идём дальше, если есть куда
        if (idx + 1 < count) { idx++; continue; }
        break;
    }

    if (!showedAny)
        MessageBoxW(owner, S_MSG_NO_METADATA, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
}
