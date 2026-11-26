#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#define NTDDI_VERSION 0x0A000000

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <string>
#include <vector>

// {F4DE0D56-C976-41E6-801C-D83B57C6846B}
extern const CLSID CLSID_FileFolderInfoExt;

extern LONG g_cDllRef;      // счётчик активных объектов модуля
extern HINSTANCE g_hInst;   // handle модуля (нашей DLL)

// Порядок пунктов подменю. Значения — смещения относительно idCmdFirst.
enum CommandId : UINT {
    CMD_COPY_PATH = 0,
    CMD_COPY_NAME,
    CMD_COPY_DATE_MODIFIED,
    CMD_COPY_DATE_CREATED,
    CMD_COPY_DATE_ACCESSED,
    CMD_TREE_PATHS,
    CMD_TREE_NAMES,
    CMD_SHOW_SIZE,
    CMD_METADATA,
    CMD_DEPENDENCIES,
    CMD_RENAME_COPY_NUMBER,
    CMD_RENAME_COPY_DATE,
    CMD_APPEND_DATE,
    CMD_APPEND_EXIF_DATE,
    CMD_FIX_DATES_FROM_EXIF,
    CMD_CLONE_DATE_MODIFIED,
    CMD_CLONE_DATE_CREATED,
    CMD_CLONE_NUMBERED,
    CMD_ROTATE_LEFT,
    CMD_ROTATE_RIGHT,
    CMD_COUNT
};

// Какие временные штампы копируем/используем
enum class TimeKind { Modified, Created, Accessed };

struct SelectionInfo {
    std::vector<std::wstring> paths;
    bool allFiles = true;
    bool allFolders = true;
};

// ---- util.cpp ----
SelectionInfo GetSelectionInfo(IDataObject* pDataObj);
void CopyTextToClipboard(HWND hwnd, const std::wstring& text);
unsigned long long GetPathSizeRecursive(const std::wstring& path, bool* pAnyError = nullptr);
std::wstring FormatPlainNumber(unsigned long long value);              // без разделителей разрядов
std::wstring FormatFixed(double value, int decimals);                  // без разделителей разрядов
std::wstring RussianMonthGenitive(int month1to12);
bool GetFileTimes(const std::wstring& path, FILETIME* ftCreate, FILETIME* ftAccess, FILETIME* ftWrite);
void SystemTimeLocalFromFileTime(const FILETIME& ft, SYSTEMTIME* stLocal);
bool FileTimeUtcFromSystemTimeLocal(const SYSTEMTIME& stLocal, FILETIME* ftUtc);
std::wstring FormatDateVariant(const SYSTEMTIME& st, int variant); // variant 1..5, см. commands.cpp

// Разбор имени файла/папки на (базовое_имя, расширение, число повторов суффикса "— копия")
struct CopySuffixInfo {
    std::wstring base;   // имя без расширения (для папок — целиком) и без суффиксов копий
    std::wstring ext;    // расширение файла вместе с точкой, например ".png"; для папок всегда пусто
    int repeats = 0;     // сколько раз подряд встретился суффикс " — копия"
};
CopySuffixInfo ParseCopySuffix(const std::wstring& name, bool isFolder);

// Открыть GPS URL: встроенный Ultralight-помощник, системный браузер или выбранный браузер.
void OpenMapUrl(HWND owner, HWND anchor, const std::wstring& url);

// Показ окошка с несколькими строками "подпись: значение", каждая строка - копируемое поле
struct InfoRow { std::wstring label; std::wstring value; int action = 0; int extraLines = 0; };
// action: 0 = нет кнопки; 1 = открыть ShellExecute; 2 = показать в папке; 3 = меню GPS-ссылки

// Возврат: 0 = закрыто, 1 = нажали «Далее», -1 = нажали «Назад».
// showNav включает кнопки навигации; enablePrev/enableNext управляют их доступностью.
int ShowInfoDialog(HWND owner, const std::wstring& title, const std::vector<InfoRow>& rows,
                   bool showNav = false, bool enablePrev = false, bool enableNext = false);

// ---- commands.cpp: обработчики 9 пунктов меню ----
void Cmd_CopyPath(HWND owner, const SelectionInfo& sel);
void Cmd_CopyName(HWND owner, const SelectionInfo& sel);
void Cmd_ShowSize(HWND owner, const SelectionInfo& sel);
void Cmd_DateModified(HWND owner, const SelectionInfo& sel);
void Cmd_DateCreated(HWND owner, const SelectionInfo& sel);
void Cmd_DateAccessed(HWND owner, const SelectionInfo& sel);
void Cmd_RenameCopyNumber(HWND owner, const SelectionInfo& sel);
void Cmd_RenameCopyDate(HWND owner, const SelectionInfo& sel);
void Cmd_AppendDate(HWND owner, const SelectionInfo& sel);
void Cmd_AppendExifDate(HWND owner, const SelectionInfo& sel);
void Cmd_FixDatesFromExif(HWND owner, const SelectionInfo& sel);
void Cmd_CloneDateModified(HWND owner, const SelectionInfo& sel);
void Cmd_CloneDateCreated(HWND owner, const SelectionInfo& sel);
void Cmd_CloneNumbered(HWND owner, const SelectionInfo& sel);
void Cmd_TreePaths(HWND owner, const SelectionInfo& sel);
void Cmd_TreeNames(HWND owner, const SelectionInfo& sel);
void Cmd_ShowMetadata(HWND owner, const SelectionInfo& sel);
void Cmd_ShowDependencies(HWND owner, const SelectionInfo& sel);
void Cmd_RotateLeft(HWND owner, const SelectionInfo& sel);
void Cmd_RotateRight(HWND owner, const SelectionInfo& sel);

// ---- peinfo.cpp: разбор PE-заголовка (exe/dll) ----
bool GetPeArchAndDotNetInfo(const std::wstring& path, std::wstring* archText, std::wstring* dotNetVersionText);

// ---- rotate.cpp: EXIF (JPEG) ----
bool FindExifDateTaken(const std::wstring& path, SYSTEMTIME* outLocal); // DateTimeOriginal/DateTimeDigitized/DateTime
bool FindExifGPS(const std::wstring& path, double* outLat, double* outLon); // GPSLatitude/GPSLongitude

// ---- metadata.cpp: разбор ярлыков (.lnk) — ручной разбор по MS-SHLLINK, без COM/IShellLinkW ----
struct ShortcutInfo {
    bool isShortcut = false;
    bool resolved = false;          // сам файл .lnk прочитан и разобран как ShellLink
    bool hasLocalPath = false;      // нашли LocalBasePath (обычный файл/папка на диске)
    bool isVirtualLocation = false; // есть только IDList, LinkInfo нет — системное расположение (Панель управления и т.п.)
    bool isMsiAdvertisedUnresolved = false; // MSI advertised-ярлык (HasDarwinID), компонент не установлен локально
    bool targetExists = false;
    bool targetIsFolder = false;
    std::wstring targetPath;
};
ShortcutInfo ResolveShortcut(const std::wstring& lnkPath);

// ---- shellext.h/.cpp ----
class CShellExt; // forward
