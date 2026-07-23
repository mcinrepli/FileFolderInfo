// Нативный (Linux, обычный g++) тест ЧИСТОЙ ЛОГИКИ без WinAPI —
// та же логика, что в src/util.cpp / src/commands.cpp, скопирована сюда
// специально, чтобы прогнать на реальных примерах из ТЗ и увидеть настоящий вывод.
#include <string>
#include <vector>
#include <cstdio>
#include <cassert>

struct CopySuffixInfo { std::wstring base, ext; int repeats = 0; };

static const std::wstring kCopySuffix = L" \u2014 \u043A\u043E\u043F\u0438\u044F"; // " — копия"

CopySuffixInfo ParseCopySuffix(const std::wstring& name, bool isFolder)
{
    CopySuffixInfo info;
    std::wstring base = name, ext;
    if (!isFolder) {
        size_t dot = base.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) { ext = base.substr(dot); base = base.substr(0, dot); }
    }
    int repeats = 0;
    while (base.size() >= kCopySuffix.size() &&
           base.compare(base.size() - kCopySuffix.size(), kCopySuffix.size(), kCopySuffix) == 0) {
        base.resize(base.size() - kCopySuffix.size());
        repeats++;
    }
    info.base = base; info.ext = ext; info.repeats = repeats;
    return info;
}

std::wstring FormatFilenameDate(int Y, int Mo, int D, int H, int Mi, int S)
{
    wchar_t buf[64];
    swprintf(buf, 64, L"%04d.%02d.%02d %02d-%02d-%02d", Y, Mo, D, H, Mi, S);
    return buf;
}

static const wchar_t* MONTHS_LOWER[12] = {
    L"\u044F\u043D\u0432\u0430\u0440\u044F", L"\u0444\u0435\u0432\u0440\u0430\u043B\u044F", L"\u043C\u0430\u0440\u0442\u0430",
    L"\u0430\u043F\u0440\u0435\u043B\u044F", L"\u043C\u0430\u044F", L"\u0438\u044E\u043D\u044F", L"\u0438\u044E\u043B\u044F",
    L"\u0430\u0432\u0433\u0443\u0441\u0442\u0430", L"\u0441\u0435\u043D\u0442\u044F\u0431\u0440\u044F", L"\u043E\u043A\u0442\u044F\u0431\u0440\u044F",
    L"\u043D\u043E\u044F\u0431\u0440\u044F", L"\u0434\u0435\u043A\u0430\u0431\u0440\u044F"
};
static const wchar_t* MONTHS_UPPER[12] = {
    L"\u042F\u043D\u0432\u0430\u0440\u044F", L"\u0424\u0435\u0432\u0440\u0430\u043B\u044F", L"\u041C\u0430\u0440\u0442\u0430",
    L"\u0410\u043F\u0440\u0435\u043B\u044F", L"\u041C\u0430\u044F", L"\u0418\u044E\u043D\u044F", L"\u0418\u044E\u043B\u044F",
    L"\u0410\u0432\u0433\u0443\u0441\u0442\u0430", L"\u0421\u0435\u043D\u0442\u044F\u0431\u0440\u044F", L"\u041E\u043A\u0442\u044F\u0431\u0440\u044F",
    L"\u041D\u043E\u044F\u0431\u0440\u044F", L"\u0414\u0435\u043A\u0430\u0431\u0440\u044F"
};

std::wstring FormatDateVariant(int variant, int H, int Mi, int S, int D, int Mo, int Y)
{
    wchar_t buf[160];
    switch (variant) {
    case 1: swprintf(buf,160,L"%02d-%02d-%02d %d %ls %d \u0433\u043E\u0434\u0430", H,Mi,S,D,MONTHS_LOWER[Mo-1],Y); break;
    case 2: swprintf(buf,160,L"%02d-%02d-%02d %02d.%02d.%04d", H,Mi,S,D,Mo,Y); break;
    case 3: swprintf(buf,160,L"%02d:%02d:%02d %d %ls %d \u0433\u043E\u0434\u0430", H,Mi,S,D,MONTHS_LOWER[Mo-1],Y); break;
    case 4: swprintf(buf,160,L"%02d:%02d:%02d %02d.%02d.%04d", H,Mi,S,D,Mo,Y); break;
    case 5: swprintf(buf,160,L"%02d\u0447 %02d\u043C %02d\u0441 %d %ls %d \u0413\u043E\u0434\u0430", H,Mi,S,D,MONTHS_UPPER[Mo-1],Y); break;
    }
    return buf;
}

void pw(const std::wstring& w) {
    // печатаем как UTF-8 через промежуточный mbstate — проще: код-поинты вручную в UTF-8
    for (wchar_t wc : w) {
        unsigned cp = (unsigned)wc;
        if (cp < 0x80) putchar((char)cp);
        else if (cp < 0x800) { putchar((char)(0xC0 | (cp>>6))); putchar((char)(0x80 | (cp&0x3F))); }
        else { putchar((char)(0xE0 | (cp>>12))); putchar((char)(0x80 | ((cp>>6)&0x3F))); putchar((char)(0x80 | (cp&0x3F))); }
    }
}

int main() {
    setlocale(LC_ALL, "C"); // намеренно НЕ трогаем локаль — как и в реальном DLL

    printf("=== Тест 1: разбор суффикса копий (файл) ===\n");
    {
        std::wstring name = L"Photo" + kCopySuffix + kCopySuffix + L".png"; // "Photo — копия — копия.png"
        auto info = ParseCopySuffix(name, false);
        printf("repeats="); printf("%d", info.repeats); printf(" base="); pw(info.base); printf(" ext="); pw(info.ext); printf("\n");
        assert(info.repeats == 2);
        assert(info.base == L"Photo");
        assert(info.ext == L".png");
        std::wstring newName = info.base + L"-" + std::to_wstring(info.repeats) + info.ext;
        printf("=> "); pw(newName); printf("\n");
        assert(newName == L"Photo-2.png");
    }

    printf("\n=== Тест 2: пример из ТЗ (папка, без расширения) ===\n");
    {
        std::wstring name = L"HOME-ICON" + kCopySuffix; // "HOME-ICON — копия"
        auto info = ParseCopySuffix(name, true); // ПАПКА — расширение не режем
        printf("repeats="); printf("%d", info.repeats); printf(" base="); pw(info.base); printf(" ext=["); pw(info.ext); printf("]\n");
        assert(info.repeats == 1);
        assert(info.base == L"HOME-ICON");
        assert(info.ext == L"");
    }

    printf("\n=== Тест 3: точный пример из ТЗ (файл HOME-ICON — копия.png -> дата) ===\n");
    {
        std::wstring name = L"HOME-ICON" + kCopySuffix + L".png";
        auto info = ParseCopySuffix(name, false);
        assert(info.repeats == 1);
        assert(info.base == L"HOME-ICON");
        assert(info.ext == L".png");
        std::wstring dateSuf = FormatFilenameDate(2026, 7, 11, 22, 10, 55);
        std::wstring newName = info.base + L" " + dateSuf + info.ext;
        pw(newName); printf("\n");
        assert(newName == L"HOME-ICON 2026.07.11 22-10-55.png");
        printf("OK: совпало с примером из ТЗ\n");
    }

    printf("\n=== Тест 4: пункт 9 (просто добавить дату, без снятия копии) ===\n");
    {
        std::wstring name = L"HOME-ICON.png";
        size_t dot = name.find_last_of(L'.');
        std::wstring base = name.substr(0, dot), ext = name.substr(dot);
        std::wstring dateSuf = FormatFilenameDate(2026, 7, 11, 22, 10, 55);
        std::wstring newName = base + L" " + dateSuf + ext;
        pw(newName); printf("\n");
        assert(newName == L"HOME-ICON 2026.07.11 22-10-55.png");
        printf("OK: совпало с примером из ТЗ\n");
    }

    printf("\n=== Тест 5: пять форматов дат (пример из ТЗ: 17:05:22, 10 ноября 2025) ===\n");
    {
        for (int v = 1; v <= 5; v++) {
            std::wstring s = FormatDateVariant(v, 17, 5, 22, 10, 11, 2025);
            printf("  [%d] ", v); pw(s); printf("\n");
        }
    }

    printf("\n=== Тест 6: имя без суффикса копии — должно остаться repeats=0 ===\n");
    {
        auto info = ParseCopySuffix(L"document.txt", false);
        assert(info.repeats == 0);
        assert(info.base == L"document");
        assert(info.ext == L".txt");
        printf("OK\n");
    }

    printf("\nВСЕ ПРОВЕРКИ ПРОШЛИ.\n");
    return 0;
}
