// Вариант 2. Расписание транспорта.
// Читает input.txt, пишет output.txt. Формат — в комментарии перед main().
//
// Модель обработки (по разъяснению преподавателя — строгая):
//   любая НЕКОРРЕКТНАЯ запись делает ВЕСЬ output.txt пустым (не «пропустить строку»);
//   дубликаты номеров рейса — это валидные записи, остаются обе;
//   несовпадение числа строк с N, пустой/нечитаемый ввод, параметр-мусор при N>0 => пусто;
//   N=0 => "0".
// Устойчивость: длина строки и общий объём чтения ограничены (защита от бесконечного
//   потока), числа парсятся строго (формат/диапазон/переполнение), память растёт сама;
//   нехватка памяти, превышение предела и ошибка записи => пустой файл, а не частичный.
// Ввод-вывод на стандартных fopen/fread/fwrite — собирается любым C++17-компилятором.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cmath>
#include <cerrno>
#include <climits>
#include <new>
#include <clocale>
#include <string>
#include <algorithm>

static const size_t MAX_LINE    = 8192;        // буфер строки: до MAX_LINE-1 байт
static const long   MAX_RECORDS = 2000000L;    // предел подходящих рейсов — защита от OOM
static const long   MAX_TOTAL   = 128L*1024*1024;  // потолок объёма чтения — защита от бесконечного потока

struct Flight {
    std::string dest;
    long   flightNo;
    int    hour;
    int    minute;
    double price;
};

// Читает одну строку через fgetc. Концом строки считается LF, CRLF и одиночный CR
// (поддержка файлов с окончаниями строк \n, \r\n и \r). Строка длиннее MAX_LINE-1 байт
// помечается tooLong и дочитывается до конца. Поток без конца строки обрывается по budget
// (overBudget), чтобы не читаться вечно. Нулевой байт и прочие управляющие в буфер не
// попадают (hadCtrl помечает их наличие). rawLen — сколько байт прочитано.
// Возвращает длину в буфере или -1 в конце файла.
static int readLine(FILE* f, char* buf, size_t cap, long budget,
                    bool& tooLong, bool& hadCtrl, bool& overBudget, long& rawLen) {
    tooLong = false; hadCtrl = false; overBudget = false; rawLen = 0;
    size_t len = 0;
    int c = fgetc(f);
    if (c == EOF) return -1;
    while (c != EOF && c != '\n') {
        int next = fgetc(f);
        if (c == '\r') {                                     // конец строки: CR, CRLF или \r в конце файла
            if (next != '\n' && next != EOF) ungetc(next, f); // одиночный CR (стиль старого Mac) — байт вернуть
            break;
        }
        if (rawLen >= budget) { overBudget = true; break; }
        rawLen++;
        unsigned char uc = (unsigned char)c;
        if (rawLen >= (long)cap) {
            tooLong = true;
        } else if (uc == '\0' || uc == 0x7F || (uc < 0x20 && uc != '\t')) {
            hadCtrl = true;                                  // управляющий байт
        } else {
            buf[len++] = (char)c;
        }
        c = next;
    }
    buf[len] = '\0';
    return (int)len;
}

// Обрезает пробелы с краёв прямо в строке.
static char* trim(char* s) {
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char* end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

// Делит строку по разделителю, заполняя out[0..maxFields-1].
// Возвращает полное число полей (может быть больше maxFields).
static int split(char* s, char delim, char** out, int maxFields) {
    int count = 1;
    if (maxFields > 0) out[0] = s;
    for (char* p = s; *p; ++p) {
        if (*p == delim) {
            *p = '\0';
            if (count < maxFields) out[count] = p + 1;
            count++;
        }
    }
    return count;
}

// Целое: необязательный '-' и только цифры. strtol принял бы "+5" и пробелы — проверяем формат до него.
static bool parseLong(const char* s, long& out) {
    if (!s) return false;
    const char* p = s;
    bool neg = (*p == '-');
    if (neg) p++;
    if (*p == '\0') return false;
    for (const char* q = p; *q; ++q)
        if (*q < '0' || *q > '9') return false;
    errno = 0;
    long v = strtol(s, nullptr, 10);
    if (errno == ERANGE) return false;                // переполнение long
    if (neg && v == 0) return false;                  // "-0" не число
    out = v;
    return true;
}

// Цена: необязательный '-', цифры и не больше одной точки. Без формата strtod принял бы "1e3", "inf", "0x1p3".
static bool parseDouble(const char* s, double& out) {
    if (!s) return false;
    const char* p = s;
    if (*p == '-') p++;
    int digits = 0, dots = 0;
    for (; *p; ++p) {
        if (*p >= '0' && *p <= '9') digits++;
        else if (*p == '.')         dots++;
        else return false;
    }
    if (digits == 0 || dots > 1) return false;
    double v = strtod(s, nullptr);
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

// Запись output.txt. Пустая строка => пустой файл (0 байт). Возвращает true, только если
// данные гарантированно записаны и файл корректно закрыт.
static bool writeOutput(const std::string& content) {
    remove("output.txt");                  // снять возможный симлинк: писать в новый обычный файл, не по чужой ссылке
    FILE* out = fopen("output.txt", "wb");
    if (!out) return false;
    bool ok = true;
    if (!content.empty() && fwrite(content.data(), 1, content.size(), out) != content.size())
        ok = false;
    if (fflush(out) != 0 || ferror(out)) ok = false;
    if (fclose(out) != 0) ok = false;
    return ok;
}

// ======================= Вариант 2. Расписание =======================
// Формат input.txt:
//   строка 1: N                       — число записей (целое >= 0)
//   строка 2: пункт назначения        — параметр фильтра
//   строки 3..N+2: пункт;рейс;часы;минуты;цена   — ровно N записей
// Правила:
//   - пустой/нечитаемый/некорректный ввод  -> output.txt пустой (0 байт);
//   - N=0                                   -> output.txt содержит "0";
//   - число строк записей != N              -> пустой;
//   - ЛЮБАЯ некорректная запись (не 5 полей, пустой пункт, не число, час>23, минута>59,
//     отрицательные рейс/цена, переполнение) -> весь output.txt пустой;
//   - дубликаты номеров рейса остаются (обе записи).
// Формат output.txt:
//   строка 1: M                       — число подходящих рейсов
//   строки 2..M+1: пункт;рейс;ЧЧ:ММ;цена   — по времени, затем по номеру рейса; в конце каждой строки '\n'
int main() {
    setlocale(LC_NUMERIC, "C");

    FILE* in = fopen("input.txt", "rb");
    if (!in) { writeOutput(""); return 0; }

    // Пропускаем UTF-8 BOM. Если файл короче — возвращаемся в начало.
    { unsigned char bom[3] = {0,0,0}; size_t g = fread(bom,1,3,in);
      long s = (g>=3 && bom[0]==0xEF && bom[1]==0xBB && bom[2]==0xBF) ? 3 : 0;
      if (fseek(in, s, SEEK_SET) != 0) { fclose(in); writeOutput(""); return 0; } }

    char line[MAX_LINE];
    long totalBytes = 0;

    // ---- строка 1: N ----
    { bool tl=false,hc=false,ob=false; long rl=0;
      int l1 = readLine(in, line, MAX_LINE, MAX_TOTAL - totalBytes, tl,hc,ob,rl);
      if (l1 < 0 || ob || tl || hc) { fclose(in); writeOutput(""); return 0; }
      totalBytes += rl + 1; }
    long N;
    { char* s = trim(line);
      if (!parseLong(s, N) || N < 0) { fclose(in); writeOutput(""); return 0; } }

    // ---- строка 2: пункт-параметр ----
    std::string target;
    bool haveParam = false;
    { bool tl=false,hc=false,ob=false; long rl=0;
      int l2 = readLine(in, line, MAX_LINE, MAX_TOTAL - totalBytes, tl,hc,ob,rl);
      if (l2 >= 0) {
          totalBytes += rl + 1;
          if ((ob || tl || hc) && N > 0) { fclose(in); writeOutput(""); return 0; }
          haveParam = true;
          target = trim(line);
      } }
    if (!haveParam && N > 0) { fclose(in); writeOutput(""); return 0; }

    // ---- ровно N строк записей ----
    Flight* arr = nullptr; long size = 0, cap = 0;
    bool broken = false;

    try {
    for (long i = 0; i < N; i++) {
        bool tl=false,hc=false,ob=false; long rl=0;
        int len = readLine(in, line, MAX_LINE, MAX_TOTAL - totalBytes, tl,hc,ob,rl);
        if (len < 0) { broken = true; break; }        // строк меньше N
        totalBytes += rl + 1;
        if (ob || tl || hc) { broken = true; break; } // слишком длинная / управляющие / поток — весь файл битый

        char* s = trim(line);
        char* f[5];
        if (split(s, ';', f, 5) != 5) { broken = true; break; }   // не 5 полей — весь файл битый
        for (int k = 0; k < 5; k++) f[k] = trim(f[k]);

        if (f[0][0] == '\0') { broken = true; break; }            // пустой пункт назначения — некорректная запись
        long flight; if (!parseLong(f[1], flight) || flight < 0) { broken = true; break; }
        long h;      if (!parseLong(f[2], h) || h < 0 || h > 23)  { broken = true; break; }
        long m;      if (!parseLong(f[3], m) || m < 0 || m > 59)  { broken = true; break; }
        double price;
        if (!parseDouble(f[4], price) || std::signbit(price))     { broken = true; break; }

        if (target != f[0]) continue;                 // валидная запись, но не наш пункт — не выводим (это не ошибка)
        if (size >= MAX_RECORDS) { broken = true; break; }        // превышен предел — не выдаём частичный результат
        if (size == cap) {
            long nc = (cap == 0) ? 16 : cap * 2;
            if (nc > MAX_RECORDS) nc = MAX_RECORDS;
            Flight* tmp = new (std::nothrow) Flight[nc];
            if (!tmp) { broken = true; break; }        // нет памяти — весь файл пустой, не частичный
            for (long j = 0; j < size; j++) tmp[j] = arr[j];
            delete[] arr; arr = tmp; cap = nc;
        }
        arr[size].dest = f[0]; arr[size].flightNo = flight;
        arr[size].hour = (int)h; arr[size].minute = (int)m; arr[size].price = price;
        size++;
    }
    } catch (const std::bad_alloc&) { broken = true; }

    // ---- после N записей — только пустые строки ----
    if (!broken) {
        while (true) {
            bool tl=false,hc=false,ob=false; long rl=0;
            int len = readLine(in, line, MAX_LINE, MAX_TOTAL - totalBytes, tl,hc,ob,rl);
            if (len < 0) break;                        // EOF
            totalBytes += rl + 1;
            char* s = trim(line);
            if (ob || tl || hc || *s != '\0') { broken = true; break; }   // лишняя непустая строка — битый
        }
    }
    fclose(in);

    if (broken) { delete[] arr; writeOutput(""); return 0; }

    // сортировка и вывод; нехватка памяти при формировании вывода => пустой файл
    try {
        if (size > 1)                                  // при size 0/1 сортировка не нужна (и arr может быть nullptr)
            std::sort(arr, arr + size, [](const Flight& a, const Flight& b) {
                int ta = a.hour*60 + a.minute, tb = b.hour*60 + b.minute;
                if (ta != tb) return ta < tb;
                return a.flightNo < b.flightNo;
            });

        std::string out = std::to_string(size) + "\n";
        char tbuf[16], pbuf[512];          // pbuf с запасом: %.2f очень большой цены (до ~1e308) не обрезается
        for (long i = 0; i < size; i++) {
            snprintf(tbuf, sizeof tbuf, "%02d:%02d", arr[i].hour, arr[i].minute);
            snprintf(pbuf, sizeof pbuf, "%.2f", arr[i].price);
            out += arr[i].dest; out += ';';
            out += std::to_string(arr[i].flightNo); out += ';';
            out += tbuf; out += ';';
            out += pbuf; out += '\n';
        }
        delete[] arr; arr = nullptr;
        return writeOutput(out) ? 0 : 1;               // ошибка записи на диск => код возврата 1
    } catch (const std::bad_alloc&) {
        delete[] arr;
        writeOutput("");
        return 0;
    }
}
