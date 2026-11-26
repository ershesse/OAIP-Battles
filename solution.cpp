// ===== Электронная библиотека (вариант 3) =====
// Читает input.txt, сортирует книги по страницам, ищет книги, изданные ПОСЛЕ заданного года (строго >).
// Результат: output.txt, ошибки и аномалии: errors.log. Коды возврата: 0 - штатно, 1 - ошибка файла, 2 - нет памяти.
// Сборка: g++ -O2 -std=c++17 -Wall -Wextra -static main.cpp -o library.exe  |  MSVC: cl /O2 /EHsc /MT /utf-8 main.cpp
#define _CRT_SECURE_NO_WARNINGS
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <new>
#include <string>
#include <unordered_map>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#define N(x) std::to_string((long long)(x)).c_str()    // число -> текст (без %lld: на старом MinGW он глючит)

const int MAX_CHARS = 40, MIN_YEAR = 1450, MIN_PAGES = 1, MAX_PAGES = 20000, DEFAULT_YEAR = 2000;
const int MAX_LINE = 65536, SAMPLE = 500, MAXF = 16, EDGE = 3;
const long long MAX_LOG = 20000, MAX_FILE = 1000000000LL;   // подробных записей в errors.log; максимальный размер input.txt
struct Book { char author[161]; char title[161]; int year; int pages; long long line; };   // 40 символов <= 160 байт
struct Item { int key; size_t idx; };                 // для сортировки: число страниц и номер книги
Book* g_books = NULL; Item *g_items = NULL, *g_tmp = NULL; char *g_data = NULL, *g_start = NULL, *g_pos = NULL, *g_end = NULL;
size_t g_cap = 0, g_count = 0, g_final = 0;           // вместимость, принято книг, осталось после удаления дубликатов
FILE *g_in = NULL, *g_out = NULL, *g_log = NULL;
int g_maxYear = 2100, g_thr = DEFAULT_YEAR; bool g_thrSet = false, g_noRecords = false, g_cp1251 = false;
long long g_declared = -1, g_lineNo = 0, g_idx = 0, g_total = 0, g_lines = 0, g_rejected = 0, g_dups = 0, g_conflicts = 0, g_logged = 0;
unsigned long long g_seed = 0; char g_orig[128]; std::string g_inShown, g_fmtText;
std::map<std::string, long long> g_cnt;               // счётчики событий по уровням и кодам
double g_ms[6]; long long g_validNs = 0;              // время этапов: чтение, проверка, дубликаты, сортировка, фильтр, запись
typedef std::chrono::steady_clock Clock;
double lap(Clock::time_point* t) {                    // сколько мс прошло с *t; таймер запускается заново
    Clock::time_point now = Clock::now(); double ms = std::chrono::duration<double, std::milli>(now - *t).count(); *t = now; return ms;
}

// ===== Символы, журнал =====
// Длина символа UTF-8 (1-4) и его код; 0 - если байты не образуют корректный символ
int decodeLen(const unsigned char* s, unsigned* cp) {
    static const unsigned MINV[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (s[0] < 0x80) { *cp = s[0]; return 1; }
    int len = s[0] >= 0xF0 ? 4 : s[0] >= 0xE0 ? 3 : s[0] >= 0xC2 ? 2 : 0;
    if (len == 0 || s[0] > 0xF4) return 0;
    unsigned c = s[0] & (0x7F >> len);
    for (int i = 1; i < len; i++) { if ((s[i] & 0xC0) != 0x80) return 0; c = (c << 6) | (s[i] & 0x3F); }
    if (c < MINV[len] || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return 0;
    *cp = c; return len;
}
// Недопустимые символы: управляющие, NBSP, bidi (U+202E), невидимые (zero-width, BOM), юникод-пробелы
bool badCp(unsigned c) {
    return c < 0x20 || (c >= 0x7F && c <= 0xA0) || c == 0xAD || c == 0x1680 || c == 0x3000 || c == 0xFEFF || c >= 0xFFF9 ||
           (c >= 0x2000 && c <= 0x200F) || (c >= 0x2028 && c <= 0x202F) || (c >= 0x205F && c <= 0x206F);
}
void escape(const char* s, char* out, int maxBytes) {   // безопасный вид строки: плохие символы -> \xNN
    char* o = out;
    for (int i = 0; s[i] && i < maxBytes;) {
        unsigned cp; int len = decodeLen((const unsigned char*)s + i, &cp);
        if (len > 0 && !badCp(cp)) { memcpy(o, s + i, len); o += len; i += len; }
        else { o += sprintf(o, "\\x%02X", (unsigned char)s[i]); i++; }
    }
    *o = 0;
}
void logEvent(const char* level, const char* code, long long line, const char* msg, const char* text) {
    g_cnt[level]++; g_cnt[code]++;
    if (g_log == NULL || g_logged >= MAX_LOG) return;     // после MAX_LOG записей только счётчики
    g_logged++;
    char prev[600]; escape(text, prev, 100);
    fprintf(g_log, "строка %s [%s] %s: %s | \"%s\"\n", N(line), level, code, msg, prev);
}
void logCur(const char* level, const char* code, const char* msg) { logEvent(level, code, g_lineNo, msg, g_orig); }
void reject(const char* code, const char* what, long long line) { g_rejected++; logEvent("ERROR", code, line, what, g_orig); }

// ===== Проверка полей =====
void foldKey(char* s) {                    // нижний регистр для латиницы и кириллицы (UTF-8) прямо в строке
    for (; *s; s++) {
        unsigned char c = *s, d = s[1];
        if (c >= 'A' && c <= 'Z') *s = (char)(c + 32);
        else if (c == 0xD0 && d >= 0x90 && d <= 0x9F) s[1] = (char)(d + 32);                  // А-П -> а-п
        else if (c == 0xD0 && d >= 0xA0 && d <= 0xAF) { *s = (char)0xD1; s[1] = (char)(d - 32); }   // Р-Я -> р-я
        else if (c == 0xD0 && d >= 0x80 && d <= 0x8F) { *s = (char)0xD1; s[1] = (char)(d + 16); }   // Ё и др.
    }
}
// Строгое целое [+-]цифры (пробелы по краям можно). NULL - число прочитано, иначе код ошибки
const char* parseInt(const char* s, long long* out) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s == 0) return "EMPTY_FIELD";
    bool neg = *s == '-';
    if (*s == '+' || *s == '-') s++;
    if (*s < '0' || *s > '9') return "NOT_A_NUMBER";    // «1e5», «0x10», «--5», «1 000» не числа
    long long v = 0;
    for (; *s >= '0' && *s <= '9'; s++) if (v < 4000000000LL) v = v * 10 + (*s - '0');
    while (*s == ' ' || *s == '\t') s++;
    if (*s) return "NOT_A_NUMBER";
    if (v > (neg ? 2147483648LL : 2147483647LL)) return "INT_OVERFLOW";
    *out = neg ? -v : v; return NULL;
}
// Проверка и чистка текстового поля прямо в строке: UTF-8, запрещённые символы, пробелы, длина. NULL - всё хорошо
const char* cleanField(char* s) {
    unsigned char *r = (unsigned char*)s, *w = r; int chars = 0; bool space = false;
    while (*r) {
        unsigned cp; int len = decodeLen(r, &cp);
        if (len == 0) return "BAD_UTF8";
        if (badCp(cp)) return "BAD_CHAR";                // отклоняем, а не чиним: «грязная» запись не попадёт в данные
        if (cp == ' ') { space = true; r++; continue; }  // лишние пробелы схлопываем, по краям убираем
        if (space && w != (unsigned char*)s) { *w++ = ' '; chars++; }
        space = false;
        for (int i = 0; i < len; i++) *w++ = *r++;
        if (++chars > MAX_CHARS) return "FIELD_TOO_LONG";    // считаем символы, а не байты
    }
    *w = 0;
    return chars == 0 ? "EMPTY_FIELD" : NULL;
}

// ===== Файл целиком в памяти (binary!); строки берутся оттуда, длинные не копируются =====
const unsigned short CP1251[64] = {0x402,0x403,0x201A,0x453,0x201E,0x2026,0x2020,0x2021,0x20AC,0x2030,0x409,0x2039,0x40A,0x40C,0x40B,0x40F,
    0x452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0xFFFD,0x2122,0x459,0x203A,0x45A,0x45C,0x45B,0x45F,0xA0,0x40E,0x45E,0x408,
    0xA4,0x490,0xA6,0xA7,0x401,0xA9,0x404,0xAB,0xAC,0xAD,0xAE,0x407,0xB0,0xB1,0x406,0x456,0x491,0xB5,0xB6,0xB7,0x451,0x2116,0x454,0xBB,
    0x458,0x405,0x455,0x457};
int stop(const char* code, const char* ru, int rc) {      // ранний выход: output.txt всё равно создаём
    logEvent("ERROR", code, 0, ru, "");
    fprintf(g_out, "=== Электронная библиотека: отчёт ===\nФайл: %s\n%s\n", g_inShown.c_str(), ru);
    printf("ERROR: %s\n", code); return rc;
}
// Читает input.txt, определяет кодировку, считает строки (верхняя граница числа записей). -1 - всё хорошо, иначе код возврата
int loadFile() {
    fseek(g_in, 0, SEEK_END); long size = ftell(g_in); rewind(g_in);
    if (size < 0 || size > MAX_FILE) return stop("IO_ERROR", "input.txt - папка, слишком большой (> 1 ГБ) или недоступен", 1);
    g_data = new (std::nothrow) char[(size_t)size + 8];
    if (!g_data) return stop("OUT_OF_MEMORY", "не хватило памяти под файл", 2);
    size_t n = fread(g_data, 1, (size_t)size + 1, g_in);          // на 1 байт больше размера: заметим и папку, и «выросший» файл
    if (ferror(g_in)) return stop("IO_ERROR", "ошибка чтения input.txt", 1);
    if (n > (size_t)size) n = (size_t)size;
    memset(g_data + n, 0, 8); g_end = g_data + n; g_start = g_data;
    const unsigned char* u = (const unsigned char*)g_data;
    if (n >= 2 && ((u[0] == 0xFF && u[1] == 0xFE) || (u[0] == 0xFE && u[1] == 0xFF))) return stop("UNSUPPORTED_ENCODING", "UTF-16/UTF-32 не поддерживается, сохраните input.txt как UTF-8", 0);
    if (n >= 3 && memcmp(g_data, "\xEF\xBB\xBF", 3) == 0) g_start += 3;        // UTF-8 с BOM
    bool text = false; long long ok = 0, bad = 0;
    for (char* p = g_start; p < g_end; p++) {
        if (*p == 0) *p = 1;                                       // NUL обрезал бы C-строку: заменяем на \x01 (поле потом отклонится)
        if (*p == '\n' || (*p == '\r' && p[1] != '\n')) { g_lines++; if (text) g_total++; text = false; }
        else if (*p != ' ' && *p != '\t' && *p != '\r') text = true;
    }
    if (text) g_total++;
    for (char* p = g_start; p < g_end;) {                          // сколько корректных многобайтных символов UTF-8 и сколько битых байтов
        unsigned cp; int l = (unsigned char)*p < 0x80 ? 1 : decodeLen((const unsigned char*)p, &cp);
        if (l == 0) { bad++; p++; } else { if (l > 1) ok++; p += l; }
    }
    g_cp1251 = g_start == g_data && ok == 0 && bad >= 3;           // cp1251 - только если корректного UTF-8 нет совсем и битых байтов >= 3
    if (g_cp1251) logEvent("WARN", "ENCODING_GUESS", 0, "корректного UTF-8 в файле нет, читаю как cp1251", "");
    g_pos = g_start;
    return g_total == 0 ? stop("EMPTY_FILE", "input.txt пуст или состоит из пустых строк", 0) : -1;
}
// Следующая строка (\n, \r\n или \r) копируется в line в UTF-8; если она длиннее MAX_LINE - копируется только начало (tooLong)
bool nextLine(char* line, bool* tooLong) {
    if (g_pos >= g_end) return false;
    char* e = g_pos; while (e < g_end && *e != '\n' && *e != '\r') e++;
    size_t len = (size_t)(e - g_pos); *tooLong = len > (size_t)MAX_LINE; if (*tooLong) len = MAX_LINE;
    int n = 0;
    if (!g_cp1251) { memcpy(line, g_pos, len); n = (int)len; }
    else for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)g_pos[i];
        unsigned cp = c < 0x80 ? c : c < 0xC0 ? CP1251[c - 0x80] : 0x410u + c - 0xC0;      // cp1251 -> Юникод -> UTF-8
        if (cp < 0x80) line[n++] = (char)cp;
        else if (cp < 0x800) { line[n++] = (char)(0xC0 | cp >> 6); line[n++] = (char)(0x80 | (cp & 0x3F)); }
        else { line[n++] = (char)(0xE0 | cp >> 12); line[n++] = (char)(0x80 | (cp >> 6 & 0x3F)); line[n++] = (char)(0x80 | (cp & 0x3F)); }
    }
    line[n] = 0;
    if (e < g_end && e[0] == '\r' && e[1] == '\n') e++;
    g_pos = e < g_end ? e + 1 : g_end; return true;
}

// ===== Поля строки и служебные строки =====
struct Word { const char* w; int role; };  // role: 0 автор, 1 название, 2 год, 3 страницы, 4 порог года
const Word WORDS[] = {{"после",4},{"порог",4},{"after",4},{"thresh",4},{"since",4},{"автор",0},{"author",0},{"фио",0},
    {"назв",1},{"title",1},{"книг",1},{"заглав",1},{"name",1},{"год",2},{"year",2},{"стр",3},{"page",3}};
int roleOfName(const char* name) {
    char t[64]; if (strlen(name) > 40) return -1;
    strcpy(t, name); foldKey(t);
    for (size_t i = 0; i < sizeof(WORDS) / sizeof(WORDS[0]); i++) if (strstr(t, WORDS[i].w)) return WORDS[i].role;
    return -1;
}
// Служебная строка: 1 - одиночное число, 2 - «порог: N», 3 - «год: N» (или year=N); 0 - обычная строка
int metaKind(char* line, long long* v) {
    if (!parseInt(line, v)) return 1;
    char* sep = strpbrk(line, "=:");
    if (sep == NULL || sep == line) return 0;
    char c = *sep; *sep = 0; int role = roleOfName(line); *sep = c;
    if ((role == 4 || role == 2) && !parseInt(sep + 1, v)) return role == 4 ? 2 : 3;
    return 0;
}
bool g_bad = false;                        // в строке была ошибка в кавычках
bool isDelim(const char* r, char d) { return d == ' ' ? (r[0] == ' ' && r[1] == ' ') : *r == d; }   // ' ' = 2 и более пробела
// Режет строку на поля прямо в буфере. Кавычки как в CSV: "" внутри поля = одна кавычка
int splitLine(char* s, char d, char** f) {
    int n = 0;
    for (;;) {
        while (*s == ' ' || (*s == '\t' && d != '\t')) s++;
        char *r = s, *w = s;               // r читает, w пишет (w <= r)
        if (*r == '"') {
            for (r++; *r && !(*r == '"' && r[1] != '"');) { if (*r == '"') r++; *w++ = *r++; }
            if (*r == '"') r++; else g_bad = true;            // кавычка не закрыта
            while (*r && !isDelim(r, d)) { if (*r != ' ') g_bad = true; r++; }
        } else {
            while (*r && !isDelim(r, d)) r++;
            for (w = r; w > s && (w[-1] == ' ' || (w[-1] == '\t' && d != '\t'));) w--;
        }
        char more = *r; *w = 0;
        if (n < MAXF) f[n] = s;
        n++;
        if (!more) return n;
        s = r + 1;
    }
}
int trimEmpty(char** f, int n) { while (n > 4 && n <= MAXF && f[n - 1][0] == 0) n--; return n; }   // висящие разделители

// ===== Определяем формат по первым SAMPLE строкам с данными =====
const char* DELIMS = ";,\t| ";             // кандидаты; последний ' ' означает «два и более пробела»
char g_delim = ';'; bool g_header = false; int g_roleAt[4] = {0, 1, 2, 3};   // g_roleAt[колонка] = автор/название/год/страницы
bool isHeader(char** f, int n) {           // заголовок: 4 разных известных названия колонок; порядок колонок берём из него
    int role[4], seen = 0;
    if (n != 4) return false;
    for (int i = 0; i < 4; i++) {
        role[i] = roleOfName(f[i]);
        if (role[i] < 0 || role[i] > 3 || (seen >> role[i] & 1)) return false;
        seen |= 1 << role[i];
    }
    memcpy(g_roleAt, role, sizeof role); return true;
}
void detectFormat(char* line) {
    char tmp[256], first[256] = "", *f[MAXF]; int dn = 0, best = 0, score[5] = {0, 0, 0, 0, 0}; long long v; bool tooLong;
    while (dn < SAMPLE && nextLine(line, &tooLong)) {
        const char* p = line; while (*p == ' ' || *p == '\t') p++;
        if (tooLong || *p == 0 || *p == '#' || (p[0] == '/' && p[1] == '/') || metaKind(line, &v)) continue;   // не данные
        if (dn++ == 0) { strncpy(first, line, 255); first[255] = 0; }
        for (int d = 0; d < 5; d++) {      // при каком разделителе строка даёт ровно 4 поля?
            strncpy(tmp, line, 255); tmp[255] = 0;
            if (trimEmpty(f, splitLine(tmp, DELIMS[d], f)) == 4) score[d]++;
        }
    }
    for (int d = 0; d < 5; d++) if (score[d] > best) { best = score[d]; g_delim = DELIMS[d]; }
    if (best * 10 < dn * 6) {              // ни один разделитель не набрал 60%: откат на таблицу с ';' (вместо отказа от всего файла)
        g_delim = ';'; logEvent("WARN", "FORMAT_FALLBACK", 0, "формат не определён уверенно, беру таблицу с ';' и 4 полями", "");
    }
    g_header = dn > 0 && isHeader(f, trimEmpty(f, splitLine(first, g_delim, f)));
    g_fmtText = std::string("таблица, разделитель ") + (g_delim == '\t' ? "табуляция" : g_delim == ' ' ? "2+ пробела" : std::string(1, g_delim)) +
                (g_header ? ", есть заголовок" : ", без заголовка");
    logEvent("INFO", "FORMAT_DETECTED", 0, g_fmtText.c_str(), "");
}

// ===== Разбор строк и проверка записей =====
void acceptRecord(char** v) {              // v[0..3]: автор, название, год, страницы
    Clock::time_point t0 = Clock::now(); const char* what = "автор"; long long year = 0, pages = 0;
    const char* e = cleanField(v[0]);
    if (!e) { what = "название"; e = cleanField(v[1]); }
    if (!e) { what = "год"; e = parseInt(v[2], &year); if (!e && (year < MIN_YEAR || year > g_maxYear)) e = "YEAR_RANGE"; }   // до текущий год + 1
    if (!e) { what = "страницы"; e = parseInt(v[3], &pages); if (!e && (pages < MIN_PAGES || pages > MAX_PAGES)) e = "PAGES_RANGE"; }
    if (e) reject(e, what, g_lineNo);
    else if (g_count >= g_cap) reject("INTERNAL_LIMIT", "нет места в массиве", g_lineNo);          // защита, не должно случаться
    else { Book* b = &g_books[g_count++]; strcpy(b->author, v[0]); strcpy(b->title, v[1]); b->year = (int)year; b->pages = (int)pages; b->line = g_lineNo; }
    g_validNs += std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
}
// Служебная строка: порог года (берём первый) или количество записей. true - строка обработана
bool takeMeta(int kind, long long v) {
    long long left = g_total - g_idx;      // сколько непустых строк осталось после этой
    if (kind != 2 && g_idx > EDGE && left >= EDGE) return false;       // число посреди данных - обычная (плохая) запись
    if (kind == 1 && (v < 1000 || v > 2100 || (v >= left - 2 && v <= left + 2))) {   // не похоже на год или равно числу строк: количество
        g_declared = v; logCur("INFO", "COUNT_DECLARED", "заявлено количество записей (сверю с фактическим)");
    } else if (g_thrSet) logCur("INFO", "THRESHOLD_IGNORED", "лишний порог года, использован первый");
    else { g_thrSet = true; g_thr = (int)v; logCur("INFO", "THRESHOLD_USED", "порог года"); }
    return true;
}
void handleLine(char* line, bool tooLong) {
    long long v; char* f[MAXF]; char* rec[4]; g_lineNo++;
    strncpy(g_orig, line, sizeof g_orig - 1);                          // начало строки для errors.log (line потом порежется на поля)
    const char* p = line; while (*p == ' ' || *p == '\t') p++;
    if (*p == 0) { if (*line) logCur("INFO", "BLANK_LINE", "строка из одних пробелов"); return; }
    g_idx++;
    if (tooLong) { reject("LINE_TOO_LONG", "строка длиннее 64 КБ, остаток пропущен", g_lineNo); return; }
    if (*p == '#' || (p[0] == '/' && p[1] == '/')) return;             // комментарий
    int kind = metaKind(line, &v);
    if (kind && takeMeta(kind, v)) return;
    g_bad = false;
    int n = trimEmpty(f, splitLine(line, g_delim, f));
    if (g_header) { g_header = false; logCur("INFO", "HEADER_SKIPPED", "строка-заголовок пропущена"); return; }
    if (g_bad) { reject("BAD_QUOTE", "ошибка в кавычках", g_lineNo); return; }
    if (n != 4) { reject("BAD_FIELD_COUNT", "нужно ровно 4 поля", g_lineNo); return; }
    for (int i = 0; i < 4; i++) rec[g_roleAt[i]] = f[i];
    acceptRecord(rec);
}

// ===== Дубликаты и сортировка (устойчивая сортировка слиянием, без рекурсии) =====
bool before(const Item& a, const Item& b) {    // a раньше b? страницы, при равенстве - автор, название, год (результат детерминирован)
    if (a.key != b.key) return a.key < b.key;
    const Book &x = g_books[a.idx], &y = g_books[b.idx];
    int c = strcmp(x.author, y.author); if (c) return c < 0;
    c = strcmp(x.title, y.title); if (c) return c < 0;
    return x.year < y.year;
}
void mergeSort(size_t n) {                 // снизу вверх: куски по 1, 2, 4... элементов; O(n log n) на любых данных
    for (size_t w = 1; w < n; w *= 2) {
        for (size_t lo = 0; lo < n; lo += 2 * w) {
            size_t mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, i = lo, j = mid, k = lo;
            while (i < mid && j < hi) g_tmp[k++] = before(g_items[j], g_items[i]) ? g_items[j++] : g_items[i++];
            while (i < mid) g_tmp[k++] = g_items[i++];
            while (j < hi) g_tmp[k++] = g_items[j++];
        }
        memcpy(g_items, g_tmp, n * sizeof(Item));
    }
}
void dedupe() {                            // ключ = автор + название + год без учёта регистра -> 64-битный хеш; первая встреча остаётся
    std::unordered_map<unsigned long long, size_t> seen;
    for (size_t i = 0; i < g_count; i++) {
        const Book& b = g_books[i]; char k[400];
        sprintf(k, "%s\x1F%s\x1F%d", b.author, b.title, b.year); foldKey(k);
        unsigned long long h = 1469598103934665603ULL ^ g_seed;        // FNV-1a; seed из времени (против подобранных коллизий)
        for (const char* p = k; *p; p++) { h ^= (unsigned char)*p; h *= 1099511628211ULL; }
        std::pair<std::unordered_map<unsigned long long, size_t>::iterator, bool> r = seen.insert(std::make_pair(h, i));
        if (r.second) { g_items[g_final].key = b.pages; g_items[g_final++].idx = i; continue; }
        const Book& a = g_books[r.first->second]; bool conflict = a.pages != b.pages;
        (conflict ? g_conflicts : g_dups)++;
        std::string msg = std::string("такая же книга уже есть в строке ") + N(a.line) + (conflict ? ", но страниц " + std::string(N(a.pages)) + " вместо " + N(b.pages) : "");
        logEvent("WARN", conflict ? "CONFLICT" : "DUPLICATE", b.line, msg.c_str(), b.author);
    }
}

// ===== Отчёт output.txt =====
int chars(const char* s) { int n = 0; for (; *s; s++) if ((*s & 0xC0) != 0x80) n++; return n; }   // символов в UTF-8
void putRow(size_t no, const Book& b) {    // колонки выровнены по символам, а не по байтам
    fprintf(g_out, "%6s  %s%*s  %s%*s  %5d  %6d\n", N(no), b.author, MAX_CHARS - chars(b.author), "", b.title, MAX_CHARS - chars(b.title), "", b.year, b.pages);
}
void writeReport() {
    const char* names[6] = {"чтение и разбор", "проверка записей", "дубликаты", "сортировка", "фильтр по году", "запись (до этой строки)"};
    size_t after = 0; Clock::time_point t = Clock::now();
    for (size_t i = 0; i < g_final; i++) if (g_books[g_items[i].idx].year > g_thr) after++;      // фильтр: год СТРОГО больше порога
    g_ms[4] = lap(&t);
    fprintf(g_out, "=== Электронная библиотека: отчёт ===\nФайл: %s\nФормат: %s\n", g_inShown.c_str(), g_fmtText.c_str());
    fprintf(g_out, "Порог года: %d (%s); берутся книги с годом СТРОГО больше порога\n", g_thr, g_thrSet ? "из файла" : "по умолчанию, в файле не найден");
    fprintf(g_out, "--- Статистика ---\nСтрок в файле: %s\nПринято: %s\nОтклонено: %s\nДубликатов: %s\nКонфликтов: %s\nВ каталоге: %s\n",
            N(g_lineNo), N(g_count), N(g_rejected), N(g_dups), N(g_conflicts), N(g_final));
    if (g_noRecords) fputs("Формат не распознан. Поддерживается таблица из 4 полей (автор, название, год, страниц): разделитель ; , табуляция | или 2+ пробела,\n"
        "кавычки как в CSV, заголовок необязателен; порог года - строка 2000, year=2000 или «после: 2000» в начале/конце файла. Пример:\n"
        "Автор;Название;Год;Страниц  /  \"Толстой, Лев\";Война и мир;1869;1225\n", g_out);
    fputs("--- Каталог (по возрастанию числа страниц) ---\n", g_out);
    for (size_t i = 0; i < g_final; i++) putRow(i + 1, g_books[g_items[i].idx]);
    fprintf(g_out, "--- Книги, изданные после %d года: %s ---\n", g_thr, N(after));
    for (size_t i = 0, k = 0; i < g_final; i++) if (g_books[g_items[i].idx].year > g_thr) putRow(++k, g_books[g_items[i].idx]);
    g_ms[5] = lap(&t);
    fputs("--- Время этапов ---\n", g_out);
    for (int i = 0; i < 6; i++) { fprintf(g_out, "%s: %.3f мс\n", names[i], g_ms[i]); printf("stage %d: %.3f ms\n", i + 1, g_ms[i]); }
}

// ===== Пути, запуск, освобождение ресурсов =====
std::string exeDir(const char* argv0) {    // папка с .exe (вместе с разделителем на конце)
    char b[4096]; std::string p;
#ifdef _WIN32
    long n = (long)GetModuleFileNameA(NULL, b, sizeof b);
#else
    long n = (long)readlink("/proc/self/exe", b, sizeof b - 1);
#endif
    if (n > 0 && n < (long)sizeof b) p.assign(b, (size_t)n); else if (argv0) p = argv0;    // запасной вариант - argv[0]
    size_t i = p.find_last_of("/\\");
    return i == std::string::npos ? std::string() : p.substr(0, i + 1);
}
int run(const char* argv0) {
    time_t now = time(NULL); struct tm* tm = localtime(&now); if (tm) g_maxYear = tm->tm_year + 1901;   // текущий год + 1
    g_seed = (unsigned long long)now;
    std::string dir = exeDir(argv0);
    g_in = fopen((dir + "input.txt").c_str(), "rb"); int err = errno;
    if (!g_in && !dir.empty()) { g_in = fopen("input.txt", "rb"); if (g_in) dir.clear(); }   // рядом с .exe нет - ищем в текущей папке
    g_out = fopen((dir + "output.txt").c_str(), "w"); g_log = fopen((dir + "errors.log").c_str(), "w");
    if (!g_out || !g_log) { printf("ERROR: cannot write output.txt / errors.log: %s\n", strerror(errno)); return 1; }
#ifdef _WIN32
    fputs("\xEF\xBB\xBF", g_out); fputs("\xEF\xBB\xBF", g_log);     // BOM, чтобы Блокнот правильно показал русский текст
#endif
    setvbuf(g_log, NULL, _IOFBF, 1 << 16);                             // лог пишется через буфер, без flush на каждую строку
    char shown[600]; escape((dir + "input.txt").c_str(), shown, 140); g_inShown = shown;
    printf("Input file: %s\n", shown);
    logEvent("INFO", "INPUT_PATH", 0, shown, "");
    if (!g_in) { printf("cannot open input.txt: %s\n", strerror(err)); return stop("INPUT_OPEN", "input.txt не найден или недоступен", 1); }
    int rc = loadFile(); if (rc >= 0) return rc;
    g_cap = (size_t)g_lines + 1;           // записей не больше, чем строк
    if (g_cap > SIZE_MAX / sizeof(Book)) return stop("OUT_OF_MEMORY", "файл слишком большой", 2);   // защита от переполнения size_t
    g_books = new (std::nothrow) Book[g_cap]; g_items = new (std::nothrow) Item[g_cap]; g_tmp = new (std::nothrow) Item[g_cap];
    if (!g_books || !g_items || !g_tmp) return stop("OUT_OF_MEMORY", "не хватило памяти", 2);
    static char line[3 * MAX_LINE + 8]; bool tooLong;      // с запасом: cp1251 при переводе в UTF-8 растёт до 3 раз
    Clock::time_point t = Clock::now();
    detectFormat(line); g_pos = g_start;
    while (nextLine(line, &tooLong)) handleLine(line, tooLong);
    if (!g_thrSet) logEvent("WARN", "THRESHOLD_DEFAULT", 0, "порог года не найден, использую значение по умолчанию", "");
    if (g_declared >= 0 && g_declared != (long long)g_count + g_rejected) logEvent("WARN", "COUNT_MISMATCH", 0, "заявленное количество не совпало с фактическим, верю фактическому", "");
    if (g_count == 0) { g_noRecords = true; logEvent("ERROR", "FORMAT_UNRECOGNIZED", 0, "ни одной корректной записи", ""); }
    g_ms[1] = (double)g_validNs / 1e6; g_ms[0] = lap(&t) - g_ms[1];
    dedupe(); g_ms[2] = lap(&t);
    mergeSort(g_final); g_ms[3] = lap(&t);
    writeReport();
    return 0;
}
int main(int argc, char** argv) {
    int rc = 1;
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);       // консоль в UTF-8; сообщения на английском, данные из файла в консоль не выводим
#endif
        rc = run(argc > 0 ? argv[0] : NULL);
    } catch (const std::bad_alloc&) { printf("ERROR: out of memory\n"); rc = 2; }
    catch (...) { printf("ERROR: unexpected internal error\n"); rc = 1; }
    if (g_log) {                           // итоговая сводка: счётчики по уровням и кодам
        fprintf(g_log, "\n=== СВОДКА (подробно записано: %s) ===\n", N(g_logged));
        for (std::map<std::string, long long>::iterator it = g_cnt.begin(); it != g_cnt.end(); ++it) fprintf(g_log, "%s = %s\n", it->first.c_str(), N(it->second));
        fclose(g_log); g_log = NULL;
    }
    if (g_out) { if ((ferror(g_out) || fclose(g_out) != 0) && rc == 0) { printf("ERROR: cannot write output.txt\n"); rc = 1; } g_out = NULL; }
    if (g_in) { fclose(g_in); g_in = NULL; }
    delete[] g_data; g_data = NULL; delete[] g_books; g_books = NULL; delete[] g_items; g_items = NULL; delete[] g_tmp; g_tmp = NULL;
    return rc;
}
