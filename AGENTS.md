# FileFolderInfo — заметки для агента

Расширение контекстного меню Windows Explorer (метаданные, зависимости DLL,
поворот JPEG, EXIF, ярлыки, карта по GPS). Кросс-компиляция MinGW-w64 с Linux,
сейчас только x64.

## Сборка (Linux → Windows)

Песочница часто сбрасывается — тулчейн надо ставить заново:

```
sudo apt-get update -qq
sudo apt-get install -y --fix-missing mingw-w64 mingw-w64-tools zip unzip
make           # dist/FileFolderInfo.dll
make probe     # dist/ultralight_probe.exe (офф-скрин карта Ultralight)
make package   # dist/ultralight_test.zip (exe+DLL+resources+page) и dist/src.zip
```

`--fix-missing` нужен: snapshot.debian.org периодически отдаёт 503/504.

## Проверка после сборки

```
x86_64-w64-mingw32-objdump -p dist/FileFolderInfo.dll | grep -E "DllCanUnloadNow|DllGetClassObject|DllRegisterServer|DllUnregisterServer"
x86_64-w64-mingw32-objdump -p dist/FileFolderInfo.dll | grep "DLL Name:"
```

Ожидаемо: 4 экспорта; импорты только штатные системные
(ADVAPI32, GDI32, KERNEL32, PROPSYS, SHELL32, SHLWAPI, USER32, msi.dll,
msvcrt, ole32). Ultralight подгружается динамически и в импортах не виден.

## Ключевые уроки

- `ContextMenuHandlers` работает только из HKLM (нужны права администратора).
- MinGW wide-printf: `%ls` для `wchar_t*`, не `%s`.
- Property System: `S_OK` может вернуть пустой PROPVARIANT — проверяй `pv.vt`.
- Проводник для `.lnk` создаёт второй инстанс расширения на цель ярлыка с
  флагом `CMF_VERBOSONLY` — в `QueryContextMenu` выходим сразу.
  `GetCommandString` строковый verb не публикует.
- `make clean` не должен сносить `dist/` — там tracked README/install/uninstall.
- Внутри расширения остаётся временная диагностика `DiagLog` →
  `%TEMP%\FileFolderInfo_diag.log` (metadata.cpp/shellext.cpp) — убрать,
  когда всё стабилизируется.

## Ultralight (карта)

- SDK в репо: `inc/ultralight/{bin/*.dll,resources/,license/,заголовки}`.
- Проба: `test/ultralight_probe.cpp`, страница `test/leaflet_probe_page/`.
- Схема: `LoadLibrary`/`GetProcAddress` (MSVC-.lib с MinGW не дружат), чистый
  C API, офф-скрин CPU-рендер (`ulCreateRenderer`), битмапа через GDI
  `StretchDIBits`. ОБЯЗАТЕЛЬНО `ulEnablePlatformFontLoader` и
  `ulEnablePlatformFileSystem(".")` до создания рендерера, иначе ICU не грузится.
- Диагностика пробы (env-флаги): `FFI_DIAG=1` (лог хешей/JS), 
  `FFI_FORCE_REPAINT=1` (`Config::force_repaint`), `FFI_NOANIM=1` (выключить
  `zoomAnimation` у Leaflet). Лог — `ultralight_probe_input.log` рядом с exe.
- Зум колесом: Leaflet 1.9.4 применяет его НЕ сразу —
  `setTimeout(_performZoom, wheelDebounceTime=40)` + CSS-transition ~250мс.
  Мгновенная перерисовка после `WM_MOUSEWHEEL` зум не поймает.
- Wine не годится для отладки: WebKit падает в page fault (проверено).

## Git

- Рабочая ветка — `openhands-dev` (не пушить в `main` без явной просьбы).
- user.name `mcinrepli`, user.email
  `300741771+mcinrepli@users.noreply.github.com` (слетает при сбросе песочницы).
- `credential.helper=store` не работает — подставлять токен в URL remote.
