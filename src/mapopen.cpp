#include "common.h"
#include "strings.h"

#include <algorithm>
#include <cwchar>
#include <vector>

namespace {

constexpr UINT ID_OPEN_BUILTIN = 5101;
constexpr UINT ID_OPEN_DEFAULT = 5102;
constexpr UINT ID_FIRST_BROWSER = 5200;

struct BrowserEntry {
    std::wstring name;
    std::wstring path;
    std::vector<std::wstring> arguments;
};

bool ReadRegistryString(HKEY root, const std::wstring& subkey, REGSAM view,
                        const wchar_t* valueName, std::wstring* value)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS)
        return false;

    DWORD type = 0;
    DWORD bytes = 0;
    LONG rc = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &bytes);
    if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t)) {
        RegCloseKey(key);
        return false;
    }

    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 2, L'\0');
    rc = RegQueryValueExW(key, valueName, nullptr, &type,
                          reinterpret_cast<BYTE*>(buffer.data()), &bytes);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS)
        return false;

    std::wstring result(buffer.data());
    if (type == REG_EXPAND_SZ) {
        DWORD needed = ExpandEnvironmentStringsW(result.c_str(), nullptr, 0);
        if (!needed) return false;
        std::vector<wchar_t> expanded(needed, L'\0');
        DWORD written = ExpandEnvironmentStringsW(result.c_str(), expanded.data(), needed);
        if (!written || written > needed) return false;
        result.assign(expanded.data());
    }
    *value = std::move(result);
    return true;
}

bool ResolveExecutable(std::wstring path, std::wstring* resolved)
{
    if (path.empty()) return false;
    size_t first = path.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return false;
    size_t last = path.find_last_not_of(L" \t\r\n");
    path = path.substr(first, last - first + 1);
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"')
        path = path.substr(1, path.size() - 2);
    DWORD needed = ExpandEnvironmentStringsW(path.c_str(), nullptr, 0);
    if (needed) {
        std::vector<wchar_t> expanded(needed, L'\0');
        DWORD written = ExpandEnvironmentStringsW(path.c_str(), expanded.data(), needed);
        if (written && written <= needed) path.assign(expanded.data());
    }

    if (path.find_first_of(L"\\/:") == std::wstring::npos) {
        std::vector<wchar_t> found(32768, L'\0');
        DWORD len = SearchPathW(nullptr, path.c_str(), nullptr,
                                static_cast<DWORD>(found.size()), found.data(), nullptr);
        if (!len || len >= found.size()) return false;
        path.assign(found.data(), len);
    }

    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    *resolved = std::move(path);
    return true;
}

bool ExecutableFromOpenCommand(const std::wstring& command, std::wstring* executable,
                               std::vector<std::wstring>* arguments)
{
    if (command.empty()) return false;
    DWORD expandedLength = ExpandEnvironmentStringsW(command.c_str(), nullptr, 0);
    std::wstring expandedCommand = command;
    if (expandedLength) {
        std::vector<wchar_t> expanded(expandedLength, L'\0');
        DWORD written = ExpandEnvironmentStringsW(command.c_str(), expanded.data(), expandedLength);
        if (written && written <= expandedLength) expandedCommand.assign(expanded.data());
    }
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(expandedCommand.c_str(), &argc);
    if (!argv || argc < 1) {
        if (argv) LocalFree(argv);
        return false;
    }

    std::wstring candidate = argv[0];
    std::vector<std::wstring> parsedArguments;
    for (int i = 1; i < argc; ++i)
        parsedArguments.emplace_back(argv[i]);
    LocalFree(argv);

    if (!ResolveExecutable(std::move(candidate), executable))
        return false;
    *arguments = std::move(parsedArguments);
    return true;
}

void AddBrowser(std::vector<BrowserEntry>* browsers, const std::wstring& name,
                const std::wstring& candidatePath, const std::vector<std::wstring>& arguments)
{
    std::wstring path;
    if (!ResolveExecutable(candidatePath, &path)) return;
    for (const auto& browser : *browsers) {
        if (CompareStringOrdinal(browser.path.c_str(), -1, path.c_str(), -1, TRUE) == CSTR_EQUAL)
            return;
    }
    browsers->push_back({name.empty() ? path : name, std::move(path), arguments});
}

void AddBrowserFromCommand(std::vector<BrowserEntry>* browsers, const std::wstring& name,
                           const std::wstring& command)
{
    std::wstring executable;
    std::vector<std::wstring> arguments;
    if (ExecutableFromOpenCommand(command, &executable, &arguments))
        AddBrowser(browsers, name, executable, arguments);
}

void EnumerateStartMenuInternet(HKEY root, REGSAM view, std::vector<BrowserEntry>* browsers)
{
    const wchar_t* base = L"SOFTWARE\\Clients\\StartMenuInternet";
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, base, 0, KEY_READ | view, &key) != ERROR_SUCCESS)
        return;

    for (DWORD index = 0; ; ++index) {
        wchar_t child[512] = {};
        DWORD childLen = 512;
        LONG rc = RegEnumKeyExW(key, index, child, &childLen, nullptr, nullptr, nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS) continue;

        std::wstring appKey = std::wstring(base) + L"\\" + child;
        std::wstring displayName;
        if (!ReadRegistryString(root, appKey, view, nullptr, &displayName) || displayName.empty())
            displayName = child;

        std::wstring command;
        std::wstring commandKey = appKey + L"\\shell\\open\\command";
        if (ReadRegistryString(root, commandKey, view, nullptr, &command))
            AddBrowserFromCommand(browsers, displayName, command);
    }
    RegCloseKey(key);
}

void EnumerateRegisteredApplications(HKEY root, REGSAM view, std::vector<BrowserEntry>* browsers)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, L"SOFTWARE\\RegisteredApplications", 0, KEY_READ | view, &key) != ERROR_SUCCESS)
        return;

    for (DWORD index = 0; ; ++index) {
        wchar_t appName[512] = {};
        wchar_t capabilities[2048] = {};
        DWORD nameChars = 512;
        DWORD dataBytes = sizeof(capabilities);
        DWORD type = 0;
        LONG rc = RegEnumValueW(key, index, appName, &nameChars, nullptr, &type,
                                reinterpret_cast<BYTE*>(capabilities), &dataBytes);
        if (rc == ERROR_NO_MORE_ITEMS) break;
        if (rc != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) continue;

        std::wstring capabilitiesPath(capabilities);
        if (capabilitiesPath.empty()) continue;
        if (capabilitiesPath.size() >= 8 &&
            CompareStringOrdinal(capabilitiesPath.data(), 8, L"SOFTWARE", 8, TRUE) == CSTR_EQUAL) {
            // Already rooted at SOFTWARE.
        } else if (capabilitiesPath.size() >= 8 &&
                   CompareStringOrdinal(capabilitiesPath.data(), 8, L"Software", 8, TRUE) == CSTR_EQUAL) {
            capabilitiesPath = L"SOFTWARE\\" + capabilitiesPath.substr(9);
        } else {
            capabilitiesPath = L"SOFTWARE\\" + capabilitiesPath;
        }
        if (type == REG_EXPAND_SZ) {
            DWORD needed = ExpandEnvironmentStringsW(capabilitiesPath.c_str(), nullptr, 0);
            if (needed) {
                std::vector<wchar_t> expanded(needed, L'\0');
                DWORD written = ExpandEnvironmentStringsW(capabilitiesPath.c_str(), expanded.data(), needed);
                if (written && written <= needed) capabilitiesPath.assign(expanded.data());
            }
        }

        std::wstring capabilitiesKey = capabilitiesPath;
        const std::wstring suffix = L"\\Capabilities";
        if (capabilitiesKey.size() < suffix.size() ||
            CompareStringOrdinal(capabilitiesKey.data() + capabilitiesKey.size() - suffix.size(),
                                 static_cast<int>(suffix.size()), suffix.c_str(),
                                 static_cast<int>(suffix.size()), TRUE) != CSTR_EQUAL)
            capabilitiesKey += suffix;

        std::wstring displayName;
        if (!ReadRegistryString(root, capabilitiesKey, view, L"ApplicationName", &displayName) || displayName.empty())
            displayName = appName;

        const wchar_t* schemes[] = {L"http", L"https"};
        for (const wchar_t* scheme : schemes) {
            std::wstring progId;
            if (!ReadRegistryString(root, capabilitiesKey + L"\\UrlAssociations",
                                    view, scheme, &progId) || progId.empty())
                continue;
            std::wstring command;
            std::wstring commandKey = L"SOFTWARE\\Classes\\" + progId + L"\\shell\\open\\command";
            if (ReadRegistryString(root, commandKey, view, nullptr, &command))
                AddBrowserFromCommand(browsers, displayName, command);
        }
    }
    RegCloseKey(key);
}

void EnumerateKnownAppPaths(HKEY root, REGSAM view, std::vector<BrowserEntry>* browsers)
{
    struct KnownBrowser { const wchar_t* exe; const wchar_t* name; };
    static const KnownBrowser known[] = {
        {L"msedge.exe", L"Microsoft Edge"},
        {L"chrome.exe", L"Google Chrome"},
        {L"firefox.exe", L"Mozilla Firefox"},
        {L"brave.exe", L"Brave"},
        {L"opera.exe", L"Opera"},
        {L"vivaldi.exe", L"Vivaldi"},
        {L"iexplore.exe", L"Internet Explorer"}
    };
    const std::wstring base = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\";
    for (const auto& item : known) {
        std::wstring path;
        if (ReadRegistryString(root, base + item.exe, view, nullptr, &path))
            AddBrowser(browsers, item.name, path, {});
    }
}

std::vector<BrowserEntry> FindInstalledBrowsers()
{
    std::vector<BrowserEntry> browsers;
    const REGSAM views[] = { KEY_WOW64_64KEY, KEY_WOW64_32KEY };
    const HKEY roots[] = { HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE };
    for (HKEY root : roots) {
        for (REGSAM view : views) {
            EnumerateStartMenuInternet(root, view, &browsers);
            EnumerateRegisteredApplications(root, view, &browsers);
            EnumerateKnownAppPaths(root, view, &browsers);
        }
    }
    // Future portable-browser path settings can append explicit BrowserEntry
    // values here without changing default URL associations.
    std::sort(browsers.begin(), browsers.end(), [](const BrowserEntry& a, const BrowserEntry& b) {
        return CompareStringOrdinal(a.name.c_str(), -1, b.name.c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    return browsers;
}

std::wstring QuoteArgument(const std::wstring& argument)
{
    std::wstring quoted(1, L'"');
    size_t slashes = 0;
    for (wchar_t ch : argument) {
        if (ch == L'\\') {
            ++slashes;
        } else if (ch == L'"') {
            quoted.append(slashes * 2 + 1, L'\\');
            quoted.push_back(ch);
            slashes = 0;
        } else {
            quoted.append(slashes, L'\\');
            slashes = 0;
            quoted.push_back(ch);
        }
    }
    quoted.append(slashes * 2, L'\\');
    quoted.push_back(L'"');
    return quoted;
}

bool ReplaceUrlSlots(std::wstring* argument, const std::wstring& url)
{
    bool replaced = false;
    const wchar_t* slots[] = {L"%1", L"%L", L"%l", L"%*"};
    for (const wchar_t* slot : slots) {
        size_t at = 0;
        while ((at = argument->find(slot, at)) != std::wstring::npos) {
            argument->replace(at, 2, url);
            at += url.size();
            replaced = true;
        }
    }
    return replaced;
}

void ShowLaunchError(HWND owner, const wchar_t* message, DWORD error)
{
    wchar_t text[512];
    swprintf(text, 512, message, static_cast<unsigned long>(error));
    MessageBoxW(owner, text, S_MSG_TITLE, MB_OK | MB_ICONERROR);
}

std::wstring GetMapHelperPath(std::wstring* helperDirectory)
{
    std::vector<wchar_t> buffer(32768, L'\0');
    DWORD length = GetModuleFileNameW(g_hInst, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length >= buffer.size()) return {};
    std::wstring modulePath(buffer.data(), length);
    size_t slash = modulePath.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return {};
    std::wstring dllDirectory = modulePath.substr(0, slash);
    *helperDirectory = dllDirectory + L"\\map-probe";
    return *helperDirectory + L"\\FileFolderInfoMap.exe";
}

bool LaunchBuiltIn(HWND owner, const std::wstring& url)
{
    std::wstring helperDirectory;
    std::wstring helperPath = GetMapHelperPath(&helperDirectory);
    DWORD attributes = helperPath.empty() ? INVALID_FILE_ATTRIBUTES : GetFileAttributesW(helperPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY)) {
        std::wstring message = std::wstring(S_MAP_HELPER_NOT_FOUND) + L"\n\n" +
            (helperPath.empty() ? std::wstring() : helperPath + L"\n\n") + S_MAP_HELPER_EXTRACT_HINT;
        MessageBoxW(owner, message.c_str(), S_MSG_TITLE, MB_OK | MB_ICONERROR);
        return false;
    }

    std::wstring commandLine = QuoteArgument(helperPath) + L" " + QuoteArgument(url);
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(helperPath.c_str(), &commandLine[0], nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, nullptr, helperDirectory.c_str(),
                        &startup, &process)) {
        ShowLaunchError(owner, S_MAP_HELPER_START_ERROR_FMT, GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

void LaunchInBrowser(HWND owner, const BrowserEntry& browser, const std::wstring& url)
{
    std::vector<std::wstring> arguments = browser.arguments;
    bool hasUrlSlot = false;
    for (auto& argument : arguments)
        hasUrlSlot = ReplaceUrlSlots(&argument, url) || hasUrlSlot;
    if (!hasUrlSlot) arguments.push_back(url);

    std::wstring commandLine = QuoteArgument(browser.path);
    for (const auto& argument : arguments)
        commandLine += L" " + QuoteArgument(argument);

    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    if (!CreateProcessW(browser.path.c_str(), &commandLine[0], nullptr, nullptr, FALSE,
                        CREATE_UNICODE_ENVIRONMENT, nullptr, nullptr, &startup, &process)) {
        wchar_t text[512];
        swprintf(text, 512, S_MAP_BROWSER_START_ERROR_FMT, browser.name.c_str(),
                 static_cast<unsigned int>(GetLastError()));
        MessageBoxW(owner, text, S_MSG_TITLE, MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}

} // namespace

void OpenMapUrl(HWND owner, HWND anchor, const std::wstring& url)
{
    std::vector<BrowserEntry> browsers = FindInstalledBrowsers();
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    AppendMenuW(menu, MF_STRING, ID_OPEN_BUILTIN, S_MAP_OPEN_BUILTIN);
    AppendMenuW(menu, MF_STRING, ID_OPEN_DEFAULT, S_MAP_OPEN_DEFAULT);

    HMENU browserMenu = CreatePopupMenu();
    if (browserMenu) {
        if (browsers.empty()) {
            AppendMenuW(browserMenu, MF_STRING | MF_GRAYED, 0, S_MAP_NO_BROWSERS);
        } else {
            for (size_t i = 0; i < browsers.size() && i < 1000; ++i) {
                UINT id = ID_FIRST_BROWSER + static_cast<UINT>(i);
                AppendMenuW(browserMenu, MF_STRING, id, browsers[i].name.c_str());
            }
        }
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(browserMenu), S_MAP_OPEN_OTHER);
    }

    POINT point = {};
    RECT rect = {};
    if (anchor && GetWindowRect(anchor, &rect)) {
        point.x = rect.left;
        point.y = rect.bottom;
    } else {
        GetCursorPos(&point);
    }

    SetForegroundWindow(owner);
    UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON,
                                 point.x, point.y, 0, owner, nullptr);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);

    if (choice == ID_OPEN_BUILTIN) {
        LaunchBuiltIn(owner, url);
    } else if (choice == ID_OPEN_DEFAULT) {
        ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    } else if (choice >= ID_FIRST_BROWSER && choice < ID_FIRST_BROWSER + 1000) {
        size_t index = choice - ID_FIRST_BROWSER;
        if (index < browsers.size()) LaunchInBrowser(owner, browsers[index], url);
    }
}
