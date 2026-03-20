#pragma once
#ifndef _TERM_UTIL_HPP_
#define _TERM_UTIL_HPP_
/**
 * @file util.hpp
 * @brief 공통 유틸리티 함수들 — 경로 처리, 설정 파일 경로 생성, 크기/시간 포맷팅 등 (TDD §11)
 */

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <filesystem>
#include <fstream>
#include <vector>
#include <system_error>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <unistd.h>
    #include <limits.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#endif

namespace term::util {

namespace fs = std::filesystem;

//////////////////////////////////////////////////////////////////////////
// 내부 유틸
//////////////////////////////////////////////////////////////////////////

/**
 * @brief 문자열 확장자 정규화
 *
 * "conf" → ".conf"
 */
inline std::string normalize_extension(std::string_view ext)
{
    if (ext.empty())
        return "";

    std::string e(ext);

    if (e.front() != '.')
        e.insert(e.begin(), '.');

    return e;
}

/**
 * @brief 파일 생성 (필요 시)
 */
inline void create_file_if_missing(
    const fs::path& p,
    std::string_view contents)
{
    if (fs::exists(p))
        return;

    std::ofstream ofs(p);

    if (ofs && !contents.empty())
        ofs << contents;
}

//////////////////////////////////////////////////////////////////////////
// 실행 파일 경로
//////////////////////////////////////////////////////////////////////////

/**
 * @brief 현재 실행 파일의 절대 경로 반환
 *
 * 플랫폼별 구현
 *
 * Windows
 *  - GetModuleFileNameW
 *
 * Linux
 *  - /proc/self/exe
 *
 * macOS
 *  - _NSGetExecutablePath
 *
 * @return fs::path 실행 파일 경로
 * @retval "" 실패 시
 */
inline fs::path get_executable_path()
{

#if defined(_WIN32)

    std::wstring buf(32768, L'\0');

    DWORD len =
        GetModuleFileNameW(nullptr, buf.data(), buf.size());

    if (len == 0 || len >= buf.size())
        return {};

    buf.resize(len);

    return fs::weakly_canonical(fs::path(buf));

#elif defined(__linux__)

    std::string buf(4096, '\0');
    ssize_t len;

    while (true)
    {
        len = readlink("/proc/self/exe", buf.data(), buf.size());

        if (len < 0)
            return {};

        if ((size_t)len < buf.size())
            break;

        buf.resize(buf.size() * 2);
    }

    buf.resize(len);

    return fs::weakly_canonical(fs::path(buf));

#elif defined(__APPLE__)

    uint32_t size = 0;

    _NSGetExecutablePath(nullptr, &size);

    std::vector<char> buf(size + 1);

    if (_NSGetExecutablePath(buf.data(), &size) != 0)
        return {};

    buf[size] = '\0';

    return fs::weakly_canonical(fs::path(buf.data()));

#else
#error Unsupported platform
#endif
}

//////////////////////////////////////////////////////////////////////////
// Executable Config
//////////////////////////////////////////////////////////////////////////

/**
 * @brief 실행 파일 이름 기반 설정 파일 경로
 *
 * 예
 *
 * ```
 * /usr/bin/myapp
 * → /usr/bin/myapp.conf
 * ```
 *
 * @param ext 확장자
 * @param create 파일 생성 여부
 * @param contents 기본 파일 내용
 */
inline fs::path get_executable_conf(
    std::string_view ext = "conf",
    bool create = false,
    std::string_view contents = {})
{
    fs::path exe = get_executable_path();

    if (exe.empty())
        return {};

    fs::path cfg = exe;

    cfg.replace_extension(normalize_extension(ext));

    if (create)
        create_file_if_missing(cfg, contents);

    return cfg;
}

//////////////////////////////////////////////////////////////////////////
// Portable Config
//////////////////////////////////////////////////////////////////////////

/**
 * @brief Portable 설정 파일 경로
 *
 * 구조
 *
 * ```
 * ./.config/app/config.ini
 * ```
 */
inline fs::path get_portable_config(
    std::string_view name,
    bool create = false,
    std::string_view contents = {})
{
    fs::path exe = get_executable_path();

    if (exe.empty())
        return {};

    fs::path cfg =
        exe.parent_path() /
        ".config" /
        exe.stem() /
        fs::path(name).filename();

    if (create)
    {
        std::error_code ec;

        fs::create_directories(cfg.parent_path(), ec);

        if (!ec)
            create_file_if_missing(cfg, contents);
    }

    return cfg;
}

//////////////////////////////////////////////////////////////////////////
// HOME CONFIG
//////////////////////////////////////////////////////////////////////////

/**
 * @brief OS 표준 config 디렉토리
 */
inline fs::path get_home_config_dir()
{
    fs::path exe = get_executable_path();

    if (exe.empty())
        return {};

    std::string app = exe.stem().string();

#if defined(_WIN32)

    if (auto p = std::getenv("APPDATA"))
        return fs::path(p) / app;

#elif defined(__APPLE__)

    if (auto p = std::getenv("HOME"))
        return fs::path(p) /
               "Library" /
               "Application Support" /
               app;

#else

    if (auto x = std::getenv("XDG_CONFIG_HOME"))
        return fs::path(x) / app;

    if (auto h = std::getenv("HOME"))
        return fs::path(h) / ".config" / app;

#endif

    return {};
}

/**
 * @brief OS 표준 config 파일
 */
inline fs::path get_home_config(
    std::string_view name,
    bool create = false,
    std::string_view contents = {})
{
    fs::path base = get_home_config_dir();

    if (base.empty())
        return {};

    fs::path cfg = base / fs::path(name).filename();

    if (create)
    {
        std::error_code ec;

        fs::create_directories(base, ec);

        if (!ec)
            create_file_if_missing(cfg, contents);
    }

    return cfg;
}

//////////////////////////////////////////////////////////////////////////
// CACHE
//////////////////////////////////////////////////////////////////////////

/**
 * @brief Cache 디렉토리
 */
inline fs::path get_cache_dir()
{
    fs::path exe = get_executable_path();

    if (exe.empty())
        return {};

    std::string app = exe.stem().string();

#if defined(_WIN32)

    if (auto p = std::getenv("LOCALAPPDATA"))
        return fs::path(p) / app / "cache";

#elif defined(__APPLE__)

    if (auto p = std::getenv("HOME"))
        return fs::path(p) /
               "Library" /
               "Caches" /
               app;

#else

    if (auto x = std::getenv("XDG_CACHE_HOME"))
        return fs::path(x) / app;

    if (auto h = std::getenv("HOME"))
        return fs::path(h) / ".cache" / app;

#endif

    return {};
}

//////////////////////////////////////////////////////////////////////////
// LOG
//////////////////////////////////////////////////////////////////////////

/**
 * @brief 로그 디렉토리
 */
inline fs::path get_log_dir()
{
    fs::path exe = get_executable_path();

    if (exe.empty())
        return {};

    std::string app = exe.stem().string();

#if defined(_WIN32)

    if (auto p = std::getenv("LOCALAPPDATA"))
        return fs::path(p) / app / "log";

#elif defined(__APPLE__)

    if (auto p = std::getenv("HOME"))
        return fs::path(p) /
               "Library" /
               "Logs" /
               app;

#else

    if (auto x = std::getenv("XDG_STATE_HOME"))
        return fs::path(x) / app / "log";

    if (auto h = std::getenv("HOME"))
        return fs::path(h) /
               ".local" /
               "state" /
               app /
               "log";

#endif

    return {};
}

/**
 * @brief 바이트 크기를 사람이 읽기 쉬운 문자열로 변환합니다.
 *
 * 예: 1536 → "1.5 KB", 2097152 → "2.0 MB", 0 → "0 B"
 *
 * @param bytes 변환할 바이트 수
 * @param precision 소수점 아래 자릿수 (기본 1)
 * @return std::string 포맷된 크기 문자열
 */
inline std::string size2str(uint64_t bytes, int precision = 1) {
    static constexpr const char* units[] = {"B ", "KB", "MB", "GB", "TB", "PB"};
    double val = static_cast<double>(bytes);
    int    ui  = 0;
    while (val >= 1024.0 && ui < 5) { val /= 1024.0; ++ui; }
    char buf[48];
    if (ui == 0)
        std::snprintf(buf, sizeof(buf), "%llu B",
                      static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buf, sizeof(buf), "%.*f %s", precision, val, units[ui]);
    return buf;
}

/**
 * @brief 초 단위 시간을 사람이 읽기 쉬운 문자열로 변환합니다.
 *
 * @param seconds 변환할 시간 (초 단위, 소수 가능)
 * @param compact true → "01:23:45" 형식 / false → "1h 23m 45s" 형식
 * @return std::string 포맷된 시간 문자열
 */
inline std::string time2str(double seconds, bool compact = false) {
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    char buf[32];
    if (compact) {
        if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
        else       std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    } else {
        if (h > 0)      std::snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
        else if (m > 0) std::snprintf(buf, sizeof(buf), "%dm %ds", m, s);
        else            std::snprintf(buf, sizeof(buf), "%ds", s);
    }
    return buf;
}

/**
 * @brief chrono duration (나노초 단위)을 사람이 읽기 쉬운 짧은 문자열로 변환합니다.
 *
 * 예: 2500000000ns → "2.50s", 150000000ns → "150.0ms", 800ns → "800ns"
 *
 * @param ns 변환할 시간 (std::chrono::nanoseconds)
 * @return std::string 포맷된 지속 시간 문자열
 */
inline std::string duration2str(std::chrono::nanoseconds ns) {
    using namespace std::chrono;
    auto s  = duration_cast<seconds>(ns).count();
    auto ms = duration_cast<milliseconds>(ns).count() % 1000;
    auto us = duration_cast<microseconds>(ns).count() % 1000;
    auto ns_ = ns.count() % 1000;

    char buf[64]{};

    if (s >= 86400) { // 1일 이상
        auto d = s / 86400; s %= 86400;
        std::snprintf(buf, sizeof(buf), "%lld %02lld:%02lld:%02lld", d, s/3600, (s/60)%60, s%60);
    }
    else if (s >= 3600) {
        std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld", s/3600, (s/60)%60, s%60);
    }
    else if (s >= 60) {
        std::snprintf(buf, sizeof(buf), "%lldm %02llds", s/60, s%60);
    }
    else if (s >= 10) {
        std::snprintf(buf, sizeof(buf), "%llds", s);
    }
    else if (ms >= 1) {
        std::snprintf(buf, sizeof(buf), "%lld.%03llds", s, ms);
    }
    else if (us >= 1) {
        std::snprintf(buf, sizeof(buf), "%lld.%03lldms", s, us);
    }
    else {
        std::snprintf(buf, sizeof(buf), "%lldns", ns_);
    }

    return buf;
}

/**
 * @brief 시간점(time_point)을 지정한 형식의 문자열로 변환합니다.
 *
 * @param tp 변환할 시간점 (system_clock 기준)
 * @param fmt strftime 형식 문자열 (기본: "%Y-%m-%d %H:%M:%S")
 * @return std::string 포맷된 날짜/시간 문자열
 *
 * @note 로컬 시간 기준으로 변환됩니다.
 */
inline std::string timestamp2str(std::chrono::system_clock::time_point tp,
                                 std::string_view fmt = "%Y-%m-%d %H:%M:%S") {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    char buf[64] = {};
    std::strftime(buf, sizeof(buf), fmt.data(), std::localtime(&tt));
    return buf;
}

} // namespace term::util
#endif // _TERM_UTIL_HPP_