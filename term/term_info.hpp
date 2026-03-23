#pragma once
/**
 * @file term_info.hpp
 * @brief 터미널 정보 조회 및 관리  (TDD §8.1)
 *
 * 렌더러(AnsiRenderer)와 독립적으로 사용 가능한 터미널 유틸리티입니다.
 *
 * ## 구성
 *
 * ### 자유 함수
 * - `detect_color_level()` — 환경 변수 기반 컬러 레벨 감지
 * - `get_terminal_size()`  — 현재 터미널 크기 (열·행) 조회
 *
 * ### Terminal 클래스 (RAII)
 * Terminal 자체가 완전한 RAII 객체입니다.
 * 진입 함수를 호출하면 소멸자가 역순으로 전부 복원합니다.
 *
 * ```cpp
 * {
 *     term::Terminal t;
 *     t.enter_raw_mode();          // 소멸 시 자동 해제
 *     t.show_cursor(false);        // 소멸 시 자동 복원
 *     t.enter_alt_screen();        // 소멸 시 자동 탈출
 *     t.set_scroll_region(0, 20);  // 소멸 시 자동 초기화
 *     // ... 앱 실행 ...
 * }   // ← scroll_region / raw_mode / cursor / alt_screen 전부 복원
 * ```
 *
 * ### 출력 버퍼링
 * ```cpp
 * t.write("...");       // out_buffer_ 에 누적
 * t.write("...");
 * t.flush();            // 실제 syscall — 렌더 루프 마지막에 한 번만 호출
 * ```
 *
 * ### SIGWINCH (단독 사용 시)
 * ```cpp
 * t.on_resize([&](int cols, int rows) { screen.resize(cols, rows); });
 * while (running) { t.poll_resize(); }
 * ```
 *
 * ### input.hpp 와 함께 사용 시 (권장)
 * ```cpp
 * // Terminal 은 출력만 담당, SIGWINCH/raw mode 는 InputDriver 가 담당
 * term::Terminal term;
 * auto input = term::make_input_driver();    // manage_raw_mode=true (기본값)
 * term.enter_alt_screen();
 * term.show_cursor(false);
 * input->setup();    // raw mode + SIGWINCH 설치 (SA_RESTART 없음)
 * // term.enter_raw_mode() / term.on_resize() 를 호출하지 않는다
 *
 * while (running) {
 *     auto ev = input->read_key(16);
 *     if (ev.key == KEY_RESIZE) renderer.handle_resize();
 *     renderer.render();
 * }
 * ```
 *
 * ## SIGWINCH 충돌 해결
 *
 * term_info.hpp + input.hpp 동시 사용 시 SIGWINCH 핸들러가 이중 등록되는 문제를
 * term_shared.hpp 의 `shared_detail::ensure_sigwinch_handler()` 로 해결한다.
 *
 * - `on_resize()` → `shared_detail::ensure_sigwinch_handler()` 호출 (중복 설치 방지)
 * - `PosixInput::setup()` → 동일 함수 호출 (SA_RESTART 없음으로 통일)
 * - 양쪽이 같은 `shared_detail::g_sigwinch_flag` 를 공유한다
 *
 * ## 의존 방향
 * ```
 * platform.hpp
 *   └── term_shared.hpp
 *         └── term_info.hpp
 *               └── ansi_renderer.hpp
 * ```
 */

#include "platform.hpp"
#include "term/term_shared.hpp"   // 공유 SIGWINCH 상태 (input.hpp 와 충돌 방지)
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>

#if defined(TERM_PLATFORM_POSIX)
#  include <signal.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  컬러 레벨
// ═══════════════════════════════════════════════════════════════════════════

enum class ColorLevel {
    None,       ///< 컬러 미지원 (dumb 터미널 등)
    Basic8,     ///< 기본 8색
    Index256,   ///< 256색 팔레트
    TrueColor   ///< 24bit RGB
};

/**
 * @brief 환경 변수로부터 컬러 지원 레벨을 감지합니다.
 *
 * 검사 순서:
 *  1. WT_SESSION    → TrueColor (Windows Terminal)
 *  2. COLORTERM     → truecolor/24bit/256color
 *  3. TERM_PROGRAM  → Apple_Terminal/iTerm.app/vscode/Hyper → TrueColor
 *  4. TERM          → *256color* / dumb
 *  5. CI            → Basic8
 *  6. 기본값        → Basic8
 */
[[nodiscard]] inline ColorLevel detect_color_level() noexcept {
    if (std::getenv("WT_SESSION")) return ColorLevel::TrueColor;

    if (auto* ct = std::getenv("COLORTERM"); ct) {
        std::string_view sv(ct);
        if (sv == "truecolor" || sv == "24bit") return ColorLevel::TrueColor;
        if (sv == "256color")                   return ColorLevel::Index256;
    }

#if !defined(TERM_PLATFORM_WINDOWS)
    if (auto* tp = std::getenv("TERM_PROGRAM"); tp) {
        std::string_view sv(tp);
        if (sv == "Apple_Terminal" || sv == "iTerm.app" ||
            sv == "vscode"         || sv == "Hyper")
            return ColorLevel::TrueColor;
    }

    if (auto* t = std::getenv("TERM"); t) {
        std::string_view sv(t);
        if (sv.find("256color") != std::string_view::npos) return ColorLevel::Index256;
        if (sv == "xterm" || sv == "screen" || sv == "xterm-color")
            return ColorLevel::Index256;
        if (sv == "dumb") return ColorLevel::None;
    }

    if (std::getenv("CI")) return ColorLevel::Basic8;
    return ColorLevel::Basic8;
#else
    return ColorLevel::Index256;
#endif
}

// ═══════════════════════════════════════════════════════════════════════════
//  터미널 크기
// ═══════════════════════════════════════════════════════════════════════════

struct TermSize {
    int cols;
    int rows;
    bool operator==(const TermSize& o) const noexcept {
        return cols == o.cols && rows == o.rows;
    }
    bool operator!=(const TermSize& o) const noexcept { return !(*this == o); }
};

/**
 * @brief 현재 터미널 크기를 OS에 직접 조회합니다.
 *
 * POSIX: ioctl(STDOUT) 실패 시 ioctl(STDERR) 재시도.
 * 폴백: 환경변수 COLUMNS/LINES → 80×24.
 */
[[nodiscard]] inline TermSize get_terminal_size() noexcept {
#if defined(TERM_PLATFORM_WINDOWS)
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return { csbi.srWindow.Right  - csbi.srWindow.Left + 1,
                 csbi.srWindow.Bottom - csbi.srWindow.Top  + 1 };
#else
    struct winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0)
        ::ioctl(STDERR_FILENO, TIOCGWINSZ, &ws);
    if (ws.ws_col > 0)
        return { ws.ws_col, ws.ws_row };
#endif
    const char* ce = std::getenv("COLUMNS");
    const char* re = std::getenv("LINES");
    return { ce ? std::atoi(ce) : 80, re ? std::atoi(re) : 24 };
}

// ═══════════════════════════════════════════════════════════════════════════
//  Terminal  — RAII 터미널 관리
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief 터미널 초기화·제어·정보 조회를 담당하는 RAII 클래스
 *
 * 소멸자 복원 순서:
 *  1. 출력 버퍼 flush
 *  2. 스크롤 영역 초기화
 *  3. Raw mode 해제
 *  4. 커서 표시 복원
 *  5. Alt screen 탈출
 *
 * ### input.hpp 와 함께 사용 시 주의사항
 *
 * `PosixInput::setup()` 이 SIGWINCH 핸들러와 raw mode 를 관리하므로
 * `Terminal::enter_raw_mode()` 와 `Terminal::on_resize()` 를 **중복 호출하지 않는다**.
 *
 * ```
 * ❌ 잘못된 조합:
 *    term.enter_raw_mode() + input->setup()   → raw mode 이중 설정
 *    term.on_resize(cb)    + input->setup()   → 핸들러 덮어씌워짐 (no-op)
 *
 * ✅ 올바른 조합:
 *    input->setup()   (raw mode + SIGWINCH 소유)
 *    term.write(...)  (출력만 담당)
 *    KEY_RESIZE 이벤트로 크기 변경 감지
 * ```
 */
class Terminal {
public:
    Terminal() {
        color_level_ = detect_color_level();
        out_buffer_.reserve(4096);
        setup();
    }

    ~Terminal() {
        flush();
        if (scroll_region_set_) reset_scroll_region();
        if (raw_mode_active_)   leave_raw_mode_impl();
        if (!cursor_visible_)   flush_write("\x1b[?25h");
        if (in_alt_screen_)     leave_alt_screen();
#if defined(TERM_PLATFORM_POSIX)
        shared_detail::g_resize_callback = nullptr;
#endif
    }

    Terminal(const Terminal&)            = delete;
    Terminal& operator=(const Terminal&) = delete;
    Terminal(Terminal&&)                 = delete;
    Terminal& operator=(Terminal&&)      = delete;

    // ── 정보 조회 ─────────────────────────────────────────────────────────

    [[nodiscard]] TermSize   size()            const noexcept { return get_terminal_size(); }
    [[nodiscard]] int        cols()            const noexcept { return get_terminal_size().cols; }
    [[nodiscard]] int        rows()            const noexcept { return get_terminal_size().rows; }
    [[nodiscard]] ColorLevel color_level()     const noexcept { return color_level_; }
    [[nodiscard]] bool       vt_enabled()      const noexcept { return vt_enabled_; }
    [[nodiscard]] bool       is_tty()          const noexcept { return is_tty_; }
    [[nodiscard]] bool       in_alt_screen()   const noexcept { return in_alt_screen_; }
    [[nodiscard]] bool       raw_mode_active() const noexcept { return raw_mode_active_; }
    [[nodiscard]] bool       cursor_visible()  const noexcept { return cursor_visible_; }

    // ── 출력 버퍼 ─────────────────────────────────────────────────────────

    /**
     * @brief 데이터를 출력 버퍼에 누적합니다.
     *
     * 실제 syscall은 `flush()` 호출 시 일괄 처리됩니다.
     * stdout이 TTY가 아니면 (pipe/file 리다이렉트) 버퍼링만 하고 flush에서 무시합니다.
     */
    void write(std::string_view data) {
        if (data.empty()) return;
        out_buffer_.append(data);
    }

    /**
     * @brief 출력 버퍼를 실제 터미널에 씁니다.
     *
     * 렌더 루프 마지막에 한 번만 호출하면 syscall 횟수를 최소화합니다.
     */
    void flush() {
        if (out_buffer_.empty()) return;
        if (is_tty_) flush_write(out_buffer_);
        out_buffer_.clear();
    }

    // ── Alt screen ────────────────────────────────────────────────────────

    void enter_alt_screen() {
        if (in_alt_screen_) return;
        write("\x1b[?1049h\x1b[H");
        in_alt_screen_ = true;
    }

    void leave_alt_screen() {
        if (!in_alt_screen_) return;
        if (scroll_region_set_) reset_scroll_region();
        write("\x1b[?1049l");
        in_alt_screen_ = false;
    }

    // ── 커서 ──────────────────────────────────────────────────────────────

    void show_cursor(bool visible) {
        write(visible ? "\x1b[?25h" : "\x1b[?25l");
        cursor_visible_ = visible;
    }

    void move_cursor(int x, int y) {
        char buf[32];
        int n = std::snprintf(buf, sizeof(buf), "\x1b[%d;%dH", y + 1, x + 1);
        if (n > 0) write({buf, static_cast<std::size_t>(n)});
    }

    // ── 화면 제어 ─────────────────────────────────────────────────────────

    void clear_screen() { write("\x1b[2J\x1b[H"); }

    void set_title(std::string_view title) {
        std::string s;
        s.reserve(title.size() + 6);
        s = "\x1b]0;";
        s.append(title);
        s += "\x07";
        write(s);
    }

    void set_scroll_region(int top, int bottom) {
        char buf[32];
        int n = std::snprintf(buf, sizeof(buf), "\x1b[%d;%dr", top + 1, bottom + 1);
        if (n > 0) write({buf, static_cast<std::size_t>(n)});
        scroll_region_set_ = true;
    }

    void reset_scroll_region() {
        write("\x1b[r");
        scroll_region_set_ = false;
    }

    void reset_attrs() { write("\x1b[0m"); }

    // ── Raw mode ──────────────────────────────────────────────────────────

    /**
     * @brief Raw mode로 전환합니다. 소멸자에서 자동 해제.
     *
     * @param keep_signals true(기본값): ISIG 유지 — Ctrl+C/Z가 SIGINT/SIGTSTP 발생.
     *
     * ### input.hpp 와 함께 사용 시
     * `PosixInput::setup()` 이 raw mode 를 관리하는 경우 이 함수를 호출하지 않는다.
     * `InputOptions::manage_raw_mode = false` 로 PosixInput 을 생성한 경우에만 호출한다.
     */
    void enter_raw_mode(bool keep_signals = true) {
        if (raw_mode_active_) return;
        enter_raw_mode_impl(keep_signals);
        raw_mode_active_ = true;
    }

    void leave_raw_mode() {
        if (!raw_mode_active_) return;
        leave_raw_mode_impl();
        raw_mode_active_ = false;
    }

    // ── SIGWINCH ──────────────────────────────────────────────────────────

    /**
     * @brief 터미널 크기 변경 콜백 등록. POSIX 전용, Windows는 no-op.
     *
     * `poll_resize()` 를 메인 루프에서 호출해야 합니다.
     *
     * ### input.hpp 와 함께 사용 시
     * `PosixInput::setup()` 이 SIGWINCH 를 KEY_RESIZE 이벤트로 즉시 반환하므로
     * 이 함수를 호출할 필요가 없다. KEY_RESIZE 이벤트를 직접 처리하면 된다.
     *
     * ### 충돌 방지 (변경점)
     * 기존: sigaction() 직접 호출 (SA_RESTART 포함)
     * 변경: shared_detail::ensure_sigwinch_handler() 호출
     *       → 이미 PosixInput 이 핸들러를 설치했으면 no-op
     *       → 설치 안 됐으면 SA_RESTART 없이 설치 (즉시 감지 방식으로 통일)
     */
    void on_resize(std::function<void(int, int)> cb) {
#if defined(TERM_PLATFORM_POSIX)
        shared_detail::g_resize_callback = std::move(cb);
        last_size_ = get_terminal_size();

        // [충돌 방지] 공유 핸들러 설치 (이미 설치된 경우 no-op)
        // 기존 SA_RESTART 방식 제거 → SA_RESTART 없이 통일
        shared_detail::ensure_sigwinch_handler();
#else
        (void)cb;
#endif
    }

    /**
     * @brief SIGWINCH 수신 시 콜백을 메인 스레드에서 안전하게 호출합니다.
     *
     * ### 충돌 방지 (변경점)
     * 기존: 자체 term_detail::g_sigwinch_flag 확인
     * 변경: 공유 shared_detail::g_sigwinch_flag 확인
     *       → PosixInput 이 SIGWINCH 를 감지해도 poll_resize() 가 올바르게 작동
     */
    void poll_resize() {
#if defined(TERM_PLATFORM_POSIX)
        // [충돌 방지] 공유 플래그 확인 (PosixInput 과 동일한 플래그)
        if (!shared_detail::g_sigwinch_flag.exchange(false)) return;

        auto s = get_terminal_size();
        if (s == last_size_) return;
        last_size_ = s;

        if (shared_detail::g_resize_callback)
            shared_detail::g_resize_callback(s.cols, s.rows);
#endif
    }

private:
    ColorLevel  color_level_       = ColorLevel::Basic8;
    bool        vt_enabled_        = false;
    bool        is_tty_            = false;
    bool        in_alt_screen_     = false;
    bool        raw_mode_active_   = false;
    bool        cursor_visible_    = true;
    bool        scroll_region_set_ = false;

    std::string out_buffer_;

#if defined(TERM_PLATFORM_POSIX)
    struct termios orig_termios_{};
    TermSize       last_size_{ 0, 0 };
#endif

#if defined(TERM_PLATFORM_WINDOWS)
    HANDLE out_handle_   = INVALID_HANDLE_VALUE;
    DWORD  orig_in_mode_ = 0;

    bool init_windows() {
        out_handle_ = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out_handle_ == INVALID_HANDLE_VALUE) return false;
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
        is_tty_     = vt_enabled_;
#else
        is_tty_     = (::isatty(STDOUT_FILENO) == 1);
        vt_enabled_ = is_tty_;
#endif
    }

    void flush_write(std::string_view data) {
        if (data.empty()) return;
#if defined(TERM_PLATFORM_WINDOWS)
        if (out_handle_ != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(out_handle_, data.data(),
                      static_cast<DWORD>(data.size()), &written, nullptr);
        }
#else
        const char* ptr       = data.data();
        std::size_t remaining = data.size();
        while (remaining > 0) {
            ssize_t n = ::write(STDOUT_FILENO, ptr, remaining);
            if (n > 0) {
                ptr       += static_cast<std::size_t>(n);
                remaining -= static_cast<std::size_t>(n);
            } else if (errno == EINTR || errno == EAGAIN) {
                continue;
            } else {
                break;
            }
        }
#endif
    }

    void enter_raw_mode_impl(bool keep_signals) {
#if defined(TERM_PLATFORM_POSIX)
        if (::tcgetattr(STDIN_FILENO, &orig_termios_) == -1) return;
        struct termios raw = orig_termios_;
        raw.c_iflag &= ~static_cast<tcflag_t>(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
        raw.c_cflag |=  static_cast<tcflag_t>(CS8);
        tcflag_t lflag_mask = static_cast<tcflag_t>(ECHO | ICANON | IEXTEN);
        if (!keep_signals) lflag_mask |= static_cast<tcflag_t>(ISIG);
        raw.c_lflag &= ~lflag_mask;
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 1;
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#elif defined(TERM_PLATFORM_WINDOWS)
        (void)keep_signals;
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        if (in == INVALID_HANDLE_VALUE) return;
        GetConsoleMode(in, &orig_in_mode_);
        DWORD mode = orig_in_mode_;
        // [충돌 방지] ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT 포함
        // → WindowsInput(manage_raw_mode=false) 와 함께 사용 가능
        mode &= ~static_cast<DWORD>(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        mode |= ENABLE_VIRTUAL_TERMINAL_INPUT | ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
        SetConsoleMode(in, mode);
#endif
    }

    void leave_raw_mode_impl() {
#if defined(TERM_PLATFORM_POSIX)
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios_);
#elif defined(TERM_PLATFORM_WINDOWS)
        HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
        if (in != INVALID_HANDLE_VALUE) SetConsoleMode(in, orig_in_mode_);
#endif
    }
};

} // namespace term