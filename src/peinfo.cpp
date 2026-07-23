#include "common.h"
#include "strings.h"
#include <corhdr.h>
#include <delayimp.h>
#include <set>
#include <cwctype>
#include <cstring>

namespace {

// ------------------------------------------------------------------ маппинг файла в память

struct MappedFile {
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = nullptr;
    const BYTE* base = nullptr;
    DWORD64 size = 0;

    ~MappedFile() {
        if (base) UnmapViewOfFile(base);
        if (hMap) CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    }

    bool Open(const std::wstring& path) {
        hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li;
        if (!GetFileSizeEx(hFile, &li) || li.QuadPart < 64) return false;
        size = (DWORD64)li.QuadPart;
        hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!hMap) return false;
        base = (const BYTE*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
        return base != nullptr;
    }

    template <typename T>
    const T* At(DWORD64 off) const {
        if (!base || off + sizeof(T) > size) return nullptr;
        return reinterpret_cast<const T*>(base + off);
    }

    // Возвращает указатель на ASCII-строку, только если где-то впереди (в пределах maxLen) есть '\0'
    const char* CStrAt(DWORD64 off, DWORD64 maxLen = 260) const {
        if (!base || off >= size) return nullptr;
        DWORD64 lim = (off + maxLen < size) ? off + maxLen : size;
        for (DWORD64 i = off; i < lim; i++)
            if (base[i] == 0) return reinterpret_cast<const char*>(base + off);
        return nullptr;
    }
};

DWORD RvaToOffset(const IMAGE_SECTION_HEADER* sections, WORD count, DWORD rva)
{
    for (WORD i = 0; i < count; i++) {
        DWORD start = sections[i].VirtualAddress;
        DWORD vsize = sections[i].Misc.VirtualSize ? sections[i].Misc.VirtualSize : sections[i].SizeOfRawData;
        if (rva >= start && rva < start + vsize)
            return sections[i].PointerToRawData + (rva - start);
    }
    return 0;
}

struct PeParsed {
    bool ok = false;
    bool is64 = false;
    WORD machine = 0;
    bool isDotNet = false;
    DWORD corFlags = 0;
    std::wstring clrVersionString; // например "v4.0.30319"
    std::vector<std::string> directImports;
    std::vector<std::string> delayImports;
};

bool ParsePe(const std::wstring& path, PeParsed* out)
{
    MappedFile mf;
    if (!mf.Open(path)) return false;

    auto dos = mf.At<IMAGE_DOS_HEADER>(0);
    if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;

    DWORD64 ntOff = (DWORD64)(LONG)dos->e_lfanew;
    auto sig = mf.At<DWORD>(ntOff);
    if (!sig || *sig != IMAGE_NT_SIGNATURE) return false;

    auto fileHeader = mf.At<IMAGE_FILE_HEADER>(ntOff + sizeof(DWORD));
    if (!fileHeader) return false;
    out->machine = fileHeader->Machine;

    DWORD64 optOff = ntOff + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
    auto magic = mf.At<WORD>(optOff);
    if (!magic) return false;
    out->is64 = (*magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC);

    const IMAGE_DATA_DIRECTORY* dataDirs = nullptr;
    DWORD numDirs = 0;

    if (out->is64) {
        auto opt = mf.At<IMAGE_OPTIONAL_HEADER64>(optOff);
        if (!opt) return false;
        numDirs = opt->NumberOfRvaAndSizes;
        dataDirs = opt->DataDirectory;
    } else {
        auto opt = mf.At<IMAGE_OPTIONAL_HEADER32>(optOff);
        if (!opt) return false;
        numDirs = opt->NumberOfRvaAndSizes;
        dataDirs = opt->DataDirectory;
    }

    DWORD64 sectionsOff = optOff + fileHeader->SizeOfOptionalHeader;
    WORD numSections = fileHeader->NumberOfSections;
    if (sectionsOff + (DWORD64)numSections * sizeof(IMAGE_SECTION_HEADER) > mf.size) return false;
    auto sections = mf.At<IMAGE_SECTION_HEADER>(sectionsOff);
    if (!sections) return false;

    auto dir = [&](DWORD idx) -> const IMAGE_DATA_DIRECTORY* {
        return (idx < numDirs) ? &dataDirs[idx] : nullptr;
    };

    // ---- прямой импорт ----
    if (auto d = dir(IMAGE_DIRECTORY_ENTRY_IMPORT); d && d->VirtualAddress) {
        DWORD off = RvaToOffset(sections, numSections, d->VirtualAddress);
        if (off) {
            for (int i = 0; i < 500; i++) {
                auto desc = mf.At<IMAGE_IMPORT_DESCRIPTOR>((DWORD64)off + (DWORD64)i * sizeof(IMAGE_IMPORT_DESCRIPTOR));
                if (!desc || desc->Name == 0) break;
                DWORD nameOff = RvaToOffset(sections, numSections, desc->Name);
                if (nameOff) {
                    const char* s = mf.CStrAt(nameOff);
                    if (s) out->directImports.push_back(s);
                }
            }
        }
    }

    // ---- отложенный импорт ----
    if (auto d = dir(IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT); d && d->VirtualAddress) {
        DWORD off = RvaToOffset(sections, numSections, d->VirtualAddress);
        if (off) {
            for (int i = 0; i < 500; i++) {
                auto desc = mf.At<ImgDelayDescr>((DWORD64)off + (DWORD64)i * sizeof(ImgDelayDescr));
                if (!desc || desc->rvaDLLName == 0) break;
                DWORD nameOff = RvaToOffset(sections, numSections, desc->rvaDLLName);
                if (nameOff) {
                    const char* s = mf.CStrAt(nameOff);
                    if (s) out->delayImports.push_back(s);
                }
            }
        }
    }

    // ---- .NET CLR header ----
    if (auto d = dir(IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR); d && d->VirtualAddress) {
        DWORD off = RvaToOffset(sections, numSections, d->VirtualAddress);
        auto cor = off ? mf.At<IMAGE_COR20_HEADER>(off) : nullptr;
        if (cor) {
            out->isDotNet = true;
            out->corFlags = cor->Flags;

            if (cor->MetaData.VirtualAddress) {
                DWORD mdOff = RvaToOffset(sections, numSections, cor->MetaData.VirtualAddress);
                if (mdOff) {
                    auto mdSig = mf.At<DWORD>(mdOff);
                    if (mdSig && *mdSig == 0x424A5342u) { // "BSJB"
                        auto len = mf.At<DWORD>(mdOff + 12);
                        if (len && *len > 0 && *len < 64 && mdOff + 16 + *len <= mf.size) {
                            const char* verStr = reinterpret_cast<const char*>(mf.base + mdOff + 16);
                            size_t n = strnlen(verStr, *len);
                            out->clrVersionString.assign(verStr, verStr + n);
                        }
                    }
                }
            }
        }
    }

    out->ok = true;
    return true;
}

std::wstring DirNameOfLocal(const std::wstring& path)
{
    size_t p = path.find_last_of(L"\\/");
    return (p == std::wstring::npos) ? std::wstring() : path.substr(0, p);
}

std::wstring ToLowerCopy(const std::wstring& s)
{
    std::wstring r = s;
    for (auto& c : r) c = (wchar_t)towlower(c);
    return r;
}

bool IsApiSetName(const std::wstring& nameLower)
{
    return nameLower.rfind(L"api-ms-", 0) == 0 || nameLower.rfind(L"ext-ms-", 0) == 0;
}

std::wstring TryResolveDllPath(const std::wstring& name, const std::wstring& exeDir, const std::wstring& sysDir)
{
    std::wstring p1 = exeDir + L"\\" + name;
    if (GetFileAttributesW(p1.c_str()) != INVALID_FILE_ATTRIBUTES) return p1;
    std::wstring p2 = sysDir + L"\\" + name;
    if (GetFileAttributesW(p2.c_str()) != INVALID_FILE_ATTRIBUTES) return p2;
    return L"";
}

} // namespace

// ------------------------------------------------------------------ публичные функции

bool GetPeArchAndDotNetInfo(const std::wstring& path, std::wstring* archText, std::wstring* dotNetVersionText)
{
    PeParsed pe;
    if (!ParsePe(path, &pe)) return false;

    if (pe.isDotNet) {
        bool req32 = COR_IS_32BIT_REQUIRED(pe.corFlags);
        bool pref32 = COR_IS_32BIT_PREFERRED(pe.corFlags);
        if (pref32)
            *archText = S_ARCH_ANYCPU_32PREFERRED;
        else if (req32)
            *archText = S_ARCH_X86;
        else if (pe.machine == IMAGE_FILE_MACHINE_AMD64)
            *archText = S_ARCH_X64;
        else
            *archText = S_ARCH_ANYCPU;

        if (!pe.clrVersionString.empty())
            *dotNetVersionText = pe.clrVersionString;
    } else {
        switch (pe.machine) {
        case IMAGE_FILE_MACHINE_I386:  *archText = S_ARCH_X86; break;
        case IMAGE_FILE_MACHINE_AMD64: *archText = S_ARCH_X64; break;
        case IMAGE_FILE_MACHINE_ARM64: *archText = S_ARCH_ARM64; break;
        case IMAGE_FILE_MACHINE_ARMNT: *archText = S_ARCH_ARM; break;
        default: {
            wchar_t buf[16];
            swprintf(buf, 16, L"%04X)", pe.machine);
            *archText = std::wstring(S_ARCH_UNKNOWN_PREFIX) + buf;
        }
        }
    }
    return true;
}

void Cmd_ShowDependencies(HWND owner, const SelectionInfo& sel)
{
    if (sel.paths.empty() || !sel.allFiles) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    const std::wstring& startPath = sel.paths[0];

    PeParsed rootPe;
    if (!ParsePe(startPath, &rootPe)) {
        MessageBoxW(owner, S_MSG_NOT_PE, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    wchar_t sysDirBuf[MAX_PATH];
    GetSystemDirectoryW(sysDirBuf, MAX_PATH);
    std::wstring sysDir = sysDirBuf;
    std::wstring startDir = DirNameOfLocal(startPath);

    std::vector<InfoRow> rows;
    std::set<std::wstring> visited;
    std::vector<std::wstring> queue;
    queue.push_back(startPath);

    int totalProcessed = 0;
    bool capReached = false;

    while (!queue.empty()) {
        if (totalProcessed >= 200) { capReached = true; break; }

        std::wstring cur = queue.front();
        queue.erase(queue.begin());

        PeParsed pe;
        if (cur != startPath) {
            if (!ParsePe(cur, &pe)) continue;
        } else {
            pe = rootPe;
        }
        totalProcessed++;

        auto handleList = [&](const std::vector<std::string>& names, bool delay) {
            for (auto& nameA : names) {
                std::wstring name(nameA.begin(), nameA.end());
                std::wstring key = ToLowerCopy(name);
                if (visited.count(key)) continue;
                visited.insert(key);

                bool isApiSet = IsApiSetName(key);
                std::wstring resolvedPath = isApiSet ? L"" : TryResolveDllPath(name, startDir, sysDir);
                bool resolved = !resolvedPath.empty();

                std::wstring status;
                if (isApiSet)
                    status = S_DEP_APISET;
                else if (resolved)
                    status = S_DEP_FOUND + std::wstring(L": ") + resolvedPath;
                else
                    status = S_DEP_NOT_FOUND + std::wstring(L" (\u043E\u0436\u0438\u0434\u0430\u0435\u043C\u043E\u0435 \u043C\u0435\u0441\u0442\u043E: ") +
                              startDir + L"\\" + name + L")";

                std::wstring label = name + (delay ? S_DEP_DELAYLOAD : L"");
                rows.push_back({ label, status, false, isApiSet ? 0 : 1 });

                if (resolved) queue.push_back(resolvedPath);
            }
        };

        handleList(pe.directImports, false);
        handleList(pe.delayImports, true);
    }

    if (capReached)
        rows.push_back({ S_DEP_CAP_REACHED, L"" });

    if (rows.empty()) {
        MessageBoxW(owner, S_MSG_NOTHING_SELECTED, S_MSG_TITLE, MB_OK | MB_ICONINFORMATION);
        return;
    }

    ShowInfoDialog(owner, S_DLG_TITLE_DEPENDENCIES, rows);
}
