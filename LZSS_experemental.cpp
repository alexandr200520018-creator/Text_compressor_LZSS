#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <filesystem>
#include <bitset>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <clocale>
#include <windows.h>
#include <set>

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#endif

namespace fs = std::filesystem;

// Функция для преобразования UTF-8 строки в wide string (UTF-16)
std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (len <= 0) return L"";
    std::wstring wide(len - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wide[0], len);
    return wide;
}

// Функция для преобразования wide string в UTF-8
std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, NULL, 0, NULL, NULL);
    if (len <= 0) return "";
    std::string utf8(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], len, NULL, NULL);
    return utf8;
}

// Функция для чтения файла в UTF-8 и преобразования в ANSI (CP1251) для работы
std::string readUtf8FileAsAnsi(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) return "";

    std::string utf8Content((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    // Удаляем BOM (Byte Order Mark) если он есть
    if (utf8Content.length() >= 3 &&
        (unsigned char)utf8Content[0] == 0xEF &&
        (unsigned char)utf8Content[1] == 0xBB &&
        (unsigned char)utf8Content[2] == 0xBF) {
        utf8Content = utf8Content.substr(3);
    }

    // Удаляем символы возврата каретки
    utf8Content.erase(std::remove(utf8Content.begin(), utf8Content.end(), '\r'), utf8Content.end());

    // Пробуем преобразовать UTF-8 в UTF-16
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8Content.c_str(), -1, NULL, 0);
    if (wideLen <= 0) {
        return utf8Content;
    }

    std::vector<wchar_t> wide(wideLen);
    MultiByteToWideChar(CP_UTF8, 0, utf8Content.c_str(), -1, wide.data(), wideLen);

    // Преобразуем UTF-16 в CP1251 (ANSI)
    int ansiLen = WideCharToMultiByte(1251, 0, wide.data(), -1, NULL, 0, NULL, NULL);
    if (ansiLen <= 0) {
        return utf8Content;
    }

    std::string ansi(ansiLen - 1, '\0');
    WideCharToMultiByte(1251, 0, wide.data(), -1, &ansi[0], ansiLen, NULL, NULL);

    return ansi;
}

// Функция для сохранения строки в UTF-8 файл без BOM
void saveStringToUtf8File(const std::string& filename, const std::string& content) {
    std::ofstream file(filename, std::ios::binary);
    if (!file) return;

    // Конвертируем ANSI (CP1251) в UTF-8
    std::wstring wide;
    int len = MultiByteToWideChar(1251, 0, content.c_str(), -1, NULL, 0);
    if (len > 0) {
        wide.resize(len - 1);
        MultiByteToWideChar(1251, 0, content.c_str(), -1, &wide[0], len);

        std::string utf8 = wideToUtf8(wide);
        file.write(utf8.c_str(), utf8.length());
    }
    file.close();
}

void setConsoleEncoding() {
#ifdef _WIN32
    SetConsoleOutputCP(1251);
    SetConsoleCP(1251);
#endif
    std::setlocale(LC_ALL, "Russian");
}

// Функция для получения строки с текущим временем для имени файла
std::string getTimeForFilename() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    char buffer[80];

#ifdef _WIN32
    localtime_s(&timeinfo, &now);
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &timeinfo);
#else
    localtime_r(&now, &timeinfo);
    strftime(buffer, sizeof(buffer), "%Y%m%d_%H%M%S", &timeinfo);
#endif
    return std::string(buffer);
}

class LZSSCompressor {
private:
    std::map<char, std::string> charToCode;
    std::map<std::string, char> codeToChar;
    std::set<char> allowedChars;
    long int dictSize;
    long int bufferSize;
    bool useAsciiMode;  // Режим кодирования: true - ASCII коды, false - таблица из файла
    bool useExtendedAscii; // Использовать расширенный ASCII (8 бит)

    // Преобразование символа в 8-битный код (для расширенного ASCII)
    std::string charToExtendedBinary(char c) {
        unsigned char uc = static_cast<unsigned char>(c);
        return std::bitset<8>(uc).to_string();
    }

    // Преобразование 8-битного кода в символ
    char extendedBinaryToChar(const std::string& binary) {
        int val = std::stoi(binary, nullptr, 2);
        return static_cast<char>(val);
    }

    // Преобразование символа в 7-битный ASCII код
    std::string charToAsciiBinary(char c) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (uc > 127) {
            // Для не-ASCII символов используем 8 бит
            return std::bitset<8>(uc).to_string();
        }
        return std::bitset<7>(uc).to_string();
    }

    // Преобразование ASCII кода в символ
    char asciiBinaryToChar(const std::string& binary, bool is8bit) {
        int ascii = std::stoi(binary, nullptr, 2);
        return static_cast<char>(ascii);
    }

    std::string binaryToHex(const std::string& binary) {
        std::string hex;
        std::string padded = binary;

        while (padded.length() % 4 != 0) {
            padded += '0';
        }

        for (size_t i = 0; i < padded.length(); i += 4) {
            std::string nibble = padded.substr(i, 4);
            int val = 0;
            for (char c : nibble) {
                val = (val << 1) | (c - '0');
            }
            if (val < 10) {
                hex += static_cast<char>('0' + val);
            }
            else {
                hex += static_cast<char>('A' + (val - 10));
            }
        }
        return hex;
    }

    std::string hexToBinary(const std::string& hex) {
        std::string binary;
        for (char c : hex) {
            int val;
            if (c >= '0' && c <= '9') {
                val = c - '0';
            }
            else if (c >= 'A' && c <= 'F') {
                val = 10 + (c - 'A');
            }
            else if (c >= 'a' && c <= 'f') {
                val = 10 + (c - 'a');
            }
            else {
                continue;
            }

            // Преобразуем в 4 бита
            for (int i = 3; i >= 0; i--) {
                binary += ((val >> i) & 1) ? '1' : '0';
            }
        }
        return binary;
    }

    int getBitSize(int n) {
        if (n <= 1) return 1;
        return static_cast<int>(std::ceil(std::log2(static_cast<double>(n))));
    }

    std::string getCurrentTime() {
        time_t now = time(nullptr);
        struct tm timeinfo;
        char buffer[80];

#ifdef _WIN32
        localtime_s(&timeinfo, &now);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
#else
        localtime_r(&now, &timeinfo);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
#endif
        return std::string(buffer);
    }

    // Форматирование строки словаря фиксированной длины
    std::string formatDictWindow(const std::string& window) {
        if (window.length() <= static_cast<size_t>(dictSize)) {
            // Дополняем 'е' (пустым символом) слева до размера словаря
            std::string result(dictSize - window.length(), 'е');
            result += window;
            return result;
        }
        else {
            // Берём последние dictSize символов
            return window.substr(window.length() - dictSize);
        }
    }

    void logCompression(const std::string& message,
        const std::vector<std::tuple<std::string, std::string, std::string, int, int, std::string>>& steps,
        const std::string& compressedHex,
        const std::map<char, std::string>& codes) {
        std::ofstream log("compression_log.txt", std::ios::app);
        if (!log) return;

        log << "\n=== LOG " << getCurrentTime() << " ===\n";

        log << "Code table:\n";
        if (useAsciiMode) {
            if (useExtendedAscii) {
                log << "Mode: Extended ASCII (8-bit)\n";
            }
            else {
                log << "Mode: ASCII (7-bit)\n";
            }
        }
        else {
            for (const auto& pair : codes) {
                log << pair.first << "\t" << pair.second << "\n";
            }
        }

        log << "\nCompression table (е represents empty symbol)\n";
        log << "j\tAj (size " << dictSize << ")\tBj (size " << bufferSize << ")\tCj\n";
        log << "\t\t\t\t\t\n";

        for (size_t i = 0; i < steps.size(); i++) {
            log << i << "\t" << std::get<0>(steps[i]) << "\t" << std::get<1>(steps[i]) << "\t"
                << std::get<3>(steps[i]) << "\t" << std::get<2>(steps[i]) << "\t" << std::get<5>(steps[i]) << "\n";
        }

        log << "\nMessage:\n";
        log << "Code\t" << compressedHex << "\n";
        log << "Dictionary size\t" << dictSize << "\n";
        log << "Buffer size\t" << bufferSize << "\n";
        log << "Mode: " << (useAsciiMode ? (useExtendedAscii ? "Extended ASCII (8-bit)" : "ASCII (7-bit)") : "Custom table") << "\n";
        log.close();
    }

    void logDecompression(const std::string& hexCode,
        const std::vector<std::tuple<std::string, std::string, std::string, int, int, std::string>>& steps) {
        std::ofstream log("decompression_log.txt", std::ios::app);
        if (!log) return;

        log << "\n=== LOG " << getCurrentTime() << " ===\n";

        log << "Message:\n";
        log << "Code\t" << hexCode << "\n";
        log << "Dictionary size\t" << dictSize << "\n";
        log << "Buffer size\t" << bufferSize << "\n";
        log << "Mode: " << (useAsciiMode ? (useExtendedAscii ? "Extended ASCII (8-bit)" : "ASCII (7-bit)") : "Custom table") << "\n";

        if (!useAsciiMode) {
            log << "Code table\n";
            for (const auto& pair : codeToChar) {
                log << pair.second << "\t" << pair.first << "\n";
            }
        }

        log << "\nCj\t\t\tAj (size " << dictSize << ")\t\t\tDecoded characters\n";
        for (const auto& step : steps) {
            log << std::get<3>(step) << "\t" << std::get<2>(step) << "\t\t"
                << std::get<0>(step) << "\t" << std::get<1>(step) << "\n";
        }
        log.close();
    }

public:
    LZSSCompressor() : dictSize(0), bufferSize(0), useAsciiMode(false), useExtendedAscii(false) {}

    void setAsciiMode(bool mode, bool extended = false) {
        useAsciiMode = mode;
        useExtendedAscii = extended;

        if (useAsciiMode) {
            // В режиме ASCII разрешаем все символы от 0 до 255
            allowedChars.clear();
            for (int i = 0; i <= 255; i++) {
                allowedChars.insert(static_cast<char>(i));
            }
            if (useExtendedAscii) {
                std::cout << "Extended ASCII mode enabled (8-bit). All 0-255 characters are allowed." << std::endl;
            }
            else {
                std::cout << "ASCII mode enabled (7-bit). All 0-127 characters are allowed." << std::endl;
            }
        }
    }

    bool loadCodes(const std::string& filename) {
        if (useAsciiMode) {
            // В режиме ASCII не нужен файл с кодами
            return true;
        }

        std::string content = readUtf8FileAsAnsi(filename);
        if (content.empty()) {
            std::cerr << "Cannot open or read codes file: " << filename << std::endl;
            return false;
        }

        charToCode.clear();
        codeToChar.clear();
        allowedChars.clear();

        std::istringstream iss(content);
        std::string line;

        while (std::getline(iss, line)) {
            if (line.empty()) continue;

            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            size_t tabPos = line.find('\t');
            if (tabPos == std::string::npos) {
                continue;
            }

            if (tabPos == 0) {
                continue;
            }

            char symbol = line[0];
            std::string code = line.substr(tabPos + 1);

            charToCode[symbol] = code;
            codeToChar[code] = symbol;
            allowedChars.insert(symbol);
        }

        std::cout << "Loaded " << allowedChars.size() << " allowed characters from codes file." << std::endl;

        return !charToCode.empty();
    }

    std::string getAllowedCharsString() {
        if (useAsciiMode) {
            if (useExtendedAscii) {
                return "All 0-255 characters (Extended ASCII)";
            }
            else {
                return "All 0-127 characters (ASCII)";
            }
        }

        std::string result;
        int count = 0;
        for (char c : allowedChars) {
            if (c == '\n') {
                result += "[LF] ";
            }
            else if (c == '\r') {
                result += "[CR] ";
            }
            else if (c == '\t') {
                result += "[TAB] ";
            }
            else if (c == ' ') {
                result += "[SPACE] ";
            }
            else if (c >= 32 && c < 127) {
                result += c;
                result += ' ';
            }
            else {
                result += "[";
                result += std::to_string((int)(unsigned char)c);
                result += "] ";
            }
            count++;
            if (count > 100) {
                result += "...";
                break;
            }
        }
        return result;
    }

    bool validateString(const std::string& str) {
        if (useAsciiMode) {
            // В режиме ASCII проверяем, что символы в диапазоне 0-255 (все допустимы)
            return true;
        }

        for (char c : str) {
            if (allowedChars.find(c) == allowedChars.end()) {
                std::cout << "Invalid character found: '" << c << "' (ASCII: " << (int)(unsigned char)c << ")" << std::endl;
                return false;
            }
        }
        return true;
    }

    const std::set<char>& getAllowedChars() const {
        return allowedChars;
    }

    std::string compress(const std::string& message, int dict, int buffer) {
        dictSize = dict;
        bufferSize = buffer;

        std::string searchWindow;
        std::vector<std::tuple<std::string, std::string, std::string, int, int, std::string>> steps;

        size_t pos = 0;
        std::string compressedBits;

        int dictBitSize = getBitSize(dictSize);
        int bufferBitSize = getBitSize(bufferSize);

        // В ASCII режиме используем 7 или 8 бит на символ
        int charBits = 0;
        if (useAsciiMode) {
            charBits = useExtendedAscii ? 8 : 7;
        }
        else {
            charBits = 6;
        }

        std::cout << "Compression mode: " << (useAsciiMode ? (useExtendedAscii ? "Extended ASCII (8-bit)" : "ASCII (7-bit)") : "Custom table (6-bit)") << std::endl;
        std::cout << "Char bits: " << charBits << std::endl;

        while (pos < message.length()) {
            int maxMatchLen = 0;
            int matchPos = 0;

            int searchStart = static_cast<int>(searchWindow.length()) - dictSize;
            if (searchStart < 0) searchStart = 0;

            for (int i = searchStart; i < static_cast<int>(searchWindow.length()); i++) {
                int len = 0;
                while (len < bufferSize &&
                    pos + static_cast<size_t>(len) < message.length() &&
                    static_cast<size_t>(i + len) < searchWindow.length() &&
                    searchWindow[static_cast<size_t>(i + len)] == message[static_cast<size_t>(pos + len)]) {
                    len++;
                }
                if (len > maxMatchLen) {
                    maxMatchLen = len;
                    matchPos = static_cast<int>(searchWindow.length()) - i;
                }
            }

            std::string aj = formatDictWindow(searchWindow);
            int remainingLength = static_cast<int>(message.length() - pos);
            int bufferLen = (bufferSize < remainingLength) ? bufferSize : remainingLength;
            std::string bj = message.substr(pos, static_cast<size_t>(bufferLen));

            if (maxMatchLen >= 2 && matchPos > 0) {
                compressedBits += '1';

                if (matchPos > dictSize) {
                    matchPos = dictSize;
                }

                if (maxMatchLen > bufferSize) {
                    maxMatchLen = bufferSize;
                }

                std::string offsetBits = std::bitset<16>(static_cast<unsigned long long>(matchPos)).to_string().substr(16 - dictBitSize);
                int encodedLen = maxMatchLen - 1;
                std::string lenBits = std::bitset<16>(static_cast<unsigned long long>(encodedLen)).to_string().substr(16 - bufferBitSize);

                compressedBits += offsetBits + lenBits;

                std::string matched = message.substr(pos, static_cast<size_t>(maxMatchLen));
                steps.emplace_back(aj, bj, offsetBits + lenBits, 1, maxMatchLen, matched);

                searchWindow += matched;
                pos += static_cast<size_t>(maxMatchLen);
            }
            else {
                compressedBits += '0';

                if (useAsciiMode) {
                    if (useExtendedAscii) {
                        compressedBits += charToExtendedBinary(message[pos]);
                    }
                    else {
                        unsigned char uc = static_cast<unsigned char>(message[pos]);
                        if (uc > 127) {
                            // Для расширенных символов в 7-битном режиме используем 8 бит
                            compressedBits += charToExtendedBinary(message[pos]);
                        }
                        else {
                            compressedBits += charToAsciiBinary(message[pos]);
                        }
                    }
                }
                else {
                    compressedBits += charToCode[message[pos]];
                }

                std::string singleChar = message.substr(pos, 1);
                std::string codeStr;
                if (useAsciiMode) {
                    if (useExtendedAscii) {
                        codeStr = charToExtendedBinary(message[pos]);
                    }
                    else {
                        unsigned char uc = static_cast<unsigned char>(message[pos]);
                        if (uc > 127) {
                            codeStr = charToExtendedBinary(message[pos]);
                        }
                        else {
                            codeStr = charToAsciiBinary(message[pos]);
                        }
                    }
                }
                else {
                    codeStr = charToCode[message[pos]];
                }
                steps.emplace_back(aj, bj, codeStr, 0, 0, singleChar);

                searchWindow += message[pos];
                pos++;
            }

            if (searchWindow.length() > static_cast<size_t>(dictSize + bufferSize)) {
                searchWindow = searchWindow.substr(searchWindow.length() - static_cast<size_t>(dictSize + bufferSize));
            }
        }

        std::string hexResult = binaryToHex(compressedBits);
        logCompression(message, steps, hexResult, charToCode);

        return hexResult;
    }

    std::string decompress(const std::string& hexCode, int dict, int buffer) {
        dictSize = dict;
        bufferSize = buffer;

        std::string bits = hexToBinary(hexCode);
        std::string result;
        std::string searchWindow;
        std::vector<std::tuple<std::string, std::string, std::string, int, int, std::string>> steps;

        int dictBitSize = getBitSize(dictSize);
        int bufferBitSize = getBitSize(bufferSize);

        size_t pos = 0;

        while (pos < bits.length()) {
            if (pos >= bits.length()) break;

            char flag = bits[pos];
            pos++;

            std::string aj = formatDictWindow(searchWindow);
            std::string decoded;

            if (flag == '0') {
                // Для литералов нужно определить длину кода
                // Пробуем найти код от 1 до 6 бит
                bool found = false;
                for (int codeLen = 1; codeLen <= 6 && pos + codeLen <= bits.length(); codeLen++) {
                    std::string code = bits.substr(pos, codeLen);
                    if (codeToChar.find(code) != codeToChar.end()) {
                        decoded = codeToChar[code];
                        pos += codeLen;
                        found = true;
                        break;
                    }
                }

                if (found) {
                    result += decoded;
                    searchWindow += decoded;
                    steps.emplace_back(aj, decoded, "", 0, 0, decoded);
                }
                else {
                    std::cout << "Error: Cannot decode at position " << pos << std::endl;
                    break;
                }
            }
            else {
                if (pos + static_cast<size_t>(dictBitSize + bufferBitSize) > bits.length()) break;

                std::string offsetBits = bits.substr(pos, static_cast<size_t>(dictBitSize));
                pos += static_cast<size_t>(dictBitSize);
                std::string lenBits = bits.substr(pos, static_cast<size_t>(bufferBitSize));
                pos += static_cast<size_t>(bufferBitSize);

                int offset = std::stoi(offsetBits, nullptr, 2);
                int length = std::stoi(lenBits, nullptr, 2);
                length = length + 1;

                if (offset > 0 && offset <= static_cast<int>(searchWindow.length())) {
                    int startPos = static_cast<int>(searchWindow.length()) - offset;
                    for (int i = 0; i < length; i++) {
                        decoded += searchWindow[static_cast<size_t>(startPos + i)];
                    }

                    result += decoded;
                    searchWindow += decoded;

                    steps.emplace_back(aj, decoded, offsetBits + lenBits, 1, length, decoded);
                }
            }

            if (searchWindow.length() > static_cast<size_t>(dictSize + bufferSize)) {
                searchWindow = searchWindow.substr(searchWindow.length() - dictSize - bufferSize);
            }
        }

        logDecompression(hexCode, steps);
        return result;
    }

    void saveCompressed(const std::string& filename, const std::string& compressed, int dict, int buffer) {
        std::ofstream file(filename);
        if (file) {
            std::string modeStr;
            if (useAsciiMode) {
                if (useExtendedAscii) {
                    modeStr = "EXTASCII";
                }
                else {
                    modeStr = "ASCII";
                }
            }
            else {
                modeStr = "CUSTOM";
            }
            file << dict << "\n" << buffer << "\n" << modeStr << "\n" << compressed;
            file.close();
        }
    }

    bool loadCompressed(const std::string& filename, std::string& compressed, int& dict, int& buffer) {
        std::ifstream file(filename);
        if (!file) return false;

        std::string mode;
        file >> dict >> buffer >> mode;
        file.ignore();
        std::getline(file, compressed);
        file.close();

        // Устанавливаем режим на основе загруженного файла
        if (mode == "ASCII") {
            useAsciiMode = true;
            useExtendedAscii = false;
        }
        else if (mode == "EXTASCII") {
            useAsciiMode = true;
            useExtendedAscii = true;
        }
        else {
            useAsciiMode = false;
            useExtendedAscii = false;
        }

        return true;
    }
};

// Функция для создания файла codes.txt в кодировке UTF-8
void createCodesFile() {
    std::ofstream file("codes.txt", std::ios::binary);
    if (!file) {
        std::cerr << "Cannot create codes.txt" << std::endl;
        return;
    }

    const char* utf8Content =
        "_\t000000\n"
        ".\t000001\n"
        ",\t000010\n"
        "А\t000011\n"
        "Б\t000100\n"
        "В\t000101\n"
        "Г\t000110\n"
        "Д\t000111\n"
        "Е\t001000\n"
        "Ё\t001001\n"
        "Ж\t001010\n"
        "З\t001011\n"
        "И\t001100\n"
        "Й\t001101\n"
        "К\t001110\n"
        "Л\t001111\n"
        "М\t010000\n"
        "Н\t010001\n"
        "О\t010010\n"
        "П\t010011\n"
        "Р\t010100\n"
        "С\t010101\n"
        "Т\t010110\n"
        "У\t010111\n"
        "Ф\t011000\n"
        "Х\t011001\n"
        "Ц\t011010\n"
        "Ч\t011011\n"
        "Ш\t011100\n"
        "Щ\t011101\n"
        "Ъ\t011110\n"
        "Ы\t011111\n"
        "Ь\t100000\n"
        "Э\t100001\n"
        "Ю\t100010\n"
        "Я\t100011\n"
        "0\t100100\n"
        "1\t100101\n"
        "2\t100110\n"
        "3\t100111\n"
        "4\t101000\n"
        "5\t101001\n"
        "6\t101010\n"
        "7\t101011\n"
        "8\t101100\n"
        "9\t101101\n";

    file.write(utf8Content, strlen(utf8Content));
    file.close();
    std::cout << "File codes.txt created successfully in UTF-8 encoding!" << std::endl;
}

int main() {
    setConsoleEncoding();

    LZSSCompressor lzss;

    // Выбор режима кодирования
    std::cout << "LZSS Text Compression Program" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << "Select encoding mode:" << std::endl;
    std::cout << "1. Custom table (6-bit codes from file)" << std::endl;
    std::cout << "2. ASCII mode (7-bit, English only)" << std::endl;
    std::cout << "3. Extended ASCII mode (8-bit, supports Russian CP1251)" << std::endl;
    std::cout << "Choose (1-3): ";

    int modeChoice;
    std::cin >> modeChoice;
    std::cin.ignore();

    if (modeChoice == 2) {
        lzss.setAsciiMode(true, false);
    }
    else if (modeChoice == 3) {
        lzss.setAsciiMode(true, true);
    }
    else {
        lzss.setAsciiMode(false, false);

        if (!fs::exists("codes.txt")) {
            std::cout << "codes.txt not found. Creating..." << std::endl;
            createCodesFile();
        }

        if (!lzss.loadCodes("codes.txt")) {
            std::cerr << "Error loading codes from codes.txt" << std::endl;
            return 1;
        }
    }

    std::cout << "\nAllowed characters: " << lzss.getAllowedCharsString() << std::endl;
    std::cout << "==============================\n" << std::endl;

    while (true) {
        std::cout << "\nMenu:" << std::endl;
        std::cout << "1. Compress message" << std::endl;
        std::cout << "2. Compress TXT file" << std::endl;
        std::cout << "3. Decompress message" << std::endl;
        std::cout << "4. Exit" << std::endl;
        std::cout << "Choose option (1-4): ";

        int choice;
        std::cin >> choice;
        std::cin.ignore();

        if (choice == 4) {
            std::cout << "Goodbye!" << std::endl;
            break;
        }

        if (choice == 1) {
            int dictSize, bufferSize;
            std::cout << "Enter dictionary size: ";
            std::cin >> dictSize;
            std::cout << "Enter buffer size: ";
            std::cin >> bufferSize;
            std::cin.ignore();

            std::string message;
            while (true) {
                std::cout << "Enter message (allowed symbols): ";
                std::getline(std::cin, message);

                if (lzss.validateString(message)) {
                    break;
                }
                else {
                    std::cout << "Message contains invalid symbols!" << std::endl;
                    std::cout << "Please try again." << std::endl;
                }
            }

            int originalSize = static_cast<int>(message.length());
            std::string compressed = lzss.compress(message, dictSize, bufferSize);
            int compressedDataSize = (static_cast<int>(compressed.length()) + 1) / 2;
            std::string dictStr = std::to_string(dictSize);
            std::string bufferStr = std::to_string(bufferSize);
            int metadataSize = static_cast<int>(dictStr.length() + bufferStr.length() + 3);
            int totalCompressedSize = metadataSize + compressedDataSize;

            double compressionRatio = 0.0;
            if (originalSize > 0) {
                compressionRatio = (1.0 - static_cast<double>(totalCompressedSize) / originalSize) * 100.0;
            }

            std::string timestamp = getTimeForFilename();
            std::string filename = "compressed_" + timestamp + ".lzss";

            lzss.saveCompressed(filename, compressed, dictSize, bufferSize);

            std::cout << "\n========== COMPRESSION STATISTICS ==========" << std::endl;
            std::cout << "Original message size:  " << originalSize << " bytes" << std::endl;
            std::cout << "Compressed data size:   " << compressedDataSize << " bytes (HEX encoded)" << std::endl;
            std::cout << "Metadata size:          " << metadataSize << " bytes" << std::endl;
            std::cout << "Total compressed size:  " << totalCompressedSize << " bytes" << std::endl;
            std::cout << "Compression ratio:      " << std::fixed << std::setprecision(2) << compressionRatio << "%" << std::endl;
            std::cout << "Space saved:            " << (originalSize - totalCompressedSize) << " bytes" << std::endl;
            std::cout << "=============================================" << std::endl;
            std::cout << "\nMessage compressed and saved to: " << filename << std::endl;
            std::cout << "Compressed data (HEX): " << compressed << std::endl;
        }
        else if (choice == 2) {
            // Сжатие TXT файла
            std::vector<std::string> txtFiles;

            std::cout << "\nAvailable TXT files:" << std::endl;
            for (const auto& entry : fs::directory_iterator(".")) {
                if (entry.path().extension() == ".txt") {
                    txtFiles.push_back(entry.path().filename().string());
                    auto size = fs::file_size(entry.path());
                    std::cout << "- " << txtFiles.back() << " (" << size << " bytes)" << std::endl;
                }
            }

            if (txtFiles.empty()) {
                std::cout << "No TXT files found in current directory." << std::endl;
                continue;
            }

            std::string filename;
            std::cout << "\nEnter TXT filename to compress: ";
            std::getline(std::cin, filename);

            // Проверяем существование файла
            if (!fs::exists(filename)) {
                std::cout << "File not found: " << filename << std::endl;
                continue;
            }

            // Определяем кодировку файла по расширению режима
            std::string message;

            if (modeChoice == 3) {
                // Extended ASCII mode - читаем как есть
                std::ifstream file(filename, std::ios::binary);
                if (file) {
                    message.assign((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
                    file.close();
                }
            }
            else {
                // Для других режимов конвертируем из UTF-8 в ANSI
                message = readUtf8FileAsAnsi(filename);
            }

            if (message.empty()) {
                std::cout << "Failed to read file or file is empty." << std::endl;
                continue;
            }

            // Проверяем допустимые символы (в расширенном ASCII режиме все символы допустимы)
            if (modeChoice != 3 && !lzss.validateString(message)) {
                std::cout << "\nFile contains invalid characters!" << std::endl;
                std::cout << "You may need to use Extended ASCII mode (option 3) for this file." << std::endl;
                continue;
            }

            int dictSize, bufferSize;
            std::cout << "\nEnter dictionary size: ";
            std::cin >> dictSize;
            std::cout << "Enter buffer size: ";
            std::cin >> bufferSize;
            std::cin.ignore();

            int originalSize = static_cast<int>(message.length());
            std::string compressed = lzss.compress(message, dictSize, bufferSize);
            int compressedDataSize = (static_cast<int>(compressed.length()) + 1) / 2;
            std::string dictStr = std::to_string(dictSize);
            std::string bufferStr = std::to_string(bufferSize);
            int metadataSize = static_cast<int>(dictStr.length() + bufferStr.length() + 3);
            int totalCompressedSize = metadataSize + compressedDataSize;

            double compressionRatio = 0.0;
            if (originalSize > 0) {
                compressionRatio = (1.0 - static_cast<double>(totalCompressedSize) / originalSize) * 100.0;
            }

            // Создаём имя сжатого файла
            std::string baseName = fs::path(filename).stem().string();
            std::string timestamp = getTimeForFilename();
            std::string compressedFilename = baseName + "_" + timestamp + ".lzss";

            lzss.saveCompressed(compressedFilename, compressed, dictSize, bufferSize);

            std::cout << "\n========== FILE COMPRESSION STATISTICS ==========" << std::endl;
            std::cout << "Original file:          " << filename << std::endl;
            std::cout << "Original size:          " << originalSize << " bytes" << std::endl;
            std::cout << "Compressed data size:   " << compressedDataSize << " bytes (HEX encoded)" << std::endl;
            std::cout << "Metadata size:          " << metadataSize << " bytes" << std::endl;
            std::cout << "Total compressed size:  " << totalCompressedSize << " bytes" << std::endl;
            std::cout << "Compression ratio:      " << std::fixed << std::setprecision(2) << compressionRatio << "%" << std::endl;
            std::cout << "Space saved:            " << (originalSize - totalCompressedSize) << " bytes" << std::endl;
            std::cout << "=================================================" << std::endl;
            std::cout << "\nFile compressed and saved to: " << compressedFilename << std::endl;
        }
        else if (choice == 3) {
            std::vector<std::string> compressedFiles;

            std::cout << "\nAvailable compressed files:" << std::endl;
            for (const auto& entry : fs::directory_iterator(".")) {
                if (entry.path().extension() == ".lzss") {
                    compressedFiles.push_back(entry.path().filename().string());
                    std::cout << "- " << compressedFiles.back() << " (" << fs::file_size(entry.path()) << " bytes)" << std::endl;
                }
            }

            if (compressedFiles.empty()) {
                std::cout << "No compressed files to decompress." << std::endl;
                continue;
            }

            std::string filename;
            std::cout << "Enter filename to decompress: ";
            std::getline(std::cin, filename);

            std::string compressed;
            int dictSize, bufferSize;

            if (lzss.loadCompressed(filename, compressed, dictSize, bufferSize)) {
                std::string decompressed = lzss.decompress(compressed, dictSize, bufferSize);

                auto compressedFileSize = fs::file_size(filename);
                int decompressedSize = static_cast<int>(decompressed.length());
                double compressionRatio = 0.0;
                if (decompressedSize > 0) {
                    compressionRatio = (1.0 - static_cast<double>(compressedFileSize) / decompressedSize) * 100.0;
                }

                std::cout << "\n========== DECOMPRESSION STATISTICS ==========" << std::endl;
                std::cout << "Compressed file size:   " << compressedFileSize << " bytes" << std::endl;
                std::cout << "Decompressed size:      " << decompressedSize << " bytes" << std::endl;
                std::cout << "Compression ratio:      " << std::fixed << std::setprecision(2) << compressionRatio << "%" << std::endl;
                std::cout << "Space saved:            " << (decompressedSize - compressedFileSize) << " bytes" << std::endl;
                std::cout << "==============================================" << std::endl;

                std::cout << "\nDecompressed message:" << std::endl;
                std::cout << decompressed << std::endl;

                std::string timestamp = getTimeForFilename();
                std::string outFilename = "decompressed_" + timestamp + ".txt";


                saveStringToUtf8File(outFilename, decompressed);
                std::cout << "\nDecompressed message saved to: " << outFilename << std::endl;
            }
            else {
                std::cout << "Error loading file: " << filename << std::endl;
            }
        }
    }

    return 0;
}