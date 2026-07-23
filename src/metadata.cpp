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

std::vector<InfoRow> BuildRegularFileMetadataRows(const std::wstring& targetPath)
{
    std::vector<InfoRow> rows;

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

    HRESULT hrCo = CoInitialize(nullptr);
    bool needUninit = (hrCo == S_OK);

    IShellItem2* psi = nullptr;
    HRESULT hr = SHCreateItemFromParsingName(targetPath.c_str(), nullptr, IID_PPV_ARGS(&psi));

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

        for (auto& p : props) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(psi->GetProperty(*p.key, &pv)) && pv.vt != VT_EMPTY) {
                std::wstring text = FormatPropertyForDisplay(*p.key, pv);
                if (!text.empty())
                    rows.push_back({ p.label, text });
            }
            PropVariantClear(&pv);
        }

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
    return rows;
}

struct ShortcutInfo {
    bool isShortcut = false;
    bool resolved = false;
    bool targetExists = false;
    bool targetIsFolder = false;
    std::wstring targetPath;
};

ShortcutInfo ResolveShortcut(const std::wstring& lnkPath)
{
    ShortcutInfo info;
    if (GetExtLower(lnkPath) != L"lnk") return info;
    info.isShortcut = true;

    HRESULT hrCo = CoInitialize(nullptr);
    bool needUninit = (hrCo == S_OK);

    IShellLinkW* psl = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&psl);
    if (SUCCEEDED(hr) && psl) {
        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf)) && ppf) {
            if (SUCCEEDED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
                wchar_t buf[MAX_PATH * 2] = {};
                WIN32_FIND_DATAW wfd = {};
                if (SUCCEEDED(psl->GetPath(buf, MAX_PATH * 2, &wfd, SLGP_RAWPATH))) {
                    info.resolved = true;
                    info.targetPath = buf;
                }
            }
            ppf->Release();
        }
        psl->Release();
    }

    if (needUninit) CoUninitialize();

    if (info.resolved && !info.targetPath.empty()) {
        DWORD attrs = GetFileAttributesW(info.targetPath.c_str());
        info.targetExists = (attrs != INVALID_FILE_ATTRIBUTES);
        info.targetIsFolder = info.targetExists && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return info;
}

std::vector<InfoRow> BuildFileMetadataRows(const std::wstring& targetPath)
{
    ShortcutInfo sc = ResolveShortcut(targetPath);
    if (!sc.isShortcut)
        return BuildRegularFileMetadataRows(targetPath);

    std::vector<InfoRow> rows;

    if (!sc.resolved) {
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
