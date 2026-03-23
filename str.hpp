// str.hpp — std::string utils + 숫자/시간 문자열 변환 (C++17)
// namespace: strutil  (구 str:: → strutil:: 로 변경)
#pragma once
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace strutil {

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
inline std::string_view trim(std::string_view s) {
    return rtrim(ltrim(s));
}

/// 양쪽 공백 제거 — string 반환 (소유권 필요할 때 사용)
inline std::string strip(std::string_view s) {
    return std::string(trim(s));
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
    result.emplace_back(s.substr(start));
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
                result.push_back(std::string(token));
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
// ──────────────────────────────────────────────

inline std::string to_string(int v)    { return std::to_string(v); }
inline std::string to_string(long v)   { return std::to_string(v); }
inline std::string to_string(float v)  { return std::to_string(v); }
inline std::string to_string(double v) { return std::to_string(v); }
inline std::string to_string(bool v)   { return v ? "true" : "false"; }

// ──────────────────────────────────────────────
// 8. string → 타입 변환 (실패 시 std::nullopt, 예외 없음)
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

} // namespace strutil

// ──────────────────────────────────────────────
// 9. 숫자 / 시간 → 문자열 변환
//    (구 term::util 에서 이동)
//    filesystem 의존이 없는 순수 변환 함수.
//    fs::file_time_type → 문자열은 fnutil::ftime2str 사용.
// ──────────────────────────────────────────────

namespace strutil {

/**
 * @brief 바이트 크기를 사람이 읽기 쉬운 문자열로 변환합니다.
 *
 * 예: 0 → "0 B" | 1536 → "1.5 KB" | 2097152 → "2.0 MB"
 *
 * @param bytes     변환할 바이트 수
 * @param precision 소수점 아래 자릿수 (기본 1)
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
 * @brief 초(double) → 사람이 읽기 쉬운 시간 문자열.
 *
 * compact=false (기본): "1h 23m 45s" / compact=true: "1:23:45"
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
 * @brief 초(double) → HH:MM:SS 포맷 문자열.
 *
 * 포맷 토큰: %H(시) %M(분) %S(초) %Z(10ms 단위 2자리)
 *
 * 예) sec2str(75.5)             → "01:15"
 *     sec2str(3661, "%H:%M:%S") → "01:01:01"
 *     sec2str(1.23, "%M:%S.%Z") → "00:01.23"
 */
inline std::string sec2str(double seconds, std::string_view format = "%M:%S") {
    long long total_ms = static_cast<long long>(seconds * 1000.0 + 0.5);
    long long h  = total_ms / 3600000; total_ms %= 3600000;
    long long m  = total_ms / 60000;   total_ms %= 60000;
    long long s  = total_ms / 1000;
    long long ms = total_ms % 1000;

    const bool show_hour = (format.find("%H") != std::string_view::npos);
    const bool show_ms   = (format.find("%Z") != std::string_view::npos);
    if (!show_hour) { m += h * 60; h = 0; }

    char buf[64]; char* p = buf;
    auto write2 = [&](long long v) {
        if (v < 0) v = 0;
        if (v < 100) { *p++ = char('0' + v / 10); *p++ = char('0' + v % 10); }
        else p += std::snprintf(p, sizeof(buf) - static_cast<size_t>(p - buf), "%lld", v);
    };
    if (show_hour) { write2(h); *p++ = ':'; }
    write2(m); *p++ = ':'; write2(s);
    if (show_ms) { *p++ = '.'; write2(ms / 10); }
    return std::string(buf, static_cast<size_t>(p - buf));
}

/**
 * @brief chrono::nanoseconds → 사람이 읽기 쉬운 시간 문자열.
 *
 * 예: 2'500'000'000ns → "2.50s" | 150'000'000ns → "150.0ms" | 800ns → "800ns"
 */
inline std::string duration2str(std::chrono::nanoseconds ns) {
    using namespace std::chrono;
    auto s   = static_cast<long long>(duration_cast<seconds>(ns).count());
    auto ms  = static_cast<long long>(duration_cast<milliseconds>(ns).count() % 1000);
    auto us  = static_cast<long long>(duration_cast<microseconds>(ns).count() % 1000);
    auto ns_ = static_cast<long long>(ns.count() % 1000);
    char buf[64]{};
    if      (s >= 86400) { auto d = s/86400; s %= 86400;
                           std::snprintf(buf,sizeof(buf),"%lld %02lld:%02lld:%02lld",d,s/3600,(s/60)%60,s%60); }
    else if (s >= 3600)  { std::snprintf(buf,sizeof(buf),"%02lld:%02lld:%02lld",s/3600,(s/60)%60,s%60); }
    else if (s >= 60)    { std::snprintf(buf,sizeof(buf),"%lldm %02llds",s/60,s%60); }
    else if (s >= 10)    { std::snprintf(buf,sizeof(buf),"%llds",s); }
    else if (ms >= 1)    { std::snprintf(buf,sizeof(buf),"%lld.%03llds",s,ms); }
    else if (us >= 1)    { std::snprintf(buf,sizeof(buf),"%lld.%03lldms",s,us); }
    else                 { std::snprintf(buf,sizeof(buf),"%lldns",ns_); }
    return buf;
}

/**
 * @brief system_clock::time_point → 날짜/시간 문자열.
 *
 * 로컬 시간 기준. 기본 포맷: "%Y-%m-%d %H:%M:%S"
 */
inline std::string timestamp2str(std::chrono::system_clock::time_point tp,
                                  std::string_view fmt = "%Y-%m-%d %H:%M:%S") {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    char buf[64] = {};
    std::strftime(buf, sizeof(buf), fmt.data(), std::localtime(&tt));
    return buf;
}

} // namespace strutil

// ──────────────────────────────────────────────
// 하위 호환 shim — 기존 코드에서 str:: 를 사용하는 경우
// ──────────────────────────────────────────────
namespace str = strutil;
