// =====================================================================
//  Вариант 1. «Склад» (группа 678101, team1)
//
//  input.txt:
//      первая строка — N (количество записей, целое число >= 0);
//      далее N строк вида  name;article;price;quantity
//          name     — название, непустое, не длиннее 30 символов. Считаются
//                     символы, а не байты: русская буква в UTF-8 занимает
//                     2 байта, но это один символ
//          article  — артикул, целое число (int) >= 0, не повторяется
//          price    — цена, дробное число с точкой (double) >= 0
//          quantity — количество, целое число (int) >= 0
//
//  output.txt:
//      первая строка — общая стоимость (сумма price * quantity), 2 знака после точки;
//      далее N строк  name;article;price;quantity,
//      отсортированных по цене (по возрастанию), при равной цене — по артикулу.
//      Цена печатается с 2 знаками после точки.
//
//  N = 0 — правильный пустой список: в output.txt одна строка "0.00".
//  Если input.txt пустой или некорректный — output.txt пустой.
//  Ошибки: некорректное N (тогда остальные строки не проверяются),
//  записей меньше N, не то число полей, не число в числовом поле, дробная
//  часть не через точку, пустое или слишком длинное название, неправильный
//  UTF-8, отрицательное число, повторяющийся артикул.
//  Разбираются ровно N строк записей; что идёт после них, не читается.
//
//  Консоль (stdin/stdout) программа не использует, других файлов,
//  кроме input.txt и output.txt, не создаёт.
//  Размер файла и длина строки не ограничены: файл читается блоками
//  по 64 Кбайт (fread), массив для строки растёт при необходимости.
// =====================================================================

#define _CRT_SECURE_NO_WARNINGS   // чтобы Visual Studio не запрещала fopen и strcpy

#include <stdio.h>    // FILE, fopen, fclose, fread, fseek, fprintf, sscanf
#include <string.h>   // strcpy
#include <ctype.h>    // isdigit
#include <new>        // std::nothrow

// ---------------------------------------------------------------------
//  Константы (в C++ вместо #define лучше const)
// ---------------------------------------------------------------------
const int    NAME_MAX_CHARS = 30;          // название: не больше 30 символов
const int    NAME_BUF       = 30 * 4 + 1;  // символ UTF-8 — до 4 байт, плюс '\0'
const int    LINE_START     = 512;         // начальный размер массива для строки
const int    READ_BUF       = 65536;       // файл читаем блоками по 64 Кбайт
const double INT_MAX_D      = 2147483647.0;    // наибольшее значение int
const double DOUBLE_MAX_D   = 1.7976931348623157e308;   // наибольшее значение double

// ---------------------------------------------------------------------
//  Структуры
// ---------------------------------------------------------------------
struct Product {
    char   name[NAME_BUF];   // название
    int    article;          // артикул
    double price;            // цена
    int    quantity;         // количество
    int    index;            // номер записи в файле — чтобы равные записи не меняли порядок
};

// Всё, что нужно для чтения файла блоками
struct Reader {
    FILE         *f;              // открытый файл
    unsigned char buf[READ_BUF];  // очередной прочитанный блок
    int           size;           // сколько байт в блоке
    int           pos;            // какой байт блока отдадим следующим
    char         *line;           // прочитанная строка (динамический массив)
    int           cap;            // размер массива line
};

// Итог чтения одной строки (перечисление)
enum LineStatus { LINE_OK, LINE_TOO_LONG, LINE_ZERO_BYTE };

// ---------------------------------------------------------------------
//  Чтение файла блоками (блочный ввод: fread, fseek).
//  Берём из файла 64 Кбайт и отдаём по одному байту. Весь файл
//  в память не загружается, поэтому его размер может быть любым.
// ---------------------------------------------------------------------
void startReading(Reader *r, FILE *f) {
    r->f = f;
    r->size = 0;
    r->pos = 0;
    fseek(f, 0, SEEK_SET);                           // в начало файла
}

// Следующий байт файла (0..255) или -1, если файл кончился
int nextByte(Reader *r) {
    if (r->pos >= r->size) {                         // блок кончился — читаем следующий
        r->size = (int)fread(r->buf, 1, READ_BUF, r->f);
        r->pos = 0;
        if (r->size <= 0) {
            r->size = 0;
            return -1;
        }
    }
    return r->buf[r->pos++];
}

// Тот же байт, но не забирая его («подсмотреть»)
int peekByte(Reader *r) {
    int c = nextByte(r);
    if (c != -1) r->pos--;
    return c;
}

// Пропускает метку BOM (байты EF BB BF), которую Блокнот ставит в начало файла
void skipBom(Reader *r) {
    if (peekByte(r) != 0xEF) return;
    if (r->size - r->pos >= 3 && r->buf[r->pos + 1] == 0xBB && r->buf[r->pos + 2] == 0xBF) {
        r->pos += 3;
    }
}

// ---------------------------------------------------------------------
//  Увеличивает массив для строки вдвое: новый массив через new[],
//  копируем старое содержимое, старый удаляем через delete[].
//  Возвращает 0, если памяти не хватило.
// ---------------------------------------------------------------------
int growLine(Reader *r) {
    if (r->cap > 1000000000) return 0;               // больше 1 Гбайт на строку не берём
    int newCap = r->cap * 2;
    char *bigger = new (std::nothrow) char[newCap];
    if (bigger == nullptr) return 0;
    for (int i = 0; i < r->cap; i++) bigger[i] = r->line[i];
    delete [] r->line;
    r->line = bigger;
    r->cap = newCap;
    return 1;
}

// ---------------------------------------------------------------------
//  Читает одну строку в r->line. Возвращает 1, если строка есть, и 0 в конце файла.
//  Концы строк: \n, \r\n или \r. Сам знак конца строки в строку не попадает.
//  Длина строки не ограничена: если массив заполнен, он увеличивается вдвое.
//  Через указатель status сообщает, что со строкой не так.
// ---------------------------------------------------------------------
int readLine(Reader *r, LineStatus *status) {
    int c;
    int len = 0;
    int got = 0;
    *status = LINE_OK;

    while ((c = nextByte(r)) != -1) {
        got = 1;
        if (c == '\n') break;
        if (c == '\r') {
            if (peekByte(r) == '\n') nextByte(r);    // пара \r\n — один конец строки
            break;
        }
        if (c == 0) {
            *status = LINE_ZERO_BYTE;
            continue;
        }
        if (*status == LINE_TOO_LONG) continue;      // памяти не хватило — байты пропускаем
        if (len == r->cap - 1 && !growLine(r)) {
            *status = LINE_TOO_LONG;
            continue;
        }
        r->line[len] = (char)c;
        len++;
    }
    r->line[len] = '\0';                             // строка кончается '\0'
    return got;
}

// ---------------------------------------------------------------------
//  Проверка названия: непустое, не длиннее 30 символов, правильный UTF-8,
//  без управляющих символов. Возвращает 1, если название подходит.
//  Длина считается в символах, а не в байтах: латинская буква — 1 байт,
//  русская в UTF-8 — 2 байта, но и то и другое — один символ.
//  «Молоток слесарный большой» — 25 символов (48 байт), подходит.
// ---------------------------------------------------------------------
int checkName(const char *s) {
    if (s[0] == '\0') return 0;                      // пустое название
    int i = 0;
    int chars = 0;                                   // сколько символов (не байт)
    while (s[i] != '\0') {
        unsigned char c = (unsigned char)s[i];
        int extra;                                   // сколько байт после первого
        if (c < 0x80) extra = 0;
        else if (c >= 0xC2 && c <= 0xDF) extra = 1;
        else if (c >= 0xE0 && c <= 0xEF) extra = 2;
        else if (c >= 0xF0 && c <= 0xF4) extra = 3;
        else return 0;                               // не UTF-8
        if (c < 32 || c == 127) return 0;            // управляющий символ
        for (int k = 1; k <= extra; k++) {
            unsigned char next = (unsigned char)s[i + k];
            if (next < 0x80 || next > 0xBF) return 0; // '\0' сюда тоже попадает
        }
        if (extra > 0) {
            // Лишне длинная запись символа и «суррогаты» в UTF-8 запрещены
            unsigned char second = (unsigned char)s[i + 1];
            if (c == 0xE0 && second < 0xA0) return 0;
            if (c == 0xED && second > 0x9F) return 0;
            if (c == 0xF0 && second < 0x90) return 0;
            if (c == 0xF4 && second > 0x8F) return 0;
        }
        i = i + extra + 1;
        chars++;
    }
    // Каждый символ — не больше 4 байт, поэтому 30 символов помещаются в name[121]
    return chars <= NAME_MAX_CHARS;
}

// ---------------------------------------------------------------------
//  Разбор целого числа (int): только цифры, перед ними может стоять '-'.
//  Пробелы, буквы, '+', точка — ошибка. Результат кладём по адресу out.
//  Число набираем в double, чтобы заметить выход за пределы int.
// ---------------------------------------------------------------------
int parseInt(const char *s, int *out) {
    int i = 0;
    int negative = 0;
    if (s[0] == '-') {
        negative = 1;
        i = 1;
    }
    if (s[i] == '\0') return 0;                      // нет числа
    double value = 0;
    for (; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i])) return 0; // не целое число
        value = value * 10 + (s[i] - '0');          // '7' - '0' = 7
        if (value > INT_MAX_D + 1) return 0;         // дальше не считаем: уже не помещается
    }
    if (negative) value = -value;
    if (value > INT_MAX_D || value < -INT_MAX_D - 1) return 0;   // не помещается в int
    *out = (int)value;                               // явное приведение
    return 1;
}

// ---------------------------------------------------------------------
//  Разбор дробного числа (double): цифры, может быть '-' в начале
//  и одна точка с цифрами по обе стороны: "12", "12.5", "-0.75".
//  Запятая, "1e5", "nan", "inf", ".5", "5." — ошибка.
//
//  Сначала сами проверяем каждый символ, затем переводим строку в число
//  через sscanf с форматом %lf — это тот же scanf, только читает не
//  с клавиатуры, а из строки. Он округляет число точно, как и другие программы.
// ---------------------------------------------------------------------
int parseDouble(const char *s, double *out) {
    int i = 0;
    if (s[0] == '-') i = 1;
    int before = 0;        // цифр до точки
    int after = 0;         // цифр после точки
    int point = 0;         // была ли точка
    for (; s[i] != '\0'; i++) {
        if (isdigit((unsigned char)s[i])) {
            if (point) after++;
            else before++;
        } else if (s[i] == '.' && !point) {
            point = 1;
        } else {
            return 0;                                // не число (дробная часть — через точку)
        }
    }
    if (before == 0 || (point && after == 0)) return 0;
    double value = 0;
    sscanf(s, "%lf", &value);                        // строка уже проверена
    if (value > DOUBLE_MAX_D || value < -DOUBLE_MAX_D) return 0;   // слишком большое для double
    if (value == 0) value = 0;                       // "-0.0" превращаем в обычный ноль
    *out = value;
    return 1;
}

// ---------------------------------------------------------------------
//  Разбор строки-записи name;article;price;quantity в структуру p.
//  Возвращает 1, если запись правильная.
// ---------------------------------------------------------------------
int parseRecord(char *line, LineStatus status, Product *p) {
    if (status != LINE_OK) return 0;                 // нулевой байт или не хватило памяти

    // Делим строку на поля: каждую ';' заменяем на '\0'.
    // field — массив из 4 указателей на начала полей
    char *field[4];
    int count = 1;
    field[0] = line;
    for (char *c = line; *c != '\0'; c++) {
        if (*c == ';') {
            *c = '\0';
            if (count < 4) field[count] = c + 1;
            count++;
        }
    }
    if (count != 4) return 0;                        // нужно 4 поля: name;article;price;quantity

    int article = 0;
    double price = 0;
    int quantity = 0;
    if (!checkName(field[0])) return 0;
    if (!parseInt(field[1], &article)) return 0;
    if (!parseDouble(field[2], &price)) return 0;
    if (!parseInt(field[3], &quantity)) return 0;

    // Отрицательных значений быть не должно
    if (article < 0 || price < 0 || quantity < 0) return 0;

    strcpy(p->name, field[0]);                       // копирование вместе с '\0'
    p->article = article;
    p->price = price;
    p->quantity = quantity;
    return 1;
}

// ---------------------------------------------------------------------
//  Порядок записей: цена по возрастанию, при равной цене — артикул
//  по возрастанию, при полном совпадении — порядок в файле.
//  Возвращает 1, если x должен стоять раньше y.
// ---------------------------------------------------------------------
int goesBefore(const Product *x, const Product *y) {
    if (x->price != y->price) return x->price < y->price;
    if (x->article != y->article) return x->article < y->article;
    return x->index < y->index;
}

// Порядок для поиска повторов: артикул, затем порядок в файле
int byArticle(const Product *x, const Product *y) {
    if (x->article != y->article) return x->article < y->article;
    return x->index < y->index;
}

// Указатель на функцию сравнения: сортировка не знает, по какому полю сортирует
typedef int (*Before)(const Product *, const Product *);

// ---------------------------------------------------------------------
//  Сортировка слиянием — основная: около n*log2(n) сравнений.
//  Сортировка прямым выбором делает n*n/2 сравнений:
//  на 100 000 записей это 5 миллиардов, а слиянием — меньше 2 миллионов.
//
//  Идея: соседние куски длиной 1 сливаем в упорядоченные куски длиной 2,
//  те — в куски длиной 4, потом 8 и так далее, пока не получится весь массив.
//  Для слияния нужен второй массив такого же размера.
//  Возвращает 1 при успехе и 0, если на второй массив не хватило памяти.
// ---------------------------------------------------------------------
int mergeSort(Product *a, int n, Before before) {
    if (n < 2) return 1;
    Product *buf = new (std::nothrow) Product[n];
    if (!buf) return 0;

    Product *src = a;      // откуда берём
    Product *dst = buf;    // куда складываем
    for (int width = 1; width < n; width = width * 2) {
        for (int left = 0; left < n; left = left + 2 * width) {
            int mid = left + width;                  // конец левого куска
            if (mid > n) mid = n;
            int right = left + 2 * width;            // конец правого куска
            if (right > n) right = n;
            int i = left, j = mid, k = left;
            while (i < mid && j < right) {           // берём меньший из двух первых
                if (before(&src[j], &src[i])) dst[k++] = src[j++];
                else                              dst[k++] = src[i++];
            }
            while (i < mid)   dst[k++] = src[i++];   // остаток левого куска
            while (j < right) dst[k++] = src[j++];   // остаток правого куска
        }
        Product *t = src;                            // обмен через временную переменную
        src = dst;
        dst = t;
    }
    if (src != a) {                                  // результат во втором массиве — копируем
        for (int i = 0; i < n; i++) a[i] = src[i];
    }
    delete [] buf;
    buf = nullptr;                                   // нет висячего указателя
    return 1;
}

// Запасная сортировка прямым выбором: дополнительная память не нужна.
// Используется, только если на сортировку слиянием не хватило памяти.
void selectionSort(Product *a, int n, Before before) {
    for (int i = 0; i < n - 1; i++) {
        int best = i;                                // «рекорд» — минимальный элемент
        for (int j = i + 1; j < n; j++) {
            if (before(&a[j], &a[best])) best = j;
        }
        if (best != i) {
            Product t = a[i];                        // обмен через временную переменную
            a[i] = a[best];
            a[best] = t;
        }
    }
}

// ---------------------------------------------------------------------
//  Проход по файлу. Если items == NULL — только проверка,
//  иначе правильные записи складываются в массив items.
//  Возвращает 1, если файл правильный. В *n кладёт N из первой строки,
//  в *complete — 1, если в файле есть все N записей.
//  Некорректное N — сразу 0, остальные строки не проверяются.
//  Читаются ровно N строк записей, всё, что после них, не читается.
// ---------------------------------------------------------------------
int processFile(Reader *r, FILE *in, Product *items, int *n, int *complete) {
    LineStatus status;
    int ok = 1;
    *complete = 0;

    startReading(r, in);
    skipBom(r);

    // 1) Первая строка — N
    if (!readLine(r, &status)) return 0;             // файл пустой
    int count = 0;
    if (status != LINE_OK) return 0;
    if (!parseInt(r->line, &count)) return 0;
    if (count < 0) return 0;
    *n = count;

    // 2) Ровно N записей
    int got = 0;
    Product tmp;
    while (got < count && readLine(r, &status)) {
        if (parseRecord(r->line, status, &tmp)) {
            tmp.index = got;
            if (items != NULL) items[got] = tmp;     // структуры можно присваивать целиком
        } else {
            ok = 0;
            if (items == NULL) return 0;             // при проверке дальше читать незачем
        }
        got++;
    }
    if (got < count) return 0;                       // записей меньше, чем N
    *complete = 1;
    // Строки после N записей не читаем: на результат они не влияют
    return ok;
}

// ---------------------------------------------------------------------
//  Повторяющиеся артикулы (дублирование ключа) — ошибка.
//  Сортируем записи по артикулу: одинаковые окажутся рядом.
//  Возвращает 1, если есть хотя бы один повтор.
// ---------------------------------------------------------------------
int hasDuplicates(Product *items, int n) {
    if (!mergeSort(items, n, byArticle)) selectionSort(items, n, byArticle);
    for (int i = 1; i < n; i++) {
        if (items[i].article == items[i - 1].article) return 1;
    }
    return 0;
}

// =====================================================================
//  Главная функция: выполнение программы начинается с main
// =====================================================================
int main() {
    // Файлы — в текущей папке (рядом с программой при запуске двойным щелчком)
    FILE *in = fopen("input.txt", "rb");             // "b" — байты как есть
    // "wb" — двоичный режим: строки кончаются на \n, как в ожидаемых ответах.
    // В режиме "w" Windows заменила бы каждый \n на \r\n.
    FILE *out = fopen("output.txt", "wb");           // пустой output.txt создаётся сразу
    if (out == NULL) {
        if (in != NULL) fclose(in);
        return 1;
    }
    if (in == NULL) {                                // input.txt не найден — output.txt пустой
        fclose(out);
        return 1;
    }

    // Читатель занимает 64 Кбайт — создаём в куче, а не в стеке
    Reader *reader = new (std::nothrow) Reader;
    if (reader != nullptr) {
        reader->cap = LINE_START;
        reader->line = new (std::nothrow) char[LINE_START];
        if (reader->line == nullptr) {
            delete reader;
            reader = nullptr;
        }
    }
    Product *items = nullptr;
    int n = 0;
    int complete = 0;
    int valid = 0;
    double total = 0;                                // общая стоимость

    if (reader != nullptr) {
        // Проход 1: проверка формата, в память ничего не берём
        valid = processFile(reader, in, NULL, &n, &complete);
        // Память выделяем, только когда файл правильный и в нём есть все N записей
        // (иначе "N = 2000000000" и одна строка заняли бы гигабайты)
        if (valid && complete && n > 0) {
            items = new (std::nothrow) Product[n];
            if (items == nullptr) {
                valid = 0;                           // не хватило памяти для записей
            } else {
                processFile(reader, in, items, &n, &complete);   // проход 2: чтение
                // Общая стоимость (типовой алгоритм «сумма») — в порядке файла,
                // до любых сортировок, чтобы сумма double не зависела от порядка
                for (int i = 0; i < n; i++) total = total + items[i].price * items[i].quantity;
                if (hasDuplicates(items, n)) valid = 0;
            }
        }
        delete [] reader->line;
        reader->line = nullptr;
        delete reader;
        reader = nullptr;
    }
    fclose(in);

    if (valid && total <= DOUBLE_MAX_D && total >= -DOUBLE_MAX_D) {
        if (!mergeSort(items, n, goesBefore)) selectionSort(items, n, goesBefore);
        fprintf(out, "%.2f\n", total);
        for (int i = 0; i < n; i++) {
            fprintf(out, "%s;%d;%.2f;%d\n",
                    items[i].name, items[i].article, items[i].price, items[i].quantity);
        }
    }

    delete [] items;                                 // для массива — delete []
    items = nullptr;
    fclose(out);
    return 0;
}
