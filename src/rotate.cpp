// Поворот JPEG через прямой патч тега EXIF Orientation — БЕЗ перекодирования
// пикселей. Обоснование смены подхода (после двух неудачных попыток через
// WIC decode/encode): тот путь оказался и медленным (зависания Проводника
// на 30-60 сек, а на повторном вызове — насмерть), и разрушал остальные
// EXIF-метаданные (GPS и т.д.) при перекодировании. Патч тега — то, как
// сами телефоны в большинстве случаев и обозначают поворот: не трогая
// пиксели, вообще не трогая никакие другие метаданные, мгновенно.
#include "common.h"
#include "strings.h"

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
    return ext == L"jpg" || ext == L"jpeg";
}

// Таблица "довернуть текущую ориентацию ещё на 90° по/против часовой" —
// выведена из канонической таблицы коррекции (sylvana.net/exif_orientation),
// разложением каждого значения на (поворот, есть_отражение) и добавлением
// +/-90° к повороту при неизменном отражении. index = текущая ориентация (1-8).
const int kRotateCW[9]  = { 0, 6, 5, 8, 7, 4, 3, 2, 1 };
const int kRotateCCW[9] = { 0, 8, 7, 6, 5, 2, 1, 4, 3 };

// Находит абсолютное смещение в файле 2-байтового значения тега Orientation
// (0x0112) внутри EXIF/APP1, если он там есть. Возвращает 0, если не нашли.
// bigEndian получает порядок байт TIFF-блока (нужен, чтобы прочитать/записать
// значение правильно).
DWORD FindOrientationValueOffset(HANDLE hFile, bool* bigEndianOut)
{
    // Проверяем SOI
    BYTE soi[2];
    DWORD read = 0;
    SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, soi, 2, &read, nullptr) || read != 2) return 0;
    if (soi[0] != 0xFF || soi[1] != 0xD8) return 0;

    DWORD pos = 2;
    for (int guard = 0; guard < 200; guard++) {
        BYTE marker[2];
        SetFilePointer(hFile, pos, nullptr, FILE_BEGIN);
        if (!ReadFile(hFile, marker, 2, &read, nullptr) || read != 2) return 0;
        if (marker[0] != 0xFF) return 0;
        if (marker[1] == 0xD8 || marker[1] == 0xD9) return 0;   // SOI/EOI без длины
        if (marker[1] == 0xDA) return 0;                          // Start of Scan — EXIF дальше не бывает
        if (marker[1] >= 0xD0 && marker[1] <= 0xD7) { pos += 2; continue; } // restart-маркеры без длины

        BYTE lenBuf[2];
        if (!ReadFile(hFile, lenBuf, 2, &read, nullptr) || read != 2) return 0;
        WORD segLen = (WORD)((lenBuf[0] << 8) | lenBuf[1]); // длина сегмента - BIG ENDIAN, включает эти 2 байта
        if (segLen < 2) return 0;

        DWORD payloadStart = pos + 4;

        if (marker[1] == 0xE1) { // APP1 — кандидат на Exif
            BYTE sig[6];
            if (segLen >= 8 && ReadFile(hFile, sig, 6, &read, nullptr) && read == 6 &&
                memcmp(sig, "Exif\0\0", 6) == 0) {
                DWORD tiffStart = payloadStart + 6;

                BYTE bo[4]; // byte order (2) + magic (2)
                SetFilePointer(hFile, tiffStart, nullptr, FILE_BEGIN);
                if (!ReadFile(hFile, bo, 4, &read, nullptr) || read != 4) return 0;
                bool bigEndian;
                if (bo[0] == 'I' && bo[1] == 'I') bigEndian = false;
                else if (bo[0] == 'M' && bo[1] == 'M') bigEndian = true;
                else return 0;

                auto rd16 = [&](const BYTE* p) -> WORD {
                    return bigEndian ? (WORD)((p[0] << 8) | p[1]) : (WORD)((p[1] << 8) | p[0]);
                };
                auto rd32 = [&](const BYTE* p) -> DWORD {
                    return bigEndian
                        ? ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3]
                        : ((DWORD)p[3] << 24) | ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0];
                };

                WORD magic = rd16(bo + 2);
                if (magic != 0x002A) return 0;

                BYTE ifdOffBuf[4];
                SetFilePointer(hFile, tiffStart + 4, nullptr, FILE_BEGIN);
                if (!ReadFile(hFile, ifdOffBuf, 4, &read, nullptr) || read != 4) return 0;
                DWORD ifd0Off = rd32(ifdOffBuf);
                if (ifd0Off == 0 || ifd0Off > 0x100000) return 0; // разумный предел

                DWORD ifd0Pos = tiffStart + ifd0Off;
                BYTE cntBuf[2];
                SetFilePointer(hFile, ifd0Pos, nullptr, FILE_BEGIN);
                if (!ReadFile(hFile, cntBuf, 2, &read, nullptr) || read != 2) return 0;
                WORD entryCount = rd16(cntBuf);
                if (entryCount > 200) return 0; // защита от мусора

                for (WORD i = 0; i < entryCount; i++) {
                    DWORD entryPos = ifd0Pos + 2 + (DWORD)i * 12;
                    BYTE entry[12];
                    SetFilePointer(hFile, entryPos, nullptr, FILE_BEGIN);
                    if (!ReadFile(hFile, entry, 12, &read, nullptr) || read != 12) return 0;
                    WORD tag = rd16(entry);
                    WORD type = rd16(entry + 2);
                    DWORD count = rd32(entry + 4);
                    if (tag == 0x0112) {
                        if (type != 3 || count != 1) return 0; // не тот формат — не трогаем
                        if (bigEndianOut) *bigEndianOut = bigEndian;
                        return entryPos + 8; // SHORT-значение лежит в первых 2 байтах 4-байтового поля
                    }
                }
            }
        }

        pos += 2 + segLen; // к следующему маркеру
    }
    return 0;
}

HRESULT RotateJpegOrientation(const std::wstring& path, bool rotateRight)
{
    FILETIME ftCreate, ftAccess, ftWrite;
    if (!GetFileTimes(path, &ftCreate, &ftAccess, &ftWrite))
        return E_FAIL;

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());

    bool bigEndian = false;
    DWORD valueOff = FindOrientationValueOffset(hFile, &bigEndian);
    if (valueOff == 0) {
        CloseHandle(hFile);
        return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED); // тега нет — не трогаем файл вообще
    }

    BYTE valBuf[2];
    DWORD rw = 0;
    SetFilePointer(hFile, valueOff, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, valBuf, 2, &rw, nullptr) || rw != 2) { CloseHandle(hFile); return E_FAIL; }

    int cur = bigEndian ? ((valBuf[0] << 8) | valBuf[1]) : ((valBuf[1] << 8) | valBuf[0]);
    if (cur < 1 || cur > 8) cur = 1;
    int next = rotateRight ? kRotateCW[cur] : kRotateCCW[cur];

    BYTE outBuf[2];
    if (bigEndian) { outBuf[0] = (BYTE)(next >> 8); outBuf[1] = (BYTE)(next & 0xFF); }
    else            { outBuf[0] = (BYTE)(next & 0xFF); outBuf[1] = (BYTE)(next >> 8); }

    SetFilePointer(hFile, valueOff, nullptr, FILE_BEGIN);
    BOOL ok = WriteFile(hFile, outBuf, 2, &rw, nullptr) && rw == 2;
    if (ok) FlushFileBuffers(hFile); // гарантируем, что запись реально дошла до диска до восстановления дат
    CloseHandle(hFile);

    if (ok) {
        // Отдельный, новый дескриптор — открытый ПОСЛЕ того, как файл точно
        // дозаписан и закрыт. SetFileTime на ТОМ ЖЕ дескрипторе, которым
        // только что писали, ненадёжен: ОС может обновить "дату изменения"
        // из-за самой записи чуть позже (при сбросе кэша), уже после
        // восстановления, и тем самым переписать его обратно.
        HANDLE h2 = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h2 != INVALID_HANDLE_VALUE) {
            SetFileTime(h2, &ftCreate, &ftAccess, &ftWrite);
            CloseHandle(h2);
        }
    }

    return ok ? S_OK : E_FAIL;
}

void DoRotateOperation(HWND owner, const SelectionInfo& sel, bool rotateRight)
{
    if (sel.paths.empty() || !sel.allFiles) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    int done = 0, skipped = 0;
    for (auto& p : sel.paths) {
        if (!IsRotatableImageExtension(p)) { skipped++; continue; }
        HRESULT hr = RotateJpegOrientation(p, rotateRight);
        if (SUCCEEDED(hr)) done++; else skipped++;
    }

    wchar_t msg[256];
    swprintf(msg, 256, S_MSG_ROTATE_DONE_FMT, done, skipped);
    MessageBoxW(owner, msg, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
}

} // namespace

// Дата съёмки: EXIF DateTimeOriginal (0x9003) или DateTimeDigitized (0x9004)
// из EXIF SubIFD (на которую указывает тег 0x8769 в IFD0); если ни того ни
// другого нет — DateTime (0x0132) из самого IFD0. Формат тега — ASCII
// "YYYY:MM:DD HH:MM:SS" (ровно 19 значащих байт + завершающий 0). Часовой
// пояс EXIF не хранит — трактуем как локальное время съёмки, как и камеры.
bool FindExifDateTaken(const std::wstring& path, SYSTEMTIME* outLocal)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    bool found = false;
    wchar_t d[20] = {0};

    BYTE soi[2];
    DWORD read = 0;
    SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, soi, 2, &read, nullptr) || read != 2 || soi[0] != 0xFF || soi[1] != 0xD8) {
        CloseHandle(hFile);
        return false;
    }

    DWORD pos = 2;
    for (int guard = 0; guard < 200 && !found; guard++) {
        BYTE marker[2];
        SetFilePointer(hFile, pos, nullptr, FILE_BEGIN);
        if (!ReadFile(hFile, marker, 2, &read, nullptr) || read != 2) break;
        if (marker[0] != 0xFF) break;
        if (marker[1] == 0xD8 || marker[1] == 0xD9) break;
        if (marker[1] == 0xDA) break;
        if (marker[1] >= 0xD0 && marker[1] <= 0xD7) { pos += 2; continue; }

        BYTE lenBuf[2];
        if (!ReadFile(hFile, lenBuf, 2, &read, nullptr) || read != 2) break;
        WORD segLen = (WORD)((lenBuf[0] << 8) | lenBuf[1]);
        if (segLen < 2) break;
        DWORD payloadStart = pos + 4;

        if (marker[1] == 0xE1 && segLen >= 8) {
            BYTE sig[6];
            if (ReadFile(hFile, sig, 6, &read, nullptr) && read == 6 && memcmp(sig, "Exif\0\0", 6) == 0) {
                DWORD tiffStart = payloadStart + 6;
                BYTE bo[4];
                SetFilePointer(hFile, tiffStart, nullptr, FILE_BEGIN);
                if (ReadFile(hFile, bo, 4, &read, nullptr) && read == 4) {
                    bool bigEndian = false, okBo = true;
                    if (bo[0] == 'I' && bo[1] == 'I') bigEndian = false;
                    else if (bo[0] == 'M' && bo[1] == 'M') bigEndian = true;
                    else okBo = false;

                    if (okBo) {
                        auto rd16 = [&](const BYTE* p) -> WORD {
                            return bigEndian ? (WORD)((p[0] << 8) | p[1]) : (WORD)((p[1] << 8) | p[0]);
                        };
                        auto rd32 = [&](const BYTE* p) -> DWORD {
                            return bigEndian
                                ? ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3]
                                : ((DWORD)p[3] << 24) | ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0];
                        };

                        if (rd16(bo + 2) == 0x002A) {
                            BYTE ifdOffBuf[4];
                            SetFilePointer(hFile, tiffStart + 4, nullptr, FILE_BEGIN);
                            if (ReadFile(hFile, ifdOffBuf, 4, &read, nullptr) && read == 4) {
                                DWORD ifd0Off = rd32(ifdOffBuf);
                                if (ifd0Off != 0 && ifd0Off <= 0x100000) {
                                    DWORD ifdToScan[2] = { tiffStart + ifd0Off, 0 };
                                    int ifdCount = 1;

                                    for (int ifdIdx = 0; ifdIdx < ifdCount && !found; ifdIdx++) {
                                        DWORD ifdPos = ifdToScan[ifdIdx];
                                        BYTE cntBuf[2];
                                        SetFilePointer(hFile, ifdPos, nullptr, FILE_BEGIN);
                                        if (!ReadFile(hFile, cntBuf, 2, &read, nullptr) || read != 2) continue;
                                        WORD entryCount = rd16(cntBuf);
                                        if (entryCount > 200) continue;

                                        for (WORD i = 0; i < entryCount; i++) {
                                            DWORD entryPos = ifdPos + 2 + (DWORD)i * 12;
                                            BYTE entry[12];
                                            SetFilePointer(hFile, entryPos, nullptr, FILE_BEGIN);
                                            if (!ReadFile(hFile, entry, 12, &read, nullptr) || read != 12) continue;
                                            WORD tag = rd16(entry);
                                            WORD type = rd16(entry + 2);
                                            DWORD count = rd32(entry + 4);

                                            if (ifdIdx == 0 && tag == 0x8769 && ifdCount < 2) {
                                                DWORD exifIfdOff = rd32(entry + 8);
                                                if (exifIfdOff != 0 && exifIfdOff <= 0x100000) {
                                                    ifdToScan[1] = tiffStart + exifIfdOff;
                                                    ifdCount = 2;
                                                }
                                            }

                                            bool wantedTag = (tag == 0x9003 || tag == 0x9004 || (ifdIdx == 0 && tag == 0x0132));
                                            if (wantedTag && type == 2 && count >= 19 && count <= 20) {
                                                DWORD strOff = (count <= 4) ? (entryPos + 8) : (tiffStart + rd32(entry + 8));
                                                BYTE strBuf[19];
                                                SetFilePointer(hFile, strOff, nullptr, FILE_BEGIN);
                                                if (ReadFile(hFile, strBuf, 19, &read, nullptr) && read == 19) {
                                                    for (int k = 0; k < 19; k++) d[k] = (wchar_t)strBuf[k];
                                                    found = true;
                                                    break;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        pos += 2 + segLen;
    }

    CloseHandle(hFile);
    if (!found) return false;

    bool validFormat = true;
    for (int i = 0; i < 19 && validFormat; i++) {
        if (i == 4 || i == 7 || i == 13 || i == 16) { if (d[i] != L':') validFormat = false; }
        else if (i == 10) { if (d[i] != L' ') validFormat = false; }
        else { if (d[i] < L'0' || d[i] > L'9') validFormat = false; }
    }
    if (!validFormat) return false;

    SYSTEMTIME st = {0};
    st.wYear   = (WORD)((d[0]-L'0')*1000 + (d[1]-L'0')*100 + (d[2]-L'0')*10 + (d[3]-L'0'));
    st.wMonth  = (WORD)((d[5]-L'0')*10 + (d[6]-L'0'));
    st.wDay    = (WORD)((d[8]-L'0')*10 + (d[9]-L'0'));
    st.wHour   = (WORD)((d[11]-L'0')*10 + (d[12]-L'0'));
    st.wMinute = (WORD)((d[14]-L'0')*10 + (d[15]-L'0'));
    st.wSecond = (WORD)((d[17]-L'0')*10 + (d[18]-L'0'));
    if (st.wYear < 1980 || st.wYear > 2100 || st.wMonth < 1 || st.wMonth > 12 || st.wDay < 1 || st.wDay > 31)
        return false;

    *outLocal = st;
    return true;
}

// Координаты съёмки: EXIF GPSLatitude/GPSLongitude (теги 0x0002/0x0004,
// RATIONAL[3] — градусы,минуты,секунды) + GPSLatitudeRef/GPSLongitudeRef
// (0x0001/0x0003, 'N'/'S'/'E'/'W') из отдельного GPS IFD (на него указывает
// тег 0x8825 в IFD0). Возвращает десятичные градусы (широта: +N/-S,
// долгота: +E/-W).
bool FindExifGPS(const std::wstring& path, double* outLat, double* outLon)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ,
                                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;

    bool foundLat = false, foundLon = false;
    double lat = 0, lon = 0;
    char latRef = 0, lonRef = 0;

    BYTE soi[2];
    DWORD read = 0;
    SetFilePointer(hFile, 0, nullptr, FILE_BEGIN);
    if (!ReadFile(hFile, soi, 2, &read, nullptr) || read != 2 || soi[0] != 0xFF || soi[1] != 0xD8) {
        CloseHandle(hFile);
        return false;
    }

    DWORD pos = 2;
    bool doneScanning = false;
    for (int guard = 0; guard < 200 && !doneScanning; guard++) {
        BYTE marker[2];
        SetFilePointer(hFile, pos, nullptr, FILE_BEGIN);
        if (!ReadFile(hFile, marker, 2, &read, nullptr) || read != 2) break;
        if (marker[0] != 0xFF) break;
        if (marker[1] == 0xD8 || marker[1] == 0xD9) break;
        if (marker[1] == 0xDA) break;
        if (marker[1] >= 0xD0 && marker[1] <= 0xD7) { pos += 2; continue; }

        BYTE lenBuf[2];
        if (!ReadFile(hFile, lenBuf, 2, &read, nullptr) || read != 2) break;
        WORD segLen = (WORD)((lenBuf[0] << 8) | lenBuf[1]);
        if (segLen < 2) break;
        DWORD payloadStart = pos + 4;

        if (marker[1] == 0xE1 && segLen >= 8) {
            BYTE sig[6];
            if (ReadFile(hFile, sig, 6, &read, nullptr) && read == 6 && memcmp(sig, "Exif\0\0", 6) == 0) {
                DWORD tiffStart = payloadStart + 6;
                BYTE bo[4];
                SetFilePointer(hFile, tiffStart, nullptr, FILE_BEGIN);
                if (ReadFile(hFile, bo, 4, &read, nullptr) && read == 4) {
                    bool bigEndian = false, okBo = true;
                    if (bo[0] == 'I' && bo[1] == 'I') bigEndian = false;
                    else if (bo[0] == 'M' && bo[1] == 'M') bigEndian = true;
                    else okBo = false;

                    if (okBo) {
                        auto rd16 = [&](const BYTE* p) -> WORD {
                            return bigEndian ? (WORD)((p[0] << 8) | p[1]) : (WORD)((p[1] << 8) | p[0]);
                        };
                        auto rd32 = [&](const BYTE* p) -> DWORD {
                            return bigEndian
                                ? ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3]
                                : ((DWORD)p[3] << 24) | ((DWORD)p[2] << 16) | ((DWORD)p[1] << 8) | p[0];
                        };
                        // RATIONAL[3] (deg,min,sec) -> десятичные градусы
                        auto readRational3 = [&](DWORD off) -> double {
                            BYTE buf[24];
                            SetFilePointer(hFile, off, nullptr, FILE_BEGIN);
                            DWORD r2 = 0;
                            if (!ReadFile(hFile, buf, 24, &r2, nullptr) || r2 != 24) return -1.0;
                            double parts[3];
                            for (int k = 0; k < 3; k++) {
                                DWORD num = rd32(buf + k * 8);
                                DWORD den = rd32(buf + k * 8 + 4);
                                parts[k] = (den != 0) ? (double)num / (double)den : 0.0;
                            }
                            return parts[0] + parts[1] / 60.0 + parts[2] / 3600.0;
                        };

                        if (rd16(bo + 2) == 0x002A) {
                            BYTE ifdOffBuf[4];
                            SetFilePointer(hFile, tiffStart + 4, nullptr, FILE_BEGIN);
                            if (ReadFile(hFile, ifdOffBuf, 4, &read, nullptr) && read == 4) {
                                DWORD ifd0Off = rd32(ifdOffBuf);
                                if (ifd0Off != 0 && ifd0Off <= 0x100000) {
                                    DWORD ifd0Pos = tiffStart + ifd0Off;
                                    BYTE cntBuf[2];
                                    SetFilePointer(hFile, ifd0Pos, nullptr, FILE_BEGIN);
                                    DWORD gpsIfdPos = 0;
                                    if (ReadFile(hFile, cntBuf, 2, &read, nullptr) && read == 2) {
                                        WORD entryCount = rd16(cntBuf);
                                        if (entryCount <= 200) {
                                            for (WORD i = 0; i < entryCount; i++) {
                                                DWORD entryPos = ifd0Pos + 2 + (DWORD)i * 12;
                                                BYTE entry[12];
                                                SetFilePointer(hFile, entryPos, nullptr, FILE_BEGIN);
                                                if (!ReadFile(hFile, entry, 12, &read, nullptr) || read != 12) continue;
                                                if (rd16(entry) == 0x8825) { // GPSInfoIFDPointer
                                                    DWORD gpsOff = rd32(entry + 8);
                                                    if (gpsOff != 0 && gpsOff <= 0x100000)
                                                        gpsIfdPos = tiffStart + gpsOff;
                                                    break;
                                                }
                                            }
                                        }
                                    }

                                    if (gpsIfdPos != 0) {
                                        BYTE gpsCntBuf[2];
                                        SetFilePointer(hFile, gpsIfdPos, nullptr, FILE_BEGIN);
                                        if (ReadFile(hFile, gpsCntBuf, 2, &read, nullptr) && read == 2) {
                                            WORD gpsCount = rd16(gpsCntBuf);
                                            if (gpsCount <= 200) {
                                                for (WORD i = 0; i < gpsCount; i++) {
                                                    DWORD entryPos = gpsIfdPos + 2 + (DWORD)i * 12;
                                                    BYTE entry[12];
                                                    SetFilePointer(hFile, entryPos, nullptr, FILE_BEGIN);
                                                    if (!ReadFile(hFile, entry, 12, &read, nullptr) || read != 12) continue;
                                                    WORD tag = rd16(entry);
                                                    WORD type = rd16(entry + 2);
                                                    DWORD count = rd32(entry + 4);

                                                    if (tag == 0x0001 && type == 2 && count >= 1) { // LatRef
                                                        latRef = (char)entry[8];
                                                    } else if (tag == 0x0003 && type == 2 && count >= 1) { // LonRef
                                                        lonRef = (char)entry[8];
                                                    } else if (tag == 0x0002 && type == 5 && count == 3) { // Lat
                                                        double v = readRational3(tiffStart + rd32(entry + 8));
                                                        if (v >= 0) { lat = v; foundLat = true; }
                                                    } else if (tag == 0x0004 && type == 5 && count == 3) { // Lon
                                                        double v = readRational3(tiffStart + rd32(entry + 8));
                                                        if (v >= 0) { lon = v; foundLon = true; }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    doneScanning = true; // IFD0/GPS IFD один раз, дальше в файле не встретится
                                }
                            }
                        }
                    }
                }
            }
        }
        pos += 2 + segLen;
    }

    CloseHandle(hFile);
    if (!foundLat || !foundLon || !latRef || !lonRef) return false;

    if (latRef == 'S' || latRef == 's') lat = -lat;
    if (lonRef == 'W' || lonRef == 'w') lon = -lon;
    if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return false;

    *outLat = lat;
    *outLon = lon;
    return true;
}

void Cmd_RotateLeft(HWND owner, const SelectionInfo& sel) { DoRotateOperation(owner, sel, false); }
void Cmd_RotateRight(HWND owner, const SelectionInfo& sel) { DoRotateOperation(owner, sel, true); }
