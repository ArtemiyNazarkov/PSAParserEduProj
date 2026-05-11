#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>
#include <clocale>
#include <windows.h>
#include <sqlite3.h>

// структуры

struct LaunchRecord {
    std::string path; // путь к исполняемому файлу
    std::string time; // временная метка запуска
};

struct GeneralRecord {
    std::string time; // временная метка
    int type; // тип события (0,1,2,3)
    std::string path; // путь к программе
    std::string name; // имя программы
    std::string publisher; // издатель
    std::string version; // версия
    std::string hash; // хэш (отпечаток)
    std::string status; // статус/сообщение об ошибке
};

// глобальный указатель на БД
sqlite3* db = nullptr;

// функции для парсинга

// удалить пробелы, табуляции, переносы строк в начале и конце строки
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n"); // искать первый символ не из набора \t\r\n
    if (start == std::string::npos) return ""; // строка состоит только из пробелов
    size_t end = s.find_last_not_of(" \t\r\n"); // последний символ не из набора
    return s.substr(start, end - start + 1); // вырезать часть строки от start до end включительно
}

// оставить только печатные символы (коды 32-126) необходимо для general файла
// удалить управляющие символы, BOM, русские буквы и т.д.
std::string cleanString(const std::string& s) {
    std::string result;
    for (char c : s) {
        // ascii коды: 32=пробел, 126=тильда, всё между ними - печатные символы
        if (c >= 32 && c <= 126) {
            result += c;
        }
    }
    return result;
}

// извлечь все цифры из строки и преобразовать их в число
int extractNumber(const std::string& s) {
    std::string digits;
    for (char c : s) {
        if (std::isdigit(static_cast<unsigned char>(c))) { // неотрицательный символ от 0 до 9 - digit
            digits += c;
        }
    }
    if (digits.empty()) return -1;
    try {
        return std::stoi(digits); // stoi = string to int необходимо для нормального парсинга времени и general-числа
    } catch (...) {
        return -1; // если преобразование не удалось (например, число слишком большое)
    }
}

// извлечь имя файла из полного пути
std::string getFileName(const std::string& path) {
    // \\ - экранированный обратный слеш для windows, / - для linux/mac
    size_t pos = path.find_last_of("\\/"); // искать последнее появление любого из символов "\\/"
    if (pos != std::string::npos) {
        return path.substr(pos + 1); // +1 чтобы пропустить сам разделитель
    }
    return path; // если разделителей нет - вернуть исходную строку
}

// извлечь расширение файла из пути
std::string getFileExt(const std::string& path) {
    std::string name = getFileName(path);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos && dot + 1 < name.length()) {
        std::string ext = name.substr(dot + 1);
        for (char& c : ext) c = std::tolower(c);
        return ext;
    }
    return "no_ext"; // нет расширения
}

// извлечь час из временной метки
int extractHour(const std::string& timeStr) {
    if (timeStr.length() < 13) return -1; // слишком короткая строка
    size_t spacePos = timeStr.find(' '); // искать пробел между датой и временем
    if (spacePos == std::string::npos) return -1;
    std::string hourStr = timeStr.substr(spacePos + 1, 2); // надо взять 2 символа после пробела (часы)
    return extractNumber(hourStr);
}

// преобразование UTF-8 string в wstring (для корректного вывода кириллицы)
std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    // необходимый размер буфера
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), NULL, 0);
    if (len <= 0) return L"";
    std::wstring result(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.length(), &result[0], len);
    return result;
}

// работа с БД

// инициализация БД и создание таблиц
bool initDatabase() {
    int rc = sqlite3_open("pca_analysis.db", &db);
    if (rc != SQLITE_OK) {
        std::wcerr << L"не удалось открыть бд: " << utf8ToWide(sqlite3_errmsg(db)) << std::endl;
        return false;
    }
    
    // таблица для launch записей
    const char* sqlLaunch = "create table if not exists launches(id integer primary key autoincrement,program_path text not null,program_name text,launch_time text,hour integer);";
    
    // таблица для general записей
    const char* sqlGeneral = "create table if not exists general_events(id integer primary key autoincrement,event_time text,event_type integer,program_path text,program_name text,publisher text,version text,hash text,status text,hour integer);";
    
    char* errMsg = nullptr;
    
    rc = sqlite3_exec(db, sqlLaunch, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::wcerr << L"ошибка создания таблицы launches: " << utf8ToWide(errMsg) << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    rc = sqlite3_exec(db, sqlGeneral, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::wcerr << L"ошибка создания таблицы general_events: " << utf8ToWide(errMsg) << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    // очистить старые данные перед загрузкой новых (чтобы не дублировать при повторных запусках)
    rc = sqlite3_exec(db, "delete from launches;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::wcerr << L"ошибка очистки таблицы launches: " << utf8ToWide(errMsg) << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    rc = sqlite3_exec(db, "delete from general_events;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::wcerr << L"ошибка очистки таблицы general_events: " << utf8ToWide(errMsg) << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    
    return true;
}

// вставка записи из PcaAppLaunchDic.txt
void insertLaunchRecord(const LaunchRecord& rec) {
    const char* sql = "insert into launches(program_path,program_name,launch_time,hour) values(?,?,?,?);";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    sqlite3_bind_text(stmt, 1, rec.path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, getFileName(rec.path).c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, rec.time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, extractHour(rec.time));
    
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// вставка записи из PcaGeneralDb.txt
void insertGeneralRecord(const GeneralRecord& rec) {
    const char* sql = "insert into general_events(event_time,event_type,program_path,program_name,publisher,version,hash,status,hour) values(?,?,?,?,?,?,?,?,?);";
    
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    
    sqlite3_bind_text(stmt, 1, rec.time.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, rec.type);
    sqlite3_bind_text(stmt, 3, rec.path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, rec.name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 5, rec.publisher.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 6, rec.version.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 7, rec.hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 8, rec.status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 9, extractHour(rec.time));
    
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// закрытие БД
void closeDatabase() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

// парсинг PcaAppLaunchDic.txt
// формат строки: путь|время

void parseLaunchFile(const std::string& filename) {
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::wcerr << L"не удалось открыть: " << utf8ToWide(filename) << std::endl;
        return;
    }
    
    std::string line;
    int count = 0;
    
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        
        // разбить на поля по разделителю '|'
        std::vector<std::string> fields;
        std::string current;
        for (char c : line) {
            if (c == '|') {
                fields.push_back(current); // сохранить текущее поле
                current.clear(); // и очистить для следующего
            } else {
                current += c; // добавить символ к текущему полю
            }
        }
        if (!current.empty()) fields.push_back(current); // последнее поле
        
        if (fields.size() >= 2) {
            LaunchRecord rec;
            rec.path = trim(fields[0]); // путь
            rec.time = trim(fields[1]); // время
            if (!rec.path.empty()) {
                insertLaunchRecord(rec);
                count++;
            }
        }
    }
    
    std::wcout << L"загружено из " << utf8ToWide(filename) << L": " << count << L" записей" << std::endl;
}

// парсинг PcaGeneralDb.txt
// формат строки: время|тип|путь|имя|издатель|версия|хэш|статус

void parseGeneralFile(const std::string& filename) {
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        return;
    }
    
    std::string line;
    int count = 0;
    
    while (std::getline(file, line)) {
        // сначала очистить строку от непечатных символов (нужно для general)
        std::string clean = cleanString(line);
        if (clean.empty()) continue;
        
        // разбить на поля по разделителю '|'
        std::vector<std::string> fields;
        std::string current;
        for (char c : clean) {
            if (c == '|') {
                fields.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty()) fields.push_back(current);
        
        if (fields.size() >= 8) {
            GeneralRecord rec;
            rec.time = fields[0]; // временная метка
            rec.type = extractNumber(fields[1]); // тип события (извлекаем цифры)
            rec.path = fields[2]; // путь к программе
            rec.name = fields[3]; // имя программы
            rec.publisher = fields[4]; // издатель
            rec.version = fields[5]; // версия
            rec.hash = fields[6]; // хэш
            rec.status = fields[7]; // статус
            
            // сохранить только подходящие записи (тип определён, имя не пустое)
            if (rec.type >= 0 && !rec.name.empty()) {
                insertGeneralRecord(rec);
                count++;
            }
        }
    }
    
    std::wcout << L"загружено из " << utf8ToWide(filename) << L": " << count << L" записей" << std::endl;
}

//  статистика из БД

void printStatisticsFromDB() {
    std::wcout << L"oooo" << std::endl;
    std::wcout << L"oooo" << std::endl;
    std::wcout << L"статистика" << std::endl;
    
    sqlite3_stmt* stmt;
    int rc;
    
    // общее количество уникальных программ
    std::wcout << L"уникальных программ: ";
    rc = sqlite3_prepare_v2(db, "select count(distinct program_path) from launches;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        std::wcout << sqlite3_column_int(stmt, 0);
    } else {
        std::wcout << L"0";
    }
    sqlite3_finalize(stmt);
    std::wcout << std::endl;
    
    // общее количество записей (launches + general_events)
    int totalLaunches = 0;
    int totalGeneral = 0;
    
    rc = sqlite3_prepare_v2(db, "select count(*) from launches;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        totalLaunches = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    rc = sqlite3_prepare_v2(db, "select count(*) from general_events;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
        totalGeneral = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    std::wcout << L"всего записей: " << totalLaunches << L" + " << totalGeneral 
               << L" = " << (totalLaunches + totalGeneral) << std::endl;
    
    // топ-5 самых часто запускаемых программ
    std::wcout << L"топ-5 самых часто запускаемых программ:" << std::endl;
    rc = sqlite3_prepare_v2(db, "select program_name,count(*) as cnt from general_events where program_name is not null and program_name != '' group by program_name order by cnt desc limit 5;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = (const char*)sqlite3_column_text(stmt, 0);
            int cnt = sqlite3_column_int(stmt, 1);
            std::wcout << rank++ << L". " << utf8ToWide(name ? name : "?") << L" (" << cnt << L" запусков)" << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    
    // топ-5 самых часто используемых путей
    std::wcout << L"топ-5 самых часто используемых путей:" << std::endl;
    rc = sqlite3_prepare_v2(db, "select program_path,count(*) as cnt from launches group by program_path order by cnt desc limit 5;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        int rank = 1;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* path = (const char*)sqlite3_column_text(stmt, 0);
            std::wcout << rank++ << L". " << utf8ToWide(path ? path : "?") << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    
    // количество записей по типам (из GeneralDb)
    std::wcout << L"записи по типам (pcageneraldb):" << std::endl;
    rc = sqlite3_prepare_v2(db, "select event_type,count(*) from general_events group by event_type order by event_type;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int type = sqlite3_column_int(stmt, 0);
            int cnt = sqlite3_column_int(stmt, 1);
            std::wstring typeName;
            switch(type) {
                case 0: typeName = L"installer"; break;
                case 1: typeName = L"driverblocked"; break;
                case 2: typeName = L"abnormalexit"; break;
                case 3: typeName = L"compatissue"; break;
                default: typeName = L"unknown";
            }
            std::wcout << L"type " << type << L" (" << typeName << L"): " << cnt << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    
    // статистика по расширениям файлов
    std::wcout << L"статистика по расширениям файлов:" << std::endl;
    rc = sqlite3_prepare_v2(db, "select program_path from launches;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        std::map<std::string, int> extCount;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* path = (const char*)sqlite3_column_text(stmt, 0);
            if (path) {
                extCount[getFileExt(std::string(path))]++;
            }
        }
        for (const auto& pair : extCount) {
            std::wcout << L"." << utf8ToWide(pair.first) << L": " << pair.second << L" записей" << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    
    // временная шкала активности (по часам) из launches
    std::wcout << L"временная шкала активности (по часам):" << std::endl;
    rc = sqlite3_prepare_v2(db, "select hour,count(*) from launches where hour>=0 group by hour order by hour;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::wcout << sqlite3_column_int(stmt, 0) << L":00 - " << sqlite3_column_int(stmt, 1) << L" событий" << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    
    // временная шкала активности (по часам) из general_events
    rc = sqlite3_prepare_v2(db, "select hour,count(*) from general_events where hour>=0 group by hour order by hour;", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::wcout << sqlite3_column_int(stmt, 0) << L":00 - " << sqlite3_column_int(stmt, 1) << L" событий (general)" << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    
    std::wcout << L"oooo" << std::endl;
}

int main() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251); 
    std::setlocale(LC_ALL, "Russian");
    
    std::wcout << L"oooo" << std::endl;
    std::wcout << L"program compatibility assistant analyzer (with sqlite)" << std::endl;
    std::wcout << L"oooo" << std::endl;
    
    // инициализация БД
    if (!initDatabase()) {
        std::wcerr << L"ошибка инициализации бд" << std::endl;
        return 1;
    }
    
    // загрузка данных (сразу в БД)
    parseLaunchFile("C:/Windows/appcompat/pca/PcaAppLaunchDic.txt");
    parseGeneralFile("C:/Windows/appcompat/pca/PcaGeneralDb0.txt");
    parseGeneralFile("C:/Windows/appcompat/pca/PcaGeneralDb1.txt");
    
    // статистика из БД
    printStatisticsFromDB();
    
    // закрытие БД
    closeDatabase();
    
    return 0;
}