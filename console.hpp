#pragma once

/**
 * console.hpp — 콘솔 제어 & 포맷팅 유틸리티 (C++17, 단일 헤더)
 *
 * 의존: encode.hpp (같은 경로에 있어야 함)
 *
 * 제공 기능:
 *   - 콘솔 크기 조회 (get_console_size)
 *   - 콘솔 타이틀 설정 (set_console_title)
 *   - 콘솔 인코딩 설정 (set_console_encoding)
 *   - 유니코드 콘솔 출력 (write_console)
 *   - 크기/시간/초 포맷 문자열 변환 (size2str, ftime2str, sec2str)
 *
 * 파일시스템 래퍼 (get_extension, file_exists 등)는 fnutil.hpp 에 있음.
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#include "encode.hpp"   // codepage, utf8/wstring 변환, PLATFORM_WINDOWS

#if PLATFORM_WINDOWS
    #include <windows.h>
#else
    #include <sys/ioctl.h>
    #include <unistd.h>
#endif

namespace util {

namespace fs = std::filesystem;

// =========================================================
// 1. 콘솔 크기 & 타이틀
// =========================================================

struct ConsoleSize { int width; int height; };

/// 현재 터미널 열/행 수 반환 (실패 시 80×24)
inline ConsoleSize get_console_size() {
#if PLATFORM_WINDOWS
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        return {
            csbi.srWindow.Right - csbi.srWindow.Left + 1,
            csbi.srWindow.Bottom - csbi.srWindow.Top + 1
        };
    }
#else
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
        return {ws.ws_col, ws.ws_row};
#endif
    return {80, 24};
}

/// 터미널 타이틀 설정
inline void set_console_title(std::string_view title) {
#if PLATFORM_WINDOWS
    SetConsoleTitleA(title.data());
#else
    std::cout << "\033]0;" << title << "\007" << std::flush;
#endif
}

// =========================================================
// 2. 콘솔 인코딩 설정
// =========================================================

/// 콘솔 입출력 인코딩을 codepage 값으로 설정
inline void set_console_encoding(codepage cp) {
#if PLATFORM_WINDOWS
    UINT win_cp = to_windows_codepage(cp);
    SetConsoleOutputCP(win_cp);
    SetConsoleCP(win_cp);
    if (win_cp == 65001) std::setlocale(LC_ALL, ".UTF8");
#else
    switch (cp) {
        case codepage::UTF8:  std::setlocale(LC_ALL, "en_US.UTF-8");  break;
        case codepage::CP949: std::setlocale(LC_ALL, "ko_KR.EUC-KR"); break;
        case codepage::CP932: std::setlocale(LC_ALL, "ja_JP.SJIS");   break;
        case codepage::CP936: std::setlocale(LC_ALL, "zh_CN.GBK");    break;
        case codepage::CP950: std::setlocale(LC_ALL, "zh_TW.Big5");   break;
        default:              std::setlocale(LC_ALL, "");              break;
    }
#endif
}

/// 인코딩 이름 문자열로 콘솔 인코딩 설정 ("UTF-8", "cp949" 등)
inline void set_console_encoding(std::string_view name) {
    set_console_encoding(parse_codepage(name));
}

// =========================================================
// 3. 유니코드 콘솔 출력
// =========================================================

/// wstring을 콘솔 인코딩에 맞게 출력 (Windows: WriteConsoleW)
inline void write_console(const std::wstring& ws) {
#if PLATFORM_WINDOWS
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode;
    if (GetConsoleMode(h, &mode)) {
        DWORD written;
        WriteConsoleW(h, ws.data(), (DWORD)ws.size(), &written, nullptr);
    } else {
        // 파이프/리다이렉트: UTF-8로 변환 후 출력
        std::string out = wstring_to_utf8(ws);
        fwrite(out.data(), 1, out.size(), stdout);
    }
#else
    std::string out = detail::wstring_to_iconv(ws, "");
    fwrite(out.data(), 1, out.size(), stdout);
#endif
}

// =========================================================
// 4. 포맷 변환 — size / time / seconds
// =========================================================

/// 바이트 크기를 사람이 읽기 쉬운 단위 문자열로 변환
/// 예) 1536 → "1.50 KB"
inline std::string size2str(uintmax_t size) {
    static constexpr const char* UNITS[] = {
        "B","KB","MB","GB","TB","PB","EB","ZB","YB"
    };
    double value = static_cast<double>(size);
    int unit = 0;
    while (value >= 1024.0 && unit < 8) { value /= 1024.0; ++unit; }

    char buf[64];
    if (unit == 0)
        std::snprintf(buf, sizeof(buf), "%ju %s", size, UNITS[unit]);
    else
        std::snprintf(buf, sizeof(buf), "%.2f %s", value, UNITS[unit]);
    return buf;
}

/// filesystem::file_time_type → 날짜 문자열 변환
/// 기본 포맷: "%Y-%m-%d %H:%M"
inline std::string ftime2str(fs::file_time_type ftime,
                              const std::string& format = "%Y-%m-%d %H:%M")
{
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + system_clock::now());
    std::time_t tt = system_clock::to_time_t(sctp);
    std::tm tm{};
#if PLATFORM_WINDOWS
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), format.c_str(), &tm);
    return buf;
}

namespace detail {
    // "00"~"99" 룩업 테이블 — sec2str 고속화용
    static constexpr char TWO_DIGITS[201] =
        "0001020304050607080910111213141516171819"
        "2021222324252627282930313233343536373839"
        "4041424344454647484950515253545556575859"
        "6061626364656667686970717273747576777879"
        "8081828384858687888990919293949596979899";
} // namespace detail

/// 초(double) → 시간 문자열 변환
///   format 토큰: %H(시), %M(분), %S(초), %Z(밀리초 2자리)
///   예) sec2str(75.5)            → "01:15"
///       sec2str(3661, "%H:%M:%S") → "01:01:01"
///       sec2str(1.23, "%M:%S.%Z") → "00:01.23"
inline std::string sec2str(double seconds,
                            std::string_view format = "%M:%S",
                            bool /*pad_zero*/ = true)
{
    long long total_ms = static_cast<long long>(seconds * 1000 + 0.5);
    long long h  = total_ms / 3600000; total_ms %= 3600000;
    long long m  = total_ms / 60000;   total_ms %= 60000;
    long long s  = total_ms / 1000;
    long long ms = total_ms % 1000;

    const bool show_hour = (format.find("%H") != std::string_view::npos);
    const bool show_ms   = (format.find("%Z") != std::string_view::npos);
    if (!show_hour) { m += h * 60; h = 0; }

    char buf[64];
    char* p = buf;

    auto write2 = [&](long long v) {
        if (v < 100) {
            const char* d = &detail::TWO_DIGITS[v * 2];
            *p++ = d[0]; *p++ = d[1];
        } else {
            p += std::snprintf(p, sizeof(buf) - (p - buf), "%lld", v);
        }
    };

    if (show_hour) { write2(h); *p++ = ':'; }
    write2(m); *p++ = ':';
    write2(s);
    if (show_ms) {
        *p++ = '.';
        write2(ms / 10); // 10ms 단위 2자리
    }

    return std::string(buf, static_cast<size_t>(p - buf));
}

} // namespace util

// =========================================================
// 편의 매크로
// =========================================================

/// 리터럴 문자열을 wstring 리터럴로 변환
#define ANSI_TEXT(str)  L##str
/// UTF-8 string → wstring 변환
#define ANSI_UTF8(str)  util::utf8_to_wstring(str)

#ifdef _DEBUG
    #define ANSI_DEBUG(msg)      std::wcout << L"[DEBUG] " << msg << std::endl
    #define ANSI_DEBUG_UTF8(msg) std::cout  <<  "[DEBUG] " << msg << std::endl
#else
    #define ANSI_DEBUG(msg)
    #define ANSI_DEBUG_UTF8(msg)
#endif