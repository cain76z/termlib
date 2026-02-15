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
        ? std::tolower(static_cast<unsigned char>(c))
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
        if (std::isdigit((unsigned char)a[ia]) &&
            std::isdigit((unsigned char)b[ib])) {

            size_t ja = ia, jb = ib;

            while (ja < a.size() && std::isdigit((unsigned char)a[ja])) ++ja;
            while (jb < b.size() && std::isdigit((unsigned char)b[jb])) ++jb;
            // Improved numeric parsing with error handling
            try {
                long long na = std::stoll(a.substr(ia, ja - ia));
                long long nb = std::stoll(b.substr(ib, jb - ib));
                if (na != nb) return na < nb;
            } catch (const std::exception&) {
                // Fall back to lexicographic comparison
                return a.substr(ia, ja - ia) < b.substr(ib, jb - ib);
            }

            if ((ja - ia) != (jb - ib))
                return (ja - ia) < (jb - ib);

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
            return naturalLess(sa, sb, ic);
        }
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
 *  wildcard → regex
 * ========================================================= */
inline std::regex wildcardToRegex(const std::string& pattern,
                                  bool ignoreCase) {
    std::string rx;
    rx.reserve(pattern.size() * 2);

    for (char c : pattern) {
        switch (c) {
        case '*': rx += ".*"; break;
        case '?': rx += "."; break;
        case '.': rx += "\\."; break;
        default:  rx += c; break;
        }
    }

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

    if (!fs::is_directory(directory))
        return result;

    const auto regex = wildcardToRegex(pattern, ignoreCase);

    if (recursive) {
        for (const auto& e :
             fs::recursive_directory_iterator(
                 directory,
                 fs::directory_options::skip_permission_denied)) {

            if (!e.is_regular_file()) continue;

            const auto name = e.path().filename().string();
            if (std::regex_match(name, regex)) {
                result.push_back(e.path());
            }
        }
    }
    else {
        for (const auto& e :
             fs::directory_iterator(
                 directory,
                 fs::directory_options::skip_permission_denied)) {

            if (!e.is_regular_file()) continue;

            const auto name = e.path().filename().string();
            if (std::regex_match(name, regex)) {
                result.push_back(e.path());
            }
        }
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

inline std::string wildcardToRegexEx(const std::string& wc) {
    std::string re = "^";
    re.reserve(wc.size() * 2);

    for (char c : wc) {
        switch (c) {
        case '*': re += ".*"; break;
        case '?': re += ".";  break;

        case '.': case '^': case '$': case '+':
        case '(': case ')': case '[': case ']':
        case '{': case '}': case '|': case '\\':
            re += '\\';
            re += c;
            break;

        default:
            re += c;
            break;
        }
    }

    re += "$";
    return re;
}

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

    std::regex::flag_type flags = std::regex::ECMAScript;
    if (opt.ignoreCase)
        flags |= std::regex::icase;

    const std::regex re(
        wildcardToRegexEx(pattern),
        flags
    );

    try {
        if (!fs::exists(dir) || !fs::is_directory(dir))
            return matches;

        if (opt.recursive) {
            for (const auto& e :
                 fs::recursive_directory_iterator(
                     dir,
                     fs::directory_options::skip_permission_denied)) {

                if (!opt.include_directories &&
                    !e.is_regular_file())
                    continue;

                const auto name =
                    e.path().filename().string();

                if (std::regex_match(name, re)) {
                    matches.push_back(
                        opt.absolute
                        ? fs::absolute(e.path())
                        : e.path());
                }
            }
        } else {
            for (const auto& e :
                 fs::directory_iterator(
                     dir,
                     fs::directory_options::skip_permission_denied)) {

                if (!opt.include_directories &&
                    !e.is_regular_file())
                    continue;

                const auto name =
                    e.path().filename().string();

                if (std::regex_match(name, re)) {
                    matches.push_back(
                        opt.absolute
                        ? fs::absolute(e.path())
                        : e.path());
                }
            }
        }
    }
    catch (const fs::filesystem_error&) {
        // fnutil 스타일: 조용히 무시
    }

    // 매칭 실패 시 원본 유지 (CLI 관례)
    if (matches.empty()) {
        matches.push_back(input);
    }
    else {
        fnutil::sort(
            matches.begin(),
            matches.end(),
            flag::naturalPath | flag::ignoreCase);
    }

    return matches;
}

/* =========================================================
 *  path split
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

/* =========================================================
 *  path join
 * ========================================================= */
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
