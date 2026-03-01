// str.hpp — std::string utils (C++17)
#pragma once
#include <algorithm>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace str {

// ──────────────────────────────────────────────
// 1. 공백 제거 (Trimming)
// ──────────────────────────────────────────────

/// 왼쪽 공백 제거 — view 반환 (zero-copy)
inline std::string_view ltrim(std::string_view s) {
    auto it = std::find_if_not(s.begin(), s.end(),
                               [](unsigned char c) { return std::isspace(c); });
    return s.substr(static_cast<std::size_t>(it - s.begin()));
}

/// 오른쪽 공백 제거 — view 반환 (zero-copy)
inline std::string_view rtrim(std::string_view s) {
    auto it = std::find_if_not(s.rbegin(), s.rend(),
                               [](unsigned char c) { return std::isspace(c); });
    return s.substr(0, static_cast<std::size_t>(s.rend() - it));
}

/// 양쪽 공백 제거 — view 반환 (zero-copy)
inline std::string_view strip(std::string_view s) {
    return rtrim(ltrim(s));
}

/// 양쪽 공백 제거 — string 반환 (소유권 필요할 때 사용)
inline std::string trim(std::string_view s) {
    return std::string(strip(s));
}

// ──────────────────────────────────────────────
// 2. 대소문자 변환 (Case Conversion)
// ──────────────────────────────────────────────

inline void to_lower_inplace(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
}

inline std::string to_lower(std::string_view s) {
    std::string result{s};
    to_lower_inplace(result);
    return result;
}

inline void to_upper_inplace(std::string& s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
}

inline std::string to_upper(std::string_view s) {
    std::string result{s};
    to_upper_inplace(result);
    return result;
}

// ──────────────────────────────────────────────
// 3. 문자열 분리 (Split)
//
//   split(s, string_view) — 구분자 문자열, 빈 토큰 포함, 트림 없음
//   split(s, char)        — 구분자 문자, 빈 토큰 제거 + 각 토큰 trim
//   split_comma(s)        — 콤마 구분, 빈 토큰 제거 + 각 토큰 trim
// ──────────────────────────────────────────────

/// 구분자 문자열로 분리 — 빈 토큰 포함, 트림 없음
inline std::vector<std::string> split(std::string_view s,
                                      std::string_view delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0, pos = 0;

    while ((pos = s.find(delimiter, start)) != std::string_view::npos) {
        result.emplace_back(s.substr(start, pos - start));
        start = pos + delimiter.size();
    }
    result.emplace_back(s.substr(start)); // 마지막 토큰
    return result;
}

/// 구분자 문자로 분리 — 각 토큰 trim, 빈 토큰 제거
inline std::vector<std::string> split(std::string_view s, char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0;

    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == delimiter) {
            auto token = trim(s.substr(start, i - start));
            if (!token.empty())
                result.push_back(std::move(token));
            start = i + 1;
        }
    }
    return result;
}

/// 콤마로 분리 — 각 토큰 trim, 빈 토큰 제거
/// "mp3, ogg, wav" → {"mp3", "ogg", "wav"}
inline std::vector<std::string> split_comma(std::string_view s) {
    return split(s, ',');
}

// ──────────────────────────────────────────────
// 4. 주석 제거 (Comment Stripping)
//
//   INI / conf 파일의 인라인 주석을 제거한다.
//   따옴표('" 또는 ') 내부의 # / ; 는 주석으로 처리하지 않음.
// ──────────────────────────────────────────────

/// '#' 또는 ';' 이후를 제거 — 따옴표 내부는 보존
inline std::string strip_comment(std::string_view s) {
    bool in_quote   = false;
    char quote_char = 0;

    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (!in_quote && (c == '"' || c == '\'')) {
            in_quote   = true;
            quote_char = c;
        } else if (in_quote && c == quote_char) {
            in_quote = false;
        } else if (!in_quote && (c == ';' || c == '#')) {
            return std::string(s.substr(0, i));
        }
    }
    return std::string(s);
}

// ──────────────────────────────────────────────
// 5. 문자열 결합 (Join)
// ──────────────────────────────────────────────

template <typename Range>
inline std::string join(const Range& range, std::string_view delimiter) {
    std::string result;
    auto it  = std::begin(range);
    auto end = std::end(range);

    if (it != end) {
        result.append(*it);
        ++it;
    }
    for (; it != end; ++it) {
        result.append(delimiter);
        result.append(*it);
    }
    return result;
}

// ──────────────────────────────────────────────
// 6. 포함 여부 (Contains)
// ──────────────────────────────────────────────

inline bool contains(std::string_view s, std::string_view sub) {
    return s.find(sub) != std::string_view::npos;
}

// ──────────────────────────────────────────────
// 7. 타입 → string 변환
//    bool은 "true"/"false" 문자열로 반환
//    float/double은 std::to_string 사용
// ──────────────────────────────────────────────

inline std::string to_string(int v)    { return std::to_string(v); }
inline std::string to_string(long v)   { return std::to_string(v); }
inline std::string to_string(float v)  { return std::to_string(v); }
inline std::string to_string(double v) { return std::to_string(v); }
inline std::string to_string(bool v)   { return v ? "true" : "false"; }

// ──────────────────────────────────────────────
// 8. string → 타입 변환
//    실패 시 std::nullopt 반환 (예외 없음)
//
//    ※ from_chars(float/double)는 C++17 명세에 있으나
//      GCC 11 / Clang 14 미만에서 미지원.
//      이식성을 위해 stof / stod 사용.
// ──────────────────────────────────────────────

inline std::optional<int> to_int(std::string_view s) {
    int value{};
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec == std::errc{} && ptr == s.data() + s.size())
        return value;
    return std::nullopt;
}

inline std::optional<float> to_float(std::string_view s) {
    try {
        std::size_t pos{};
        float v = std::stof(std::string{s}, &pos);
        if (pos == s.size()) return v;
    } catch (...) {}
    return std::nullopt;
}

inline std::optional<double> to_double(std::string_view s) {
    try {
        std::size_t pos{};
        double v = std::stod(std::string{s}, &pos);
        if (pos == s.size()) return v;
    } catch (...) {}
    return std::nullopt;
}

/// "true" / "1"  → true
/// "false" / "0" → false
/// 그 외          → nullopt
inline std::optional<bool> to_bool(std::string_view s) {
    const auto lower = to_lower(s);
    if (lower == "true"  || lower == "1") return true;
    if (lower == "false" || lower == "0") return false;
    return std::nullopt;
}

} // namespace str