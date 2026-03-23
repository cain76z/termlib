#ifndef FNUTIL_HPP
#define FNUTIL_HPP
#pragma once

/**
 * fnutil.hpp — 파일시스템 유틸리티 + 고속 디렉토리 열거 (C++17, 단일 헤더)
 *
 * ── 포함 기능 ────────────────────────────────────────────────────────────────
 *
 *  [정렬 / glob]
 *   fnutil::flag          — 정렬 옵션 플래그 (naturalName, naturalPath, ignoreCase)
 *   fnutil::naturalLess   — 숫자 구간 수치 비교 자연 정렬 (단일 구현)
 *   fnutil::sort()        — fs::path 벡터 정렬
 *   fnutil::glob()        — 와일드카드 파일 매칭
 *   fnutil::glob_ex()     — 경로+와일드카드 통합 처리
 *   fnutil::path_parts    — 경로 분해 구조체
 *   fnutil::split()       — 경로 → path_parts 분해
 *   fnutil::join()        — path 목록 결합
 *   fnutil::get_extension / get_stem / get_directory 등 — wstring 래퍼
 *
 *  [앱 경로 / 설정] ← 구 term::util 에서 이동
 *   fnutil::get_executable_path() — 현재 실행 파일 경로 (dynamic buffer, weakly_canonical)
 *   fnutil::get_executable_conf() — 실행파일명.ext 설정 경로
 *   fnutil::get_portable_config() — ./.config/<app>/<name>
 *   fnutil::get_home_config_dir() — OS 표준 config 디렉터리
 *   fnutil::get_home_config()     — OS 표준 config 파일
 *   fnutil::get_cache_dir()       — OS 표준 cache 디렉터리
 *   fnutil::get_log_dir()         — OS 표준 log 디렉터리
 *   fnutil::normalize_extension() — "conf" → ".conf"
 *   fnutil::create_file_if_missing()
 *   fnutil::ftime2str()           — fs::file_time_type → 날짜 문자열
 *
 *  [고속 디렉토리 열거] ← fastls.hpp 통합
 *   fnutil::entry         — 파일 항목 (이름·경로·크기·수정시각)
 *   fnutil::ls_options    — 열거 옵션
 *   fnutil::list_names()  — 파일명 목록 (stat 없음 — 최고 속도)
 *   fnutil::list()        — 전체 경로 목록 (stat 없음)
 *   fnutil::list_stat()   — 크기·날짜 포함 목록 (파일별 stat 1회)
 *   fnutil::count()       — 파일 수 카운트
 *
 * ── 속도 향상 원리 ────────────────────────────────────────────────────────────
 *   Linux  : getdents64(2) 직접 syscall — 커널 버퍼(64KB) 일괄 읽기
 *   Windows: FindFirstFileEx + FindExInfoBasic + FIND_FIRST_EX_LARGE_FETCH
 */

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "encode.hpp"   // util::to_lower_ascii

// ── 플랫폼 헤더 ──────────────────────────────────────────────────────────────
#if defined(_WIN32) || defined(_WIN64)
#   define FNUTIL_WINDOWS 1
#   ifndef WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif
#   ifndef NOMINMAX
#       define NOMINMAX
#   endif
#   include <windows.h>
#else
#   define FNUTIL_WINDOWS 0
#   include <dirent.h>
#   include <fcntl.h>
#   include <limits.h>
#   include <sys/stat.h>
#   include <sys/syscall.h>
#   include <sys/types.h>
#   include <unistd.h>
#   if defined(__APPLE__)
#       include <mach-o/dyld.h>
#   endif
// getdents64 는 glibc 2.30 이전까지 래퍼가 없으므로 syscall 직접 호출
#   ifndef __NR_getdents64
#       if defined(__x86_64__)
#           define __NR_getdents64 217
#       elif defined(__aarch64__)
#           define __NR_getdents64 217
#       elif defined(__i386__)
#           define __NR_getdents64 220
#       else
#           define __NR_getdents64 217
#       endif
#   endif
#endif

namespace fnutil {

namespace fs = std::filesystem;

// =========================================================
// ── 1. 정렬 플래그 ──────────────────────────────────────
// =========================================================

enum class flag : unsigned {
    none        = 0,
    naturalName = 1 << 0,   ///< 파일명 기준 자연 정렬
    naturalPath = 1 << 1,   ///< 전체 경로 자연 정렬
    ignoreCase  = 1 << 2,   ///< 대소문자 무시
};

inline flag operator|(flag a, flag b) {
    return static_cast<flag>(static_cast<unsigned>(a) | static_cast<unsigned>(b));
}
inline bool has(flag f, flag v) {
    return (static_cast<unsigned>(f) & static_cast<unsigned>(v)) != 0;
}

// =========================================================
// ── 2. 자연 정렬 ────────────────────────────────────────
//
//   ★ 단일 구현. 구 fastls::detail::natural_less 제거됨.
//     list/list_names/list_stat 모두 이 함수를 사용.
//
//   알고리즘:
//     숫자 구간을 선행 0 제거 후 자릿수 비교 → 자릿수 같으면 사전순.
//     "02"와 "2" 는 동등 처리 (Windows Explorer 스타일).
// =========================================================

// ── string_view 단일 구현 ────────────────────────────────
// const char*, string, string_view 모두 string_view 로 암시적 변환되므로
// 오버로드를 하나로 통일해 모호성을 제거한다.
inline bool naturalLess(std::string_view a, std::string_view b, bool ic = true)
{
    auto lower = [ic](char c) -> char {
        return ic ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c;
    };

    size_t ia = 0, ib = 0;

    while (ia < a.size() && ib < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[ia])) &&
            std::isdigit(static_cast<unsigned char>(b[ib])))
        {
            size_t za = ia, zb = ib;
            while (za < a.size() && a[za] == '0') ++za;
            while (zb < b.size() && b[zb] == '0') ++zb;

            size_t ja = za, jb = zb;
            while (ja < a.size() && std::isdigit(static_cast<unsigned char>(a[ja]))) ++ja;
            while (jb < b.size() && std::isdigit(static_cast<unsigned char>(b[jb]))) ++jb;

            const size_t da = ja - za;
            const size_t db = jb - zb;
            if (da != db) return da < db;

            for (size_t k = 0; k < da; ++k) {
                if (a[za + k] != b[zb + k])
                    return a[za + k] < b[zb + k];
            }
            ia = ja;
            ib = jb;
        }
        else {
            char ca = lower(a[ia]);
            char cb = lower(b[ib]);
            if (ca != cb) return ca < cb;
            ++ia; ++ib;
        }
    }
    return (a.size() - ia) < (b.size() - ib);
}

// =========================================================
// ── 3. fs::path 비교 / sort ─────────────────────────────
// =========================================================

inline bool pathLess(const fs::path& a, const fs::path& b, flag flags) {
    const bool ic = has(flags, flag::ignoreCase);

    if (has(flags, flag::naturalPath)) {
        auto ia = a.begin(), ib = b.begin();
        for (; ia != a.end() && ib != b.end(); ++ia, ++ib) {
            auto sa = ia->string(), sb = ib->string();
            if (sa == sb) continue;
            return naturalLess(sa, sb, ic);
        }
        return std::distance(a.begin(), a.end()) <
               std::distance(b.begin(), b.end());
    }

    if (has(flags, flag::naturalName))
        return naturalLess(a.filename().string(), b.filename().string(), ic);

    return a.native() < b.native();
}

template <typename It>
inline void sort(It first, It last, flag flags = flag::none) {
    using T = typename std::iterator_traits<It>::value_type;
    static_assert(std::is_same_v<T, fs::path>,
                  "fnutil::sort requires std::filesystem::path");
    std::sort(first, last,
        [flags](const fs::path& a, const fs::path& b) {
            return pathLess(a, b, flags);
        });
}

// =========================================================
// ── 4. 와일드카드 → 정규식 ──────────────────────────────
// =========================================================

inline std::string wildcardToRegexString(const std::string& pattern) {
    std::string rx;
    rx.reserve(pattern.size() * 2);
    for (char c : pattern) {
        switch (c) {
        case '*': rx += ".*"; break;
        case '?': rx += ".";  break;
        case '.': case '^': case '$': case '+':
        case '(': case ')': case '[': case ']':
        case '{': case '}': case '|': case '\\':
            rx += '\\'; rx += c; break;
        default: rx += c; break;
        }
    }
    return rx;
}

inline std::regex wildcardToRegex(const std::string& pattern, bool ignoreCase) {
    std::regex::flag_type flags = std::regex::ECMAScript;
    if (ignoreCase) flags |= std::regex::icase;
    return std::regex(wildcardToRegexString(pattern), flags);
}

// =========================================================
// ── 5. glob ─────────────────────────────────────────────
// =========================================================

inline std::vector<fs::path> glob(
    const fs::path& directory,
    const std::string& pattern,
    bool recursive  = false,
    bool ignoreCase = true)
{
    std::vector<fs::path> result;

    std::error_code ec;
    if (!fs::is_directory(directory, ec)) return result;

    const auto re = wildcardToRegex(pattern, ignoreCase);

    auto process = [&](const fs::directory_entry& e) {
        std::error_code ec2;
        if (!e.is_regular_file(ec2)) return;
        if (std::regex_match(e.path().filename().string(), re))
            result.push_back(e.path());
    };

    if (recursive) {
        fs::recursive_directory_iterator it(
            directory, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            process(*it);
        }
    } else {
        fs::directory_iterator it(
            directory, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            process(*it);
        }
    }
    return result;
}

// =========================================================
// ── 6. glob_ex ──────────────────────────────────────────
// =========================================================

struct glob_options {
    bool include_directories = false;
    bool recursive           = false;
    bool ignoreCase          = true;
    bool absolute            = true;
    bool keep_unmatched      = false;
};

inline std::vector<fs::path> glob_ex(
    const fs::path& input,
    glob_options opt = {})
{
    std::vector<fs::path> matches;

    fs::path dir = input.parent_path();
    if (dir.empty()) dir = fs::current_path();

    std::string pattern = input.filename().string();
    if (pattern.empty()) pattern = "*";

    std::regex::flag_type rflags = std::regex::ECMAScript;
    if (opt.ignoreCase) rflags |= std::regex::icase;
    const std::regex re(wildcardToRegexString(pattern), rflags);

    std::error_code ec;
    if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) return matches;

    auto process = [&](const fs::directory_entry& e) {
        std::error_code ec2;
        if (!opt.include_directories && !e.is_regular_file(ec2)) return;
        if (std::regex_match(e.path().filename().string(), re))
            matches.push_back(opt.absolute ? fs::absolute(e.path()) : e.path());
    };

    if (opt.recursive) {
        fs::recursive_directory_iterator it(
            dir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            process(*it);
        }
    } else {
        fs::directory_iterator it(
            dir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            process(*it);
        }
    }

    if (!matches.empty()) {
        fnutil::sort(matches.begin(), matches.end(),
                     flag::naturalPath | flag::ignoreCase);
    } else if (opt.keep_unmatched) {
        matches.push_back(input);
    }

    return matches;
}

// =========================================================
// ── 7. 경로 분해 / join ─────────────────────────────────
// =========================================================

struct path_parts {
    fs::path drive;
    fs::path dir;
    fs::path name;
    fs::path ext;
};

inline path_parts split(const fs::path& p) {
    const fs::path parent = p.parent_path();
    fs::path dir = parent.relative_path();
    return { p.root_name(), dir, p.stem(), p.extension() };
}

inline fs::path join(const std::vector<fs::path>& parts) {
    fs::path out;
    for (const auto& p : parts) out /= p;
    return out;
}

inline fs::path join(std::initializer_list<fs::path> parts) {
    fs::path out;
    for (const auto& p : parts) out /= p;
    return out;
}

// =========================================================
// ── 8. 앱 경로 / 설정 디렉터리 (구 term::util 에서 이동)
// =========================================================

/**
 * @brief 현재 실행 파일의 절대 경로를 반환합니다.
 *
 * 플랫폼별 구현:
 *  - Windows : GetModuleFileNameW (32768자 동적 버퍼)
 *  - Linux   : /proc/self/exe readlink (동적 버퍼 자동 확장)
 *  - macOS   : _NSGetExecutablePath (필요 크기 먼저 조회)
 *
 * @return 실행 파일의 weakly_canonical 경로. 실패 시 빈 경로.
 */
inline fs::path get_executable_path() {
#if FNUTIL_WINDOWS
    std::wstring buf(32768, L'\0');
    DWORD len = GetModuleFileNameW(nullptr, buf.data(),
                                   static_cast<DWORD>(buf.size()));
    if (len == 0 || len >= buf.size()) return {};
    buf.resize(len);
    return fs::weakly_canonical(fs::path(buf));

#elif defined(__linux__)
    std::string buf(4096, '\0');
    while (true) {
        ssize_t len = ::readlink("/proc/self/exe", buf.data(), buf.size());
        if (len < 0) return {};
        if (static_cast<size_t>(len) < buf.size()) {
            buf.resize(static_cast<size_t>(len));
            break;
        }
        buf.resize(buf.size() * 2);
    }
    return fs::weakly_canonical(fs::path(buf));

#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> buf(size + 1, '\0');
    if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
    return fs::weakly_canonical(fs::path(buf.data()));

#else
    return {};
#endif
}

/**
 * @brief 확장자 문자열을 정규화합니다. "conf" → ".conf"
 */
inline std::string normalize_extension(std::string_view ext) {
    if (ext.empty()) return "";
    std::string e(ext);
    if (e.front() != '.') e.insert(e.begin(), '.');
    return e;
}

/**
 * @brief 파일이 없을 때만 생성합니다.
 */
inline void create_file_if_missing(const fs::path& p,
                                    std::string_view contents = {}) {
    if (fs::exists(p)) return;
    std::ofstream ofs(p);
    if (ofs && !contents.empty()) ofs << contents;
}

/**
 * @brief 실행 파일 이름 기반 설정 파일 경로를 반환합니다.
 *
 * 예) /usr/bin/myapp → /usr/bin/myapp.conf
 *
 * @param ext      확장자 (기본 "conf")
 * @param create   true 이면 파일이 없을 때 빈 파일 생성
 * @param contents create=true 일 때 쓸 기본 내용
 */
inline fs::path get_executable_conf(std::string_view ext = "conf",
                                     bool create = false,
                                     std::string_view contents = {}) {
    fs::path exe = get_executable_path();
    if (exe.empty()) return {};
    fs::path cfg = exe;
    cfg.replace_extension(normalize_extension(ext));
    if (create) create_file_if_missing(cfg, contents);
    return cfg;
}

/**
 * @brief Portable 설정 파일 경로를 반환합니다.
 *
 * 구조: <exe_dir>/.config/<app_name>/<name>
 */
inline fs::path get_portable_config(std::string_view name,
                                     bool create = false,
                                     std::string_view contents = {}) {
    fs::path exe = get_executable_path();
    if (exe.empty()) return {};
    fs::path cfg = exe.parent_path() / ".config" / exe.stem()
                   / fs::path(name).filename();
    if (create) {
        std::error_code ec;
        fs::create_directories(cfg.parent_path(), ec);
        if (!ec) create_file_if_missing(cfg, contents);
    }
    return cfg;
}

/**
 * @brief OS 표준 config 디렉터리를 반환합니다.
 *
 * - Windows : %APPDATA%/<app>
 * - macOS   : ~/Library/Application Support/<app>
 * - Linux   : $XDG_CONFIG_HOME/<app> 또는 ~/.config/<app>
 */
inline fs::path get_home_config_dir() {
    fs::path exe = get_executable_path();
    if (exe.empty()) return {};
    const std::string app = exe.stem().string();
#if FNUTIL_WINDOWS
    if (auto p = std::getenv("APPDATA"))       return fs::path(p) / app;
#elif defined(__APPLE__)
    if (auto p = std::getenv("HOME"))
        return fs::path(p) / "Library" / "Application Support" / app;
#else
    if (auto x = std::getenv("XDG_CONFIG_HOME")) return fs::path(x) / app;
    if (auto h = std::getenv("HOME"))             return fs::path(h) / ".config" / app;
#endif
    return {};
}

/**
 * @brief OS 표준 config 파일 경로를 반환합니다.
 */
inline fs::path get_home_config(std::string_view name,
                                 bool create = false,
                                 std::string_view contents = {}) {
    fs::path base = get_home_config_dir();
    if (base.empty()) return {};
    fs::path cfg = base / fs::path(name).filename();
    if (create) {
        std::error_code ec;
        fs::create_directories(base, ec);
        if (!ec) create_file_if_missing(cfg, contents);
    }
    return cfg;
}

/**
 * @brief OS 표준 cache 디렉터리를 반환합니다.
 *
 * - Windows : %LOCALAPPDATA%/<app>/cache
 * - macOS   : ~/Library/Caches/<app>
 * - Linux   : $XDG_CACHE_HOME/<app> 또는 ~/.cache/<app>
 */
inline fs::path get_cache_dir() {
    fs::path exe = get_executable_path();
    if (exe.empty()) return {};
    const std::string app = exe.stem().string();
#if FNUTIL_WINDOWS
    if (auto p = std::getenv("LOCALAPPDATA")) return fs::path(p) / app / "cache";
#elif defined(__APPLE__)
    if (auto p = std::getenv("HOME"))         return fs::path(p) / "Library" / "Caches" / app;
#else
    if (auto x = std::getenv("XDG_CACHE_HOME")) return fs::path(x) / app;
    if (auto h = std::getenv("HOME"))            return fs::path(h) / ".cache" / app;
#endif
    return {};
}

/**
 * @brief OS 표준 log 디렉터리를 반환합니다.
 *
 * - Windows : %LOCALAPPDATA%/<app>/log
 * - macOS   : ~/Library/Logs/<app>
 * - Linux   : $XDG_STATE_HOME/<app>/log 또는 ~/.local/state/<app>/log
 */
inline fs::path get_log_dir() {
    fs::path exe = get_executable_path();
    if (exe.empty()) return {};
    const std::string app = exe.stem().string();
#if FNUTIL_WINDOWS
    if (auto p = std::getenv("LOCALAPPDATA")) return fs::path(p) / app / "log";
#elif defined(__APPLE__)
    if (auto p = std::getenv("HOME"))         return fs::path(p) / "Library" / "Logs" / app;
#else
    if (auto x = std::getenv("XDG_STATE_HOME")) return fs::path(x) / app / "log";
    if (auto h = std::getenv("HOME"))
        return fs::path(h) / ".local" / "state" / app / "log";
#endif
    return {};
}

/**
 * @brief fs::file_time_type → 날짜/시간 문자열.
 *
 * fs::file_time_type 에 의존하므로 strutil 대신 fnutil에 위치.
 *
 * 기본 포맷: "%Y-%m-%d %H:%M"
 *
 * 예) ftime2str(fs::last_write_time(p)) → "2024-03-15 09:42"
 */
inline std::string ftime2str(fs::file_time_type ftime,
                              const std::string& format = "%Y-%m-%d %H:%M") {
    using namespace std::chrono;
    auto sctp = time_point_cast<system_clock::duration>(
        ftime - fs::file_time_type::clock::now() + system_clock::now());
    std::time_t tt = system_clock::to_time_t(sctp);
    std::tm tm{};
#if FNUTIL_WINDOWS
    localtime_s(&tm, &tt);
#else
    localtime_r(&tt, &tm);
#endif
    char buf[64];
    std::strftime(buf, sizeof(buf), format.c_str(), &tm);
    return buf;
}

// =========================================================
// ── 9. wstring 기반 파일시스템 래퍼 ────────────────────
// =========================================================

/// 파일 확장자 반환 (점 제외, 소문자) — L"image.PNG" → L"png"
inline std::wstring get_extension(const std::wstring& path,
                                   bool lower_case = true,
                                   bool with_dot   = false) {
    std::wstring ext = fs::path(path).extension().wstring();
    if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
    if (lower_case) ext = util::to_lower_ascii(std::move(ext));
    if (with_dot && !ext.empty()) ext = L"." + ext;
    return ext;
}

/// 확장자 없는 파일명 반환
inline std::wstring get_stem(const std::wstring& path) {
    return fs::path(path).stem().wstring();
}

/// 부모 디렉토리 경로 반환
inline std::wstring get_directory(const std::wstring& path) {
    return fs::path(path).parent_path().wstring();
}

/// 확장자 교체
inline std::wstring change_extension(const std::wstring& path,
                                      const std::wstring& ext) {
    fs::path p(path);
    p.replace_extension(ext);
    return p.wstring();
}

/// 파일 존재 여부
inline bool file_exists(const std::wstring& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

/// 디렉토리 존재 여부
inline bool directory_exists(const std::wstring& path) {
    std::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

/// 파일 크기 반환 (실패 시 -1)
inline std::intmax_t get_file_size(const std::wstring& path) {
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    return ec ? -1 : static_cast<std::intmax_t>(sz);
}

// =========================================================
// ── 10. 고속 디렉토리 열거 (구 fastls.hpp 통합) ──────────
//
//   list_names / list / list_stat / count
//   자연 정렬 = fnutil::naturalLess(ic=true) 사용 (중복 제거)
// =========================================================

/// list_stat() 반환 타입 — stat 1회 포함
struct entry {
    std::string  name;
    fs::path     path;
    std::int64_t size  = 0;   ///< 파일 크기 (바이트)
    std::int64_t mtime = 0;   ///< 수정 시각 (Unix timestamp 초)
    bool         is_dir = false;
};

/// 고속 열거 옵션
struct ls_options {
    std::vector<std::string> extensions = {};   ///< 빈 경우 모든 파일 포함
    bool recursive      = false;                ///< 재귀 탐색
    bool include_dirs   = false;                ///< 디렉토리도 결과에 포함
    bool sort_natural   = true;                 ///< 자연 정렬 (파일명 기준)
};

// ── 내부 구현 ────────────────────────────────────────────

namespace detail {

/// 확장자 소문자 정규화: ".PNG" → ".png", "png" → ".png"
inline std::string normalize_ext(std::string_view e) {
    std::string out;
    out.reserve(e.size() + 1);
    if (!e.empty() && e[0] != '.') out += '.';
    for (char c : e)
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

inline std::vector<std::string> normalize_exts(
    const std::vector<std::string>& exts)
{
    std::vector<std::string> out;
    out.reserve(exts.size());
    for (const auto& e : exts) out.push_back(normalize_ext(e));
    return out;
}

/// 파일명에서 확장자 추출 (소문자)
inline std::string file_ext(std::string_view name) {
    auto pos = name.rfind('.');
    if (pos == std::string_view::npos || pos == 0) return "";
    std::string ext;
    ext.reserve(name.size() - pos);
    for (size_t i = pos; i < name.size(); ++i)
        ext += static_cast<char>(std::tolower(
            static_cast<unsigned char>(name[i])));
    return ext;
}

inline bool ext_ok(std::string_view name,
                   const std::vector<std::string>& norm_exts) {
    if (norm_exts.empty()) return true;
    const auto ext = file_ext(name);
    for (const auto& e : norm_exts)
        if (ext == e) return true;
    return false;
}

// ── 플랫폼별 readdir ────────────────────────────────────

#if !FNUTIL_WINDOWS

// GCC/Clang extension: C99 flexible array member.
// -Wpedantic 경고를 피하기 위해 크기 1 배열로 선언한다.
// 실제 접근은 syscall이 채운 버퍼를 직접 가리키므로 안전하다.
struct linux_dirent64 {
    ino64_t        d_ino;
    off64_t        d_off;
    unsigned short d_reclen;
    unsigned char  d_type;
    char           d_name[1]; // flexible array (C99 ext) 대신 크기 1 사용
};

constexpr int DTYPE_REG = 8;
constexpr int DTYPE_DIR = 4;
constexpr int DTYPE_LNK = 10;
constexpr int DTYPE_UNK = 0;

inline void readdir_fast(const std::string& dirpath,
                         const std::vector<std::string>& norm_exts,
                         bool include_dirs,
                         std::vector<std::string>& names,
                         std::vector<std::string>& subdirs)
{
    int fd = ::open(dirpath.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;

    constexpr std::size_t BUFSIZE = 65536;
    alignas(linux_dirent64) char buf[BUFSIZE];

    for (;;) {
        long nread = ::syscall(__NR_getdents64, fd, buf, BUFSIZE);
        if (nread <= 0) break;

        for (long pos = 0; pos < nread; ) {
            auto* d = reinterpret_cast<linux_dirent64*>(buf + pos);
            pos += d->d_reclen;

            const char* name = d->d_name;
            if (name[0] == '.' &&
                (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
                continue;

            unsigned char dtype = d->d_type;
            bool is_dir = (dtype == DTYPE_DIR);
            bool is_file = (dtype == DTYPE_REG || dtype == DTYPE_LNK ||
                            dtype == DTYPE_UNK);

            if (is_dir) {
                subdirs.emplace_back(name);
                if (include_dirs) names.emplace_back(name);
            } else if (is_file) {
                if (ext_ok(name, norm_exts)) names.emplace_back(name);
            }
        }
    }
    ::close(fd);
}

inline entry make_entry(const std::string& dirpath, const std::string& name) {
    entry e;
    e.name = name;
    e.path = fs::path(dirpath) / name;
    struct stat st{};
    if (::stat(e.path.c_str(), &st) == 0) {
        e.size   = static_cast<std::int64_t>(st.st_size);
        e.mtime  = static_cast<std::int64_t>(st.st_mtime);
        e.is_dir = S_ISDIR(st.st_mode);
    }
    return e;
}

#else // FNUTIL_WINDOWS

inline void readdir_fast(const std::string& dirpath,
                         const std::vector<std::string>& norm_exts,
                         bool include_dirs,
                         std::vector<std::string>& names,
                         std::vector<std::string>& subdirs)
{
    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileExA(
        (dirpath + "\\*").c_str(),
        FindExInfoBasic,
        &fd,
        FindExSearchNameMatch,
        nullptr,
        FIND_FIRST_EX_LARGE_FETCH
    );
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        const char* name = fd.cFileName;
        if (name[0] == '.' &&
            (name[1] == '\0' || (name[1] == '.' && name[2] == '\0')))
            continue;

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            subdirs.emplace_back(name);
            if (include_dirs) names.emplace_back(name);
        } else {
            if (ext_ok(name, norm_exts)) names.emplace_back(name);
        }
    } while (::FindNextFileA(h, &fd));

    ::FindClose(h);
}

inline entry make_entry(const std::string& dirpath, const std::string& name) {
    entry e;
    e.name = name;
    e.path = fs::path(dirpath) / name;
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (::GetFileAttributesExA(e.path.string().c_str(),
                               GetFileExInfoStandard, &info))
    {
        ULARGE_INTEGER sz;
        sz.LowPart  = info.nFileSizeLow;
        sz.HighPart = info.nFileSizeHigh;
        e.size  = static_cast<std::int64_t>(sz.QuadPart);

        ULARGE_INTEGER ft;
        ft.LowPart  = info.ftLastWriteTime.dwLowDateTime;
        ft.HighPart = info.ftLastWriteTime.dwHighDateTime;
        e.mtime = static_cast<std::int64_t>(
            (ft.QuadPart - 116444736000000000ULL) / 10000000ULL);
        e.is_dir = (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }
    return e;
}

#endif // FNUTIL_WINDOWS

// ── 재귀 수집 공통 로직 ──────────────────────────────────

inline void collect_names(const std::string& dirpath,
                          const std::vector<std::string>& norm_exts,
                          bool include_dirs,
                          bool recursive,
                          std::vector<std::pair<std::string,std::string>>& out)
{
    std::vector<std::string> names, subdirs;
    readdir_fast(dirpath, norm_exts, include_dirs, names, subdirs);

    for (auto& n : names)
        out.emplace_back(dirpath, std::move(n));

    if (recursive) {
        for (const auto& sub : subdirs) {
#if FNUTIL_WINDOWS
            const std::string subpath = dirpath + '\\' + sub;
#else
            const std::string subpath = dirpath + '/' + sub;
#endif
            collect_names(subpath, norm_exts, include_dirs, recursive, out);
        }
    }
}

} // namespace detail

// =========================================================
// ── 공개 API ─────────────────────────────────────────────
// =========================================================

// ─────────────────────────────────────────────────────────
// 진입점 설계 원칙:
//   fs::path 단일 오버로드가 const char*, std::string, std::string_view,
//   fs::path 를 모두 수용한다. (std::string 과 fs::path 양쪽 오버로드를
//   두면 const char* 에서 모호성이 발생하므로 fs::path 하나로 통일)
// ─────────────────────────────────────────────────────────

// ── list_names — 파일명만 (stat 없음, 최고 속도) ──────────

/// 파일명 목록 반환 (경로 없음, stat 없음)
/// dirpath: const char*, std::string, std::string_view, fs::path 모두 수용
inline std::vector<std::string>
list_names(const fs::path& dirpath,
           const std::vector<std::string>& extensions = {},
           bool recursive   = false,
           bool sort_result = true)
{
    const auto norm = detail::normalize_exts(extensions);
    std::vector<std::pair<std::string,std::string>> raw;
    raw.reserve(512);
    detail::collect_names(dirpath.string(), norm, false, recursive, raw);

    std::vector<std::string> result;
    result.reserve(raw.size());
    for (auto& [dir, name] : raw)
        result.push_back(std::move(name));

    if (sort_result)
        std::sort(result.begin(), result.end(),
                  [](const std::string& a, const std::string& b) {
                      return naturalLess(a, b, true);
                  });
    return result;
}

// ── list — 전체 경로 목록 (stat 없음) ─────────────────────

/// 전체 경로 목록 반환 (stat 없음)
/// dirpath: const char*, std::string, std::string_view, fs::path 모두 수용
inline std::vector<fs::path>
list(const fs::path& dirpath,
     const std::vector<std::string>& extensions = {},
     bool recursive   = false,
     bool sort_result = true)
{
    const auto norm = detail::normalize_exts(extensions);
    std::vector<std::pair<std::string,std::string>> raw;
    raw.reserve(512);
    detail::collect_names(dirpath.string(), norm, false, recursive, raw);

    std::vector<fs::path> result;
    result.reserve(raw.size());
    for (auto& [dir, name] : raw)
        result.push_back(fs::path(dir) / name);

    if (sort_result)
        std::sort(result.begin(), result.end(),
                  [](const fs::path& a, const fs::path& b) {
                      return naturalLess(a.filename().string(),
                                         b.filename().string(), true);
                  });
    return result;
}

inline std::vector<fs::path>
list(const fs::path& dirpath, const ls_options& opt)
{
    return list(dirpath, opt.extensions, opt.recursive, opt.sort_natural);
}

// ── list_stat — 크기·날짜 포함 (파일별 stat 1회) ──────────

/// 상세 정보 포함 목록 반환 (파일별 stat() 1회)
/// dirpath: const char*, std::string, std::string_view, fs::path 모두 수용
inline std::vector<entry>
list_stat(const fs::path& dirpath,
          const std::vector<std::string>& extensions = {},
          bool recursive   = false,
          bool sort_result = true)
{
    const auto norm = detail::normalize_exts(extensions);
    std::vector<std::pair<std::string,std::string>> raw;
    raw.reserve(512);
    detail::collect_names(dirpath.string(), norm, false, recursive, raw);

    std::vector<entry> result;
    result.reserve(raw.size());
    for (auto& [dir, name] : raw)
        result.push_back(detail::make_entry(dir, name));

    if (sort_result)
        std::sort(result.begin(), result.end(),
                  [](const entry& a, const entry& b) {
                      return naturalLess(a.name, b.name, true);
                  });
    return result;
}

// ── count — 파일 수만 (최고 속도) ─────────────────────────

/// 파일 수 반환
/// dirpath: const char*, std::string, std::string_view, fs::path 모두 수용
inline std::size_t count(const fs::path& dirpath,
                         const std::vector<std::string>& extensions = {},
                         bool recursive = false)
{
    const auto norm = detail::normalize_exts(extensions);
    std::vector<std::pair<std::string,std::string>> raw;
    detail::collect_names(dirpath.string(), norm, false, recursive, raw);
    return raw.size();
}

} // namespace fnutil

#endif // FNUTIL_HPP
