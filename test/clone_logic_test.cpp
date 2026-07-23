#include <string>
#include <vector>
#include <set>
#include <cstdio>
#include <cassert>

// Имитация "существующих" файлов - вместо GetFileAttributesW
std::set<std::wstring> g_existing;
bool Exists(const std::wstring& full) { return g_existing.count(full) > 0; }

std::wstring FormatFilenameDate(int Y,int Mo,int D,int H,int Mi,int S) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d.%02d.%02d %02d-%02d-%02d", Y,Mo,D,H,Mi,S);
    return buf;
}

bool FindFreeCloneNameByDate(const std::wstring& dir, const std::wstring& base, const std::wstring& ext,
                              const std::wstring& dateSuf, std::wstring* outNewName)
{
    for (int n = 1; n <= 1000; n++) {
        std::wstring candidate = (n == 1) ? (base + L" " + dateSuf + ext)
                                           : (base + L" " + dateSuf + L" (" + std::to_wstring(n) + L")" + ext);
        std::wstring full = dir.empty() ? candidate : (dir + L"\\" + candidate);
        if (!Exists(full)) { *outNewName = candidate; return true; }
    }
    return false;
}

bool FindFreeCloneNameNumbered(const std::wstring& dir, const std::wstring& base, const std::wstring& ext,
                                std::wstring* outNewName)
{
    for (int n = 1; n <= 100000; n++) {
        std::wstring candidate = base + L"_" + std::to_wstring(n) + ext;
        std::wstring full = dir.empty() ? candidate : (dir + L"\\" + candidate);
        if (!Exists(full)) { *outNewName = candidate; return true; }
    }
    return false;
}

void pw(const std::wstring& w) { for (wchar_t c : w) putchar((char)c); }

int main() {
    printf("=== Numbered: pervyj klon ===\n");
    {
        std::wstring name;
        bool ok = FindFreeCloneNameNumbered(L"C:\\dir", L"Photo", L".png", &name);
        assert(ok); pw(name); printf("\n");
        assert(name == L"Photo_1.png");
    }

    printf("=== Numbered: _1 i _2 uzhe zanyaty ===\n");
    {
        g_existing = { L"C:\\dir\\Photo_1.png", L"C:\\dir\\Photo_2.png" };
        std::wstring name;
        FindFreeCloneNameNumbered(L"C:\\dir", L"Photo", L".png", &name);
        pw(name); printf("\n");
        assert(name == L"Photo_3.png");
        printf("OK\n");
    }

    printf("=== Numbered: papka bez rasshireniya ===\n");
    {
        g_existing.clear();
        std::wstring name;
        FindFreeCloneNameNumbered(L"C:\\dir", L"MyFolder", L"", &name);
        pw(name); printf("\n");
        assert(name == L"MyFolder_1");
        printf("OK\n");
    }

    printf("=== Data: pervyj klon (net kollizii) ===\n");
    {
        g_existing.clear();
        std::wstring dateSuf = FormatFilenameDate(2026,7,14,12,0,0);
        std::wstring name;
        FindFreeCloneNameByDate(L"C:\\dir", L"Report", L".docx", dateSuf, &name);
        pw(name); printf("\n");
        assert(name == L"Report 2026.07.14 12-00-00.docx");
        printf("OK\n");
    }

    printf("=== Data: kollizia -> dobavlyaem (2) ===\n");
    {
        std::wstring dateSuf = FormatFilenameDate(2026,7,14,12,0,0);
        g_existing = { L"C:\\dir\\Report " + dateSuf + L".docx" };
        std::wstring name;
        FindFreeCloneNameByDate(L"C:\\dir", L"Report", L".docx", dateSuf, &name);
        pw(name); printf("\n");
        assert(name == (L"Report " + dateSuf + L" (2).docx"));
        printf("OK\n");
    }

    printf("\nVSE PROVERKI PROSHLI.\n");
    return 0;
}
