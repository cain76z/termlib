#ifndef FNUTIL_HPP
#define FNUTIL_HPP
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <regex>
#include <string>
#include <system_error>
#include <type_traits>
#include <vector>

#include "encode.hpp"   // util::to_lower_ascii

namespace fnutil {

namespace fs = std::filesystem;

/* =========================================================
 *  flag (정렬 옵션)
 * ========================================================= */
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

/* =========================================================
 *  naturalLess — 숫자 구간을 수치로 비교하는 자연 정렬
 *
 *  [fix 3] stoll + try-catch 제거 → 예외 없는 직접 비교
 *    1) 선행 0 제거 후 유효 자릿수 비교 (긴 쪽이 큼)
 *    2) 자릿수 같으면 사전순 비교
 *
 *  [fix 4] 선행 0 정책 — Windows Explorer 스타일
 *    "02"와 "2"는 수치가 동일하므로 동등 처리 (tie-breaker 없음).
 * ========================================================= */
inline bool naturalLess(const std::string& a,
                        const std::string& b,
                        bool ic)
{
    auto lower = [ic](char c) -> char {
        return ic ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : c;
    };

    size_t ia = 0, ib = 0;

    while (ia < a.size() && ib < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[ia])) &&
            std::isdigit(static_cast<unsigned char>(b[ib])))
        {
            // 선행 0 건너뜀
            size_t za = ia, zb = ib;
            while (za < a.size() && a[za] == '0') ++za;
            while (zb < b.size() && b[zb] == '0') ++zb;

            // 숫자 블록 끝
            size_t ja = za, jb = zb;
            while (ja < a.size() && std::isdigit(static_cast<unsigned char>(a[ja]))) ++ja;
            while (jb < b.size() && std::isdigit(static_cast<unsigned char>(b[jb]))) ++jb;

            // 유효 자릿수 비교 — 예외 없음, 할당 없음
            const size_t da = ja - za;
            const size_t db = jb - zb;
            if (da != db) return da < db;

            // 자릿수 같으면 사전순
            for (size_t k = 0; k < da; ++k) {
                if (a[za + k] != b[zb + k])
                    return a[za + k] < b[zb + k];
            }
            // 수치 동일 → 다음 블록으로
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

    // 남은 문자 수 비교.
    // a.size() < b.size() 를 쓰면 "file2"(5) vs "file02"(6) 에서
    // 숫자 블록을 소비한 뒤 ia=5, ib=6 으로 둘 다 남은 문자가 0개임에도
    // 5 < 6 = true 를 반환하는 버그가 생김.
    return (a.size() - ia) < (b.size() - ib);
}

/* =========================================================
 *  pathLess — fs::path 비교
 * ========================================================= */
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

    // [fix 5] native() 명시 (operator< 내부 동작과 동일하나 의도 명확)
    return a.native() < b.native();
}

/* =========================================================
 *  sort wrapper
 * ========================================================= */
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

/* =========================================================
 *  wildcard → regex
 *
 *  [fix 1] wildcardToRegex: icase는 ECMAScript와 OR 조합 필수
 *          (표준상 icase 단독은 syntax option이 아닌 플래그)
 * ========================================================= */
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

/* =========================================================
 *  glob — 디렉토리 + 패턴 매칭
 *
 *  [fix 8] error_code 기반 이터레이터 — 순회 중 예외 방지
 * ========================================================= */
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

/* =========================================================
 *  glob_options
 * ========================================================= */
struct glob_options {
    bool include_directories = false;
    bool recursive           = false;
    bool ignoreCase          = true;
    bool absolute            = true;
    /// [fix 2] 미매칭 시 원본을 결과에 포함할지 여부
    /// false(기본): 매칭 없으면 빈 벡터 — 호출자가 존재하지 않는 파일을 오인하지 않음
    /// true: 이전 동작 유지 (shell globbing 모방 목적으로 명시적 선택)
    bool keep_unmatched      = false;
};

/* =========================================================
 *  glob_ex — 경로+와일드카드 통합 처리
 *
 *  [fix 2] 미매칭 기본: 빈 벡터 반환
 *  [fix 8] error_code 기반 이터레이터
 * ========================================================= */
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
        matches.push_back(input);  // 명시적으로 요청한 경우에만
    }

    return matches;
}

/* =========================================================
 *  path_parts / split / join
 *
 *  [fix 6] Windows에서 parent_path()가 drive를 포함하므로
 *  dir은 root_path()를 제거한 상대 부분만 담음
 * ========================================================= */
struct path_parts {
    fs::path drive;   ///< Windows: "C:"  / POSIX: ""
    fs::path dir;     ///< drive를 제외한 부모 디렉토리 (상대 경로)
    fs::path name;    ///< 확장자 없는 파일명 (stem)
    fs::path ext;     ///< 확장자 (점 포함, 예: ".png")
};

/// 경로를 구성 요소로 분해
/// Windows 예) "C:\dir\sub\file.txt"
///   drive="C:", dir="dir/sub", name="file", ext=".txt"
/// POSIX 예) "/dir/sub/file.txt"
///   drive="",   dir="dir/sub", name="file", ext=".txt"
inline path_parts split(const fs::path& p) {
    const fs::path parent = p.parent_path();
    // root_path() = root_name() + root_directory() ("C:\" or "/")
    // relative_path()는 root를 제거한 나머지
    fs::path dir = parent.relative_path();  // "C:\dir" → "dir"

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

/* =========================================================
 *  wstring 기반 파일시스템 래퍼
 * ========================================================= */

/// 파일 확장자 반환 (점 제외, 소문자) — L"image.PNG" → L"png"
inline std::wstring get_extension(const std::wstring& path, bool lower_case = true, bool with_dot = false) {
    std::wstring ext = fs::path(path).extension().wstring();
    if (!ext.empty() && ext[0] == L'.') ext = ext.substr(1);
    if (lower_case) ext = util::to_lower_ascii(std::move(ext));
    if (with_dot && !ext.empty()) ext = L"." + ext;
    return ext;
}

/// 확장자 없는 파일명 반환 — L"archive.tar.gz" → L"archive.tar"
inline std::wstring get_stem(const std::wstring& path) {
    return fs::path(path).stem().wstring();
}

/// 부모 디렉토리 경로 반환 — L"/a/b/c.txt" → L"/a/b"
inline std::wstring get_directory(const std::wstring& path) {
    return fs::path(path).parent_path().wstring();
}

/// 확장자 교체 — change_extension(L"a/b.txt", L".md") → L"a/b.md"
inline std::wstring change_extension(const std::wstring& path,
                                     const std::wstring& ext)
{
    fs::path p(path);
    p.replace_extension(ext);
    return p.wstring();
}

/// 파일 존재 여부 (오류 시 false)
inline bool file_exists(const std::wstring& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

/// 디렉토리 존재 여부 (오류 시 false)
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

} // namespace fnutil

#endif // FNUTIL_HPP