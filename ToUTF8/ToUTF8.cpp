#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cstdlib>

// Helper: Convert wide string to UTF-8 std::string.
std::string wstringToUtf8(const std::wstring& wstr) {
    if (wstr.empty())
        return std::string();

    // Determine the size needed for the UTF-8 string.
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
        static_cast<int>(wstr.size()),
        NULL, 0, NULL, NULL);
    std::string result(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
        static_cast<int>(wstr.size()),
        &result[0], size_needed, NULL, NULL);
    return result;
}

// Check if text is valid UTF-8 (provided earlier).
bool IsTextUtf8(const std::vector<char>& text)
{
    auto Src = text.begin();
    const auto SrcEnd = text.end();
    while (Src != SrcEnd)
    {
        int C = *(Src++);
        int HighOne = 0; // Number of leftmost '1' bits.
        for (int Mask = 0x80; Mask != 0 && (C & Mask) != 0; Mask >>= 1)
            HighOne++;
        if (HighOne == 1 || HighOne > 4)
            return false;
        while (--HighOne > 0)
            if (Src == SrcEnd || (*(Src++) & 0xc0) != 0x80)
                return false;
    }
    return true;
}

// Enumeration of supported encodings.
enum class Encoding {
    Unknown,
    UTF8BOM,
    UTF8,
    UTF16LE,
    UTF16BE,
    UTF16,
    ANSI
};

// Read the entire file into a vector of char.
std::vector<char> readFile(const wchar_t* filename) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        std::cerr << "Error opening input file: " << filename << std::endl;
        exit(EXIT_FAILURE);
    }
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Detect encoding based on BOM; if no BOM, use IsTextUtf8 to decide if UTF-8.
Encoding detectEncoding(const std::vector<char>& buffer) {
    if (buffer.size() >= 3 &&
        static_cast<unsigned char>(buffer[0]) == 0xEF &&
        static_cast<unsigned char>(buffer[1]) == 0xBB &&
        static_cast<unsigned char>(buffer[2]) == 0xBF)
    {
        return Encoding::UTF8BOM;
    }
    if (buffer.size() >= 2 &&
        static_cast<unsigned char>(buffer[0]) == 0xFF &&
        static_cast<unsigned char>(buffer[1]) == 0xFE)
    {
        return Encoding::UTF16LE;
    }
    if (buffer.size() >= 2 &&
        static_cast<unsigned char>(buffer[0]) == 0xFE &&
        static_cast<unsigned char>(buffer[1]) == 0xFF)
    {
        return Encoding::UTF16BE;
    }
    // No BOM: if valid UTF-8, assume UTF-8; otherwise ANSI.
    if (IsTextUtf8(buffer))
        return Encoding::UTF8;

    // Otherwise, use IsTextUnicode to get a hint if it might be UTF-16
    BOOL isUnicode = FALSE;
    // Note: You can pass in a pointer to a flag if you need additional output.
    if (IsTextUnicode(buffer.data(), static_cast<int>(buffer.size()), &isUnicode) && isUnicode) {
        // For simplicity, assume little-endian if Unicode is detected.
        return Encoding::UTF16;
    }
    return Encoding::ANSI;
}

// Convert input buffer to a wide string.
std::wstring convertToWideString(const std::vector<char>& buffer, Encoding enc) {
    if (enc == Encoding::UTF8BOM || enc == Encoding::UTF8) {
        int bomSize = (enc == Encoding::UTF8BOM) ? 3 : 0;
        int wideSize = MultiByteToWideChar(CP_UTF8, 0, buffer.data() + bomSize,
            static_cast<int>(buffer.size() - bomSize), NULL, 0);
        std::wstring wstr(wideSize, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, buffer.data() + bomSize,
            static_cast<int>(buffer.size() - bomSize),
            &wstr[0], wideSize);
        return wstr;
    }
    else if (enc == Encoding::UTF16LE) {
        int wcharCount = (static_cast<int>(buffer.size()) - 2) / 2;
        const wchar_t* start = reinterpret_cast<const wchar_t*>(buffer.data() + 2);
        return std::wstring(start, wcharCount);
    }
    else if (enc == Encoding::UTF16) {
        int wcharCount = static_cast<int>(buffer.size()) / 2;
        const wchar_t* start = reinterpret_cast<const wchar_t*>(buffer.data());
        return std::wstring(start, wcharCount);
    }
    else if (enc == Encoding::UTF16BE) {
        int wcharCount = (static_cast<int>(buffer.size()) - 2) / 2;
        std::wstring wstr;
        wstr.resize(wcharCount);
        const unsigned char* data = reinterpret_cast<const unsigned char*>(buffer.data() + 2);
        for (int i = 0; i < wcharCount; i++) {
            wstr[i] = static_cast<wchar_t>(data[2 * i + 1] | (data[2 * i] << 8));
        }
        return wstr;
    }
    else { // ANSI
        int wideSize = MultiByteToWideChar(CP_ACP, 0, buffer.data(),
            static_cast<int>(buffer.size()), NULL, 0);
        std::wstring wstr(wideSize, L'\0');
        MultiByteToWideChar(CP_ACP, 0, buffer.data(),
            static_cast<int>(buffer.size()), &wstr[0], wideSize);
        return wstr;
    }
}

// Convert a wide string to a UTF-8 std::string.
std::string convertWideToUTF8(const std::wstring& wstr) {
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
        static_cast<int>(wstr.size()), NULL, 0, NULL, NULL);
    std::string result(size_needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(),
        static_cast<int>(wstr.size()),
        &result[0], size_needed, NULL, NULL);
    return result;
}

// Main function using a wide-character entry point.
int wmain(int argc, wchar_t* argv[])
{
    if (argc < 3) {
        std::wcerr << L"Usage: ToUTF8.exe <inputfile> <outputfile>" << std::endl;
        return EXIT_FAILURE;
    }

    // Read the entire input file.
    std::vector<char> buffer = readFile(argv[1]);

    // Detect encoding.
    Encoding encoding = detectEncoding(buffer);

    std::wcout << L"Detected encoding: ";
    switch (encoding) {
    case Encoding::UTF8BOM:     std::wcout << L"UTF-8 BOM"; break;
    case Encoding::UTF8:     std::wcout << L"UTF-8"; break;
    case Encoding::UTF16LE:   std::wcout << L"UTF-16 LE"; break;
    case Encoding::UTF16:   std::wcout << L"UTF-16"; break;
    case Encoding::UTF16BE:   std::wcout << L"UTF-16 BE"; break;
    case Encoding::ANSI:      std::wcout << L"ANSI"; break;
    default:                  std::wcout << L"Unknown"; break;
    }
    std::wcout << std::endl;

    // Convert the input file content to a wide string.
    std::wstring wideContent = convertToWideString(buffer, encoding);

    // Convert the wide string to UTF-8.
    std::string utf8Content = convertWideToUTF8(wideContent);

    // Write the UTF-8 output to the output file, and write the UTF-8 BOM first.
    std::ofstream outFile(argv[2], std::ios::binary);
    if (!outFile) {
        std::wcerr << L"Error creating output file: " << argv[2] << std::endl;
        return EXIT_FAILURE;
    }

    // Write UTF-8 BOM: 0xEF, 0xBB, 0xBF.
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    outFile.write(reinterpret_cast<const char*>(bom), sizeof(bom));

    // Write the converted UTF-8 text.
    outFile.write(utf8Content.data(), utf8Content.size());
    outFile.close();

    std::cout << "Conversion to UTF-8 with BOM completed successfully." << std::endl;
    return EXIT_SUCCESS;
}
