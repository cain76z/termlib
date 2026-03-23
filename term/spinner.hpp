#pragma once
/**
 * spinner.hpp — Spinner 프리셋 + 위젯  (TDD §10)
 *
 * 구현부가 모두 클래스 본체 안에 위치한다.
 */
#include "term/ansi_screen.hpp"
#include "term/ansi_string.hpp"
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  Spinner 프리셋  (TDD §10.2)
// ═══════════════════════════════════════════════════════════════════════════

namespace spinner {

template<std::size_t N>
struct Preset {
    std::string_view frames[N];
    int default_interval_ms;
    constexpr std::span<const std::string_view> span() const {
        return { frames, N };
    }
};

inline constexpr Preset<4>  LINE       = {{ "|","/","-","\\" },                    100};
inline constexpr Preset<10> DOTS       = {{ "⠋","⠙","⠹","⠸","⠼","⠴","⠦","⠧","⠇","⠏" }, 80};
inline constexpr Preset<10> MINI_DOTS  = {{ "⠋","⠙","⠚","⠞","⠖","⠦","⠴","⠲","⠳","⠓" }, 80};
inline constexpr Preset<4>  PULSE      = {{ "█","▓","▒","░" },                     100};
inline constexpr Preset<5>  POINTS     = {{ "∙∙∙","●∙∙","∙●∙","∙∙●","∙∙∙" },       120};
inline constexpr Preset<3>  GLOBE      = {{ "🌍","🌎","🌏" },                       150};
inline constexpr Preset<8>  MOON       = {{ "🌑","🌒","🌓","🌔","🌕","🌖","🌗","🌘" }, 100};
inline constexpr Preset<12> CLOCK      = {{ "🕛","🕐","🕑","🕒","🕓","🕔",
                                            "🕕","🕖","🕗","🕘","🕙","🕚" },         100};
inline constexpr Preset<3>  MONKEY     = {{ "🙈","🙉","🙊" },                       150};
inline constexpr Preset<6>  STAR       = {{ "✶","✸","✹","✺","✹","✸" },              80};
inline constexpr Preset<3>  HAMBURGER  = {{ "☱","☲","☴" },                         100};
inline constexpr Preset<12> GROW_V     = {{ " ","▃","▄","▅","▆","▇","█",
                                            "▇","▆","▅","▄","▃" },                  80};
inline constexpr Preset<13> GROW_H     = {{ "▉","▊","▋","▌","▍","▎","▏",
                                            "▎","▍","▌","▋","▊","▉" },              80};
inline constexpr Preset<8>  ARROW      = {{ "←","↖","↑","↗","→","↘","↓","↙" },    100};
inline constexpr Preset<4>  TRIANGLE   = {{ "◢","◣","◤","◥" },                     100};
inline constexpr Preset<4>  CIRCLE     = {{ "◐","◓","◑","◒" },                     100};
inline constexpr Preset<8>  BOUNCE     = {{ "⠁","⠂","⠄","⡀","⢀","⠠","⠐","⠈" },    80};

} // namespace spinner

// ═══════════════════════════════════════════════════════════════════════════
//  Spinner 위젯  (TDD §10.1)
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class Spinner {
public:
    struct Rect { int x=0, y=0; };

    explicit Spinner(std::span<const std::string_view> frames, int interval_ms = 80)
        : interval_ms_(interval_ms)
        , last_tick_(std::chrono::steady_clock::now())
    {
        frames_.reserve(frames.size());
        for (auto sv : frames) frames_.emplace_back(sv);
    }

    // ── 프리셋 편의 생성자 ────────────────────────────────────────────────
    template<std::size_t N>
    explicit Spinner(const spinner::Preset<N>& p)
        : Spinner(p.span(), p.default_interval_ms) {}

    // ── 제어 ──────────────────────────────────────────────────────────────
    void tick() {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - last_tick_).count();
        if (elapsed < interval_ms_) return;
        frame_idx_    = (frame_idx_ + 1) % static_cast<int>(frames_.size());
        last_tick_    = now;
        needs_render_ = true;
    }

    void render(AnsiScreen& screen) const {
        if (!visible_) return;
        AnsiString s;
        s.xy(rect_.x, rect_.y);
        s.fg(color_);
        s.text(frames_[frame_idx_]);
        if (!label_.empty()) { s.text(" "); s.text(label_); }
        s.reset();
        screen.put(s);
        const_cast<Spinner*>(this)->needs_render_ = false;
    }

    // ── 설정 ──────────────────────────────────────────────────────────────
    void set_rect(Rect r)              { rect_    = r; }
    void set_label(std::string_view l) { label_   = l; }
    void set_color(const Color& c)     { color_   = c; }
    void set_visible(bool v)           { visible_ = v; }

    [[nodiscard]] bool needs_render() const noexcept { return needs_render_; }
    [[nodiscard]] int  frame_index()  const noexcept { return frame_idx_; }

private:
    std::vector<std::string>                   frames_;
    int                                        interval_ms_;
    int                                        frame_idx_    = 0;
    bool                                       needs_render_ = true;
    bool                                       visible_      = true;
    std::string                                label_;
    Color                                      color_        = Color::default_color();
    Rect                                       rect_;
    std::chrono::steady_clock::time_point      last_tick_;
};

// ── 편의 팩토리 함수 ─────────────────────────────────────────────────────────
template<std::size_t N>
inline Spinner make_spinner(const spinner::Preset<N>& p) { return Spinner(p); }

} // namespace term
