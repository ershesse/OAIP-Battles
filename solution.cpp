#include <iostream>
#include <fstream>
#include <cstring>
#include <locale>
#include <ctime>
#include <cmath>

using namespace std;

struct Humanoid
{
    char Name[81];
    unsigned int Age;
    double Score;
    unsigned int Bad_Score;
    double Result_Score;

};

void log_Error(int Error_Line_Num, const char* Typa_Error_Happens)
{
    static ofstream log_File("log.txt", ios::app);
    if (log_File.is_open())
    {
        log_File << "строка\t" << Error_Line_Num << "\t" << Typa_Error_Happens << endl;
    }

}

bool line_Reading(ifstream& in, char* buffer, int maxLenght, bool& Too_Long, bool& So_Epmty)
{
    int idx = 0;
    Too_Long = false;
    So_Epmty = false;
    char Character;

    while (in.get(Character)) 
    {
        if (Character == '\n')
        {
            break;
        }
        if (Character == '\0')
        {
            So_Epmty = true;
        }
        if (idx < maxLenght - 1)
        {
            buffer[idx++] = Character;
        }
        else
        {
            Too_Long = true;
        }

    }

    buffer[idx] = '\0';
    return (idx > 0 || !in.eof());

}

bool Convert_to_Number(const char* line, double& result, bool Allow_Dot)
{
    if (!line || line[0] == '\0')
    {
        return false;
    }

    double value_Part = 0.0;
    double fractional_Part = 0.1;
    bool has_Dot = false;
    bool has_Digits = false;

    for (int i = 0; line[i] != '\0'; i++)
    {
        if (line[i] == '.')
        {
            if (!Allow_Dot || has_Dot)
            {
                return false;
            }
            else has_Dot = true;

        }
        else if (line[i] >= '0' && line[i] <= '9')
        {
            has_Digits = true;
            int Digit = line[i] - '0';
            if (!has_Dot)
            {
                value_Part = value_Part * 10.0 + Digit;
            }
            else
            {
                value_Part += Digit * fractional_Part;
                fractional_Part *= 0.1;
            }

            if (value_Part > 1000000.0)
            {
                return false;
            }

        }
        else
        {
            return false;
        }

    }

    if (!has_Digits)
    {
        return false;
    }
    
    result = value_Part;
    return true;

}

bool isBetter(const Humanoid& a, const Humanoid& b)
{
    if (abs(a.Result_Score - b.Result_Score) > 1e-9)
    {
        return a.Result_Score > b.Result_Score;
    }
    if (abs(a.Score - b.Score) > 1e-9)
    {
        return a.Score > b.Score;
    }
    return strcmp(a.Name, b.Name) < 0;
}

bool ru_RUwUTF8(const char* str, int& i)
{
    unsigned char c1 = static_cast<unsigned char>(str[i]);
    if (c1 == '-')
    {
        return true;
    }

    if (c1 == 0xD0 || c1 == 0xD1)
    {
        unsigned char c2 = static_cast<unsigned char>(str[i + 1]);
        if (c2 == '\0') return false;

        bool Kirill_Priv = (c1 == 0xD0 && ((c2 >= 0x90 && c2 <= 0xBF) || c2 == 0x81)) || (c1 == 0xD1 && ((c2 >= 0x80 && c2 <= 0x8F) || c2 == 0x91));

        if (Kirill_Priv)
        {
            i++;
            return true;
        }
    }

    return false;
}

void FIO_to_perfection(const char* source, char* final_point, const char* separators)
{
    int j = 0;
    for (int i = 0; source[i] != '\0'; i++)
    {
        unsigned char c1 = static_cast<unsigned char>(source[i]);
        if (strchr(separators, c1) != nullptr)
        {
            if (j > 0 && final_point[j - 1] != ' ') final_point[j++] = ' ';
            continue;
        }

        if ((c1 == 0xD0 || c1 == 0xD1) && source[i + 1] != '\0')
        {
            unsigned char c2 = static_cast<unsigned char>(source[i + 1]);
            if ((c1 == 0xD0 && c2 == 0x81) || (c1 == 0xD1 && c2 == 0x91))
            {
                final_point[j++] = static_cast<char>(0xD0);
                final_point[j++] = static_cast<char>(0xB5);
            }
            else if (c1 == 0xD0 && c2 >= 0x90 && c2 <= 0x9F)
            {
                final_point[j++] = static_cast<char>(0xD0);
                final_point[j++] = static_cast<char>(c2 + 0x20);
            }
            else if (c1 == 0xD0 && c2 >= 0xA0 && c2 <= 0xAF)
            {
                final_point[j++] = static_cast<char>(0xD1);
                final_point[j++] = static_cast<char>(c2 - 0x20);
            }
            else
            {
                final_point[j++] = source[i];
                final_point[j++] = source[i + 1];
            }
            i++;
            continue;

        }

        final_point[j++] = source[i];
    }

    final_point[j] = '\0';

}

bool decrypt_Line(const char* incrypted_Line, int line_Number, Humanoid& output_Humanoid, const Humanoid* existing, int count, double coeff)
{
    char buffer[256];
    strncpy(buffer, incrypted_Line, 255);
    buffer[255] = '\0';

    const char* separators = " \t;,|:";
    int pos = strlen(buffer) - 1;

    while (pos >= 0 && strchr(separators, buffer[pos]) != nullptr) pos--;
    if (pos < 0)
    {
        log_Error(line_Number, "эта строка такая пустая...");
        return false;
    }

    char* nums[3];
    for (int n = 2; n >= 0; n--)
    {
        int endNum = pos;
        while (pos >= 0 && strchr(separators, buffer[pos]) == nullptr) pos--;
        buffer[endNum + 1] = '\0';
        nums[n] = &buffer[pos + 1];

        while (pos >= 0 && strchr(separators, buffer[pos]) != nullptr) pos--;
        if (pos < 0)
        {
            log_Error(line_Number, "не хватает данных (добавьте ФИО или другое недостающее значеник по образцу)");
            return false;
        }

    }

    buffer[pos + 1] = '\0';
    char* namePtr = buffer;
    while (*namePtr != '\0' && strchr(separators, *namePtr) != nullptr) namePtr++;

    int nameLen = strlen(namePtr);
    if (nameLen < 8 || nameLen > 80)
    {
        log_Error(line_Number, "длина этого ФИО уже переходит все рамки дозволенного");
        return false;
    }

    for (int i = 0; i < nameLen; i++)
    {
        unsigned char c = static_cast<unsigned char>(namePtr[i]);
        if ((c >= '0' && c <= '9') || c < 32)
        {
            log_Error(line_Number, "графа ФИО заполненна некорректно");
            return false;
        }

    }

    int wordCount = 0;
    bool inWord = false;
    for (int i = 0; i < nameLen; i++)
    {
        unsigned char c = static_cast<unsigned char>(namePtr[i]);

        if (strchr(separators, c) != nullptr)
        {
            inWord = false;
        }
        else
        {
            if (!ru_RUwUTF8(namePtr, i))
            {
                log_Error(line_Number, "графа ФИО должна содержать только кириллицу");
                return false;
            }

            if (!inWord)
            {
                inWord = true;
                wordCount++;
            }
        }
    }

    if (wordCount != 3)
    {
        log_Error(line_Number, "ФИО должно содержать, что не удивительно, и Ф, и И, и О!!");
        return false;
    }

    char normalizedNew[81];
    FIO_to_perfection(namePtr, normalizedNew, separators);
    for (int i = 0; i < count; i++)
    {
        char normalizedExisting[81];
        FIO_to_perfection(existing[i].Name, normalizedExisting, separators);
        if (strcmp(normalizedExisting, normalizedNew) == 0)
        {
            log_Error(line_Number, "данные этого участника уже были занесены в список ранее/");
            return false;

        }

    }

    double ageVal = 0, scoreVal = 0, badVal = 0;

    if (!Convert_to_Number(nums[0], ageVal, false) || ageVal < 0 || ageVal > 1000)
    {
        log_Error(line_Number, "значение выходит за переделы диапазона привычного представления о возрасте человеческой единицы");
        return false;
    }

    if (!Convert_to_Number(nums[1], scoreVal, true) || scoreVal > 100000.0)
    {
        log_Error(line_Number, "значение выходит за пределы диапазона от 0 до 10000");
        return false;
    }

    if (!Convert_to_Number(nums[2], badVal, false) || badVal > 10000)
    {
        log_Error(line_Number, "нет ну это уже реально слишком(значения от 0 до 10000 было мало?)");
        return false;
    }

    strcpy(output_Humanoid.Name, namePtr);
    output_Humanoid.Age = static_cast<unsigned int>(ageVal);
    output_Humanoid.Score = scoreVal;
    output_Humanoid.Bad_Score = static_cast<unsigned int>(badVal);
    output_Humanoid.Result_Score = scoreVal - badVal*coeff ;

    return true;

}

void quickSort(Humanoid* list_Array, int left_Direction, int right_Direction)
{
    if (left_Direction >= right_Direction)
    {
        return;
    }
    
    Humanoid pivot = list_Array[(left_Direction + right_Direction) / 2];
    int i = left_Direction, j = right_Direction;
    while (i <= j)
    {
        while (isBetter(list_Array[i], pivot))
        {
            i++;
        }
        
        while (isBetter(pivot, list_Array[j]))
        {
            j--;
        }

        if (i <= j)
        {
            Humanoid temp = list_Array[i];
            list_Array[i] = list_Array[j];
            list_Array[j] = temp;
            i++;
            j--;

        }

    }

    if (left_Direction < j)
    {
        quickSort(list_Array, left_Direction, j);
    }

    if (i < right_Direction)
    {
        quickSort(list_Array, i, right_Direction);
    }

}

bool parseHeader(char* line, int& N, double& coeff)
{
    const char* separators = " \t;,|:";
    char* p = line;

    while (*p != '\0' && strchr(separators, *p) != nullptr) 
    {
        p++;
    }
    if (*p == '\0')
    {
        return false;
    }

    char* nStr = p;
    while (*p != '\0' && strchr(separators, *p) == nullptr) 
    {
        p++;
    }

    if (*p == '\0')
    {
        return false;
    }
    *p = '\0';
    p++;

   while (*p != '\0' && strchr(separators, *p) != nullptr)
    {
        p++;
    }
    if (*p == '\0')
    {
        return false;
    }

    char* coeffStr = p;
    while (*p != '\0' && strchr(separators, *p) == nullptr)
    {
        p++;
    }
    *p = '\0';

    double nVal = 0.0;
    if (!Convert_to_Number(nStr, nVal, false) || nVal < 0 || nVal > 100000)
    {
        return false;
    }
    if (!Convert_to_Number(coeffStr, coeff, true))
    {
        return false;
    }

    N = static_cast<int>(nVal);
    return true;
    
}

int main()
{
    setlocale(LC_ALL, "ru_RU.UTF-8");
    clock_t Start = clock();

    ifstream in;
    in.open("input.txt");
    if (!in.is_open())
    {
        log_Error(0, strerror(errno));
        cerr << "ощибка:" << strerror(errno) << endl;
        return 1;

    }

    const int BUF_SIZE = 256;
    int All_The_Lines = 0;
    char buffer[BUF_SIZE];
    bool Too_Long = false;
    bool So_Empty = false;

    if (!line_Reading(in, buffer, BUF_SIZE, Too_Long, So_Empty) || Too_Long)
    {
        log_Error(1, "ошибка чтения заголовка, т.е. N и/или коэффициентп");
        ofstream out("output.txt");
        if (out.is_open()) out << 0 << "\n";
        return 0;

    }

    int len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\r') buffer[len - 1] = '\0';

    int N = 0;
    double coeff = 0.0;
    if (!parseHeader(buffer, N, coeff) || N == 0)
    {
        log_Error(1, "некорректные значения N или коэффициента в первой строке");
        ofstream out("output.txt");
        if (out.is_open()) out << 0 << "\n";
        return 0;
    

    }

    Humanoid* humanoid = new Humanoid[N];
    int valid_Counter = 0;

    for (int i = 0; i < N; i++)
    {
        int lineNum = i + 2;

        if (!line_Reading(in, buffer, BUF_SIZE, Too_Long, So_Empty))
        {
            log_Error(lineNum, "файл закончился раньше, чем было прочитано N строк");
            break;
        }

        if (Too_Long)
        {
            log_Error(lineNum, "максимальная длина строки была превышена");
            continue;
        }

        len = strlen(buffer);
        if (len > 0 && buffer[len - 1] == '\r') buffer[len - 1] = '\0';

        if (strlen(buffer) == 0)
        {
            log_Error(lineNum, "на этой строке так пусто...");
            continue;
        }

        if (decrypt_Line(buffer, lineNum, humanoid[valid_Counter], humanoid, valid_Counter, coeff))
        {
            valid_Counter++;
        }
        
    }
    in.close();

    if (valid_Counter > 1)
    {
        quickSort(humanoid, 0, valid_Counter - 1);
    }

    ofstream out("output.txt");
    if (out.is_open())
    {
        for (int i = 0; i < valid_Counter; i++)
        {
            out << (i + 1) << ". ";

            if (i < 3)
            {
                out << humanoid[i].Name << ";"
                    << humanoid[i].Age << ";"
                    << humanoid[i].Score << ";"
                    << humanoid[i].Bad_Score << ";"
                    << humanoid[i].Result_Score << "\n";
                    
            }
            else
            {
                out << humanoid[i].Result_Score << ";"
                    << humanoid[i].Score << ";"
                    << humanoid[i].Name << "\n";

            }
            
        }

        delete[] humanoid;
        humanoid = nullptr;

        clock_t End = clock();
        out << "Время работы программы: " << static_cast<double>(End - Start) / CLOCKS_PER_SEC * 1000.0 << " мс" << endl;
        out.close();
    }
    else
    {
        delete[] humanoid;
        humanoid = nullptr;
        log_Error(0, strerror(errno));
        cerr << "не удалост создать output.txt: " << strerror(errno) << endl;
    }

    return 0;
}