#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cctype>
#include <clocale>
#include <windows.h>

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

// функции для парсинга

// удалить пробелы, табуляции, переносы строк в начале и конце строки
std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n"); // искать первый символ не из набора \t\r\n
    if (start == std::string::npos) return ""; // строка состоит только из пробелов
    size_t end = s.find_last_not_of(" \t\r\n"); //последний
    return s.substr(start, end - start + 1); //вырезать часть строки от start до end включительно
}

// оставить только печатные символы (коды 32-126)  необходимо для general файла
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
    return path;// если разделителей нет - вернуть исходную строку
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
    if (timeStr.length() < 13) return -1;   // слишком короткая строка
    size_t spacePos = timeStr.find(' ');     // искать пробел между датой и временем
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

// парсинг PcaAppLaunchDic.txt
// формат строки: путь|время

std::vector<LaunchRecord> parseLaunchFile(const std::string& filename) {
    std::vector<LaunchRecord> records;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::wcerr << L"не удалось открыть: " << utf8ToWide(filename) << std::endl;
        return records;
    }
    
    std::string line;
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
                current += c; // добавить разделитель к текущему полю
            }
        }
        if (!current.empty()) fields.push_back(current); // последнее поле
        
        if (fields.size() >= 2) {
            LaunchRecord rec;
            rec.path = trim(fields[0]);
            rec.time = trim(fields[1]);
            if (!rec.path.empty()) {
                records.push_back(rec);
            }
        }
    }
    
    std::wcout << L"загружено из " << utf8ToWide(filename) << L": " << records.size() << L" записей" << std::endl;
    return records;
}

// парсинг PcaGeneralDb.txt
// формат строки: время|тип|путь|имя|издатель|версия|хэш|статус

std::vector<GeneralRecord> parseGeneralFile(const std::string& filename) {
    std::vector<GeneralRecord> records;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        return records;
    }
    
    std::string line;
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
            rec.hash = fields[6];// хэш
            rec.status = fields[7]; // статус
            
            // сохранить только подходящие записи (тип определён, имя не пустое)
            if (rec.type >= 0 && !rec.name.empty()) {
                records.push_back(rec);
            }
        }
    }
    
    std::wcout << L"загружено из " << utf8ToWide(filename) << L": " << records.size() << L" записей" << std::endl;
    return records;
}

// статистика

void printStatistics(const std::vector<LaunchRecord>& launches, const std::vector<GeneralRecord>& generals) {
    std::wcout << L"oooo" << std::endl;
    std::wcout << L"oooo" << std::endl;
    std::wcout << L"статистика" << std::endl;
    
    // общее количество запускавшихся программ (уникальные)
    std::map<std::string, int> uniquePrograms;
    for (const auto& rec : launches) {
        uniquePrograms[getFileName(rec.path)]++;
    }
    for (const auto& rec : generals) {
        uniquePrograms[getFileName(rec.path)]++;
    }
    std::wcout << L"уникальных программ: " << uniquePrograms.size() << std::endl;
    std::wcout << L"всего записей: " << (launches.size() + generals.size()) << std::endl;
    
    // топ-5 самых часто запускаемых программ
    std::vector<std::pair<std::string, int>> sortedPrograms(uniquePrograms.begin(), uniquePrograms.end());
    std::sort(sortedPrograms.begin(), sortedPrograms.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::wcout << L"топ-5 самых часто запускаемых программ:" << std::endl;
    for (size_t i = 0; i < std::min(sortedPrograms.size(), (size_t)5); i++) {
        std::wcout << L"   " << i+1 << L". " << utf8ToWide(sortedPrograms[i].first) << L" (" << sortedPrograms[i].second << L" запусков)" << std::endl;
    }
    
    // топ-5 самых часто используемых путей
    std::map<std::string, int> pathCount;
    for (const auto& rec : launches) {
        size_t pos = rec.path.find_last_of("\\/");
        std::string dir = (pos != std::string::npos) ? rec.path.substr(0, pos) : rec.path;
        pathCount[dir]++;
    }
    
    std::vector<std::pair<std::string, int>> sortedPaths(pathCount.begin(), pathCount.end());
    std::sort(sortedPaths.begin(), sortedPaths.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    
    std::wcout << L"топ-5 самых часто используемых путей:" << std::endl;
    for (size_t i = 0; i < std::min(sortedPaths.size(), (size_t)5); i++) {
        std::wcout << L"   " << i+1 << L". " << utf8ToWide(sortedPaths[i].first) << std::endl;
    }
    
    // количество записей по типам (из GeneralDb)
    int type0 = 0, type1 = 0, type2 = 0, type3 = 0;
    for (const auto& rec : generals) {
        switch(rec.type) {
            case 0: type0++; break;
            case 1: type1++; break;
            case 2: type2++; break;
            case 3: type3++; break;
        }
    }
    
    std::wcout << L"записи по типам (PcaGeneralDb):" << std::endl;
    std::wcout << L"type 0 (installer): " << type0 << std::endl;
    std::wcout << L"type 1 (driverblocked): " << type1 << std::endl;
    std::wcout << L"type 2 (abnormalexit): " << type2 << std::endl;
    std::wcout << L"type 3 (compatissue): " << type3 << std::endl;
    
    std::map<std::string, int> extCount;
    for (const auto& rec : launches) {
        extCount[getFileExt(rec.path)]++;
    }
    
    std::wcout << L"статистика по расширениям файлов:" << std::endl;
    for (const auto& pair : extCount) {
        std::wcout << L"   ." << utf8ToWide(pair.first) << L": " << pair.second << L" записей" << std::endl;
    }
    
    std::map<int, int> hourActivity;
    for (const auto& rec : launches) {
        int hour = extractHour(rec.time);
        if (hour >= 0) hourActivity[hour]++;
    }
    for (const auto& rec : generals) {
        int hour = extractHour(rec.time);
        if (hour >= 0) hourActivity[hour]++;
    }
    
    std::wcout << L"временная шкала активности (по часам):" << std::endl;
    for (const auto& pair : hourActivity) {
        std::wcout << L"   " << pair.first << L":00 - " << pair.second << L" событий" << std::endl;
    }
    
    std::wcout << L"oooo" << std::endl;
}

int main() {
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251); 
    std::setlocale(LC_ALL, "Russian"); // только сетлокал не хватает на моей в данной момент машины
    
    // загрузка данных
    auto launches = parseLaunchFile("C:/Windows/appcompat/pca/PcaAppLaunchDic.txt");
    auto generals0 = parseGeneralFile("C:/Windows/appcompat/pca/PcaGeneralDb0.txt");
    auto generals1 = parseGeneralFile("C:/Windows/appcompat/pca/PcaGeneralDb1.txt");
    
    // объединить general записи
    std::vector<GeneralRecord> allGenerals;
    allGenerals.insert(allGenerals.end(), generals0.begin(), generals0.end());
    allGenerals.insert(allGenerals.end(), generals1.begin(), generals1.end());
    
    // вывод первых записей
    std::wcout << L"записи из PcaAppLaunchDic.txt" << std::endl;
    for (size_t i = 0; i < std::min(launches.size(), (size_t)3); i++) {
        std::wcout << L"  " << utf8ToWide(getFileName(launches[i].path)) 
                   << L" | " << utf8ToWide(launches[i].time) << std::endl;
    }
    
    std::wcout << L"записи из PcaGeneralDb*.txt ||*=(1,2)" << std::endl;
    for (size_t i = 0; i < std::min(allGenerals.size(), (size_t)3); i++) {
        std::wcout << L"  " << utf8ToWide(allGenerals[i].name)
                    << L" | type=" << allGenerals[i].type 
                   << L" | " << utf8ToWide(allGenerals[i].status) << std::endl;
    }
    
    // статистика
    printStatistics(launches, allGenerals);
    return 0;
}