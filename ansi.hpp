/*
 * ansi.hpp — ANSI 터미널 컨트롤 라이브러리 (C++17, 단일 헤더)
 *
 * 사용 예시:
 *   // 버퍼 방식 (권장) — 인스턴스마다 독립된 버퍼·상태
 *   ansi::buffer buf;
 *   buf.xy(x,y).fg("#808080").bg("black").style("underline").text("hello").reset();
 *   buf.xy(x,y).fg("#d0d0d0").text("world").reset();
 *   buf.flush();
 *
 *   // 여러 버퍼를 독립적으로 사용 가능
 *   ansi::buffer header, body;
 *   header.fg("bright_cyan").text("=== TITLE ===").reset();
 *   body.fg("#d0d0d0").text("content").reset();
 *   header.flush();
 *   body.flush();
 *
 *   // 스트리밍 방식 — 즉시 출력
 *   std::cout << ansi::fg("bright_red") << "텍스트\n" << ansi::reset();
 *   std::cout << ansi::fg(196)          << "256색\n"  << ansi::reset();
 *   std::cout << ansi::fg(255, 0, 100)  << "RGB\n"    << ansi::reset();
 *   std::cout << ansi::fg("#ff0064")    << "HEX\n"    << ansi::reset();
 *   std::cout << ansi::style("bold")    << "굵게\n"   << ansi::reset();
 *
 * API (buffer):
 *   xy(x,y)          커서 이동
 *   fg(color)        전경색 — 이름/"#hex"/"rgb(r,g,b)"/256색 인덱스/r,g,b
 *   bg(color)        배경색 — 동일
 *   style(name)      스타일 (bold/faint/italic/underline/blink/reverse/strike)
 *   text(str)        텍스트 추가
 *   reset()          색·스타일 초기화 시퀀스 추가 + 내부 상태 리셋
 *   clear()          버퍼·상태 모두 초기화 (출력 없음)
 *   strip()          버퍼에서 ANSI 시퀀스 제거 (plain text만 남김)
 *   flush()          stdout으로 출력 후 버퍼 비움 (상태는 유지)
 *   str()            현재 버퍼 내용 반환
 */

#ifndef ANSI_HPP
#define ANSI_HPP

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ansi {

// =========================================================
// 1. 색상 상수 (constexpr, 직접 스트리밍용)
// =========================================================

namespace color {
    namespace fg {
        constexpr const char* black          = "\033[30m";
        constexpr const char* red            = "\033[31m";
        constexpr const char* green          = "\033[32m";
        constexpr const char* yellow         = "\033[33m";
        constexpr const char* blue           = "\033[34m";
        constexpr const char* magenta        = "\033[35m";
        constexpr const char* cyan           = "\033[36m";
        constexpr const char* white          = "\033[37m";
        constexpr const char* bright_black   = "\033[90m";
        constexpr const char* bright_red     = "\033[91m";
        constexpr const char* bright_green   = "\033[92m";
        constexpr const char* bright_yellow  = "\033[93m";
        constexpr const char* bright_blue    = "\033[94m";
        constexpr const char* bright_magenta = "\033[95m";
        constexpr const char* bright_cyan    = "\033[96m";
        constexpr const char* bright_white   = "\033[97m";
    }
    namespace bg {
        constexpr const char* black          = "\033[40m";
        constexpr const char* red            = "\033[41m";
        constexpr const char* green          = "\033[42m";
        constexpr const char* yellow         = "\033[43m";
        constexpr const char* blue           = "\033[44m";
        constexpr const char* magenta        = "\033[45m";
        constexpr const char* cyan           = "\033[46m";
        constexpr const char* white          = "\033[47m";
        constexpr const char* bright_black   = "\033[100m";
        constexpr const char* bright_red     = "\033[101m";
        constexpr const char* bright_green   = "\033[102m";
        constexpr const char* bright_yellow  = "\033[103m";
        constexpr const char* bright_blue    = "\033[104m";
        constexpr const char* bright_magenta = "\033[105m";
        constexpr const char* bright_cyan    = "\033[106m";
        constexpr const char* bright_white   = "\033[107m";
    }
}

namespace style_code {
    constexpr const char* bold      = "\033[1m";
    constexpr const char* faint     = "\033[2m";
    constexpr const char* italic    = "\033[3m";
    constexpr const char* underline = "\033[4m";
    constexpr const char* blink     = "\033[5m";
    constexpr const char* reverse   = "\033[7m";
    constexpr const char* strike    = "\033[9m";
}

// =========================================================
// 2. 내부 구현 (detail)
// =========================================================

namespace detail {

/// 스타일 이름 → SGR 코드 (없으면 -1)
inline int style_code(std::string_view name_raw) {
    // ASCII 소문자 변환 (ansi.hpp는 str.hpp 비의존 설계)
    std::string name(name_raw);
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    static const std::unordered_map<std::string, int> TABLE = {
        {"bold",9999}, // 실제값으로 아래 덮어씀
        // 순서 보장을 위해 직접 매핑
    };
    // 작은 맵이라 선형 탐색이 충분히 빠름
    if (name == "bold")      return 1;
    if (name == "faint")     return 2;
    if (name == "italic")    return 3;
    if (name == "underline") return 4;
    if (name == "blink")     return 5;
    if (name == "reverse")   return 7;
    if (name == "strike")    return 9;
    return -1;
}

/// trim (의존성 없는 독립 구현)
inline std::string_view trim(std::string_view s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string_view::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

/// "black"/"bright_red"/256/"#rrggbb"/"rgb(r,g,b)" → SGR 파라미터 문자열
/// is_fg=true → 38계열, false → 48계열
/// 반환값 예) "31", "38;5;196", "38;2;255;0;100"
inline std::optional<std::string> parse_color(std::string_view spec_raw,
                                               bool is_fg)
{
    const auto spec_sv = trim(spec_raw);
    if (spec_sv.empty()) return std::nullopt;

    std::string spec(spec_sv);

    // 소문자 복사본
    std::string lower = spec;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c){ return std::tolower(c); });

    // ── 기본 색상 이름 ("black" / "bright_red" 등) ──────────
    static const std::unordered_map<std::string, int> BASE_NAMES = {
        {"black",0},{"red",1},{"green",2},{"yellow",3},
        {"blue",4},{"magenta",5},{"cyan",6},{"white",7}
    };

    std::string name = lower;
    bool bright = false;
    if (name.size() > 7 && name.compare(0, 7, "bright_") == 0) {
        bright = true;
        name   = name.substr(7);
    }
    auto it = BASE_NAMES.find(name);
    if (it != BASE_NAMES.end()) {
        int code = (is_fg ? 30 : 40) + it->second + (bright ? 60 : 0);
        return std::to_string(code);
    }

    // ── 256색 인덱스 (순수 정수 문자열) ─────────────────────
    {
        size_t pos = 0;
        bool valid = !spec.empty();
        for (char c : spec) if (!std::isdigit((unsigned char)c)) { valid = false; break; }
        if (valid && !spec.empty()) {
            int idx = std::stoi(spec, &pos);
            if (pos == spec.size() && idx >= 0 && idx <= 255)
                return (is_fg ? "38;5;" : "48;5;") + std::to_string(idx);
        }
    }

    // ── #hex 형식 ("#rgb" 또는 "#rrggbb") ───────────────────
    if (!spec.empty() && spec[0] == '#') {
        std::string hex = spec.substr(1);
        if (hex.size() == 3)
            hex = {hex[0],hex[0],hex[1],hex[1],hex[2],hex[2]};
        if (hex.size() == 6) {
            try {
                int r = std::stoi(hex.substr(0,2), nullptr, 16);
                int g = std::stoi(hex.substr(2,2), nullptr, 16);
                int b = std::stoi(hex.substr(4,2), nullptr, 16);
                return (is_fg ? "38;2;" : "48;2;")
                     + std::to_string(r) + ";"
                     + std::to_string(g) + ";"
                     + std::to_string(b);
            } catch (...) {}
        }
    }

    // ── rgb(r,g,b) 형식 ─────────────────────────────────────
    if (lower.size() > 5 &&
        lower.compare(0, 4, "rgb(") == 0 &&
        spec.back() == ')')
    {
        const auto inside = spec.substr(4, spec.size() - 5);
        std::vector<int> vals;
        std::string token;

        for (char c : inside + ",") {        // 끝에 쉼표 추가로 마지막 토큰 처리
            if (c == ',') {
                // token trim
                auto b = token.find_first_not_of(" \t");
                auto e = token.find_last_not_of(" \t");
                if (b != std::string::npos) {
                    try { vals.push_back(std::stoi(token.substr(b, e-b+1))); }
                    catch (...) { return std::nullopt; }
                }
                token.clear();
            } else {
                token += c;
            }
        }

        if (vals.size() == 3) {
            for (int v : vals)
                if (v < 0 || v > 255) return std::nullopt;
            return (is_fg ? "38;2;" : "48;2;")
                 + std::to_string(vals[0]) + ";"
                 + std::to_string(vals[1]) + ";"
                 + std::to_string(vals[2]);
        }
    }

    return std::nullopt;
}

/// RGB 값 → SGR 파라미터 (범위 검사 포함)
inline std::optional<std::string> rgb_code(int r, int g, int b, bool is_fg) {
    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255)
        return std::nullopt;
    return (is_fg ? "38;2;" : "48;2;")
         + std::to_string(r) + ";"
         + std::to_string(g) + ";"
         + std::to_string(b);
}

/// 256색 인덱스 → SGR 파라미터
inline std::optional<std::string> idx_code(int index, bool is_fg) {
    if (index < 0 || index > 255) return std::nullopt;
    return (is_fg ? "38;5;" : "48;5;") + std::to_string(index);
}

/// ANSI CSI 시퀀스를 문자열에서 제거하여 순수 텍스트만 남김
inline std::string strip_ansi(const std::string& buf) {
    std::string out;
    out.reserve(buf.size());
    size_t i = 0;
    while (i < buf.size()) {
        if (buf[i] == '\033' && i + 1 < buf.size() && buf[i+1] == '[') {
            size_t j = i + 2;
            // Parameter / Intermediate bytes: 0x20–0x3F
            while (j < buf.size() && buf[j] >= 0x20 && buf[j] <= 0x3F) ++j;
            // Final byte: 0x40–0x7E
            if (j < buf.size() && buf[j] >= 0x40 && buf[j] <= 0x7E) {
                i = j + 1;
                continue;
            }
        }
        out += buf[i++];
    }
    return out;
}

} // namespace detail

// =========================================================
// 3. 스트리밍용 free function (즉시 string 반환)
// =========================================================

/// SGR 리셋
inline std::string reset() { return "\033[0m"; }

/// 커서 이동 (0-based)
inline std::string xy(int x, int y) {
    return "\033[" + std::to_string(y+1) + ";" + std::to_string(x+1) + "H";
}

inline std::string cursor_up     (int n=1) { return "\033["+std::to_string(n)+"A"; }
inline std::string cursor_down   (int n=1) { return "\033["+std::to_string(n)+"B"; }
inline std::string cursor_forward(int n=1) { return "\033["+std::to_string(n)+"C"; }
inline std::string cursor_back   (int n=1) { return "\033["+std::to_string(n)+"D"; }
inline std::string clear_screen  ()        { return "\033[2J"; }
inline std::string clear_line    ()        { return "\033[2K"; }
inline std::string hide_cursor   ()        { return "\033[?25l"; }
inline std::string show_cursor   ()        { return "\033[?25h"; }
inline std::string save_cursor   ()        { return "\033[s"; }
inline std::string restore_cursor()        { return "\033[u"; }

/// 스타일 이름 → ESC 시퀀스
inline std::string style(std::string_view name) {
    int code = detail::style_code(name);
    return code >= 0 ? "\033[" + std::to_string(code) + "m" : "";
}

/// 전경색 — 이름/hex/rgb 문자열
inline std::string fg(std::string_view spec) {
    auto c = detail::parse_color(spec, true);
    return c ? "\033[" + *c + "m" : "";
}
/// 전경색 — 256색 인덱스
inline std::string fg(int index) {
    auto c = detail::idx_code(index, true);
    return c ? "\033[" + *c + "m" : "";
}
/// 전경색 — True Color RGB
inline std::string fg(int r, int g, int b) {
    auto c = detail::rgb_code(r, g, b, true);
    return c ? "\033[" + *c + "m" : "";
}

/// 배경색 — 이름/hex/rgb 문자열
inline std::string bg(std::string_view spec) {
    auto c = detail::parse_color(spec, false);
    return c ? "\033[" + *c + "m" : "";
}
/// 배경색 — 256색 인덱스
inline std::string bg(int index) {
    auto c = detail::idx_code(index, false);
    return c ? "\033[" + *c + "m" : "";
}
/// 배경색 — True Color RGB
inline std::string bg(int r, int g, int b) {
    auto c = detail::rgb_code(r, g, b, false);
    return c ? "\033[" + *c + "m" : "";
}

// =========================================================
// 4. buffer — 인스턴스별 독립 버퍼 (핵심 개선)
// =========================================================

/**
 * buffer — 각 인스턴스가 자신만의 버퍼·색상·스타일 상태를 보유.
 *
 * 이전 buffer_proxy 와 달리 thread_local 전역 상태를 사용하지 않으므로
 * 여러 buffer를 동시에 생성·조작해도 서로 간섭하지 않음.
 *
 *   ansi::buffer a, b;   // a와 b는 완전히 독립
 *   a.fg("red").text("A");
 *   b.fg("blue").text("B");
 *   a.flush();  // "A"만 빨간색으로 출력
 *   b.flush();  // "B"만 파란색으로 출력
 */
class buffer {
public:
    buffer() = default;
    buffer(buffer&&) = default;
    buffer& operator=(buffer&&) = default;

    // 복사 허용 (스냅샷 목적)
    buffer(const buffer&) = default;
    buffer& operator=(const buffer&) = default;

    // ── 커서 ──────────────────────────────────────────────
    buffer& xy(int x, int y) {
        buf_ += ansi::xy(x, y);
        return *this;
    }
    buffer& cursor_up     (int n=1) { buf_ += ansi::cursor_up(n);      return *this; }
    buffer& cursor_down   (int n=1) { buf_ += ansi::cursor_down(n);    return *this; }
    buffer& cursor_forward(int n=1) { buf_ += ansi::cursor_forward(n); return *this; }
    buffer& cursor_back   (int n=1) { buf_ += ansi::cursor_back(n);    return *this; }
    buffer& clear_screen  ()        { buf_ += ansi::clear_screen();    return *this; }
    buffer& clear_line    ()        { buf_ += ansi::clear_line();       return *this; }
    buffer& hide_cursor   ()        { buf_ += ansi::hide_cursor();     return *this; }
    buffer& show_cursor   ()        { buf_ += ansi::show_cursor();     return *this; }
    buffer& save_cursor   ()        { buf_ += ansi::save_cursor();     return *this; }
    buffer& restore_cursor()        { buf_ += ansi::restore_cursor();  return *this; }

    // ── 전경색 ────────────────────────────────────────────
    buffer& fg(std::string_view spec) {
        return apply_color(detail::parse_color(spec, true), cur_fg_);
    }
    buffer& fg(int index) {
        return apply_color(detail::idx_code(index, true), cur_fg_);
    }
    buffer& fg(int r, int g, int b) {
        return apply_color(detail::rgb_code(r, g, b, true), cur_fg_);
    }

    // ── 배경색 ────────────────────────────────────────────
    buffer& bg(std::string_view spec) {
        return apply_color(detail::parse_color(spec, false), cur_bg_);
    }
    buffer& bg(int index) {
        return apply_color(detail::idx_code(index, false), cur_bg_);
    }
    buffer& bg(int r, int g, int b) {
        return apply_color(detail::rgb_code(r, g, b, false), cur_bg_);
    }

    // ── 스타일 ────────────────────────────────────────────
    buffer& style(std::string_view name) {
        int code = detail::style_code(name);
        if (code >= 0 && cur_styles_.insert(code).second)  // 중복 방지
            buf_ += "\033[" + std::to_string(code) + "m";
        return *this;
    }

    // ── 텍스트 ────────────────────────────────────────────
    buffer& text(std::string_view t) {
        buf_ += t;
        return *this;
    }

    buffer& operator<<(std::string_view t) { return text(t); }

    // ── 상태 제어 ─────────────────────────────────────────

    /// SGR 리셋 시퀀스 추가 + 내부 상태 초기화
    buffer& reset() {
        buf_ += "\033[0m";
        cur_fg_.reset();
        cur_bg_.reset();
        cur_styles_.clear();
        return *this;
    }

    /// 버퍼·상태 모두 비움 (출력 없음)
    buffer& clear() {
        buf_.clear();
        cur_fg_.reset();
        cur_bg_.reset();
        cur_styles_.clear();
        return *this;
    }

    /// 버퍼에서 ANSI 시퀀스를 제거 — plain text만 남김
    buffer& strip() {
        buf_ = detail::strip_ansi(buf_);
        return *this;
    }

    /// stdout으로 출력 후 버퍼 비움.
    /// 터미널 색상 상태는 유지되므로 내부 cur_fg_/cur_bg_도 유지.
    buffer& flush(std::ostream& os = std::cout) {
        os << buf_ << std::flush;
        buf_.clear();
        return *this;
    }

    /// 현재 버퍼 내용 반환 (복사)
    std::string str() const { return buf_; }

    /// 버퍼가 비어있는지 확인
    bool empty() const { return buf_.empty(); }

    /// 버퍼 내용 길이 (바이트)
    std::size_t size() const { return buf_.size(); }

private:
    std::string              buf_;
    std::optional<std::string> cur_fg_;      // 현재 적용된 fg SGR 파라미터
    std::optional<std::string> cur_bg_;      // 현재 적용된 bg SGR 파라미터
    std::set<int>            cur_styles_;    // 현재 적용된 스타일 코드 집합

    /// 색상 코드가 이전과 다를 때만 시퀀스 추가 (중복 억제)
    buffer& apply_color(std::optional<std::string> code,
                        std::optional<std::string>& current)
    {
        if (code && code != current) {
            buf_    += "\033[" + *code + "m";
            current  = code;
        }
        return *this;
    }
};

} // namespace ansi

#endif // ANSI_HPP