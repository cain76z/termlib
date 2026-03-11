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

/**
 * @brief 터미널 색상을 나타내는 구조체입니다.
 *
 * 기본(Default), 256색 인덱스(Index256), 24비트 트루컬러(TrueColor)를 지원합니다.
 */
struct Color {
    /**
     * @brief 색상의 종류를 구분하는 열거형입니다.
     */
    enum class Type : uint8_t { Default = 0, Index256, TrueColor };

    Type    type  = Type::Default; ///< 색상 타입
    uint8_t r = 0, g = 0, b = 0;   ///< TrueColor일 경우 RGB 값 (0~255)
    uint8_t index = 0;             ///< Index256일 경우 색상 테이블 인덱스 (0~255)

    // ── constexpr 팩토리 ──────────────────────────────────────────────────

    /**
     * @brief 터미널 기본 색상(Default)을 생성합니다.
     * @return 기본 색상으로 설정된 Color 객체
     */
    [[nodiscard]] static constexpr Color default_color() noexcept { return Color{}; }

    /**
     * @brief 256색 팔레트 인덱스를 사용하여 색상을 생성합니다.
     * @param idx 0~255 사이의 색상 인덱스
     * @return 인덱스 색상으로 설정된 Color 객체
     */
    [[nodiscard]] static constexpr Color from_index(uint8_t idx) noexcept {
        Color c; c.type = Type::Index256; c.index = idx; return c;
    }

    /**
     * @brief RGB 값을 사용하여 트루컬러 색상을 생성합니다.
     * @param r_ 빨강 채널 (0~255)
     * @param g_ 초록 채널 (0~255)
     * @param b_ 파랑 채널 (0~255)
     * @return 트루컬러로 설정된 Color 객체
     */
    [[nodiscard]] static constexpr Color from_rgb(uint8_t r_,
                                                   uint8_t g_,
                                                   uint8_t b_) noexcept {
        Color c; c.type = Type::TrueColor; c.r = r_; c.g = g_; c.b = b_; return c;
    }

    // ── 런타임 파싱 팩토리 ────────────────────────────────────────────────

    /**
     * @brief 16진수 문자열("#RRGGBB" 또는 "#RGB")을 파싱하여 Color 객체를 생성합니다.
     * @param hex "#RRGGBB", "#RGB" 형식의 문자열
     * @return 파싱된 Color 객체. 형식이 올바르지 않으면 기본 색상 반환
     */
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

    /**
     * @brief "rgb(r, g, b)" 형식의 문자열을 파싱하여 Color 객체를 생성합니다.
     * @param s "rgb(...)" 형식의 문자열
     * @return 파싱된 Color 객체. 형식이 올바르지 않으면 기본 색상 반환
     */
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

    /**
     * @brief 색상 이름(예: "red", "bright_green")으로 Color 객체를 생성합니다.
     * @param name 색상 이름 문자열
     * @return 대응하는 Color 객체. 이름이 없으면 기본 색상 반환
     */
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

    /**
     * @brief 문자열 형태의 색상 정보를 자동으로 파싱하여 Color 객체를 생성합니다.
     *
     * 지원 형식:
     * - "#RRGGBB", "#RGB" (16진수)
     * - "rgb(r, g, b)" (함수형)
     * - "0"~"255" (인덱스)
     * - "red", "blue" 등 (이름)
     *
     * @param s 색상 문자열
     * @return 파싱된 Color 객체
     */
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

    /**
     * @brief 두 Color 객체가 동등한지 비교합니다.
     */
    bool operator==(const Color&) const noexcept = default;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Attr  (TDD §4.2)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief 텍스트 속성(Bold, Italic 등)을 정의하는 구조체입니다.
 *
 * 비트 마스크 형태로 속성을 조합할 수 있습니다.
 */
struct Attr {
    static constexpr uint16_t NONE      = 0;    ///< 속성 없음
    static constexpr uint16_t BOLD      = 1 << 0; ///< 굵게
    static constexpr uint16_t DIM       = 1 << 1; ///< 흐리게(Dim/Faint)
    static constexpr uint16_t ITALIC    = 1 << 2; ///< 이탤릭체
    static constexpr uint16_t UNDERLINE = 1 << 3; ///< 밑줄
    static constexpr uint16_t BLINK     = 1 << 4; ///< 깜빡임
    static constexpr uint16_t REVERSE   = 1 << 5; ///< 색상 반전
    static constexpr uint16_t STRIKE    = 1 << 6; ///< 취소선
    static constexpr uint16_t OVERLINE  = 1 << 7; ///< 상단선
    static constexpr uint16_t FAINT     = DIM;    ///< DIM의 별칭

    /**
     * @brief SGR(Set Graphics Rendition) 제어 코드 쌍(ON, OFF)을 저장하는 구조체입니다.
     */
    struct SGRPair { int on; int off; };
    
    /**
     * @brief 각 속성 비트에 대응하는 SGR 코드 매핑 테이블입니다.
     *        인덱스는 비트 시프트 값(0~7)에 대응합니다.
     */
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

/**
 * @brief ANSI 이스케이프 시퀀스를 생성하고 관리하는 빌더 클래스입니다.
 *
 * 커서 이동, 색상 변경, 텍스트 출력 등의 명령을 체이닝 방식으로 조합하여
 * 최종 ANSI 문자열을 생성합니다.
 */
class AnsiString {
public:
    /**
     * @brief AnsiString 내부에서 사용하는 명령 단위(세그먼트)입니다.
     *
     * `AnsiScreen::put` 등에서 렌더링 정보를 전달할 때 사용됩니다.
     */
    struct Segment {
        /**
         * @brief 세그먼트의 타입입니다.
         */
        enum class Type { Move, Text };
        
        Type        type = Type::Text;   ///< 세그먼트 타입
        int         x = -1, y = -1;      ///< Move 전용: 이동할 좌표 (0-based)
        std::string text;                ///< Text 전용: 출력할 텍스트 내용
        Color       fg;                  ///< 전경색
        Color       bg;                  ///< 배경색
        uint16_t    attr = 0;            ///< 텍스트 속성 (Attr 플래그)
    };

    AnsiString()  = default;
    ~AnsiString() = default;
    AnsiString(const AnsiString&)            = default;
    AnsiString& operator=(const AnsiString&) = default;
    AnsiString(AnsiString&&)                 = default;
    AnsiString& operator=(AnsiString&&)      = default;

    // ── 커서 이동 ─────────────────────────────────────────────────────────

    /**
     * @brief 커서를 지정된 좌표 (x, y)로 이동시킵니다.
     *
     * 내부적으로 ANSI CUP(Cursor Position) 시퀀스를 추가합니다.
     *
     * @param x 0-based 열(Column) 인덱스
     * @param y 0-based 행(Row) 인덱스
     * @return 자기 자신 참조 (메서드 체이닝)
     */
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

    /**
     * @brief `xy(x, y)`의 별칭 함수입니다.
     * @see xy(int, int)
     */
    AnsiString& move_to(int x, int y) { return xy(x, y); }

    // ── 색상 ──────────────────────────────────────────────────────────────

    /**
     * @brief 전경색(Foreground)을 문자열로 설정합니다.
     *
     * Color::parse를 통해 자동 파싱됩니다.
     *
     * @param s 색상 문자열 (예: "#ff0000", "red", "rgb(...)")
     * @return 자기 자신 참조
     */
    AnsiString& fg(std::string_view s) {
        cur_fg_ = Color::parse(s); append_sgr_fg(cur_fg_); return *this;
    }

    /**
     * @brief 전경색을 RGB 값으로 설정합니다.
     * @param r 빨강 (0~255)
     * @param g 초록 (0~255)
     * @param b 파랑 (0~255)
     * @return 자기 자신 참조
     */
    AnsiString& fg(uint8_t r, uint8_t g, uint8_t b) {
        cur_fg_ = Color::from_rgb(r,g,b); append_sgr_fg(cur_fg_); return *this;
    }

    /**
     * @brief 전경색을 256색 인덱스로 설정합니다.
     * @param idx 색상 인덱스 (0~255)
     * @return 자기 자신 참조
     */
    AnsiString& fg(uint8_t idx) {
        cur_fg_ = Color::from_index(idx); append_sgr_fg(cur_fg_); return *this;
    }

    /**
     * @brief 전경색을 Color 객체로 설정합니다.
     * @param c Color 구조체
     * @return 자기 자신 참조
     */
    AnsiString& fg(const Color& c) {
        cur_fg_ = c; append_sgr_fg(cur_fg_); return *this;
    }

    /**
     * @brief 배경색(Background)을 문자열로 설정합니다.
     * @param s 색상 문자열
     * @return 자기 자신 참조
     */
    AnsiString& bg(std::string_view s) {
        cur_bg_ = Color::parse(s); append_sgr_bg(cur_bg_); return *this;
    }

    /**
     * @brief 배경색을 RGB 값으로 설정합니다.
     * @param r 빨강 (0~255)
     * @param g_ 초록 (0~255)
     * @param b 파랑 (0~255)
     * @return 자기 자신 참조
     */
    AnsiString& bg(uint8_t r, uint8_t g_, uint8_t b) {
        cur_bg_ = Color::from_rgb(r,g_,b); append_sgr_bg(cur_bg_); return *this;
    }

    /**
     * @brief 배경색을 256색 인덱스로 설정합니다.
     * @param idx 색상 인덱스 (0~255)
     * @return 자기 자신 참조
     */
    AnsiString& bg(uint8_t idx) {
        cur_bg_ = Color::from_index(idx); append_sgr_bg(cur_bg_); return *this;
    }

    /**
     * @brief 배경색을 Color 객체로 설정합니다.
     * @param c Color 구조체
     * @return 자기 자신 참조
     */
    AnsiString& bg(const Color& c) {
        cur_bg_ = c; append_sgr_bg(cur_bg_); return *this;
    }

    // ── 속성 ──────────────────────────────────────────────────────────────

    /**
     * @brief 굵게(Bold) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& bold(bool on = true) {
        if (on) { cur_attr_ |=  Attr::BOLD;      raw_ += "\x1b[1m";  }
        else    { cur_attr_ &= ~Attr::BOLD;      raw_ += "\x1b[22m"; }
        return *this;
    }

    /**
     * @brief 흐림(Dim) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& dim(bool on = true) {
        if (on) { cur_attr_ |=  Attr::DIM;       raw_ += "\x1b[2m";  }
        else    { cur_attr_ &= ~Attr::DIM;       raw_ += "\x1b[22m"; }
        return *this;
    }

    /**
     * @brief 이탤릭(Italic) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& italic(bool on = true) {
        if (on) { cur_attr_ |=  Attr::ITALIC;    raw_ += "\x1b[3m";  }
        else    { cur_attr_ &= ~Attr::ITALIC;    raw_ += "\x1b[23m"; }
        return *this;
    }

    /**
     * @brief 밑줄(Underline) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& underline(bool on = true) {
        if (on) { cur_attr_ |=  Attr::UNDERLINE; raw_ += "\x1b[4m";  }
        else    { cur_attr_ &= ~Attr::UNDERLINE; raw_ += "\x1b[24m"; }
        return *this;
    }

    /**
     * @brief 깜빡임(Blink) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& blink(bool on = true) {
        if (on) { cur_attr_ |=  Attr::BLINK;     raw_ += "\x1b[5m";  }
        else    { cur_attr_ &= ~Attr::BLINK;     raw_ += "\x1b[25m"; }
        return *this;
    }

    /**
     * @brief 색상 반전(Reverse) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& reverse(bool on = true) {
        if (on) { cur_attr_ |=  Attr::REVERSE;   raw_ += "\x1b[7m";  }
        else    { cur_attr_ &= ~Attr::REVERSE;   raw_ += "\x1b[27m"; }
        return *this;
    }

    /**
     * @brief 취소선(Strikethrough) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& strikethrough(bool on = true) {
        if (on) { cur_attr_ |=  Attr::STRIKE;    raw_ += "\x1b[9m";  }
        else    { cur_attr_ &= ~Attr::STRIKE;    raw_ += "\x1b[29m"; }
        return *this;
    }

    /**
     * @brief 상단선(Overline) 속성을 설정하거나 해제합니다.
     * @param on true면 설정, false면 해제
     * @return 자기 자신 참조
     */
    AnsiString& overline(bool on = true) {
        if (on) { cur_attr_ |=  Attr::OVERLINE;  raw_ += "\x1b[53m"; }
        else    { cur_attr_ &= ~Attr::OVERLINE;  raw_ += "\x1b[55m"; }
        return *this;
    }

    // ── 텍스트 ────────────────────────────────────────────────────────────

    /**
     * @brief 현재 설정된 속성과 색상을 적용하여 텍스트를 추가합니다.
     *
     * 내부적으로 Segment를 생성하여 저장합니다.
     *
     * @param utf8 출력할 UTF-8 문자열
     * @return 자기 자신 참조
     */
    AnsiString& text(std::string_view utf8) {
        raw_.append(utf8.data(), utf8.size());
        flush_text_segment(std::string(utf8));
        return *this;
    }

    /**
     * @brief `text()` 함수의 축약 연산자입니다.
     * @param utf8 출력할 UTF-8 문자열
     * @return 자기 자신 참조
     */
    AnsiString& operator<<(std::string_view utf8) { return text(utf8); }

    // ── 제어 시퀀스 ───────────────────────────────────────────────────────

    /**
     * @brief 모든 속성과 색상을 기본값으로 초기화합니다 (SGR 0).
     * @return 자기 자신 참조
     */
    AnsiString& reset() {
        raw_      += "\x1b[0m";
        cur_fg_    = Color::default_color();
        cur_bg_    = Color::default_color();
        cur_attr_  = 0;
        return *this;
    }

    /**
     * @brief 현재 커서가 있는 행 전체를 지웁니다.
     * @return 자기 자신 참조
     */
    AnsiString& clear_line()      { raw_ += "\x1b[2K";   return *this; }

    /**
     * @brief 커서 위치부터 행의 끝까지 지웁니다.
     * @return 자기 자신 참조
     */
    AnsiString& clear_to_eol()    { raw_ += "\x1b[K";    return *this; }

    /**
     * @brief 화면 전체를 지우고 커서를 홈(1,1)으로 이동합니다.
     * @return 자기 자신 참조
     */
    AnsiString& clear_screen()    { raw_ += "\x1b[2J";   return *this; }

    /**
     * @brief 현재 커서 위치를 저장합니다.
     * @return 자기 자신 참조
     */
    AnsiString& save_cursor()     { raw_ += "\x1b[s";    return *this; }

    /**
     * @brief 저장된 커서 위치로 복원합니다.
     * @return 자기 자신 참조
     */
    AnsiString& restore_cursor()  { raw_ += "\x1b[u";    return *this; }

    /**
     * @brief 커서를 보이거나 숨깁니다.
     * @param v true면 보이기, false면 숨기기
     * @return 자기 자신 참조
     */
    AnsiString& show_cursor(bool v = true) {
        raw_ += v ? "\x1b[?25h" : "\x1b[?25l"; return *this;
    }

    /**
     * @brief 줄바꿈(CRLF)을 추가합니다.
     * @return 자기 자신 참조
     */
    AnsiString& newline()         { raw_ += "\r\n";      return *this; }

    // ── 조회 ──────────────────────────────────────────────────────────────

    /**
     * @brief 현재까지 생성된 ANSI 이스케이프 시퀀스 문자열을 반환합니다.
     * @return 원시 ANSI 문자열 참조
     */
    [[nodiscard]] const std::string&          str()      const noexcept { return raw_; }

    /**
     * @brief 내부적으로 기록된 세그먼트 리스트를 반환합니다.
     * @return Segment 벡터 참조
     */
    [[nodiscard]] const std::vector<Segment>& segments() const noexcept { return segs_; }

    /**
     * @brief 마지막으로 설정된 목적지 X 좌표를 반환합니다.
     * @return X 좌표 (0-based)
     */
    [[nodiscard]] int dest_x() const noexcept { return dest_x_; }

    /**
     * @brief 마지막으로 설정된 목적지 Y 좌표를 반환합니다.
     * @return Y 좌표 (0-based)
     */
    [[nodiscard]] int dest_y() const noexcept { return dest_y_; }

    /**
     * @brief 원시 문자열로의 암시적 변환 연산자입니다.
     */
    operator std::string_view() const noexcept { return raw_; }

    /**
     * @brief 출력 스트림 연산자입니다.
     */
    friend std::ostream& operator<<(std::ostream& os, const AnsiString& a) {
        return os << a.raw_;
    }

    /**
     * @brief 내부 버퍼와 상태를 모두 초기화합니다.
     */
    void clear_all() {
        raw_.clear(); segs_.clear();
        dest_x_ = dest_y_ = -1;
        cur_fg_ = Color::default_color();
        cur_bg_ = Color::default_color();
        cur_attr_ = 0;
    }

private:
    std::string          raw_;    ///< 생성된 ANSI 시퀀스가 누적되는 버퍼
    std::vector<Segment> segs_;   ///< 렌더링 정보를 담은 세그먼트 리스트
    int                  dest_x_   = -1; ///< 현재 커서 X 위치
    int                  dest_y_   = -1; ///< 현재 커서 Y 위치
    Color                cur_fg_;        ///< 현재 전경색 상태
    Color                cur_bg_;        ///< 현재 배경색 상태
    uint16_t             cur_attr_ = 0;  ///< 현재 속성 비트마스크 상태

    // ── private 헬퍼 ──────────────────────────────────────────────────────

    /**
     * @brief 정수를 문자열로 변환하여 raw_ 버퍼에 추가합니다.
     * @param v 변환할 정수 값
     */
    void append_int(int v) { raw_ += std::to_string(v); }

    /**
     * @brief 전경색에 해당하는 ANSI SGR 시퀀스를 raw_에 추가합니다.
     *
     * 색상 타입(Default, Index256, TrueColor)에 따라 적절한 이스케이프 코드를 생성합니다.
     *
     * @param c 적용할 Color 객체
     */
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

    /**
     * @brief 배경색에 해당하는 ANSI SGR 시퀀스를 raw_에 추가합니다.
     *
     * 색상 타입(Default, Index256, TrueColor)에 따라 적절한 이스케이프 코드를 생성합니다.
     *
     * @param c 적용할 Color 객체
     */
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

    /**
     * @brief 현재 상태(색상, 속성)를 포함하여 텍스트 세그먼트를 생성합니다.
     *
     * text() 메서드 호출 시 사용됩니다.
     *
     * @param t 세그먼트에 포함될 텍스트 내용
     */
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