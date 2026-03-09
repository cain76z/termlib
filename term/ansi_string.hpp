#pragma once
/**
 * ansi_string.hpp — Color · Attr · AnsiString  (TDD §4.1 §4.2 §6)
 *
 * 의존 방향 (단방향):
 *   platform.hpp ← ansi_string.hpp ← ansi_screen.hpp ← ...
 *
 * Color · Attr 를 이 파일에 정의한다.
 * ansi_screen.hpp 는 이 파일을 include 하므로 역방향 include 가 없어져
 * 순환 의존이 완전히 제거된다.
 *
 * 구현부는 모두 클래스·구조체 본체 안에 위치한다.
 */
#include "platform.hpp"
#include <cctype>
#include <cstdint>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  Color  (TDD §4.1)
// ═══════════════════════════════════════════════════════════════════════════

struct Color {
    enum class Type : uint8_t { Default = 0, Index256, TrueColor };

    Type    type  = Type::Default;
    uint8_t r = 0, g = 0, b = 0;
    uint8_t index = 0;

    // ── constexpr 팩토리 ──────────────────────────────────────────────────
    [[nodiscard]] static constexpr Color default_color() noexcept { return Color{}; }

    [[nodiscard]] static constexpr Color from_index(uint8_t idx) noexcept {
        Color c; c.type = Type::Index256; c.index = idx; return c;
    }

    [[nodiscard]] static constexpr Color from_rgb(uint8_t r_,
                                                   uint8_t g_,
                                                   uint8_t b_) noexcept {
        Color c; c.type = Type::TrueColor; c.r = r_; c.g = g_; c.b = b_; return c;
    }

    // ── 런타임 파싱 팩토리 ────────────────────────────────────────────────
    [[nodiscard]] static Color from_hex(std::string_view hex) noexcept {
        if (!hex.empty() && hex[0] == '#') hex.remove_prefix(1);
        auto hd = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            return 0;
        };
        if (hex.size() == 3)
            return from_rgb(static_cast<uint8_t>(hd(hex[0]) * 17u),
                            static_cast<uint8_t>(hd(hex[1]) * 17u),
                            static_cast<uint8_t>(hd(hex[2]) * 17u));
        if (hex.size() >= 6)
            return from_rgb(static_cast<uint8_t>((hd(hex[0])<<4)|hd(hex[1])),
                            static_cast<uint8_t>((hd(hex[2])<<4)|hd(hex[3])),
                            static_cast<uint8_t>((hd(hex[4])<<4)|hd(hex[5])));
        return default_color();
    }

    [[nodiscard]] static Color from_rgb_str(std::string_view s) noexcept {
        if (s.substr(0, 4) != "rgb(") return default_color();
        s.remove_prefix(4);
        if (!s.empty() && s.back() == ')') s.remove_suffix(1);
        auto p1 = s.find(',');
        if (p1 == std::string_view::npos) return default_color();
        auto p2 = s.find(',', p1 + 1);
        if (p2 == std::string_view::npos) return default_color();
        auto to_u8 = [](std::string_view t) -> uint8_t {
            int v = 0;
            for (char c : t) if (c >= '0' && c <= '9') v = v * 10 + (c - '0');
            return static_cast<uint8_t>(v);
        };
        return from_rgb(to_u8(s.substr(0, p1)),
                        to_u8(s.substr(p1+1, p2-p1-1)),
                        to_u8(s.substr(p2+1)));
    }

    [[nodiscard]] static Color from_name(std::string_view name) noexcept {
        struct Entry { const char* name; uint8_t idx; };
        static constexpr Entry kTable[] = {
            {"black",           0}, {"red",           1}, {"green",         2},
            {"yellow",          3}, {"blue",          4}, {"magenta",        5},
            {"cyan",            6}, {"white",         7},
            {"bright_black",    8}, {"gray",          8}, {"grey",           8},
            {"bright_red",      9}, {"bright_green", 10}, {"bright_yellow", 11},
            {"bright_blue",    12}, {"bright_magenta",13},{"bright_cyan",   14},
            {"bright_white",   15},
            {"dark_red",        1}, {"dark_green",    2}, {"dark_blue",      4},
            {"orange",        208}, {"purple",        5}, {"pink",          13},
        };
        for (const auto& e : kTable) if (name == e.name) return from_index(e.idx);
        return default_color();
    }

    [[nodiscard]] static Color parse(std::string_view s) noexcept {
        if (s.empty()) return default_color();
        if (s[0] == '#') return from_hex(s);
        if (s.size() >= 4 && s.substr(0, 4) == "rgb(") return from_rgb_str(s);
        bool all_digit = true;
        for (char c : s)
            if (!std::isdigit(static_cast<unsigned char>(c))) { all_digit=false; break; }
        if (all_digit && !s.empty()) {
            int v = 0;
            for (char c : s) v = v * 10 + (c - '0');
            return from_index(static_cast<uint8_t>(v));
        }
        return from_name(s);
    }

    bool operator==(const Color&) const noexcept = default;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Attr  (TDD §4.2)
// ═══════════════════════════════════════════════════════════════════════════

struct Attr {
    static constexpr uint16_t NONE      = 0;
    static constexpr uint16_t BOLD      = 1 << 0;
    static constexpr uint16_t DIM       = 1 << 1;
    static constexpr uint16_t ITALIC    = 1 << 2;
    static constexpr uint16_t UNDERLINE = 1 << 3;
    static constexpr uint16_t BLINK     = 1 << 4;
    static constexpr uint16_t REVERSE   = 1 << 5;
    static constexpr uint16_t STRIKE    = 1 << 6;
    static constexpr uint16_t OVERLINE  = 1 << 7;
    static constexpr uint16_t FAINT     = DIM;

    struct SGRPair { int on; int off; };
    static constexpr SGRPair kSGR[8] = {
        {  1, 22 }, // BOLD
        {  2, 22 }, // DIM
        {  3, 23 }, // ITALIC
        {  4, 24 }, // UNDERLINE
        {  5, 25 }, // BLINK
        {  7, 27 }, // REVERSE
        {  9, 29 }, // STRIKE
        { 53, 55 }, // OVERLINE
    };
};

// ═══════════════════════════════════════════════════════════════════════════
//  AnsiString  (TDD §6)
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class AnsiString {
public:
    // ── 세그먼트 (AnsiScreen::put 내부용) ────────────────────────────────
    struct Segment {
        enum class Type { Move, Text };
        Type        type = Type::Text;
        int         x = -1, y = -1;   ///< Move 전용
        std::string text;              ///< Text 전용
        Color       fg, bg;
        uint16_t    attr = 0;
    };

    AnsiString()  = default;
    ~AnsiString() = default;
    AnsiString(const AnsiString&)            = default;
    AnsiString& operator=(const AnsiString&) = default;
    AnsiString(AnsiString&&)                 = default;
    AnsiString& operator=(AnsiString&&)      = default;

    // ── 커서 이동 ─────────────────────────────────────────────────────────
    AnsiString& xy(int x, int y) {
        raw_ += "\x1b[";
        append_int(y + 1);   // ANSI 1-based
        raw_ += ';';
        append_int(x + 1);
        raw_ += 'H';
        dest_x_ = x; dest_y_ = y;
        Segment seg;
        seg.type = Segment::Type::Move;
        seg.x = x; seg.y = y;
        segs_.push_back(seg);
        return *this;
    }
    AnsiString& move_to(int x, int y) { return xy(x, y); }

    // ── 색상 ──────────────────────────────────────────────────────────────
    AnsiString& fg(std::string_view s) {
        cur_fg_ = Color::parse(s); append_sgr_fg(cur_fg_); return *this;
    }
    AnsiString& fg(uint8_t r, uint8_t g, uint8_t b) {
        cur_fg_ = Color::from_rgb(r,g,b); append_sgr_fg(cur_fg_); return *this;
    }
    AnsiString& fg(uint8_t idx) {
        cur_fg_ = Color::from_index(idx); append_sgr_fg(cur_fg_); return *this;
    }
    AnsiString& fg(const Color& c) {
        cur_fg_ = c; append_sgr_fg(cur_fg_); return *this;
    }
    AnsiString& bg(std::string_view s) {
        cur_bg_ = Color::parse(s); append_sgr_bg(cur_bg_); return *this;
    }
    AnsiString& bg(uint8_t r, uint8_t g_, uint8_t b) {
        cur_bg_ = Color::from_rgb(r,g_,b); append_sgr_bg(cur_bg_); return *this;
    }
    AnsiString& bg(uint8_t idx) {
        cur_bg_ = Color::from_index(idx); append_sgr_bg(cur_bg_); return *this;
    }
    AnsiString& bg(const Color& c) {
        cur_bg_ = c; append_sgr_bg(cur_bg_); return *this;
    }

    // ── 속성 ──────────────────────────────────────────────────────────────
    AnsiString& bold(bool on = true) {
        if (on) { cur_attr_ |=  Attr::BOLD;      raw_ += "\x1b[1m";  }
        else    { cur_attr_ &= ~Attr::BOLD;      raw_ += "\x1b[22m"; }
        return *this;
    }
    AnsiString& dim(bool on = true) {
        if (on) { cur_attr_ |=  Attr::DIM;       raw_ += "\x1b[2m";  }
        else    { cur_attr_ &= ~Attr::DIM;       raw_ += "\x1b[22m"; }
        return *this;
    }
    AnsiString& italic(bool on = true) {
        if (on) { cur_attr_ |=  Attr::ITALIC;    raw_ += "\x1b[3m";  }
        else    { cur_attr_ &= ~Attr::ITALIC;    raw_ += "\x1b[23m"; }
        return *this;
    }
    AnsiString& underline(bool on = true) {
        if (on) { cur_attr_ |=  Attr::UNDERLINE; raw_ += "\x1b[4m";  }
        else    { cur_attr_ &= ~Attr::UNDERLINE; raw_ += "\x1b[24m"; }
        return *this;
    }
    AnsiString& blink(bool on = true) {
        if (on) { cur_attr_ |=  Attr::BLINK;     raw_ += "\x1b[5m";  }
        else    { cur_attr_ &= ~Attr::BLINK;     raw_ += "\x1b[25m"; }
        return *this;
    }
    AnsiString& reverse(bool on = true) {
        if (on) { cur_attr_ |=  Attr::REVERSE;   raw_ += "\x1b[7m";  }
        else    { cur_attr_ &= ~Attr::REVERSE;   raw_ += "\x1b[27m"; }
        return *this;
    }
    AnsiString& strikethrough(bool on = true) {
        if (on) { cur_attr_ |=  Attr::STRIKE;    raw_ += "\x1b[9m";  }
        else    { cur_attr_ &= ~Attr::STRIKE;    raw_ += "\x1b[29m"; }
        return *this;
    }
    AnsiString& overline(bool on = true) {
        if (on) { cur_attr_ |=  Attr::OVERLINE;  raw_ += "\x1b[53m"; }
        else    { cur_attr_ &= ~Attr::OVERLINE;  raw_ += "\x1b[55m"; }
        return *this;
    }

    // ── 텍스트 ────────────────────────────────────────────────────────────
    AnsiString& text(std::string_view utf8) {
        raw_.append(utf8.data(), utf8.size());
        flush_text_segment(std::string(utf8));
        return *this;
    }
    AnsiString& operator<<(std::string_view utf8) { return text(utf8); }

    // ── 제어 시퀀스 ───────────────────────────────────────────────────────
    AnsiString& reset() {
        raw_      += "\x1b[0m";
        cur_fg_    = Color::default_color();
        cur_bg_    = Color::default_color();
        cur_attr_  = 0;
        return *this;
    }
    AnsiString& clear_line()      { raw_ += "\x1b[2K";   return *this; }
    AnsiString& clear_to_eol()    { raw_ += "\x1b[K";    return *this; }
    AnsiString& clear_screen()    { raw_ += "\x1b[2J";   return *this; }
    AnsiString& save_cursor()     { raw_ += "\x1b[s";    return *this; }
    AnsiString& restore_cursor()  { raw_ += "\x1b[u";    return *this; }
    AnsiString& show_cursor(bool v = true) {
        raw_ += v ? "\x1b[?25h" : "\x1b[?25l"; return *this;
    }
    AnsiString& newline()         { raw_ += "\r\n";      return *this; }

    // ── 조회 ──────────────────────────────────────────────────────────────
    [[nodiscard]] const std::string&          str()      const noexcept { return raw_; }
    [[nodiscard]] const std::vector<Segment>& segments() const noexcept { return segs_; }
    [[nodiscard]] int dest_x() const noexcept { return dest_x_; }
    [[nodiscard]] int dest_y() const noexcept { return dest_y_; }

    operator std::string_view() const noexcept { return raw_; }

    friend std::ostream& operator<<(std::ostream& os, const AnsiString& a) {
        return os << a.raw_;
    }

    void clear_all() {
        raw_.clear(); segs_.clear();
        dest_x_ = dest_y_ = -1;
        cur_fg_ = Color::default_color();
        cur_bg_ = Color::default_color();
        cur_attr_ = 0;
    }

private:
    std::string          raw_;
    std::vector<Segment> segs_;
    int                  dest_x_   = -1;
    int                  dest_y_   = -1;
    Color                cur_fg_;
    Color                cur_bg_;
    uint16_t             cur_attr_ = 0;

    // ── private 헬퍼 ──────────────────────────────────────────────────────
    void append_int(int v) { raw_ += std::to_string(v); }

    void append_sgr_fg(const Color& c) {
        switch (c.type) {
        case Color::Type::Default:
            raw_ += "\x1b[39m";
            break;
        case Color::Type::Index256:
            if      (c.index <  8) { raw_ += "\x1b[3"; raw_ += char('0'+c.index);   raw_ += 'm'; }
            else if (c.index < 16) { raw_ += "\x1b[9"; raw_ += char('0'+c.index-8); raw_ += 'm'; }
            else                   { raw_ += "\x1b[38;5;"; append_int(c.index); raw_ += 'm'; }
            break;
        case Color::Type::TrueColor:
            raw_ += "\x1b[38;2;";
            append_int(c.r); raw_ += ';';
            append_int(c.g); raw_ += ';';
            append_int(c.b); raw_ += 'm';
            break;
        }
    }

    void append_sgr_bg(const Color& c) {
        switch (c.type) {
        case Color::Type::Default:
            raw_ += "\x1b[49m";
            break;
        case Color::Type::Index256:
            if      (c.index <  8) { raw_ += "\x1b[4";  raw_ += char('0'+c.index);   raw_ += 'm'; }
            else if (c.index < 16) { raw_ += "\x1b[10"; append_int(c.index-8);        raw_ += 'm'; }
            else                   { raw_ += "\x1b[48;5;"; append_int(c.index); raw_ += 'm'; }
            break;
        case Color::Type::TrueColor:
            raw_ += "\x1b[48;2;";
            append_int(c.r); raw_ += ';';
            append_int(c.g); raw_ += ';';
            append_int(c.b); raw_ += 'm';
            break;
        }
    }

    void flush_text_segment(const std::string& t) {
        if (t.empty()) return;
        Segment seg;
        seg.type = Segment::Type::Text;
        seg.text = t;
        seg.fg   = cur_fg_;
        seg.bg   = cur_bg_;
        seg.attr = cur_attr_;
        segs_.push_back(std::move(seg));
    }
};

} // namespace term
