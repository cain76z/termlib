#pragma once
/**
 * ansi_renderer.hpp — 배치 출력 엔진  (TDD §8)
 *
 * 구현부가 모두 클래스 본체 안에 위치한다.
 * 자유 함수 detect_color_level / get_terminal_size 는 클래스 밖이지만
 * inline 으로 ODR 위반 없이 단일 정의된다.  [BUG-01]
 */
#include "platform.hpp"
#include "ansi_optimizer.hpp"
#include <cstdlib>
#include <string>
#include <string_view>

#if defined(TERM_PLATFORM_POSIX)
#  include <sys/ioctl.h>
#  include <unistd.h>
#endif

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  자유 함수  (클래스 밖 — inline 으로 ODR 안전)  [BUG-01]
// ═══════════════════════════════════════════════════════════════════════════

/// 터미널 컬러 지원 레벨
enum class ColorLevel { None, Basic8, Index256, TrueColor };

/// 현재 터미널의 컬러 레벨 감지
[[nodiscard]] inline ColorLevel detect_color_level() noexcept {
#if defined(TERM_PLATFORM_WINDOWS)
    if (std::getenv("WT_SESSION")) return ColorLevel::TrueColor;
    if (auto* ct = std::getenv("COLORTERM"); ct) {
        std::string_view sv(ct);
        if (sv == "truecolor" || sv == "24bit") return ColorLevel::TrueColor;
        if (sv == "256color")                   return ColorLevel::Index256;
    }
    return ColorLevel::Index256;
#else
    if (std::getenv("WT_SESSION")) return ColorLevel::TrueColor;
    if (auto* ct = std::getenv("COLORTERM"); ct) {
        std::string_view sv(ct);
        if (sv == "truecolor" || sv == "24bit") return ColorLevel::TrueColor;
        if (sv == "256color")                   return ColorLevel::Index256;
    }
    if (auto* term = std::getenv("TERM"); term) {
        std::string_view sv(term);
        if (sv.find("256color") != std::string_view::npos) return ColorLevel::Index256;
        if (sv == "xterm" || sv == "screen" ||
            sv == "xterm-color")                            return ColorLevel::Index256;
        if (sv == "dumb")                                   return ColorLevel::None;
    }
    if (std::getenv("CI")) return ColorLevel::Basic8;
    return ColorLevel::Basic8;
#endif
}

/// 터미널 크기 조회
struct TermSize { int cols; int rows; };

[[nodiscard]] inline TermSize get_terminal_size() noexcept {
#if defined(TERM_PLATFORM_WINDOWS)
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return { csbi.srWindow.Right  - csbi.srWindow.Left + 1,
                 csbi.srWindow.Bottom - csbi.srWindow.Top  + 1 };
    return {80, 24};
#else
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return { ws.ws_col, ws.ws_row };
    const char* ce = std::getenv("COLUMNS");
    const char* re = std::getenv("LINES");
    return { ce ? std::atoi(ce) : 80, re ? std::atoi(re) : 24 };
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
//  AnsiRenderer  (TDD §8)
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class AnsiRenderer {
public:
    explicit AnsiRenderer(AnsiScreen& screen)
        : screen_(screen)
    {
        color_level_ = detect_color_level();
        setup();
    }

    ~AnsiRenderer() {
        if (in_alt_screen_) leave_alt_screen();
    }

    // ── 렌더링 ────────────────────────────────────────────────────────────
    void render() {
        if (!screen_.any_dirty()) return;
        std::string out = optimizer_.optimize(screen_);
        screen_.snapshot();
        screen_.clear_dirty();
        platform_write(out);
    }

    void render_full() {
        screen_.invalidate_all();
        std::string out = optimizer_.full_redraw(screen_);
        screen_.snapshot();
        screen_.clear_dirty();
        platform_write(out);
    }

    // ── 화면 모드 ─────────────────────────────────────────────────────────
    void enter_alt_screen() {
        platform_write("\x1b[?1049h");
        platform_write("\x1b[2J");
        platform_write("\x1b[H");
        in_alt_screen_ = true;
    }

    void leave_alt_screen() {
        if (!in_alt_screen_) return;
        show_cursor(true);
        platform_write("\x1b[?1049l");
        in_alt_screen_ = false;
    }

    // ── 커서 / 제어 ───────────────────────────────────────────────────────
    void show_cursor(bool visible) {
        platform_write(visible ? "\x1b[?25h" : "\x1b[?25l");
    }

    void move_cursor(int x, int y) {
        std::string s = "\x1b[";
        s += std::to_string(y + 1); s += ';';
        s += std::to_string(x + 1); s += 'H';
        platform_write(s);
    }

    void set_title(std::string_view title) {
        std::string s = "\x1b]0;";
        s.append(title); s += "\x07";
        platform_write(s);
    }

    void clear_screen() { platform_write("\x1b[2J\x1b[H"); }

    // ── 리사이즈 ──────────────────────────────────────────────────────────
    void handle_resize() {
        auto [nc, nr] = get_terminal_size();
        screen_.resize(nc, nr);
        render_full();
    }

    [[nodiscard]] ColorLevel color_level() const noexcept { return color_level_; }
    [[nodiscard]] bool       vt_enabled()  const noexcept { return vt_enabled_; }

private:
    AnsiScreen&   screen_;
    AnsiOptimizer optimizer_;
    ColorLevel    color_level_ = ColorLevel::TrueColor;
    bool          vt_enabled_  = false;
    bool          in_alt_screen_ = false;

#if defined(TERM_PLATFORM_WINDOWS)
    HANDLE out_handle_ = INVALID_HANDLE_VALUE;

    bool init_windows() {
        out_handle_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out_handle_ == INVALID_HANDLE_VALUE) return false;  // [MIN-03]
        DWORD mode = 0;
        if (!GetConsoleMode(out_handle_, &mode)) return false;
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        mode |= DISABLE_NEWLINE_AUTO_RETURN;
        if (!SetConsoleMode(out_handle_, mode)) return false;
        SetConsoleOutputCP(CP_UTF8);
        return true;
    }
#endif

    void setup() {
#if defined(TERM_PLATFORM_WINDOWS)
        vt_enabled_ = init_windows();
#else
        vt_enabled_ = true;
#endif
    }

    void platform_write(std::string_view data) {
        if (data.empty()) return;
#if defined(TERM_PLATFORM_WINDOWS)
        // [MIN-03] INVALID_HANDLE_VALUE 체크
        if (vt_enabled_ && out_handle_ != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(out_handle_, data.data(),
                      static_cast<DWORD>(data.size()), &written, nullptr);
        }
#else
        const char* ptr       = data.data();
        std::size_t remaining = data.size();
        while (remaining > 0) {
            auto written = ::write(STDOUT_FILENO, ptr, remaining);
            if (written <= 0) break;
            ptr       += written;
            remaining -= static_cast<std::size_t>(written);
        }
#endif
    }
};

} // namespace term
