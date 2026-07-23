#!/usr/bin/env python3
# -*- coding: utf-8 -*-

def esc(s: str) -> str:
    out = []
    for ch in s:
        cp = ord(ch)
        if 32 <= cp <= 126 and ch not in ('\\', '"'):
            out.append(ch)
        elif ch == '\\':
            out.append('\\\\')
        elif ch == '"':
            out.append('\\"')
        elif ch == '\n':
            out.append('\\n')
        else:
            out.append('\\u%04X' % cp)
    return ''.join(out)

STRINGS = {
    "MENU_TOP": "Информация о файле/папке",
    "ITEM_COPY_SUBMENU": "Скопировать",

    "ITEM_COPY_PATH_FILE_1": "Путь к файлу",
    "ITEM_COPY_PATH_FILE_N": "Пути к файлам",
    "ITEM_COPY_PATH_FOLDER_1": "Путь к папке",
    "ITEM_COPY_PATH_FOLDER_N": "Пути к папкам",
    "ITEM_COPY_PATH_MIXED": "Пути",

    "ITEM_COPY_NAME_FILE_1": "Имя файла",
    "ITEM_COPY_NAME_FILE_N": "Имена файлов",
    "ITEM_COPY_NAME_FOLDER_1": "Имя папки",
    "ITEM_COPY_NAME_FOLDER_N": "Имена папок",
    "ITEM_COPY_NAME_MIXED": "Имена",

    "ITEM_SHOW_SIZE": "Показать размер...",
    "ITEM_METADATA": "Метаданные...",
    "ITEM_DEPENDENCIES": "Зависимости (DLL)...",
    "ITEM_DATE_MODIFIED": "Дату изменения...",
    "ITEM_DATE_CREATED": "Дату создания...",
    "ITEM_DATE_ACCESSED": "Дату доступа...",
    "ITEM_TREE_PATHS": "Дерево путей",
    "ITEM_TREE_NAMES": "Дерево имён",

    "ITEM_RENAME_NUMBER": "Пронумеровать копии (\u2014 копия \u2192 -N)",
    "ITEM_RENAME_DATE": "Переименовать копии по дате изменения",
    "ITEM_APPEND_DATE": "Добавить дату изменения к имени",

    "ITEM_CLONE_SUBMENU": "Клонировать",
    "ITEM_CLONE_DATE_MODIFIED": "С датой изменения в имени",
    "ITEM_CLONE_DATE_CREATED": "С датой создания в имени",
    "ITEM_CLONE_NUMBERED": "С порядковым номером",

    "ITEM_ROTATE_SUBMENU": "Повернуть",
    "ITEM_ROTATE_LEFT": "Влево на 90\u00b0",
    "ITEM_ROTATE_RIGHT": "Вправо на 90\u00b0",

    "HELP_COPY_PATH": "Скопировать в буфер обмена полный путь выбранных объектов",
    "HELP_COPY_NAME": "Скопировать в буфер обмена имена выбранных объектов",
    "HELP_SHOW_SIZE": "Показать точный размер в байтах, КБ, МБ и ГБ",
    "HELP_METADATA": "Показать метаданные (дата съёмки, камера, GPS и т.д.)",
    "HELP_DEPENDENCIES": "Показать, какие DLL нужны этому exe/dll, включая переносные",
    "HELP_DATE": "Скопировать дату/время в буфер обмена в разных форматах",
    "HELP_RENAME_NUMBER": "Заменить хвост \u00ab\u2014 копия\u00bb на -1, -2, -3...",
    "HELP_RENAME_DATE": "Заменить хвост \u00ab\u2014 копия\u00bb на дату изменения файла",
    "HELP_APPEND_DATE": "Добавить дату изменения в конец имени",
    "HELP_CLONE_DATE_MODIFIED": "Создать копию рядом, добавив дату изменения к имени",
    "HELP_CLONE_DATE_CREATED": "Создать копию рядом, добавив дату создания к имени",
    "HELP_CLONE_NUMBERED": "Создать копию рядом с порядковым номером (_1, _2, _3...)",
    "HELP_ROTATE_LEFT": "Повернуть изображение на 90\u00b0 влево, даты файла не меняются",
    "HELP_ROTATE_RIGHT": "Повернуть изображение на 90\u00b0 вправо, даты файла не меняются",
    "HELP_TREE_PATHS": "Скопировать пути всех файлов и папок внутри (рекурсивно)",
    "HELP_TREE_NAMES": "Скопировать только имена всех файлов и папок внутри (рекурсивно)",

    "DLG_TITLE_SIZE": "Размер",
    "DLG_TITLE_METADATA": "Метаданные",
    "DLG_TITLE_DEPENDENCIES": "Зависимости (DLL)",
    "DLG_TITLE_MODIFIED": "Дата изменения",

    "META_DATE_TAKEN": "Дата съёмки",
    "META_GPS_LAT": "GPS широта",
    "META_GPS_LON": "GPS долгота",
    "META_CAMERA_MAKER": "Производитель камеры",
    "META_CAMERA_MODEL": "Модель камеры",
    "META_FNUMBER": "Диафрагма",
    "META_EXPOSURE": "Выдержка",
    "META_ISO": "ISO",
    "META_FOCAL": "Фокусное расстояние",
    "META_WIDTH": "Ширина, пикс.",
    "META_HEIGHT": "Высота, пикс.",
    "META_RESOLUTION": "Разрешение",
    "META_DURATION": "Длительность",
    "META_ARTIST": "Исполнитель",
    "META_ALBUM": "Альбом",
    "META_YEAR": "Год",
    "META_SUBFOLDERS": "Подпапок",
    "EXT_NONE": "(без расширения)",

    "META_SHORTCUT_TARGET": "Ярлык ведёт на",
    "META_SHORTCUT_STATUS": "Состояние ярлыка",
    "META_SHORTCUT_REVEAL_LABEL": "Расположение файла",
    "SHORTCUT_ALIVE": "рабочий \u2014 файл существует",
    "SHORTCUT_DEAD": "СЛОМАН \u2014 файл по этому пути не найден",
    "SHORTCUT_UNREADABLE": "не удалось прочитать ярлык (повреждён?)",
    "META_BITRATE_TYPE": "Тип битрейта",
    "META_VBR": "Переменный (VBR)",
    "META_CBR": "Постоянный (CBR)",
    "META_AUDIO_BITRATE": "Битрейт аудио",
    "META_VIDEO_BITRATE": "Битрейт видео",
    "META_TOTAL_BITRATE": "Общий битрейт потока",
    "META_MAP_OSM": "OpenStreetMap",
    "META_MAP_GOOGLE": "Google Maps",
    "META_MAP_YANDEX": "Яндекс.Карты",
    "META_MAP_PASTVU": "PastVu (архивные фото места)",

    "META_ENCODING": "Кодировка текста",
    "ENC_UTF8_BOM": "UTF-8 с BOM",
    "ENC_UTF8_NOBOM": "UTF-8 без BOM",
    "ENC_ASCII": "ASCII (только латиница/цифры \u2014 неотличимо от UTF-8 без BOM)",
    "ENC_UTF16_LE": "UTF-16 LE (с BOM)",
    "ENC_UTF16_BE": "UTF-16 BE (с BOM)",
    "ENC_UTF32_LE": "UTF-32 LE (с BOM)",
    "ENC_UTF32_BE": "UTF-32 BE (с BOM)",
    "ENC_UNKNOWN": "Не UTF-8 \u2014 вероятно однобайтовая кодировка (Windows-1251, CP866 и т.п.)",
    "ENC_EMPTY": "Файл пуст",
    "MSG_NO_METADATA": "Метаданные не найдены (дата съёмки, GPS, камера и т.д. отсутствуют в этом файле).",
    "MSG_NOT_PE": "Это не похоже на исполняемый PE-файл Windows (exe/dll).",

    "META_ARCH": "Разрядность",
    "ARCH_X86": "x86 (32-бит)",
    "ARCH_X64": "x64 (64-бит)",
    "ARCH_ARM64": "ARM64",
    "ARCH_ARM": "ARM (32-бит)",
    "ARCH_ANYCPU": "AnyCPU (.NET) \u2014 на этой системе выполнится как x64",
    "ARCH_ANYCPU_32PREFERRED": "AnyCPU, но с флагом \u00abПредпочитать 32-бит\u00bb \u2014 выполнится как x86",
    "ARCH_UNKNOWN_PREFIX": "неизвестная (машинный код 0x",
    "META_DOTNET_VERSION": "Требуемая версия .NET Framework",

    "DEP_NOT_FOUND": "не найдена на этой системе",
    "DEP_APISET": "системный API-набор (виртуальный, не файл)",
    "DEP_FOUND": "найдена",
    "DEP_DELAYLOAD": " [отложенная загрузка]",
    "DEP_CAP_REACHED": "(достигнут предел в 200 файлов \u2014 дерево обрублено для безопасности)",
    "DLG_TITLE_CREATED": "Дата создания",
    "DLG_TITLE_ACCESSED": "Дата доступа",

    "ROW_BYTES": "Байты",
    "ROW_KB": "Килобайты (КБ)",
    "ROW_MB": "Мегабайты (МБ)",
    "ROW_GB": "Гигабайты (ГБ)",
    "ROW_COUNT": "Выбрано объектов",

    "ROW_FORMAT_PREFIX": "Формат",

    "BTN_COPY": "Копировать",
    "BTN_OPEN": "Открыть",
    "BTN_REVEAL": "Показать в папке",
    "BTN_CLOSE": "Закрыть",

    "YEAR_LOWER": "года",
    "YEAR_UPPER": "Года",

    "MSG_TITLE": "Информация о файле/папке",
    "MSG_NOTHING_SELECTED": "Нет выбранных объектов.",
    "MSG_RENAME_DONE_FMT": "Готово.\nПереименовано: %d\nПропущено: %d",
    "MSG_CLONE_DONE_FMT": "Готово.\nСоздано копий: %d\nПропущено: %d",
    "MSG_CLONE_ERROR": "Не удалось создать объект копирования (IFileOperation).",
    "MSG_ROTATE_DONE_FMT": "Готово.\nПовёрнуто: %d\nПропущено: %d",
    "MSG_TREE_EMPTY": "Внутри пусто \u2014 нечего копировать.",
    "MSG_ERR_TIME_PREFIX": "Не удалось прочитать время файла:",
    "MSG_MIXED_WARN": "В выделении есть и файлы, и папки \u2014 каждый объект обрабатывается по своему типу (расширение сохраняется только у файлов).",

    "COPY_SUFFIX": " \u2014 копия",
}

MONTHS_LOWER = ["января","февраля","марта","апреля","мая","июня","июля","августа","сентября","октября","ноября","декабря"]
MONTHS_UPPER = ["Января","Февраля","Марта","Апреля","Мая","Июня","Июля","Августа","Сентября","Октября","Ноября","Декабря"]

lines = []
lines.append("// АВТОГЕНЕРИРОВАНО generate_strings.py — не редактировать руками, править словарь в скрипте")
lines.append("#pragma once")
lines.append('#include <windows.h>')
lines.append("")
for k, v in STRINGS.items():
    lines.append(f'static const wchar_t* S_{k} = L"{esc(v)}";')

lines.append("")
lines.append("static const wchar_t* MONTHS_GEN_LOWER[12] = {")
lines.append("    " + ", ".join(f'L"{esc(m)}"' for m in MONTHS_LOWER))
lines.append("};")
lines.append("static const wchar_t* MONTHS_GEN_UPPER[12] = {")
lines.append("    " + ", ".join(f'L"{esc(m)}"' for m in MONTHS_UPPER))
lines.append("};")
lines.append("")

with open("/home/claude/fileinfo/inc/strings.h", "w", encoding="utf-8") as f:
    f.write("\n".join(lines) + "\n")

print("done, lines:", len(lines))
