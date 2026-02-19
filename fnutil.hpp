#ifndef FNUTIL_HPP
#define FNUTIL_HPP
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <regex>
#include <string>
#include <type_traits>
#include <vector>

namespace fnutil {

namespace fs = std::filesystem;

/* =========================================================
 *  flag (정렬 옵션)
 * ========================================================= */
enum class flag : unsigned {
    none         = 0,
    naturalName = 1 << 0, // 파일명 기준 자연 정렬
    naturalPath = 1 << 1, // 전체 경로 자연 정렬
    ignoreCase  = 1 << 2, // 대소문자 무시
};

inline flag operator|(flag a, flag b) {
    return static_cast<flag>(
        static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

inline bool has(flag f, flag v) {
    return (static_cast<unsigned>(f) &
            static_cast<unsigned>(v)) != 0;
}

/* =========================================================
 *  내부 유틸
 * ========================================================= */
inline char normChar(char c, bool ignoreCase) {
    return ignoreCase
        ? static_cast<char>(std::tolower(static_cast<unsigned char>(c)))
        : c;
}

/* =========================================================
 *  문자열 자연 정렬
 * ========================================================= */
inline bool naturalLess(
    const std::string& a,
    const std::string& b,
    bool ignoreCase
) {
    size_t ia = 0, ib = 0;

    while (ia < a.size() && ib < b.size()) {
        if (std::isdigit(static_cast<unsigned char>(a[ia])) &&
            std::isdigit(static_cast<unsigned char>(b[ib]))) {

            size_t ja = ia, jb = ib;

            while (ja < a.size() && std::isdigit(static_cast<unsigned char>(a[ja]))) ++ja;
            while (jb < b.size() && std::isdigit(static_cast<unsigned char>(b[jb]))) ++jb;

            try {
                // stoll은 예외를 던질 수 있으므로 try-catch 유지
                long long na = std::stoll(a.substr(ia, ja - ia));
                long long nb = std::stoll(b.substr(ib, jb - ib));

                if (na != nb) return na < nb;

                // [수정] 숫자 값은 같지만 자릿수가 다른 경우 처리 (예: "02" < "2")
                if ((ja - ia) != (jb - ib))
                    return (ja - ia) < (jb - ib);

            } catch (const std::exception&) {
                // 변환 실패 시 사전순 비교로 폴백
                auto sa = a.substr(ia, ja - ia);
                auto sb = b.substr(ib, jb - ib);
                if (sa != sb) return sa < sb;
            }

            ia = ja;
            ib = jb;
        }
        else {
            char ca = normChar(a[ia], ignoreCase);
            char cb = normChar(b[ib], ignoreCase);

            if (ca != cb) return ca < cb;
            ++ia;
            ++ib;
        }
    }

    return a.size() < b.size();
}

/* =========================================================
 *  path 비교
 * ========================================================= */
inline bool pathLess(const fs::path& a,
                     const fs::path& b,
                     flag flags) {
    const bool ic = has(flags, flag::ignoreCase);

    if (has(flags, flag::naturalPath)) {
        auto ia = a.begin();
        auto ib = b.begin();

        for (; ia != a.end() && ib != b.end(); ++ia, ++ib) {
            auto sa = ia->string();
            auto sb = ib->string();

            if (sa == sb) continue;
            
            // naturalLess 결과에 따라 비교 반환
            return naturalLess(sa, sb, ic);
        }
        // 경로 구성 요소 개수 비교 (예: /a vs /a/b)
        return std::distance(a.begin(), a.end()) <
               std::distance(b.begin(), b.end());
    }

    if (has(flags, flag::naturalName)) {
        return naturalLess(
            a.filename().string(),
            b.filename().string(),
            ic
        );
    }

    return a < b;
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
 *  wildcard → regex (개선됨)
 * ========================================================= */
inline std::string wildcardToRegexString(const std::string& pattern) {
    std::string rx;
    rx.reserve(pattern.size() * 2);

    for (char c : pattern) {
        switch (c) {
        case '*': rx += ".*"; break;
        case '?': rx += ".";  break;
        // 정규식 메타 문자 이스케이프 처리
        case '.': case '^': case '$': case '+':
        case '(': case ')': case '[': case ']':
        case '{': case '}': case '|': case '\\':
            rx += '\\';
            rx += c;
            break;
        default:  rx += c; break;
        }
    }
    return rx;
}

inline std::regex wildcardToRegex(const std::string& pattern,
                                  bool ignoreCase) {
    std::string rx = wildcardToRegexString(pattern);

    return std::regex(
        rx,
        ignoreCase ? std::regex::icase : std::regex::ECMAScript
    );
}

/* =========================================================
 *  디렉토리 + 와일드카드 로드
 * ========================================================= */
inline std::vector<fs::path> glob(
    const fs::path& directory,
    const std::string& pattern,
    bool recursive = false,
    bool ignoreCase = true
) {
    std::vector<fs::path> result;

    try {
        if (!fs::is_directory(directory))
            return result;

        const auto regex = wildcardToRegex(pattern, ignoreCase);

        auto process_entry = [&](const fs::directory_entry& e) {
            if (!e.is_regular_file()) return;
            const auto name = e.path().filename().string();
            if (std::regex_match(name, regex)) {
                result.push_back(e.path());
            }
        };

        if (recursive) {
            for (const auto& e : fs::recursive_directory_iterator(
                    directory, fs::directory_options::skip_permission_denied)) {
                process_entry(e);
            }
        }
        else {
            for (const auto& e : fs::directory_iterator(
                    directory, fs::directory_options::skip_permission_denied)) {
                process_entry(e);
            }
        }
    } catch (const fs::filesystem_error&) {
        // 권한 등의 문제로 디렉토리 순회 실패 시 조용히 무시
    }

    return result;
}

/* =========================================================
 *  glob_ex (확장 와일드카드)
 * ========================================================= */
struct glob_options {
    bool include_directories = false;
    bool recursive = false;
    bool ignoreCase = true;
    bool absolute = true;
};

inline std::vector<fs::path> glob_ex(
    const fs::path& input,
    glob_options opt = {}
) {
    std::vector<fs::path> matches;

    fs::path dir = input.parent_path();
    if (dir.empty())
        dir = fs::current_path();

    std::string pattern = input.filename().string();
    if (pattern.empty())
        pattern = "*";

    // 공통 함수 사용
    std::regex::flag_type flags = std::regex::ECMAScript;
    if (opt.ignoreCase)
        flags |= std::regex::icase;

    const std::regex re(wildcardToRegexString(pattern), flags);

    try {
        if (!fs::exists(dir) || !fs::is_directory(dir))
            return matches; // 디렉토리 없으면 빈 벡터 반환

        auto process_entry = [&](const fs::directory_entry& e) {
            if (!opt.include_directories && !e.is_regular_file())
                return;

            const auto name = e.path().filename().string();
            if (std::regex_match(name, re)) {
                matches.push_back(opt.absolute ? fs::absolute(e.path()) : e.path());
            }
        };

        if (opt.recursive) {
            for (const auto& e : fs::recursive_directory_iterator(
                    dir, fs::directory_options::skip_permission_denied)) {
                process_entry(e);
            }
        } else {
            for (const auto& e : fs::directory_iterator(
                    dir, fs::directory_options::skip_permission_denied)) {
                process_entry(e);
            }
        }
    }
    catch (const fs::filesystem_error&) {
        // 무시
    }

    // [정책 변경 제안] 매칭 실패 시 원본 유지 대신 빈 벡터 반환 고려
    // 여기서는 기존 코드의 의도를 존중하되, 정렬은 결과가 있을 때만 수행
    if (!matches.empty()) {
        fnutil::sort(
            matches.begin(),
            matches.end(),
            flag::naturalPath | flag::ignoreCase);
    } else {
        // 기존 코드: matches.push_back(input);
        // 필요하다면 원본 유지 로직을 여기에 둡니다.
        matches.push_back(input);
    }

    return matches;
}

/* =========================================================
 *  path split / join
 * ========================================================= */
struct path_parts {
    fs::path drive;
    fs::path dir;
    fs::path name;
    fs::path ext;
};

inline path_parts split(const fs::path& p) {
    return {
        p.root_name(),          // Windows: C:
        p.parent_path(),        // directory
        p.stem(),               // filename without ext
        p.extension()           // .png
    };
}

inline fs::path join(const std::vector<fs::path>& parts) {
    fs::path out;
    for (const auto& p : parts)
        out /= p;
    return out;
}

inline fs::path join(std::initializer_list<fs::path> parts) {
    fs::path out;
    for (const auto& p : parts)
        out /= p;
    return out;
}

} // namespace fnutil

#endif // FNUTIL_HPP