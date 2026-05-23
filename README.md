# Отчёт по заданию "Исследование файлов Program Compatibility Assistant в Windows 11"

## 1. Анализ структур данных

### 1.1 Файл `PcaAppLaunchDic.txt`

**формат:** `путь_к_программе|время_запуска`

**пример строки:**

> `C:\Program Files\7-Zip\7zFM.exe|2026-05-11 08:50:16.845`

**разбор полей:**

| поле | тип | пример | описание |
|------|-----|--------|----------|
| путь_к_программе | string | `C:\Program Files\7-Zip\7zFM.exe` | полный путь к исполняемому файлу |
| время_запуска | datetime | `2026-05-11 08:50:16.845` | временная метка последнего запуска (формат: гггг-мм-дд чч:мм:сс.мсс) |

**особенности:**

- разделитель полей: символ `|` (pipe)
- временная метка в utc
- файл обновляется при каждом запуске программы через проводник

---

### 1.2 Файл `PcaGeneralDb0.txt` / `PcaGeneralDb1.txt`

**формат:** `время|тип|путь|имя|издатель|версия|хэш|статус`

**пример строки:**

> `2026-05-11 08:50:03.563|0|%USERPROFILE%\Downloads\7z2601-x64.exe|7-zip|igor pavlov|26.01|000685542553d402da51cc742b6975b9a49900000904|Installer failed`

**разбор полей:**

| поле | тип | пример | описание |
|------|-----|--------|----------|
| время | datetime | `2026-05-11 08:50:03.563` | временная метка события |
| тип | int (0-3) | `0` | тип события (см. таблицу ниже) |
| путь | string | `%USERPROFILE%\Downloads\7z2601-x64.exe` | путь к программе (может содержать переменные окружения) |
| имя | string | `7-zip` | отображаемое имя программы |
| издатель | string | `igor pavlov` | издатель/разработчик |
| версия | string | `26.01` | версия программы |
| хэш | string | `000685542553d4...` | хэш-отпечаток (уникальный идентификатор) |
| статус | string | `Installer failed` | статус завершения / сообщение об ошибке |

**типы событий:**

| значение | название | описание |
|----------|----------|----------|
| 0 | installer | установщик завершился с ошибкой или был прерван |
| 1 | driverblocked | драйвер заблокирован (hvci/cet) |
| 2 | abnormalexit | программа завершилась аномально (ненулевой код возврата) |
| 3 | compatissue | обнаружена проблема совместимости |

**особенности:**

- разделитель полей: символ `|` (pipe)
- файлы чередуются при достижении размера 2 мб
- поле `хэш` может использоваться для устранения дубликатов
- пути могут содержать переменные окружения (`%userprofile%`, `%programfiles%`)

---

## 2. Архитектура программы

### 2.1 общая структура

> ```
> main.cpp
> ├── структуры данных (LaunchRecord, GeneralRecord)
> ├── функции парсинга (trim, cleanString, extractNumber...)
> ├── работа с бд (initDatabase, insert..., closeDatabase)
> ├── парсинг файлов (parseLaunchFile, parseGeneralFile)
> ├── статистика (printStatisticsFromDB)
> ├── вывод в файл (writeToFile)
> └── main()
> ```

### 2.2 компоненты

| компонент | назначение |
|-----------|------------|
| `trim()` | удаление пробелов и табуляции в начале и конце строки |
| `cleanString()` | оставление только печатных ascii символов |
| `extractNumber()` | извлечение всех цифр из строки и возврат числа |
| `getFileName()` | извлечение имени файла из полного пути |
| `getFileExt()` | извлечение расширения файла в нижнем регистре |
| `extractHour()` | извлечение часа из временной метки |
| `utf8ToWide()` | преобразование utf-8 строки в широкую для вывода |
| `writeToFile()` | запись строки в файл `output.txt` в кодировке utf-8 |
| `initDatabase()` | создание бд и таблиц (`launches`, `general_events`, `event_types`), очистка старых данных |
| `insertLaunchRecord()` | вставка записей о запусках в таблицу `launches` |
| `insertGeneralRecord()` | вставка записей об общих событиях в таблицу `general_events` |
| `parseLaunchFile()` | парсинг `PcaAppLaunchDic.txt` и вставка записей в бд |
| `parseGeneralFile()` | парсинг `PcaGeneralDb*.txt` и вставка записей в бд |
| `printStatisticsFromDB()` | выполнение sql-запросов и вывод статистики (в консоль и файл) |

### 2.3 схема базы данных

**таблица `launches`:**

> ```sql
> create table launches (
>     id integer primary key autoincrement,
>     program_path text not null,
>     program_name text,
>     launch_time text,
>     hour integer
> );
> ```

**таблица `general_events`:**

> ```sql
> create table general_events (
>     id integer primary key autoincrement,
>     event_time text,
>     event_type integer,
>     program_path text,
>     program_name text,
>     publisher text,
>     version text,
>     hash text,
>     status text,
>     hour integer
> );
> ```

**таблица `event_types` (фиксированная):**

> ```sql
> create table event_types (
>     type_code integer primary key,
>     type_name text,
>     description text
> );
> ```

таблица заполняется следующими данными:

| type_code | type_name | description |
|-----------|-----------|-------------|
| 0 | installer | установщик завершился с ошибкой или был прерван |
| 1 | driverblocked | драйвер заблокирован (hvci/cet) |
| 2 | abnormalexit | программа завершилась аномально |
| 3 | compatissue | обнаружена проблема совместимости |

---

## 3. Краткое руководство по использованию

### 3.1 требования

- ос: windows 11
- компилятор: gcc (mingw64) с поддержкой c++11
- библиотеки: sqlite3, cmake (v3.10+)
- права доступа: чтение `C:\Windows\appcompat\pca\*`

### 3.2 сборка (cmake)
используя MSYS2 MinGW64
> ```bash 
> cd *папка проекта*
> mkdir build
> cd build
> cmake .. -g "mingw makefiles"
> cmake --build .
> ```

### 3.3 запуск

> ```bash
> ./PCAParserEducationalProject.exe
> ```

### 3.4 вывод программы

программа выводит в консоль и одновременно сохраняет в файл `build\output.txt`:

- количество загруженных записей из каждого файла
- количество уникальных программ
- всего записей
- топ-5 самых часто запускаемых программ
- топ-5 самых часто используемых путей
- записи по типам (installer, driverblocked, abnormalexit, compatissue)
- статистику по расширениям файлов
- временную шкалу активности (по часам)

---

## 4. Результаты тестирования

**тестовая среда:**

- ос: windows 11 (виртуальная машина)
- компилятор: gcc 16.1.0 (mingw64)
- данные: файлы из `C:\Windows\appcompat\pca\`

**пример вывода (консоль / `build\output.txt`):**

> ```
> oooo
> загружено из C:/Windows/appcompat/pca/PcaAppLaunchDic.txt: 6 записей
> загружено из C:/Windows/appcompat/pca/PcaGeneralDb0.txt: 3 записей
> загружено из C:/Windows/appcompat/pca/PcaGeneralDb1.txt: 3 записей
> oooo
> oooo
> статистика
> уникальных программ: 6
> всего записей: 6 + 6 = 12
> топ-5 самых часто запускаемых программ:
> 1. wingup for notepad++ (2 запусков)
> 2. notepad++ (2 запусков)
> 3. 7-zip (2 запусков)
> топ-5 самых часто используемых путей:
> 1. C:\Users\seal\Downloads\npp.8.9.4.Installer.x64.exe
> 2. C:\Users\seal\Downloads\7z2601-x64.exe
> 3. C:\Program Files\WindowsApps\Microsoft.WindowsNotepad_11.2501.31.0_x64__8wekyb3d8bbwe\Notepad\Notepad.exe
> 4. C:\Program Files\WindowsApps\Microsoft.Paint_11.2412.295.0_x64__8wekyb3d8bbwe\PaintApp\mspaint.exe
> 5. C:\Program Files\Notepad++\notepad++.exe
> записи по типам (PcaGeneralDb):
> type 0 (installer): 4
> type 2 (abnormalexit): 2
> статистика по расширениям файлов:
> .exe: 6 записей
> временная шкала активности (по часам):
> 8:00 - 2 событий
> 9:00 - 4 событий
> 8:00 - 2 событий (general)
> 9:00 - 4 событий (general)
> oooo
> ```

**вывод программы корректен и соответствует данным из файлов.**

---

## 5. Заключение

разработанная программа успешно:

1. анализирует структуру файлов `PcaAppLaunchDic.txt` и `PcaGeneralDb*.txt`
2. парсит строки с учётом разделителя `|` и обработкой ошибок
3. сохраняет данные в базу sqlite (включая фиксированную таблицу `event_types` с описаниями типов событий)
4. вычисляет и выводит статистику в читаемом формате (в консоль и файл `output.txt`)

**архив для сдачи включает:**

- `main.cpp` — исходный код программы
- `CMakeLists.txt` — конфигурация сборки
- `отчёт.md` — данный документ
- `output.txt` — пример вывода программы