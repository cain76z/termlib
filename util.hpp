#ifndef UTIL_HPP
#define UTIL_HPP
#pragma once

#include <string>
#include <string_view>
#include <algorithm>
#include <locale>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <climits>
#include <cstring>


#if defined(_WIN32) || defined(_WIN64)
    #define ANSI_WINDOWS 1
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <shlwapi.h>
    #ifdef _MSC_VER
        #pragma comment(lib, "shlwapi.lib")
    #endif
#else
    #define ANSI_WINDOWS 0
    #include <cstdlib>
    #include <sys/ioctl.h>
    #include <sys/stat.h>
    #include <dirent.h>
    #include <unistd.h>
    #include <iconv.h>
    #include <errno.h>
#endif

namespace util
{
    // Alias inside the namespace only, or use std::filesystem directly
    namespace fs = std::filesystem;

    enum class align
    {
        left,        center,        right,
        top,         middle,        bottom
    };

    struct ConsoleSize
    {
        int width;
        int height;
    };

    inline ConsoleSize get_console_size()
    {
#ifdef _WIN32
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        return {
            csbi.srWindow.Right - csbi.srWindow.Left + 1,
            csbi.srWindow.Bottom - csbi.srWindow.Top + 1};
#else
        struct winsize ws {};
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
             // Fallback if ioctl fails
             return {80, 24}; 
        }
        return {ws.ws_col, ws.ws_row};
#endif
    }

    inline void set_console_title(std::string_view title)
    {
#ifdef _WIN32
        SetConsoleTitleA(title.data());
#else
        std::cout << "\033]0;" << title << "\007";
#endif
    }

    enum class codepage
    {
        UTF8, // 65001
        UTF16LE,
        UTF16BE,
        CP437, CP850, CP932, CP936, CP949, CP950,
        ANSI, // system default
        UNKNOWN
    };

    inline const char *codepage_name(codepage cp)
    {
        switch (cp) {
            case codepage::UTF8: return "UTF-8";
            case codepage::CP437: return "CP437";
            case codepage::CP850: return "CP850";
            case codepage::CP932: return "CP932";
            case codepage::CP936: return "CP936";
            case codepage::CP949: return "CP949";
            case codepage::CP950: return "CP950";
            case codepage::ANSI: return "ANSI";
            default: return "UNKNOWN";
        }
    }

    inline std::string normalize_encoding_name(std::string_view s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (std::isalnum(static_cast<unsigned char>(c))) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        return out;
    }

    inline codepage parse_codepage(std::string_view name) {
        std::string key = normalize_encoding_name(name);

        if (key == "utf8" || key == "utf") return codepage::UTF8;
        if (key == "utf16le" || key == "utf16") return codepage::UTF16LE;
        if (key == "utf16be") return codepage::UTF16BE;
        if (key == "cp437" || key == "ibm437") return codepage::CP437;
        if (key == "cp850") return codepage::CP850;
        if (key == "cp932" || key == "shiftjis" || key == "sjis") return codepage::CP932;
        if (key == "cp936" || key == "gbk") return codepage::CP936;
        if (key == "cp949" || key == "euckr" || key == "windows949") return codepage::CP949;
        if (key == "cp950" || key == "big5") return codepage::CP950;
        if (key == "ansi" || key == "acp" || key == "system") return codepage::ANSI;

        return codepage::UNKNOWN;
    }

    inline UINT to_windows_codepage(codepage cp)
    {
        switch (cp) {
            case codepage::UTF8: return 65001;
            case codepage::CP437: return 437;
            case codepage::CP850: return 850;
            case codepage::CP932: return 932;
            case codepage::CP936: return 936;
            case codepage::CP949: return 949;
            case codepage::CP950: return 950;
            case codepage::ANSI: return GetACP();
            default: return 65001;
        }
    }

    inline codepage from_windows_codepage(UINT cp)
    {
        switch (cp) {
            case 65001: return codepage::UTF8;
            case 437: return codepage::CP437;
            case 850: return codepage::CP850;
            case 932: return codepage::CP932;
            case 936: return codepage::CP936;
            case 949: return codepage::CP949;
            case 950: return codepage::CP950;
            default: return (cp == GetACP()) ? codepage::ANSI : codepage::UNKNOWN;
        }
    }

    inline codepage get_console_codepage()
    {
#ifdef _WIN32
        return from_windows_codepage(GetConsoleOutputCP());
#else
        const char *loc = std::setlocale(LC_CTYPE, nullptr);
        if (!loc) return codepage::UNKNOWN;
        std::string s(loc);
        if (s.find("UTF-8") != std::string::npos || s.find("utf8") != std::string::npos) return codepage::UTF8;
        if (s.find("EUC-KR") != std::string::npos || s.find("eucKR") != std::string::npos) return codepage::CP949;
        if (s.find("SJIS") != std::string::npos || s.find("Shift_JIS") != std::string::npos) return codepage::CP932;
        if (s.find("GBK") != std::string::npos) return codepage::CP936;
        if (s.find("Big5") != std::string::npos) return codepage::CP950;
        return codepage::ANSI;
#endif
    }

    inline void set_console_encoding(codepage cp)
    {
#ifdef _WIN32
        UINT win_cp = to_windows_codepage(cp);
        SetConsoleOutputCP(win_cp);
        SetConsoleCP(win_cp);
        if (win_cp == 65001) { std::setlocale(LC_ALL, ".UTF8"); }
#else
        switch (cp) {
            case codepage::UTF8: std::setlocale(LC_ALL, "en_US.UTF-8"); break;
            case codepage::CP949: std::setlocale(LC_ALL, "ko_KR.EUC-KR"); break;
            case codepage::CP932: std::setlocale(LC_ALL, "ja_JP.SJIS"); break;
            case codepage::CP936: std::setlocale(LC_ALL, "zh_CN.GBK"); break;
            case codepage::CP950: std::setlocale(LC_ALL, "zh_TW.Big5"); break;
            default: std::setlocale(LC_ALL, ""); break;
        }
#endif
    }

    inline void set_console_encoding(std::string_view name) {
        set_console_encoding(parse_codepage(name));
    }

    inline std::string size2str(uintmax_t size)
    {
        static constexpr const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB", "EB", "ZB", "YB"};
        double value = static_cast<double>(size);
        int unit = 0;
        while (value >= 1024.0 && unit < 8) {
            value /= 1024.0;
            ++unit;
        }
        char buf[64];
        if (unit == 0) std::snprintf(buf, sizeof(buf), "%ju %s", size, units[unit]);
        else std::snprintf(buf, sizeof(buf), "%.2f %s", value, units[unit]);
        return buf;
    }

    inline std::string ftime2str(fs::file_time_type ftime, const std::string &format = "%Y-%m-%d %H:%M")
    {
        using namespace std::chrono;
        auto sctp = time_point_cast<system_clock::duration>(ftime - fs::file_time_type::clock::now() + system_clock::now());
        std::time_t tt = system_clock::to_time_t(sctp);
        std::tm tm{};
#if ANSI_WINDOWS
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif
        char buf[64];
        std::strftime(buf, sizeof(buf), format.c_str(), &tm);
        return buf;
    }

    // Two digits lookup table
    static constexpr char TWO_DIGITS[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";

    inline std::string sec2str(double seconds, std::string_view format = "%M:%S", bool /*pad_zero*/ = true)
    {
        long long total_ms = static_cast<long long>(seconds * 1000 + 0.5);
        long long h = total_ms / 3600000; total_ms %= 3600000;
        long long m = total_ms / 60000;   total_ms %= 60000;
        long long s = total_ms / 1000;    long long ms = total_ms % 1000;

        bool show_hour = (format.find("%H") != std::string_view::npos);
        bool show_ms = (format.find("%Z") != std::string_view::npos);

        if (!show_hour) { m += h * 60; h = 0; }

        char buf[64];
        char *p = buf;

        if (show_hour) {
            if (h < 100) {
                const char *d = &TWO_DIGITS[h * 2];
                *p++ = d[0]; *p++ = d[1];
            } else {
                p += std::snprintf(p, sizeof(buf) - (p - buf), "%lld", h);
            }
            *p++ = ':';
        }

        if (m < 100) {
            const char *d = &TWO_DIGITS[m * 2];
            *p++ = d[0]; *p++ = d[1];
        } else {
            p += std::snprintf(p, sizeof(buf) - (p - buf), "%lld", m);
        }
        *p++ = ':';

        const char *d_sec = &TWO_DIGITS[s * 2];
        *p++ = d_sec[0]; *p++ = d_sec[1];

        if (show_ms) {
            *p++ = '.';
            int ms_val = static_cast<int>(ms / 10);
            const char *d_ms = &TWO_DIGITS[ms_val * 2];
            *p++ = d_ms[0]; *p++ = d_ms[1];
        }

        return std::string(buf, static_cast<size_t>(p - buf));
    }

    struct Range { uint32_t start, end; };
    struct Range32 { uint32_t start, end; };
    static constexpr Range FULL_WIDTH_RANGES[] = {
        {0x1100, 0x11FF}, {0x231A, 0x231B}, {0x2329, 0x232A}, {0x23E9, 0x23EC},
        {0x23F0, 0x23F3}, {0x25FD, 0x25FE}, {0x2614, 0x2615}, {0x2648, 0x2653},
        {0x267F, 0x267F}, {0x2693, 0x2693}, {0x26A1, 0x26A1}, {0x26AA, 0x26AB},
        {0x26BD, 0x26BE}, {0x26C4, 0x26C5}, {0x26CE, 0x26CE}, {0x26D4, 0x26D4},
        {0x26EA, 0x26EA}, {0x26F2, 0x26F3}, {0x26F5, 0x26FA}, {0x26FD, 0x26FD},
        {0x2705, 0x2705}, {0x270A, 0x270B}, {0x2728, 0x2728}, {0x274C, 0x274E},
        {0x2753, 0x2755}, {0x2757, 0x2757}, {0x2795, 0x2797}, {0x27B0, 0x27BF},
        {0x2B1B, 0x2B1C}, {0x2B50, 0x2B50}, {0x2B55, 0x2B55}, {0x2E80, 0xA4CF},
        {0xAC00, 0xD7A3}, {0xF900, 0xFAFF}, {0xFE10, 0xFE19}, {0xFE30, 0xFE6F},
        {0xFF00, 0xFF60}, {0xFFE0, 0xFFE6}, {0x1F000, 0x1F0FF}, {0x1F100, 0x1F1FF},
        {0x1F200, 0x1F2FF}, {0x1F300, 0x1F64F}, {0x1F680, 0x1F6FF}, {0x1F700, 0x1F7FF},
        {0x1F800, 0x1F8FF}, {0x1F900, 0x1F9FF}, {0x1FA00, 0x1FAFF}, {0x20000, 0x3FFFD}
    };
    static constexpr Range32 COMBINING_RANGES[] = {
        {0x0300, 0x036F},   // Combining Diacritical Marks
        {0x0483, 0x0489},
        {0x0591, 0x05BD},
        {0x05BF, 0x05BF},
        {0x05C1, 0x05C2},
        {0x05C4, 0x05C5},
        {0x05C7, 0x05C7},
        {0x0610, 0x061A},
        {0x064B, 0x065F},
        {0x0670, 0x0670},
        {0x06D6, 0x06DC},
        {0x06DF, 0x06E4},
        {0x06E7, 0x06E8},
        {0x06EA, 0x06ED},
        {0x0711, 0x0711},
        {0x0730, 0x074A},
        {0x07A6, 0x07B0},
        {0x07EB, 0x07F3},
        {0x0816, 0x0819},
        {0x081B, 0x0823},
        {0x0825, 0x0827},
        {0x0829, 0x082D},
        {0x0859, 0x085B},
        {0x08D3, 0x0903},
        {0x093A, 0x093C},
        {0x093E, 0x094F},
        {0x0951, 0x0957},
        {0x0962, 0x0963},
        {0x0981, 0x0983},
        {0x09BC, 0x09BC},
        {0x09BE, 0x09C4},
        {0x09C7, 0x09C8},
        {0x09CB, 0x09CD},
        {0x09D7, 0x09D7},
        {0x0A01, 0x0A03},
        {0x0A3C, 0x0A3C},
        {0x0A3E, 0x0A42},
        {0x0A47, 0x0A48},
        {0x0A4B, 0x0A4D},
        {0x0A51, 0x0A51},
        {0x0A70, 0x0A71},
        {0x0A75, 0x0A75},
        {0x200B, 0x200F},   // ZWSP / RTL marks
        {0x202A, 0x202E},
        {0x2060, 0x2064},
        {0x2066, 0x206F},
        {0x20D0, 0x20FF},   // Combining Symbols
        {0xFE00, 0xFE0F},   // Variation Selectors
        {0xFE20, 0xFE2F}    // Combining Half Marks
    };

    inline bool is_combining(uint32_t cp)
    {
        auto it = std::lower_bound(
            std::begin(COMBINING_RANGES),
            std::end(COMBINING_RANGES),
            cp,
            [](const Range32& r, uint32_t v) {
                return r.end < v;
            });

        return it != std::end(COMBINING_RANGES) &&
            cp >= it->start;
    }

    inline bool is_full_width(uint32_t cp)
    {
        auto it = std::lower_bound(std::begin(FULL_WIDTH_RANGES), std::end(FULL_WIDTH_RANGES), cp,
            [](const Range &r, uint32_t val) { return r.end < val; });
        return (it != std::end(FULL_WIDTH_RANGES) && cp >= it->start);
    }

    inline int visual_width(const std::wstring &str)
    {
        int width = 0;
        size_t len = str.length();

        for (size_t i = 0; i < len; ++i)
        {
            uint32_t cp = static_cast<uint32_t>(str[i]);

            // Windows/UTF-16 Surrogate Pairs
            if constexpr (sizeof(wchar_t) == 2) {
                if (cp >= 0xD800 && cp <= 0xDBFF) { // High
                    if (i + 1 < len) {
                        uint32_t low = static_cast<uint32_t>(str[i + 1]);
                        if (low >= 0xDC00 && low <= 0xDFFF) { // Low
                            cp = ((cp - 0xD800) << 10) + (low - 0xDC00) + 0x10000;
                            ++i;
                        }
                    }
                }
            }

            // Control chars
            if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F))
                continue;

            // Combining marks → width 0
            if (is_combining(cp))
                continue;

            // Full-width → 2, otherwise 1
            width += is_full_width(cp) ? 2 : 1;
        }
        return width;
    }

    inline std::wstring aligned_text(const std::wstring &msg, int width, align a)
    {
        int msg_w = visual_width(msg);
        if (msg_w >= width) return msg;

        int left_pad = 0;
        int right_pad = 0;
        switch (a) {
            case align::left:   right_pad = width - msg_w; break;
            case align::center: left_pad = (width - msg_w) / 2; right_pad = width - msg_w - left_pad; break;
            case align::right:  left_pad = width - msg_w; break;
            default: break;
        }

        std::wstring result;
        result.reserve(msg.size() + left_pad + right_pad);
        result.append(left_pad, L' ');
        result += msg;
        result.append(right_pad, L' ');
        return result;
    }

    // Encoding Conversions
    inline std::wstring utf8_to_wstring(const std::string &str)
    {
        if (str.empty()) return L"";
#if ANSI_WINDOWS
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), wstr.data(), size_needed);
        return wstr;
#else
        // Use codecvt with deprecation warnings disabled via macros at top of file
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        try { return converter.from_bytes(str); } catch (...) { return L""; }
#endif
    }

    inline std::string wstring_to_utf8(const std::wstring &wstr)
    {
        if (wstr.empty()) return "";
#if ANSI_WINDOWS
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), str.data(), size_needed, nullptr, nullptr);
        return str;
#else
        std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
        try { return converter.to_bytes(wstr); } catch (...) { return ""; }
#endif
    }

    inline std::wstring ansi_to_wstring(const std::string &str)
    {
        if (str.empty()) return L"";
#if ANSI_WINDOWS
        int size_needed = MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(), nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(), wstr.data(), size_needed);
        return wstr;
#else
        // Depends on current locale
        std::mbstate_t state = {};
        const char *src = str.data();
        size_t len = std::mbsrtowcs(nullptr, &src, 0, &state);
        if (len == static_cast<size_t>(-1)) return L"";
        std::wstring wstr(len, 0);
        std::mbsrtowcs(wstr.data(), &src, len + 1, &state);
        return wstr;
#endif
    }

    inline std::string wstring_to_ansi(const std::wstring &wstr)
    {
        if (wstr.empty()) return "";
#if ANSI_WINDOWS
        int size_needed = WideCharToMultiByte(CP_ACP, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_ACP, 0, wstr.data(), (int)wstr.size(), str.data(), size_needed, nullptr, nullptr);
        return str;
#else
        std::mbstate_t state = {};
        const wchar_t *src = wstr.data();
        size_t len = std::wcsrtombs(nullptr, &src, 0, &state);
        if (len == static_cast<size_t>(-1)) return "";
        std::string str(len, 0);
        std::wcsrtombs(str.data(), &src, len + 1, &state);
        return str;
#endif
    }

#if ANSI_WINDOWS
    inline std::wstring utf16_to_wstring(const std::u16string &u16str) { return {u16str.begin(), u16str.end()}; }
    inline std::u16string wstring_to_utf16(const std::wstring &wstr) { return {wstr.begin(), wstr.end()}; }
    inline std::string utf16_to_utf8(const std::u16string &u16str) { return wstring_to_utf8(utf16_to_wstring(u16str)); }
    inline std::u16string utf8_to_utf16(const std::string &str) { return wstring_to_utf16(utf8_to_wstring(str)); }
#endif

    inline std::wstring to_lower_ascii(std::wstring str) {
        for (auto &c : str) { if (c >= L'A' && c <= L'Z') c += L'a' - L'A'; }
        return str;
    }

    inline std::wstring to_upper_ascii(std::wstring str) {
        for (auto &c : str) { if (c >= L'a' && c <= L'z') c -= L'a' - L'A'; }
        return str;
    }

    // Filesystem wrappers
    inline std::wstring get_extension(const std::wstring &path) {
        fs::path p(path);
        return to_lower_ascii(p.extension().wstring().substr(p.extension().wstring().empty() ? 0 : 1));
    }
    inline std::wstring get_filename_without_extension(const std::wstring &path) {
        return fs::path(path).stem().wstring();
    }
    inline std::wstring get_directory(const std::wstring &path) {
        return fs::path(path).parent_path().wstring();
    }
    inline std::wstring change_extension(const std::wstring &full_filepath, const std::wstring &ext) {
        fs::path p(full_filepath);
        p.replace_extension(ext);
        return p.wstring();
    }
    inline bool file_exists(const std::wstring &path) { return fs::exists(path); }
    inline bool directory_exists(const std::wstring &path) { return fs::is_directory(path); }
    inline std::intmax_t get_file_size(const std::wstring &path) {
        std::error_code ec;
        auto size = fs::file_size(path, ec);
        return ec ? -1 : static_cast<std::intmax_t>(size);
    }

    // Encoding Detection helpers
    inline bool has_utf8_bom(std::string_view str) { return str.size() >= 3 && str.substr(0, 3) == "\xEF\xBB\xBF"; }
    inline bool has_utf16le_bom(std::string_view str) { return str.size() >= 2 && str.substr(0, 2) == "\xFF\xFE"; }
    inline bool has_utf16be_bom(std::string_view str) { return str.size() >= 2 && str.substr(0, 2) == "\xFE\xFF"; }

    enum class Encoding { UNKNOWN, ASCII, UTF8, UTF8_BOM, UTF16_LE, UTF16_BE, 
        ANSI        // locale / ACP
        };
    inline Encoding detect_encoding(std::string_view s)
    {
        if (s.empty()) return Encoding::ASCII;

        if (s.size() >= 3 &&
            (unsigned char)s[0] == 0xEF &&
            (unsigned char)s[1] == 0xBB &&
            (unsigned char)s[2] == 0xBF)
            return Encoding::UTF8_BOM;

        if (s.size() >= 2 &&
            (unsigned char)s[0] == 0xFF &&
            (unsigned char)s[1] == 0xFE)
            return Encoding::UTF16_LE;

        if (s.size() >= 2 &&
            (unsigned char)s[0] == 0xFE &&
            (unsigned char)s[1] == 0xFF)
            return Encoding::UTF16_BE;

        // UTF-8 유효성 검사 (간이)
        bool ascii = true;
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = s[i];
            if (c <= 0x7F) { ++i; continue; }
            ascii = false;

            int n = (c & 0xE0) == 0xC0 ? 2 :
                    (c & 0xF0) == 0xE0 ? 3 :
                    (c & 0xF8) == 0xF0 ? 4 : 0;
            if (!n || i + n > s.size()) return Encoding::ANSI;

            for (int j = 1; j < n; ++j)
                if ((s[i + j] & 0xC0) != 0x80)
                    return Encoding::ANSI;
            i += n;
        }

        return ascii ? Encoding::ASCII : Encoding::UTF8;
    }

    inline std::wstring convert_to_wstring(const std::string &str, Encoding enc = Encoding::UNKNOWN)
    {
        if (enc == Encoding::UNKNOWN) enc = detect_encoding(str);
        switch (enc) {
            case Encoding::UTF8_BOM: return utf8_to_wstring(str.substr(3));
            case Encoding::UTF8:
            case Encoding::ASCII:    return utf8_to_wstring(str);
            case Encoding::UTF16_LE: {
                if (str.size() % 2 != 0) return L"";
                std::wstring result;
                result.reserve((str.size() - 2) / 2);
                
                for (size_t i = 2; i < str.size(); i += 2) {
                    uint16_t wc = (static_cast<uint16_t>(static_cast<unsigned char>(str[i + 1]) << 8) |
                                static_cast<unsigned char>(str[i]));
                    
                    // [Non-Windows 보완] 32비트 wchar_t 시스템에서 Surrogate Pair 결합
                    if constexpr (sizeof(wchar_t) == 4) {
                        if (wc >= 0xD800 && wc <= 0xDBFF && i + 2 < str.size()) { // High Surrogate
                            uint16_t next_wc = (static_cast<uint16_t>(static_cast<unsigned char>(str[i + 3]) << 8) |
                                                static_cast<unsigned char>(str[i + 2]));
                            if (next_wc >= 0xDC00 && next_wc <= 0xDFFF) { // Low Surrogate
                                // 결합하여 32비트 문자로 저장
                                uint32_t cp = ((wc - 0xD800) << 10) + (next_wc - 0xDC00) + 0x10000;
                                result.push_back(static_cast<wchar_t>(cp));
                                i += 2; // 2바이트 더 건너뜀
                                continue;
                            }
                        }
                    }
                    result.push_back(static_cast<wchar_t>(wc));
                }
                return result;
            }
            case Encoding::UTF16_BE: {
                if (str.size() % 2 != 0) return L"";
                std::wstring result;
                result.reserve((str.size() - 2) / 2);
                for (size_t i = 2; i < str.size(); i += 2) {
                    wchar_t wc = (static_cast<wchar_t>(static_cast<unsigned char>(str[i]) << 8) |
                                  static_cast<unsigned char>(str[i + 1]));
                    result.push_back(wc);
                }
                return result;
            }
            default: return ansi_to_wstring(str);
        }
    }

    inline std::string convert_from_wstring(const std::wstring &wstr, Encoding enc = Encoding::UTF8)
    {
        switch (enc) {
            case Encoding::UTF8_BOM: return "\xEF\xBB\xBF" + wstring_to_utf8(wstr);
            case Encoding::UTF8:
            case Encoding::ASCII:    return wstring_to_utf8(wstr);
            case Encoding::ANSI:     return wstring_to_ansi(wstr);
            case Encoding::UTF16_LE: {
                std::string result = "\xFF\xFE";
                result.reserve(2 + wstr.size() * 2);
                for (wchar_t c : wstr) {
                    result.push_back(static_cast<char>(c & 0xFF));
                    result.push_back(static_cast<char>((c >> 8) & 0xFF));
                }
                return result;
            }
            case Encoding::UTF16_BE: {
                std::string result = "\xFE\xFF";
                result.reserve(2 + wstr.size() * 2);
                for (wchar_t c : wstr) {
                    result.push_back(static_cast<char>((c >> 8) & 0xFF));
                    result.push_back(static_cast<char>(c & 0xFF));
                }
                return result;
            }
            default: return wstring_to_utf8(wstr);
        }
    }
#if !ANSI_WINDOWS
inline std::wstring iconv_to_wstring(
    const std::string& input,
    const char* from_enc)
{
    iconv_t cd = iconv_open("WCHAR_T", from_enc);
    if (cd == (iconv_t)-1) return L"";

    size_t in_bytes = input.size();
    size_t out_bytes = (input.size() + 1) * sizeof(wchar_t);

    std::wstring out(out_bytes / sizeof(wchar_t), L'\0');

    char* in_buf = const_cast<char*>(input.data());
    char* out_buf = reinterpret_cast<char*>(out.data());

    if (iconv(cd, &in_buf, &in_bytes, &out_buf, &out_bytes)
        == (size_t)-1)
    {
        iconv_close(cd);
        return L"";
    }

    iconv_close(cd);
    out.resize((out.size() * sizeof(wchar_t) - out_bytes) /
               sizeof(wchar_t));
    return out;
}
#endif
inline std::wstring to_wstring(
    const std::string& s,
    Encoding enc = Encoding::UNKNOWN)
{
    if (enc == Encoding::UNKNOWN)
        enc = detect_encoding(s);

#if ANSI_WINDOWS
    switch (enc) {
        case Encoding::UTF8_BOM:
            return utf8_to_wstring(s.substr(3));
        case Encoding::UTF8:
        case Encoding::ASCII:
            return utf8_to_wstring(s);
        case Encoding::UTF16_LE:
        case Encoding::UTF16_BE:
            return convert_to_wstring(s, enc);
        default:
            return ansi_to_wstring(s);
    }
#else
    switch (enc) {
        case Encoding::UTF8_BOM:
            return iconv_to_wstring(s.substr(3), "UTF-8");
        case Encoding::UTF8:
        case Encoding::ASCII:
            return iconv_to_wstring(s, "UTF-8");
        case Encoding::UTF16_LE:
            return iconv_to_wstring(s.substr(2), "UTF-16LE");
        case Encoding::UTF16_BE:
            return iconv_to_wstring(s.substr(2), "UTF-16BE");
        default:
            return iconv_to_wstring(s, "");
    }
#endif
}
#if !ANSI_WINDOWS
inline std::string wstring_to_iconv(
    const std::wstring& ws,
    const char* to_enc)
{
    iconv_t cd = iconv_open(to_enc, "WCHAR_T");
    if (cd == (iconv_t)-1) return "";

    size_t in_bytes = ws.size() * sizeof(wchar_t);
    size_t out_bytes = in_bytes * 4 + 8;
    std::string out(out_bytes, '\0');

    char* in_buf = (char*)ws.data();
    char* out_buf = out.data();

    if (iconv(cd, &in_buf, &in_bytes, &out_buf, &out_bytes)
        == (size_t)-1)
    {
        iconv_close(cd);
        return "";
    }

    iconv_close(cd);
    out.resize(out.size() - out_bytes);
    return out;
}
#endif
inline std::string from_wstring(
    const std::wstring& ws,
    Encoding enc = Encoding::UTF8)
{
#if ANSI_WINDOWS
    if (enc == Encoding::UTF8)
        return wstring_to_utf8(ws);
    if (enc == Encoding::ANSI)
        return wstring_to_ansi(ws);
    return wstring_to_utf8(ws);
#else
    switch (enc) {
        case Encoding::UTF8:
            return wstring_to_iconv(ws, "UTF-8");
        case Encoding::UTF16_LE:
            return "\xFF\xFE" +
                wstring_to_iconv(ws, "UTF-16LE");
        case Encoding::UTF16_BE:
            return "\xFE\xFF" +
                wstring_to_iconv(ws, "UTF-16BE");
        default:
            return wstring_to_iconv(ws, "UTF-8");
    }
#endif
}

void write_console(const std::wstring& ws)
{
#if ANSI_WINDOWS
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        // 실제 콘솔 연결됨

        if (mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) {
            // VT 가능 → UTF-8 + WriteFile
            std::string out = wstring_to_utf8(ws);
            DWORD written;
            WriteFile(h, out.data(), (DWORD)out.size(), &written, nullptr);
        } else {
            // VT 불가 → WriteConsoleW
            DWORD written;
            WriteConsoleW(h, ws.data(), (DWORD)ws.size(), &written, nullptr);
        }
    } else {
        // redirect / pipe
        std::string out = wstring_to_utf8(ws);
        fwrite(out.data(), 1, out.size(), stdout);
    }
#else
    std::string out = wstring_to_iconv(ws, "");
    fwrite(out.data(), 1, out.size(), stdout);
#endif
}

} // namespace util

// Restore warning state
#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif

#define ANSI_TEXT(str) L##str
#define ANSI_UTF8(str) util::utf8_to_wstring(str)

#ifdef _DEBUG
    #include <iostream>
    #define ANSI_DEBUG(msg) std::wcout << L"[DEBUG] " << msg << std::endl
    #define ANSI_DEBUG_UTF8(msg) std::cout << "[DEBUG] " << msg << std::endl
#else
    #define ANSI_DEBUG(msg)
    #define ANSI_DEBUG_UTF8(msg)
#endif

#endif // UTIL_HPP
