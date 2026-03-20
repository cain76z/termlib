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
 *           SA_RESTART 제거 → SIGWINCH 즉시 감지 (resize 반응성 개선)
 * - [BUG-4] POSIX: 0x08(Ctrl+H) Backspace 처리 추가
 * - [OPT-1] POSIX: read_timeout() → select() 기반으로 교체
 *           (tcsetattr/tcgetattr 시스템 콜 오버헤드 제거)
 * - [FEAT-1] read_key(timeout_ms) 오버로드 추가
 *            timeout_ms == 0 → 블로킹 (기존 동작 유지)
 *            timeout_ms >  0 → 최대 timeout_ms ms 대기 후 KEY_NONE 반환
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
#  include <sys/select.h>   // [OPT-1] select() 기반 타임아웃
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

        // ── 이동 / 편집 키 ──────────────────────────────────────────────────
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

        // ── 펑션 키 ─────────────────────────────────────────────────────────
        KEY_F1  = 0x1100,
        KEY_F2,  KEY_F3,  KEY_F4,  KEY_F5,
        KEY_F6,  KEY_F7,  KEY_F8,  KEY_F9,
        KEY_F10, KEY_F11, KEY_F12,
        KEY_MENU,   ///< 메뉴/애플리케이션 키 (VK_MENU / Alt 단독)

        // ── Ctrl + 알파벳 ────────────────────────────────────────────────────
        // Ctrl+H(0x08)/I(Tab)/J(LF)/M(CR) 은 각각 BACKSPACE/TAB/ENTER 로 처리
        KEY_CTRL_A = 0x1200,
        KEY_CTRL_B, KEY_CTRL_C, KEY_CTRL_D, KEY_CTRL_E,
        KEY_CTRL_F, KEY_CTRL_G,
        KEY_CTRL_K, KEY_CTRL_L, KEY_CTRL_N, KEY_CTRL_O,
        KEY_CTRL_P, KEY_CTRL_Q, KEY_CTRL_R, KEY_CTRL_S,
        KEY_CTRL_T, KEY_CTRL_U, KEY_CTRL_V, KEY_CTRL_W,
        KEY_CTRL_X, KEY_CTRL_Y, KEY_CTRL_Z,

        // ── 시스템 이벤트 ────────────────────────────────────────────────────
        KEY_RESIZE  = 0x1300,   ///< 터미널 크기 변경 (SIGWINCH / WINDOW_BUFFER_SIZE_EVENT)
        KEY_MOUSE   = 0x1400,   ///< 마우스 이벤트 → KeyEvent::mouse 참조
    };

    /**
     * @brief Ctrl 바이트(0x01~0x1A) → KEY_CTRL_* 변환.
     * 0x08(Ctrl+H/Backspace), 0x09(Tab), 0x0A/0x0D(Enter) 는 KEY_NONE 반환.
     * 호출자가 parse_ctrl() 에서 별도 처리한다.
     */
    constexpr uint32_t ctrl_to_key(unsigned char c) noexcept {
        switch (c) {
            case 0x01: return KEY_CTRL_A;  case 0x02: return KEY_CTRL_B;
            case 0x03: return KEY_CTRL_C;  case 0x04: return KEY_CTRL_D;
            case 0x05: return KEY_CTRL_E;  case 0x06: return KEY_CTRL_F;
            case 0x07: return KEY_CTRL_G;
            // 0x08(Backspace), 0x09(Tab), 0x0A(LF), 0x0D(CR) — parse_ctrl 에서 처리
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
        int      x      = 0;   ///< 열 (1-based)
        int      y      = 0;   ///< 행 (1-based)
        bool     motion = false;
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  KeyEvent
    // ═══════════════════════════════════════════════════════════════════════════

    struct KeyEvent {
        uint32_t   key      = KEY_NONE;
        uint8_t    modifier = Modifier::NONE;
        char32_t   ch       = 0;       ///< 출력 가능 문자 (UTF-32), 0이면 없음
        MouseEvent mouse    = {};      ///< key==KEY_MOUSE 일 때 유효

        bool is_printable() const noexcept { return ch >= 0x20 && ch != 0x7F; }
        bool has_ctrl()     const noexcept { return (modifier & Modifier::CTRL)  != 0; }
        bool has_alt()      const noexcept { return (modifier & Modifier::ALT)   != 0; }
        bool has_shift()    const noexcept { return (modifier & Modifier::SHIFT) != 0; }
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  InputDriver 추상 인터페이스
    // ═══════════════════════════════════════════════════════════════════════════

    /**
     * @brief 플랫폼 독립 입력 드라이버 인터페이스
     *
     * ### read_key(timeout_ms) 의미
     * - `timeout_ms == 0` (기본값) : 블로킹 — 이벤트가 올 때까지 대기
     * - `timeout_ms >  0`          : 최대 timeout_ms 밀리초 대기,
     *                                타임아웃 시 `KeyEvent{KEY_NONE}` 반환
     *
     * ### 사용 패턴 예시
     * ```cpp
     * // 블로킹 이벤트 루프
     * while (true) {
     *     auto ev = input->read_key();   // 키 올 때까지 대기
     *     if (ev.key == KEY_ESC) break;
     * }
     *
     * // 논블로킹 poll 루프 (애니메이션 + 입력 병행)
     * while (true) {
     *     auto ev = input->read_key(16); // 최대 16ms 대기 (≈60fps)
     *     if (ev.key == KEY_NONE) {
     *         spinner.tick();            // 타임아웃: 스피너 갱신
     *     } else if (ev.key == KEY_ESC) {
     *         break;
     *     }
     *     renderer.render();
     * }
     * ```
     */
    class InputDriver {
    public:
        virtual ~InputDriver() = default;
        virtual void     setup()    {}
        virtual void     teardown() {}

        /**
         * @brief 키/마우스/리사이즈 이벤트 수신
         * @param timeout_ms  0 = 블로킹, >0 = 타임아웃(ms), 타임아웃 시 KEY_NONE 반환
         */
        virtual KeyEvent read_key(int timeout_ms = 0) = 0;
    };

    // ═══════════════════════════════════════════════════════════════════════════
    //  플랫폼별 구현
    // ═══════════════════════════════════════════════════════════════════════════

    // ───────────────────────────────────────────────────────────────────────────
    //  Windows 구현
    // ───────────────────────────────────────────────────────────────────────────
    #if defined(_WIN32) || defined(_WIN64)

    /**
     * @class WindowsInput
     * @brief Windows 콘솔 입력 드라이버 (ReadConsoleInputW)
     */
    class WindowsInput : public InputDriver {
    public:
        WindowsInput()  = default;
        ~WindowsInput() override { teardown(); }

        /**
         * @brief 콘솔 입력 모드 설정.
         * ENABLE_WINDOW_INPUT, ENABLE_MOUSE_INPUT 활성화.
         * ENABLE_PROCESSED_INPUT 비활성화(Ctrl+C 직접 수신).
         */
        void setup() override {
            stdin_handle_ = GetStdHandle(STD_INPUT_HANDLE);
            GetConsoleMode(stdin_handle_, &orig_mode_);
            DWORD mode = ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
            SetConsoleMode(stdin_handle_, mode);
            SetConsoleOutputCP(CP_UTF8);
            SetConsoleCP(CP_UTF8);
        }

        void teardown() override {
            if (stdin_handle_ != INVALID_HANDLE_VALUE)
                SetConsoleMode(stdin_handle_, orig_mode_);
        }

        /**
         * @brief 키/마우스/리사이즈 이벤트를 수신한다.
         *
         * @param timeout_ms  0 = 블로킹, >0 = WaitForSingleObject 타임아웃
         *
         * 처리 이벤트:
         * - KEY_EVENT            → translate_key()
         * - MOUSE_EVENT          → translate_mouse()  [BUG-2 수정]
         * - WINDOW_BUFFER_SIZE_EVENT → KEY_RESIZE
         */
        KeyEvent read_key(int timeout_ms = 0) override {
            while (true) {
                // [FEAT-1] timeout_ms > 0 이면 WaitForSingleObject 로 대기
                if (timeout_ms > 0) {
                    DWORD count = 0;
                    GetNumberOfConsoleInputEvents(stdin_handle_, &count);
                    if (count == 0) {
                        DWORD ret = WaitForSingleObject(stdin_handle_,
                                                        static_cast<DWORD>(timeout_ms));
                        if (ret == WAIT_TIMEOUT) return KeyEvent{};  // KEY_NONE
                    }
                }

                INPUT_RECORD rec{};
                DWORD n = 0;
                ReadConsoleInputW(stdin_handle_, &rec, 1, &n);

                if (rec.EventType == KEY_EVENT &&
                    rec.Event.KeyEvent.bKeyDown)
                    return translate_key(rec.Event.KeyEvent);

                // [BUG-2 수정] MOUSE_EVENT 처리 — 기존에 누락되어 이벤트가 소멸됨
                if (rec.EventType == MOUSE_EVENT)
                    return translate_mouse(rec.Event.MouseEvent);

                if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                    KeyEvent ke;
                    ke.key = KEY_RESIZE;
                    return ke;
                }

                // 타임아웃 모드에서 처리할 이벤트가 없으면 KEY_NONE 반환
                if (timeout_ms > 0) return KeyEvent{};
            }
        }

    private:
        HANDLE stdin_handle_ = INVALID_HANDLE_VALUE;
        DWORD  orig_mode_    = 0;

        // ── 키 변환 ─────────────────────────────────────────────────────────

        KeyEvent translate_key(const KEY_EVENT_RECORD& ke) {
            uint8_t mod = Modifier::NONE;
            DWORD   state = ke.dwControlKeyState;
            if (state & (LEFT_CTRL_PRESSED  | RIGHT_CTRL_PRESSED)) mod |= Modifier::CTRL;
            if (state & (LEFT_ALT_PRESSED   | RIGHT_ALT_PRESSED))  mod |= Modifier::ALT;
            if (state & SHIFT_PRESSED)                              mod |= Modifier::SHIFT;

            auto kev = [&](uint32_t k) {
                KeyEvent e; e.key = k; e.modifier = mod; return e;
            };
            auto kev_ch = [&](uint32_t k, char32_t c) {
                KeyEvent e; e.key = k; e.modifier = mod; e.ch = c; return e;
            };

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
                case VK_F1:     return kev(KEY_F1);
                case VK_F2:     return kev(KEY_F2);
                case VK_F3:     return kev(KEY_F3);
                case VK_F4:     return kev(KEY_F4);
                case VK_F5:     return kev(KEY_F5);
                case VK_F6:     return kev(KEY_F6);
                case VK_F7:     return kev(KEY_F7);
                case VK_F8:     return kev(KEY_F8);
                case VK_F9:     return kev(KEY_F9);
                // [BUG-1 수정] VK_F10 → KEY_F10 (기존: KEY_MENU 오매핑)
                case VK_F10:    return kev(KEY_F10);
                case VK_F11:    return kev(KEY_F11);
                case VK_F12:    return kev(KEY_F12);
                // [BUG-1 수정] VK_MENU(Alt 단독) → KEY_MENU 추가
                case VK_MENU:   return kev(KEY_MENU);
                default: break;
            }

            // Ctrl + 알파벳 → KEY_CTRL_*
            if (mod & Modifier::CTRL) {
                if (vk >= 'A' && vk <= 'Z') {
                    unsigned char ctrl_byte = static_cast<unsigned char>(vk - 'A' + 1);
                    uint32_t k = ctrl_to_key(ctrl_byte);
                    if (k != KEY_NONE) return kev(k);
                }
            }

            // 일반 문자 (UnicodeChar)
            wchar_t wc = ke.uChar.UnicodeChar;
            if (wc >= 0x20) {
                char32_t cp = static_cast<char32_t>(wc);
                KeyEvent e;
                e.key      = static_cast<uint32_t>(cp);
                e.modifier = mod;
                e.ch       = cp;
                return e;
            }

            return KeyEvent{};  // KEY_NONE
        }

        // ── 마우스 변환 [BUG-2 수정] ────────────────────────────────────────

        /**
         * @brief MOUSE_EVENT_RECORD → KeyEvent(KEY_MOUSE) 변환.
         *
         * 버튼 매핑:
         * - FROM_LEFT_1ST_BUTTON_PRESSED → LEFT
         * - RIGHTMOST_BUTTON_PRESSED     → RIGHT
         * - FROM_LEFT_2ND_BUTTON_PRESSED → MIDDLE
         * - MOUSE_WHEELED (양수)         → WHEEL_UP
         * - MOUSE_WHEELED (음수)         → WHEEL_DOWN
         * - 버튼 없음                    → RELEASE
         */
        KeyEvent translate_mouse(const MOUSE_EVENT_RECORD& mr) {
            MouseEvent me;
            // Windows 좌표는 0-based, SGR은 1-based 로 맞춤
            me.x      = mr.dwMousePosition.X + 1;
            me.y      = mr.dwMousePosition.Y + 1;
            me.motion = (mr.dwEventFlags & MOUSE_MOVED) != 0;

            if (mr.dwEventFlags & MOUSE_WHEELED) {
                // 상위 16비트가 델타값: 양수=위, 음수=아래
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

            KeyEvent ke;
            ke.key   = KEY_MOUSE;
            ke.mouse = me;
            return ke;
        }
    };

    // ───────────────────────────────────────────────────────────────────────────
    //  POSIX 구현 (Linux / macOS)
    // ───────────────────────────────────────────────────────────────────────────
    #else

    /// 마우스 추적 활성/비활성 시퀀스 (xterm SGR 마우스 프로토콜)
    static constexpr char MOUSE_ON[]  = "\x1b[?1000h\x1b[?1002h\x1b[?1006h";
    static constexpr char MOUSE_OFF[] = "\x1b[?1000l\x1b[?1002l\x1b[?1006l";

    namespace input_detail {
        /// SIGWINCH 수신 플래그 — signal handler 안에서만 쓰이므로 atomic
        inline std::atomic<bool> g_sigwinch_flag{false};

        /**
         * @brief SIGWINCH 핸들러 설치
         *
         * [BUG-3 수정] signal() → sigaction() 변경
         * [OPT-1]     SA_RESTART 제거 — SIGWINCH 즉시 감지를 위해
         *
         * SA_RESTART를 제거하면 SIGWINCH 수신 시 read()가 EINTR로 중단된다.
         * read_key() 루프에서 EINTR → 플래그 확인 → KEY_RESIZE 즉시 반환.
         */
        inline void install_sigwinch_handler() {
            struct sigaction sa{};
            sa.sa_handler = [](int) noexcept { g_sigwinch_flag.store(true); };
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;  // [OPT-1] SA_RESTART 제거: read() 인터럽트 허용
            ::sigaction(SIGWINCH, &sa, nullptr);
        }
    } // namespace input_detail

    // 편의 매크로 (함수 내부에서만 사용, 파일 하단에서 해제)
    #define TERM_KEV(k_)             KeyEvent{ .key=(k_) }
    #define TERM_KEV_M(k_, m_)       KeyEvent{ .key=(k_), .modifier=(m_) }
    #define TERM_KEV_CH(k_, m_, c_)  KeyEvent{ .key=(k_), .modifier=(m_), .ch=(c_) }

    /**
     * @class PosixInput
     * @brief POSIX 터미널 입력 드라이버 (termios raw 모드 + ESC 시퀀스 파싱)
     *
     * ## Raw mode 설정
     * - VMIN=1, VTIME=0: read()가 최소 1바이트가 올 때까지 블로킹
     * - ISIG 비활성화: Ctrl+C/Z를 시그널로 올리지 않고 이벤트로 전달
     *
     * ## Terminal::enter_raw_mode() 와 동시 사용 금지
     * PosixInput::setup()이 직접 termios를 설정하므로
     * Terminal::enter_raw_mode()와 동시에 사용하면 복원 순서가 꼬입니다.
     * 둘 중 하나만 사용하세요.
     *
     * ## SIGWINCH 처리
     * [OPT-1] SA_RESTART 제거로 SIGWINCH → read() EINTR → 즉시 KEY_RESIZE 반환.
     * 리사이즈는 read_key() → KEY_RESIZE 이벤트로 처리하거나,
     * Terminal::on_resize() + poll_resize()로 처리하세요 (둘 중 하나만).
     *
     * ## read_key(timeout_ms) 사용
     * - 블로킹: `read_key()` 또는 `read_key(0)`
     * - 논블로킹 poll: `read_key(N)` — N ms 안에 이벤트 없으면 KEY_NONE 반환
     *
     * [OPT-1] 내부 타임아웃은 select()로 구현하므로 tcsetattr 오버헤드 없음.
     */
    class PosixInput : public InputDriver {
    public:
        PosixInput()  = default;
        ~PosixInput() override { teardown(); }

        /**
         * @brief 터미널 raw 모드 전환 + 마우스 추적 활성화 + SIGWINCH 등록.
         */
        void setup() override {
            if (raw_active_) return;
            tcgetattr(STDIN_FILENO, &orig_);
            struct termios raw = orig_;
            raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
            raw.c_oflag &= ~static_cast<tcflag_t>(OPOST);
            raw.c_cflag |=  static_cast<tcflag_t>(CS8);
            raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN | ISIG);
            raw.c_cc[VMIN]  = 1;   // 최소 1바이트 수신 후 반환 (blocking)
            raw.c_cc[VTIME] = 0;   // 타임아웃 없음
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
            raw_active_ = true;

            // [OPT-1] SA_RESTART 제거 (기존 BUG-3에서 SA_RESTART 사용 → 개선)
            input_detail::install_sigwinch_handler();

            // 마우스 추적 활성화 (xterm SGR 프로토콜)
            { auto r = ::write(STDOUT_FILENO, MOUSE_ON, sizeof(MOUSE_ON) - 1); (void)r; }
        }

        /**
         * @brief 터미널 원상 복원 + 마우스 추적 비활성화.
         */
        void teardown() override {
            if (!raw_active_) return;
            { auto r = ::write(STDOUT_FILENO, MOUSE_OFF, sizeof(MOUSE_OFF) - 1); (void)r; }
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
            raw_active_ = false;
        }

        /**
         * @brief 키/마우스/리사이즈 이벤트를 수신한다.
         *
         * @param timeout_ms  0 = 블로킹, >0 = 최대 timeout_ms ms 대기
         *
         * 처리 순서:
         * 1. SIGWINCH 플래그 → KEY_RESIZE 즉시 반환
         * 2. timeout_ms > 0 이면 select()로 대기 (입력 없으면 KEY_NONE)
         * 3. 1바이트 read (EINTR 시 루프 재시작 — SIGWINCH 처리)
         * 4. ESC(0x1B)           → parse_escape()
         * 5. 제어 문자 / DEL     → parse_ctrl()
         * 6. 그 외 UTF-8 바이트  → parse_utf8()
         *
         * [OPT-1] SA_RESTART 제거로 SIGWINCH 수신 시 read()가 EINTR을 반환하고
         *         루프 상단에서 플래그를 확인하여 KEY_RESIZE를 즉시 반환한다.
         */
        KeyEvent read_key(int timeout_ms = 0) override {
            while (true) {
                // 1. SIGWINCH 플래그 확인 (atomic exchange → false 초기화)
                if (input_detail::g_sigwinch_flag.exchange(false))
                    return TERM_KEV(KEY_RESIZE);

                // 2. [FEAT-1] timeout_ms > 0: select()로 입력 가용성 확인
                if (timeout_ms > 0) {
                    int sel = wait_readable(timeout_ms);
                    if (sel == 0) return TERM_KEV(KEY_NONE);  // 타임아웃
                    if (sel < 0) {
                        // select() 중단 → SIGWINCH 플래그 재확인
                        if (input_detail::g_sigwinch_flag.exchange(false))
                            return TERM_KEV(KEY_RESIZE);
                        return TERM_KEV(KEY_NONE);
                    }
                }

                // 3. 1바이트 read
                unsigned char c = 0;
                ssize_t n = ::read(STDIN_FILENO, &c, 1);

                if (n < 0) {
                    if (errno == EINTR) {
                        // [OPT-1] SA_RESTART 제거로 SIGWINCH가 read()를 중단시킴
                        // 루프 상단으로 돌아가 플래그 확인
                        continue;
                    }
                    return TERM_KEV(KEY_NONE);
                }
                if (n == 0) continue;  // 드문 경우 (EOF 없는 재시도)

                // 4~6. 바이트 종류에 따라 파싱
                if (c == 0x1B)             return parse_escape();
                if (c < 0x20 || c == 0x7F) return parse_ctrl(c);
                return parse_utf8(c);
            }
        }

    private:
        termios orig_{};
        bool    raw_active_ = false;

        // ── [OPT-1] select() 기반 대기 ─────────────────────────────────────

        /**
         * @brief STDIN_FILENO에 읽을 수 있는 데이터가 생길 때까지 대기한다.
         *
         * tcsetattr/tcgetattr 없이 순수 select()만 사용하므로
         * 기존 termios 설정에 영향을 주지 않는다.
         *
         * @param timeout_ms  최대 대기 시간 (밀리초, 1 이상)
         * @return >0 = 데이터 가용, 0 = 타임아웃, <0 = 인터럽트/오류
         */
        int wait_readable(int timeout_ms) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);

            struct timeval tv;
            tv.tv_sec  = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;

            return ::select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        }

        // ── ESC 시퀀스 내부 타임아웃 읽기 ───────────────────────────────────

        /**
         * @brief ESC 시퀀스 연속 바이트를 타임아웃과 함께 1바이트 읽는다.
         *
         * [OPT-1] 기존의 tcsetattr/tcgetattr 방식을 select() 기반으로 교체.
         * select()로 입력 가용성을 확인하고, 가용 시 read()로 수신한다.
         * termios를 건드리지 않아 성능과 안전성이 모두 향상된다.
         *
         * @param out         읽은 바이트 저장 대상
         * @param timeout_ms  최대 대기 시간 (ms, 기본 100ms)
         * @return >0 = 읽은 바이트 수, 0 = 타임아웃, <0 = 오류/인터럽트
         */
        int read_timeout(unsigned char& out, int timeout_ms = 100) {
            int sel = wait_readable(timeout_ms);
            if (sel <= 0) return sel;   // 0=타임아웃, <0=오류

            ssize_t n = ::read(STDIN_FILENO, &out, 1);
            if (n < 0 && errno == EINTR) return -1;
            return static_cast<int>(n);
        }

        // ── ESC 시퀀스 파싱 ─────────────────────────────────────────────────

        /**
         * @brief ESC(0x1B) 시작 시퀀스 파싱.
         *
         * - 100ms 안에 다음 바이트 없음 → KEY_ESC (단독 ESC)
         * - ESC [   → parse_csi()     (방향키, Fn, 마우스 등)
         * - ESC O   → SS3 시퀀스     (F1-F4, Home, End — 일부 터미널)
         * - ESC <문자> → Alt + 문자
         */
        KeyEvent parse_escape() {
            unsigned char c2 = 0;
            if (read_timeout(c2) <= 0)
                return TERM_KEV(KEY_ESC);   // 단독 ESC

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

                // Alt + 출력 가능 문자
                if (c2 >= 0x20 && c2 < 0x7F)
                    return TERM_KEV_CH(static_cast<uint32_t>(c2),
                                       Modifier::ALT,
                                       static_cast<char32_t>(c2));

                    return TERM_KEV(KEY_ESC);
        }

        /**
         * @brief CSI (ESC [) 시퀀스 파싱.
         *
         * 지원 형식:
         * - ESC [ A/B/C/D           → 방향키 (± 수정자)
         * - ESC [ H/F               → Home / End
         * - ESC [ P/Q/R/S           → F1-F4
         * - ESC [ 1;Mod A …         → 수정자 포함 방향키
         * - ESC [ num ~             → Insert/Delete/PgUp/PgDn/Fn
         * - ESC [ < Cb;Cx;Cy M/m   → SGR 마우스 프로토콜
         */
        KeyEvent parse_csi() {
            char buf[32]{};
            int  len = 0;
            unsigned char c = 0;

            while (len < 31 && read_timeout(c) > 0) {
                buf[len++] = static_cast<char>(c);
                if (c >= 0x40 && c <= 0x7E) break;   // 최종 바이트 도달
            }
            if (len == 0) return TERM_KEV(KEY_ESC);

            char        final_ch = buf[len - 1];
            std::string params(buf, static_cast<std::size_t>(len - 1));

            // ── SGR 마우스: ESC [ < Cb;Cx;Cy M/m ───────────────────────────
            if (!params.empty() && params[0] == '<') {
                std::string p = params.substr(1);
                int cb = 0, cx = 0, cy = 0;
                auto p1 = p.find(';');
                auto p2 = (p1 != std::string::npos)
                ? p.find(';', p1 + 1)
                : std::string::npos;
                if (p1 != std::string::npos && p2 != std::string::npos) {
                    cb = std::stoi(p.substr(0, p1));
                    cx = std::stoi(p.substr(p1 + 1, p2 - p1 - 1));
                    cy = std::stoi(p.substr(p2 + 1));
                }

                MouseEvent me;
                me.x      = cx;
                me.y      = cy;
                me.motion = (cb & 32) != 0;
                int btn_raw = cb & ~32;

                if      (btn_raw == 64)   me.btn = MouseBtn::WHEEL_UP;
                else if (btn_raw == 65)   me.btn = MouseBtn::WHEEL_DOWN;
                else if (final_ch == 'm') me.btn = MouseBtn::RELEASE;
                else {
                    switch (btn_raw & 3) {
                        case 0:  me.btn = MouseBtn::LEFT;    break;
                        case 1:  me.btn = MouseBtn::MIDDLE;  break;
                        case 2:  me.btn = MouseBtn::RIGHT;   break;
                        default: me.btn = MouseBtn::RELEASE; break;
                    }
                }

                KeyEvent ke;
                ke.key   = KEY_MOUSE;
                ke.mouse = me;
                return ke;
            }

            // ── 수정자 비트 파싱 (xterm: 1;Mod 형식) ────────────────────────
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

        // ── 제어 문자 파싱 ──────────────────────────────────────────────────

        /**
         * @brief 제어 문자(0x00~0x1F) 및 DEL(0x7F) 파싱.
         *
         * - 0x7F, 0x08 → KEY_BACKSPACE  [BUG-4 수정: 0x08 추가]
         *   0x08 = Ctrl+H, 구형 VT100 / 일부 SSH 환경에서 Backspace 로 사용
         * - 0x0D, 0x0A → KEY_ENTER
         * - 0x09       → KEY_TAB
         * - 그 외      → ctrl_to_key() → KEY_CTRL_*
         */
        KeyEvent parse_ctrl(unsigned char c) {
            // [BUG-4 수정] 0x08(Ctrl+H) 도 Backspace 로 처리
            if (c == 0x7F || c == 0x08)
                return TERM_KEV(KEY_BACKSPACE);
            if (c == 0x0D || c == 0x0A)
                return TERM_KEV_CH(KEY_ENTER, Modifier::NONE, U'\n');
            if (c == 0x09)
                return TERM_KEV_CH(KEY_TAB,   Modifier::NONE, U'\t');

            uint32_t k = ctrl_to_key(c);
            if (k != KEY_NONE)
                return TERM_KEV_M(k, Modifier::CTRL);

            // ctrl_to_key() 에서 매핑되지 않은 제어 코드 — 문자로 fallback
            char32_t letter = static_cast<char32_t>(c + 0x40);
            return TERM_KEV_CH(static_cast<uint32_t>(letter), Modifier::CTRL, letter);
        }

        // ── UTF-8 파싱 ──────────────────────────────────────────────────────

        /**
         * @brief UTF-8 멀티바이트 시퀀스를 읽어 단일 코드포인트로 변환.
         *
         * 첫 바이트로 시퀀스 길이를 결정하고 나머지 바이트를 read() 로 추가 수신.
         * 잘린 시퀀스(read 실패)는 수신된 바이트까지만 처리한다.
         *
         * @param c 이미 읽은 첫 번째 바이트
         */
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

            // 바이트 조립 → UTF-32 코드포인트
            char32_t cp = static_cast<unsigned char>(buf[0]);
            if (seq_len == 2)
                cp = ((cp & 0x1F) << 6)
                | (static_cast<unsigned char>(buf[1]) & 0x3F);
            else if (seq_len == 3)
                cp = ((cp & 0x0F) << 12)
                | ((static_cast<unsigned char>(buf[1]) & 0x3F) << 6)
                |  (static_cast<unsigned char>(buf[2]) & 0x3F);
            else if (seq_len == 4)
                cp = ((cp & 0x07) << 18)
                | ((static_cast<unsigned char>(buf[1]) & 0x3F) << 12)
                | ((static_cast<unsigned char>(buf[2]) & 0x3F) << 6)
                |  (static_cast<unsigned char>(buf[3]) & 0x3F);

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
     * 반환된 드라이버는 setup() 호출 후 사용해야 한다.
     * 소멸 전 teardown()을 호출하거나, RAII 소멸자에 의해 자동 해제된다.
     *
     * @return unique_ptr<InputDriver>
     */
    inline std::unique_ptr<InputDriver> make_input_driver() {
        #if defined(_WIN32) || defined(_WIN64)
        return std::make_unique<WindowsInput>();
        #else
        return std::make_unique<PosixInput>();
        #endif
    }

} // namespace term
