#pragma once
/**
 * @file input.hpp
 * @brief 키 입력 플랫폼 추상화 (TDD §10)
 *
 * 플랫폼별 키 입력을 공통 KeyEvent 구조체로 정규화한다.
 * 상위 레이어는 플랫폼을 인식하지 않는다.
 *
 * ## 수정 이력
 * - [BUG-1] Windows: VK_F10 → KEY_F10 수정, VK_MENU → KEY_MENU 추가
 * - [BUG-2] Windows: MOUSE_EVENT 미처리 → translate_mouse() 추가
 * - [BUG-3] POSIX: SIGWINCH signal() → sigaction() 변경
 * - [BUG-4] POSIX: 0x08(Ctrl+H) Backspace 처리 추가
 * - [OPT-1] POSIX: read_timeout() → select() 기반으로 교체
 * - [FEAT-1] read_key(timeout_ms) 오버로드 추가
 * - [FIX-CONFLICT] term_info.hpp 와 동시 사용 시 충돌 해결:
 *   - SIGWINCH 플래그: 자체 `input_detail::g_sigwinch_flag` →
 *     `shared_detail::g_sigwinch_flag` (term_shared.hpp) 로 통합
 *   - SIGWINCH 핸들러: `shared_detail::ensure_sigwinch_handler()` 로 단일화
 *     (SA_RESTART 없음으로 통일, 중복 설치 방지)
 *   - Raw mode 소유권: `InputOptions::manage_raw_mode` 로 명시적 제어
 *
 * ## InputOptions — 소유권 명시 패턴
 *
 * ```cpp
 * // 단독 사용 (기본값): InputDriver 가 모든 것을 관리
 * auto input = term::make_input_driver();
 * input->setup();  // raw mode + SIGWINCH 모두 설정
 *
 * // Terminal 과 함께 사용: InputDriver 는 raw mode 만 담당
 * term::Terminal term;
 * term.enter_alt_screen();
 * term.show_cursor(false);
 * // InputOptions 기본값(manage_raw_mode=true) 사용 — Terminal::enter_raw_mode() 는 호출 안 함
 * auto input = term::make_input_driver();
 * input->setup();  // raw mode + SIGWINCH 소유
 *
 * // Terminal::enter_raw_mode() 를 먼저 호출한 경우:
 * // → manage_raw_mode=false 로 InputDriver 생성
 * term::InputOptions opts;
 * opts.manage_raw_mode = false;
 * auto input = term::make_input_driver(opts);
 * input->setup();  // SIGWINCH 만 설치, raw mode 는 Terminal 이 소유
 * ```
 *
 * ## SA_RESTART 제거 이유
 * SA_RESTART 설정 시 SIGWINCH 발생해도 read()가 자동 재시작되어
 * 다음 키 입력 전까지 KEY_RESIZE 이벤트가 전달되지 않는다.
 * SA_RESTART 제거 후 read()가 EINTR을 반환하면 루프에서
 * g_sigwinch_flag를 확인하여 즉시 KEY_RESIZE를 반환한다.
 *
 * ## NOTE
 * PosixInput::setup() 이 SIGWINCH 핸들러를 등록하므로
 * Terminal::on_resize() 와 동시에 사용하면 핸들러가 덮어써진다.
 * 둘 중 하나만 사용할 것.
 */

#include "term/term_shared.hpp"   // 공유 SIGWINCH 상태 (term_info.hpp 와 충돌 방지)
#include <cstdint>
#include <memory>
#include <string>

// ── 플랫폼 헤더 ───────────────────────────────────────────────────────────
#if defined(_WIN32) || defined(_WIN64)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <atomic>
#  include <cerrno>
#  include <csignal>
#  include <cstring>
#  include <unistd.h>
#  include <termios.h>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#endif

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  Key 코드 열거 (플랫폼 독립)
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @enum Key
 * @brief 플랫폼 독립적인 키 코드.
 * ASCII 출력 가능 문자는 해당 코드포인트를 직접 사용한다.
 */
enum Key : uint32_t {
    KEY_NONE      = 0,

    KEY_UP        = 0x1000,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_PGUP,
    KEY_PGDN,
    KEY_INS,
    KEY_DEL,
    KEY_BACKSPACE,
    KEY_ENTER,
    KEY_TAB,
    KEY_ESC,

    KEY_F1  = 0x1100,
    KEY_F2,  KEY_F3,  KEY_F4,  KEY_F5,
    KEY_F6,  KEY_F7,  KEY_F8,  KEY_F9,
    KEY_F10, KEY_F11, KEY_F12,
    KEY_MENU,

    KEY_CTRL_A = 0x1200,
    KEY_CTRL_B, KEY_CTRL_C, KEY_CTRL_D, KEY_CTRL_E,
    KEY_CTRL_F, KEY_CTRL_G,
    KEY_CTRL_K, KEY_CTRL_L, KEY_CTRL_N, KEY_CTRL_O,
    KEY_CTRL_P, KEY_CTRL_Q, KEY_CTRL_R, KEY_CTRL_S,
    KEY_CTRL_T, KEY_CTRL_U, KEY_CTRL_V, KEY_CTRL_W,
    KEY_CTRL_X, KEY_CTRL_Y, KEY_CTRL_Z,

    KEY_RESIZE  = 0x1300,
    KEY_MOUSE   = 0x1400,
};

constexpr uint32_t ctrl_to_key(unsigned char c) noexcept {
    switch (c) {
        case 0x01: return KEY_CTRL_A;  case 0x02: return KEY_CTRL_B;
        case 0x03: return KEY_CTRL_C;  case 0x04: return KEY_CTRL_D;
        case 0x05: return KEY_CTRL_E;  case 0x06: return KEY_CTRL_F;
        case 0x07: return KEY_CTRL_G;
        case 0x0B: return KEY_CTRL_K;  case 0x0C: return KEY_CTRL_L;
        case 0x0E: return KEY_CTRL_N;  case 0x0F: return KEY_CTRL_O;
        case 0x10: return KEY_CTRL_P;  case 0x11: return KEY_CTRL_Q;
        case 0x12: return KEY_CTRL_R;  case 0x13: return KEY_CTRL_S;
        case 0x14: return KEY_CTRL_T;  case 0x15: return KEY_CTRL_U;
        case 0x16: return KEY_CTRL_V;  case 0x17: return KEY_CTRL_W;
        case 0x18: return KEY_CTRL_X;  case 0x19: return KEY_CTRL_Y;
        case 0x1A: return KEY_CTRL_Z;
        default:   return KEY_NONE;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  수정자 플래그
// ═══════════════════════════════════════════════════════════════════════════

struct Modifier {
    static constexpr uint8_t NONE  = 0;
    static constexpr uint8_t CTRL  = 1 << 0;
    static constexpr uint8_t ALT   = 1 << 1;
    static constexpr uint8_t SHIFT = 1 << 2;
};

// ═══════════════════════════════════════════════════════════════════════════
//  MouseEvent
// ═══════════════════════════════════════════════════════════════════════════

enum class MouseBtn : uint8_t {
    LEFT       = 0,
    MIDDLE     = 1,
    RIGHT      = 2,
    RELEASE    = 3,
    WHEEL_UP   = 64,
    WHEEL_DOWN = 65,
};

struct MouseEvent {
    MouseBtn btn    = MouseBtn::LEFT;
    int      x      = 0;
    int      y      = 0;
    bool     motion = false;
};

// ═══════════════════════════════════════════════════════════════════════════
//  KeyEvent
// ═══════════════════════════════════════════════════════════════════════════

struct KeyEvent {
    uint32_t   key      = KEY_NONE;
    uint8_t    modifier = Modifier::NONE;
    char32_t   ch       = 0;
    MouseEvent mouse    = {};

    bool is_printable() const noexcept { return ch >= 0x20 && ch != 0x7F; }
    bool has_ctrl()     const noexcept { return (modifier & Modifier::CTRL)  != 0; }
    bool has_alt()      const noexcept { return (modifier & Modifier::ALT)   != 0; }
    bool has_shift()    const noexcept { return (modifier & Modifier::SHIFT) != 0; }
};

// ═══════════════════════════════════════════════════════════════════════════
//  InputOptions — 소유권 명시 패턴  [FIX-CONFLICT]
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief InputDriver 초기화 옵션
 *
 * term_info.hpp 와 input.hpp 를 함께 사용할 때 충돌을 방지하기 위한
 * 소유권(ownership) 명시 패턴입니다.
 *
 * ### manage_raw_mode 선택 가이드
 *
 * | 상황 | manage_raw_mode | raw mode 관리자 |
 * |------|----------------|----------------|
 * | input.hpp 단독 사용 | true (기본값) | InputDriver |
 * | term_info.hpp 단독 사용 | — | Terminal::enter_raw_mode() |
 * | 함께 사용 (권장) | true (기본값) | InputDriver |
 * | 함께 사용 (Terminal 이 먼저) | false | Terminal |
 *
 * ### 권장 패턴 (함께 사용)
 * ```cpp
 * term::Terminal term;
 * term.enter_alt_screen();
 * term.show_cursor(false);
 * // ↓ InputDriver 가 raw mode + SIGWINCH 소유 (충돌 없음)
 * auto input = term::make_input_driver();   // manage_raw_mode=true
 * input->setup();
 *
 * while (running) {
 *     auto ev = input->read_key(16);
 *     if (ev.key == KEY_RESIZE) renderer.handle_resize();
 *     // ...
 * }
 * // input 소멸: raw mode 복원
 * // term 소멸: alt screen, cursor 복원
 * ```
 */
struct InputOptions {
    /// true(기본): InputDriver 가 raw mode(termios/ConsoleMode) 를 직접 관리
    /// false      : Terminal::enter_raw_mode() 가 관리 (이미 설정된 경우)
    bool manage_raw_mode = true;

    /// true(기본): 마우스 추적 시퀀스 활성화 (POSIX SGR 프로토콜)
    bool enable_mouse    = true;
};

// ═══════════════════════════════════════════════════════════════════════════
//  InputDriver 추상 인터페이스
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief 플랫폼 독립 입력 드라이버 인터페이스
 *
 * ### read_key(timeout_ms) 의미
 * - `timeout_ms == 0` (기본값) : 블로킹
 * - `timeout_ms >  0`          : 최대 timeout_ms ms 대기, 타임아웃 시 KEY_NONE
 */
class InputDriver {
public:
    virtual ~InputDriver() = default;
    virtual void     setup()    {}
    virtual void     teardown() {}
    virtual KeyEvent read_key(int timeout_ms = 0) = 0;
};

// ═══════════════════════════════════════════════════════════════════════════
//  플랫폼별 구현
// ═══════════════════════════════════════════════════════════════════════════

#if defined(_WIN32) || defined(_WIN64)

// ───────────────────────────────────────────────────────────────────────────
//  Windows 구현
// ───────────────────────────────────────────────────────────────────────────

/**
 * @class WindowsInput
 * @brief Windows 콘솔 입력 드라이버 (ReadConsoleInputW)
 *
 * ### InputOptions::manage_raw_mode
 * - true (기본값): SetConsoleMode 로 INPUT 핸들 직접 설정/복원
 * - false: Terminal::enter_raw_mode() 가 이미 설정한 경우 (mode 변경 생략)
 *   단, Terminal::enter_raw_mode_impl() 이 ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT 을
 *   포함해야 마우스/크기변경 이벤트를 수신할 수 있다. (term_info.hpp 에 반영됨)
 */
class WindowsInput : public InputDriver {
public:
    explicit WindowsInput(InputOptions opts = {}) : opts_(opts) {}
    ~WindowsInput() override { teardown(); }

    void setup() override {
        stdin_handle_ = GetStdHandle(STD_INPUT_HANDLE);
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        if (opts_.manage_raw_mode) {
            // raw mode 소유: 현재 모드 저장 후 설정
            GetConsoleMode(stdin_handle_, &orig_mode_);
            DWORD mode = orig_mode_;
            mode |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
            mode &= ~ENABLE_PROCESSED_INPUT;
            SetConsoleMode(stdin_handle_, mode);
            mode_saved_ = true;
        } else {
            // raw mode 비소유: Terminal::enter_raw_mode_impl() 이 이미 설정함
            // ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT 포함되어 있다고 가정
            mode_saved_ = false;
        }
    }

    void teardown() override {
        if (mode_saved_ && stdin_handle_ != INVALID_HANDLE_VALUE) {
            SetConsoleMode(stdin_handle_, orig_mode_);
            mode_saved_ = false;
        }
    }

    KeyEvent read_key(int timeout_ms = 0) override {
        while (true) {
            if (timeout_ms > 0) {
                DWORD count = 0;
                GetNumberOfConsoleInputEvents(stdin_handle_, &count);
                if (count == 0) {
                    DWORD ret = WaitForSingleObject(stdin_handle_,
                                                    static_cast<DWORD>(timeout_ms));
                    if (ret == WAIT_TIMEOUT) return KeyEvent{};
                }
            }

            INPUT_RECORD rec{};
            DWORD n = 0;
            if (!ReadConsoleInputW(stdin_handle_, &rec, 1, &n) || n == 0)
                continue;

            if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown)
                return translate_key(rec.Event.KeyEvent);
            if (rec.EventType == MOUSE_EVENT)
                return translate_mouse(rec.Event.MouseEvent);
            if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                KeyEvent ke; ke.key = KEY_RESIZE; return ke;
            }
        }
    }

private:
    InputOptions opts_;
    HANDLE       stdin_handle_ = INVALID_HANDLE_VALUE;
    DWORD        orig_mode_    = 0;
    bool         mode_saved_   = false;

    KeyEvent translate_key(const KEY_EVENT_RECORD& ke) {
        uint8_t mod = Modifier::NONE;
        DWORD   state = ke.dwControlKeyState;
        if (state & (LEFT_CTRL_PRESSED  | RIGHT_CTRL_PRESSED)) mod |= Modifier::CTRL;
        if (state & (LEFT_ALT_PRESSED   | RIGHT_ALT_PRESSED))  mod |= Modifier::ALT;
        if (state & SHIFT_PRESSED)                              mod |= Modifier::SHIFT;

        auto kev    = [&](uint32_t k) { KeyEvent e; e.key=k; e.modifier=mod; return e; };
        auto kev_ch = [&](uint32_t k, char32_t c) { KeyEvent e; e.key=k; e.modifier=mod; e.ch=c; return e; };

        WORD vk = ke.wVirtualKeyCode;
        switch (vk) {
            case VK_UP:     return kev(KEY_UP);
            case VK_DOWN:   return kev(KEY_DOWN);
            case VK_LEFT:   return kev(KEY_LEFT);
            case VK_RIGHT:  return kev(KEY_RIGHT);
            case VK_HOME:   return kev(KEY_HOME);
            case VK_END:    return kev(KEY_END);
            case VK_PRIOR:  return kev(KEY_PGUP);
            case VK_NEXT:   return kev(KEY_PGDN);
            case VK_INSERT: return kev(KEY_INS);
            case VK_DELETE: return kev(KEY_DEL);
            case VK_BACK:   return kev(KEY_BACKSPACE);
            case VK_RETURN: return kev_ch(KEY_ENTER, U'\n');
            case VK_TAB:    return kev_ch(KEY_TAB,   U'\t');
            case VK_ESCAPE: return kev(KEY_ESC);
            case VK_F1:     return kev(KEY_F1);  case VK_F2:  return kev(KEY_F2);
            case VK_F3:     return kev(KEY_F3);  case VK_F4:  return kev(KEY_F4);
            case VK_F5:     return kev(KEY_F5);  case VK_F6:  return kev(KEY_F6);
            case VK_F7:     return kev(KEY_F7);  case VK_F8:  return kev(KEY_F8);
            case VK_F9:     return kev(KEY_F9);  case VK_F10: return kev(KEY_F10);
            case VK_F11:    return kev(KEY_F11); case VK_F12: return kev(KEY_F12);
            case VK_MENU:   return kev(KEY_MENU);
            default: break;
        }

        if (mod & Modifier::CTRL) {
            if (vk >= 'A' && vk <= 'Z') {
                unsigned char ctrl_byte = static_cast<unsigned char>(vk - 'A' + 1);
                uint32_t k = ctrl_to_key(ctrl_byte);
                if (k != KEY_NONE) return kev(k);
            }
        }

        wchar_t wc = ke.uChar.UnicodeChar;
        if (wc >= 0x20) {
            char32_t cp = static_cast<char32_t>(wc);
            KeyEvent e; e.key = static_cast<uint32_t>(cp); e.modifier = mod; e.ch = cp;
            return e;
        }
        return KeyEvent{};
    }

    KeyEvent translate_mouse(const MOUSE_EVENT_RECORD& mr) {
        MouseEvent me;
        me.x      = mr.dwMousePosition.X + 1;
        me.y      = mr.dwMousePosition.Y + 1;
        me.motion = (mr.dwEventFlags & MOUSE_MOVED) != 0;

        if (mr.dwEventFlags & MOUSE_WHEELED) {
            SHORT delta = static_cast<SHORT>(HIWORD(mr.dwButtonState));
            me.btn = (delta > 0) ? MouseBtn::WHEEL_UP : MouseBtn::WHEEL_DOWN;
        } else if (mr.dwButtonState & FROM_LEFT_1ST_BUTTON_PRESSED) {
            me.btn = MouseBtn::LEFT;
        } else if (mr.dwButtonState & RIGHTMOST_BUTTON_PRESSED) {
            me.btn = MouseBtn::RIGHT;
        } else if (mr.dwButtonState & FROM_LEFT_2ND_BUTTON_PRESSED) {
            me.btn = MouseBtn::MIDDLE;
        } else {
            me.btn = MouseBtn::RELEASE;
        }

        KeyEvent ke; ke.key = KEY_MOUSE; ke.mouse = me;
        return ke;
    }
};

#else

// ───────────────────────────────────────────────────────────────────────────
//  POSIX 구현 (Linux / macOS)
// ───────────────────────────────────────────────────────────────────────────

/// 마우스 추적 활성/비활성 시퀀스
static constexpr char MOUSE_ON[]  = "\x1b[?1000h\x1b[?1002h\x1b[?1006h";
static constexpr char MOUSE_OFF[] = "\x1b[?1000l\x1b[?1002l\x1b[?1006l";

// [FIX-CONFLICT] input_detail 네임스페이스 제거됨
// 기존: input_detail::g_sigwinch_flag (자체 플래그)
// 변경: shared_detail::g_sigwinch_flag (term_info 와 공유)
// SIGWINCH 핸들러 설치: shared_detail::ensure_sigwinch_handler() 사용

#define TERM_KEV(k_)             KeyEvent{ .key=(k_) }
#define TERM_KEV_M(k_, m_)       KeyEvent{ .key=(k_), .modifier=(m_) }
#define TERM_KEV_CH(k_, m_, c_)  KeyEvent{ .key=(k_), .modifier=(m_), .ch=(c_) }

/**
 * @class PosixInput
 * @brief POSIX 터미널 입력 드라이버 (termios raw 모드 + ESC 시퀀스 파싱)
 *
 * ### term_info.hpp 와 충돌 해결 [FIX-CONFLICT]
 *
 * | 항목 | 변경 전 | 변경 후 |
 * |------|---------|---------|
 * | SIGWINCH 플래그 | input_detail::g_sigwinch_flag | shared_detail::g_sigwinch_flag |
 * | 핸들러 설치 | install_sigwinch_handler() (always) | ensure_sigwinch_handler() (once) |
 * | SA_RESTART | 항상 없음 | 항상 없음 (shared 핸들러도 동일) |
 * | Raw mode | 항상 tcsetattr | InputOptions::manage_raw_mode 에 따라 선택 |
 *
 * ### 사용 패턴
 * ```cpp
 * // 패턴 A: 단독 사용
 * auto input = make_input_driver();   // manage_raw_mode=true
 * input->setup();
 *
 * // 패턴 B: Terminal 과 함께 (raw mode 는 InputDriver 소유 — 권장)
 * term::Terminal term;
 * term.enter_alt_screen();
 * auto input = make_input_driver();   // manage_raw_mode=true
 * input->setup();                     // raw mode 설정 (Terminal 은 enter_raw_mode 안 함)
 *
 * // 패턴 C: Terminal::enter_raw_mode() 를 먼저 호출한 경우
 * term::Terminal term;
 * term.enter_raw_mode();              // Terminal 이 raw mode 소유
 * term::InputOptions opts; opts.manage_raw_mode = false;
 * auto input = make_input_driver(opts);
 * input->setup();                     // SIGWINCH 만 설치
 * ```
 */
class PosixInput : public InputDriver {
public:
    explicit PosixInput(InputOptions opts = {}) : opts_(opts) {}
    ~PosixInput() override { teardown(); }

    /**
     * @brief 터미널 raw 모드 전환 + 마우스 추적 활성화 + SIGWINCH 등록.
     *
     * InputOptions::manage_raw_mode == false 인 경우 tcsetattr 를 건너뛴다.
     * SIGWINCH 핸들러는 manage_raw_mode 와 무관하게 항상 설치한다.
     */
    void setup() override {
        if (opts_.manage_raw_mode) {
            // raw mode 소유: 기존 설정 저장 후 raw mode 전환
            if (raw_active_) return;
            tcgetattr(STDIN_FILENO, &orig_);
            struct termios raw = orig_;
            raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
            raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
            raw.c_cflag |=  static_cast<tcflag_t>(CS8);
            raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
            raw.c_cc[VMIN]  = 1;
            raw.c_cc[VTIME] = 0;
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
            raw_active_ = true;
        }
        // else: Terminal::enter_raw_mode() 가 이미 raw mode 설정함

        // [FIX-CONFLICT] 공유 SIGWINCH 핸들러 설치 (중복 설치 방지, SA_RESTART 없음)
        // 기존: install_sigwinch_handler() (자체 핸들러)
        // 변경: ensure_sigwinch_handler() (shared_detail 공유)
        shared_detail::ensure_sigwinch_handler();

        if (opts_.enable_mouse) {
            auto r = ::write(STDOUT_FILENO, MOUSE_ON, sizeof(MOUSE_ON) - 1);
            (void)r;
            mouse_enabled_ = true;
        }
    }

    /**
     * @brief 터미널 원상 복원 + 마우스 추적 비활성화.
     */
    void teardown() override {
        if (mouse_enabled_) {
            auto r = ::write(STDOUT_FILENO, MOUSE_OFF, sizeof(MOUSE_OFF) - 1);
            (void)r;
            mouse_enabled_ = false;
        }
        if (raw_active_ && opts_.manage_raw_mode) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
            raw_active_ = false;
        }
    }

    /**
     * @brief 키/마우스/리사이즈 이벤트를 수신한다.
     *
     * [FIX-CONFLICT] 공유 플래그 shared_detail::g_sigwinch_flag 확인
     * 기존 input_detail::g_sigwinch_flag → shared_detail::g_sigwinch_flag
     */
    KeyEvent read_key(int timeout_ms = 0) override {
        while (true) {
            // 1. [FIX-CONFLICT] 공유 SIGWINCH 플래그 확인
            if (shared_detail::g_sigwinch_flag.exchange(false))
                return TERM_KEV(KEY_RESIZE);

            // 2. timeout 처리
            if (timeout_ms > 0) {
                int sel = wait_readable(timeout_ms);
                if (sel == 0) return TERM_KEV(KEY_NONE);
                if (sel < 0) {
                    if (shared_detail::g_sigwinch_flag.exchange(false))
                        return TERM_KEV(KEY_RESIZE);
                    return TERM_KEV(KEY_NONE);
                }
            }

            // 3. 1바이트 read
            unsigned char c = 0;
            ssize_t n = ::read(STDIN_FILENO, &c, 1);

            if (n < 0) {
                if (errno == EINTR) continue;  // SIGWINCH → 루프 상단에서 감지
                return TERM_KEV(KEY_NONE);
            }
            if (n == 0) continue;

            // 4~6. 바이트 파싱
            if (c == 0x1B)             return parse_escape();
            if (c < 0x20 || c == 0x7F) return parse_ctrl(c);
            return parse_utf8(c);
        }
    }

private:
    InputOptions opts_;
    termios      orig_{};
    bool         raw_active_    = false;
    bool         mouse_enabled_ = false;

    int wait_readable(int timeout_ms) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        struct timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        return ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
    }

    int read_timeout(unsigned char& out, int timeout_ms = 100) {
        int sel = wait_readable(timeout_ms);
        if (sel <= 0) return sel;
        ssize_t n = ::read(STDIN_FILENO, &out, 1);
        if (n < 0 && errno == EINTR) return -1;
        return static_cast<int>(n);
    }

    KeyEvent parse_escape() {
        unsigned char c2 = 0;
        if (read_timeout(c2) <= 0) return TERM_KEV(KEY_ESC);

        if (c2 == '[') return parse_csi();

        if (c2 == 'O') {
            unsigned char c3 = 0;
            if (read_timeout(c3) > 0) {
                switch (c3) {
                    case 'P': return TERM_KEV(KEY_F1);
                    case 'Q': return TERM_KEV(KEY_F2);
                    case 'R': return TERM_KEV(KEY_F3);
                    case 'S': return TERM_KEV(KEY_F4);
                    case 'H': return TERM_KEV(KEY_HOME);
                    case 'F': return TERM_KEV(KEY_END);
                    default:  break;
                }
            }
        }

        if (c2 >= 0x20 && c2 < 0x7F)
            return TERM_KEV_CH(static_cast<uint32_t>(c2),
                               Modifier::ALT,
                               static_cast<char32_t>(c2));
        return TERM_KEV(KEY_ESC);
    }

    KeyEvent parse_csi() {
        char buf[32]{};
        int  len = 0;
        unsigned char c = 0;

        while (len < 31 && read_timeout(c) > 0) {
            buf[len++] = static_cast<char>(c);
            if (c >= 0x40 && c <= 0x7E) break;
        }
        if (len == 0) return TERM_KEV(KEY_ESC);

        char        final_ch = buf[len - 1];
        std::string params(buf, static_cast<std::size_t>(len - 1));

        // SGR 마우스
        if (!params.empty() && params[0] == '<') {
            std::string p = params.substr(1);
            int cb = 0, cx = 0, cy = 0;
            auto p1 = p.find(';');
            auto p2 = (p1 != std::string::npos) ? p.find(';', p1 + 1) : std::string::npos;
            if (p1 != std::string::npos && p2 != std::string::npos) {
                cb = std::stoi(p.substr(0, p1));
                cx = std::stoi(p.substr(p1 + 1, p2 - p1 - 1));
                cy = std::stoi(p.substr(p2 + 1));
            }
            MouseEvent me;
            me.x = cx; me.y = cy; me.motion = (cb & 32) != 0;
            int btn_raw = cb & ~32;
            if      (btn_raw == 64)   me.btn = MouseBtn::WHEEL_UP;
            else if (btn_raw == 65)   me.btn = MouseBtn::WHEEL_DOWN;
            else if (final_ch == 'm') me.btn = MouseBtn::RELEASE;
            else {
                switch (btn_raw & 3) {
                    case 0: me.btn = MouseBtn::LEFT;   break;
                    case 1: me.btn = MouseBtn::MIDDLE; break;
                    case 2: me.btn = MouseBtn::RIGHT;  break;
                    default: me.btn = MouseBtn::RELEASE;
                }
            }
            KeyEvent ke; ke.key = KEY_MOUSE; ke.mouse = me;
            return ke;
        }

        uint8_t mod  = Modifier::NONE;
        auto    semi = params.find(';');
        if (semi != std::string::npos) {
            int mod_num = std::stoi(params.substr(semi + 1)) - 1;
            if (mod_num & 1) mod |= Modifier::SHIFT;
            if (mod_num & 2) mod |= Modifier::ALT;
            if (mod_num & 4) mod |= Modifier::CTRL;
            params = params.substr(0, semi);
        }
        int num = params.empty() ? 1 : std::stoi(params);

        switch (final_ch) {
            case 'A': return TERM_KEV_M(KEY_UP,    mod);
            case 'B': return TERM_KEV_M(KEY_DOWN,  mod);
            case 'C': return TERM_KEV_M(KEY_RIGHT, mod);
            case 'D': return TERM_KEV_M(KEY_LEFT,  mod);
            case 'H': return TERM_KEV_M(KEY_HOME,  mod);
            case 'F': return TERM_KEV_M(KEY_END,   mod);
            case 'P': return TERM_KEV_M(KEY_F1,    mod);
            case 'Q': return TERM_KEV_M(KEY_F2,    mod);
            case 'R': return TERM_KEV_M(KEY_F3,    mod);
            case 'S': return TERM_KEV_M(KEY_F4,    mod);
            case '~':
                switch (num) {
                    case 1:  return TERM_KEV_M(KEY_HOME, mod);
                    case 2:  return TERM_KEV_M(KEY_INS,  mod);
                    case 3:  return TERM_KEV_M(KEY_DEL,  mod);
                    case 4:  return TERM_KEV_M(KEY_END,  mod);
                    case 5:  return TERM_KEV_M(KEY_PGUP, mod);
                    case 6:  return TERM_KEV_M(KEY_PGDN, mod);
                    case 11: return TERM_KEV_M(KEY_F1,   mod);
                    case 12: return TERM_KEV_M(KEY_F2,   mod);
                    case 13: return TERM_KEV_M(KEY_F3,   mod);
                    case 14: return TERM_KEV_M(KEY_F4,   mod);
                    case 15: return TERM_KEV_M(KEY_F5,   mod);
                    case 17: return TERM_KEV_M(KEY_F6,   mod);
                    case 18: return TERM_KEV_M(KEY_F7,   mod);
                    case 19: return TERM_KEV_M(KEY_F8,   mod);
                    case 20: return TERM_KEV_M(KEY_F9,   mod);
                    case 21: return TERM_KEV_M(KEY_F10,  mod);
                    case 23: return TERM_KEV_M(KEY_F11,  mod);
                    case 24: return TERM_KEV_M(KEY_F12,  mod);
                    default: break;
                }
                break;
            default: break;
        }
        return TERM_KEV(KEY_NONE);
    }

    KeyEvent parse_ctrl(unsigned char c) {
        if (c == 0x7F || c == 0x08) return TERM_KEV(KEY_BACKSPACE);
        if (c == 0x0D || c == 0x0A) return TERM_KEV_CH(KEY_ENTER, Modifier::NONE, U'\n');
        if (c == 0x09)               return TERM_KEV_CH(KEY_TAB,   Modifier::NONE, U'\t');
        uint32_t k = ctrl_to_key(c);
        if (k != KEY_NONE) return TERM_KEV_M(k, Modifier::CTRL);
        char32_t letter = static_cast<char32_t>(c + 0x40);
        return TERM_KEV_CH(static_cast<uint32_t>(letter), Modifier::CTRL, letter);
    }

    KeyEvent parse_utf8(unsigned char c) {
        char buf[4] = { static_cast<char>(c), 0, 0, 0 };
        int  seq_len = 1;
        if      ((c & 0xE0) == 0xC0) seq_len = 2;
        else if ((c & 0xF0) == 0xE0) seq_len = 3;
        else if ((c & 0xF8) == 0xF0) seq_len = 4;

        for (int i = 1; i < seq_len; ++i) {
            unsigned char b = 0;
            if (::read(STDIN_FILENO, &b, 1) <= 0) break;
            buf[i] = static_cast<char>(b);
        }

        char32_t cp = static_cast<unsigned char>(buf[0]);
        if (seq_len == 2)
            cp = ((cp & 0x1F) << 6) | (static_cast<unsigned char>(buf[1]) & 0x3F);
        else if (seq_len == 3)
            cp = ((cp & 0x0F) << 12) | ((static_cast<unsigned char>(buf[1]) & 0x3F) << 6) | (static_cast<unsigned char>(buf[2]) & 0x3F);
        else if (seq_len == 4)
            cp = ((cp & 0x07) << 18) | ((static_cast<unsigned char>(buf[1]) & 0x3F) << 12) | ((static_cast<unsigned char>(buf[2]) & 0x3F) << 6) | (static_cast<unsigned char>(buf[3]) & 0x3F);

        return TERM_KEV_CH(static_cast<uint32_t>(cp), Modifier::NONE, cp);
    }
};

#undef TERM_KEV
#undef TERM_KEV_M
#undef TERM_KEV_CH

#endif // _WIN32 / POSIX

// ═══════════════════════════════════════════════════════════════════════════
//  팩토리
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief 플랫폼에 맞는 InputDriver를 생성한다.
 *
 * @param opts 소유권 옵션 (manage_raw_mode, enable_mouse)
 * @return unique_ptr<InputDriver>
 *
 * ### 사용 예시
 * ```cpp
 * // 기본: InputDriver 가 raw mode 와 SIGWINCH 모두 소유
 * auto input = term::make_input_driver();
 *
 * // Terminal 이 raw mode 를 먼저 설정한 경우
 * term::InputOptions opts;
 * opts.manage_raw_mode = false;
 * auto input = term::make_input_driver(opts);
 *
 * // 마우스 비활성화
 * term::InputOptions opts;
 * opts.enable_mouse = false;
 * auto input = term::make_input_driver(opts);
 * ```
 */
inline std::unique_ptr<InputDriver> make_input_driver(InputOptions opts = {}) {
#if defined(_WIN32) || defined(_WIN64)
    return std::make_unique<WindowsInput>(opts);
#else
    return std::make_unique<PosixInput>(opts);
#endif
}

} // namespace term