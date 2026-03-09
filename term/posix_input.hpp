#pragma once
/**
 * posix_input.hpp — POSIX 키 입력 드라이버 (Linux / macOS)
 *
 * termios raw 모드로 read()를 수행하고 ESC 시퀀스를 파싱한다.
 * 100ms timeout으로 단독 ESC와 ESC 시퀀스를 구분한다.
 *
 * KeyEvent 구조체는 input.hpp에서 선언된 기본값을 가지므로
 * 중괄호 초기화 시 누락된 필드(mouse) 경고가 발생하지 않는다.
 */

#if !(defined(_WIN32) || defined(_WIN64))

#include <atomic>
#include <csignal>
#include <cstring>
#include <string>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

namespace term {

// ─── SIGWINCH 플래그 ────────────────────────────────────────────────

inline std::atomic<bool> g_resize_flag{false};

static constexpr char MOUSE_ON[]  = "\x1b[?1000h\x1b[?1002h\x1b[?1006h";
static constexpr char MOUSE_OFF[] = "\x1b[?1000l\x1b[?1002l\x1b[?1006l";

// ─── 편의 매크로: KeyEvent 생성 ──────────────────────────────────────
// KeyEvent의 모든 필드에 기본값이 있으므로 지정 초기화 사용
#define TERM_KEV(k_)              KeyEvent{ .key=(k_) }
#define TERM_KEV_M(k_, m_)       KeyEvent{ .key=(k_), .modifier=(m_) }
#define TERM_KEV_CH(k_, m_, c_)  KeyEvent{ .key=(k_), .modifier=(m_), .ch=(c_) }

// ─── PosixInput ──────────────────────────────────────────────────────

class PosixInput : public InputDriver {
public:
    PosixInput()  = default;
    ~PosixInput() override { teardown(); }

    void setup() override {
        tcgetattr(STDIN_FILENO, &orig_);
        struct termios raw = orig_;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~OPOST;
        raw.c_cflag |=  CS8;
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        raw.c_cc[VMIN]  = 1;
        raw.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
        raw_active_ = true;
        ::signal(SIGWINCH, [](int){ g_resize_flag = true; });
        { auto _r = ::write(STDOUT_FILENO, MOUSE_ON, sizeof(MOUSE_ON)-1); (void)_r; }
    }

    void teardown() override {
        if (raw_active_) {
            { auto _r = ::write(STDOUT_FILENO, MOUSE_OFF, sizeof(MOUSE_OFF)-1); (void)_r; }
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_);
            raw_active_ = false;
        }
    }

    KeyEvent read_key() override {
        if (g_resize_flag.exchange(false))
            return TERM_KEV(KEY_RESIZE);

        unsigned char c = 0;
        if (::read(STDIN_FILENO, &c, 1) <= 0)
            return TERM_KEV(KEY_NONE);

        if (c == 0x1B)  return parse_escape();
        if (c < 0x20 || c == 0x7F) return parse_ctrl(c);
        return parse_utf8(c);
    }

private:
    termios orig_{};
    bool    raw_active_ = false;

    int read_timeout(unsigned char& out, int timeout_ms = 100) {
        struct termios t;
        tcgetattr(STDIN_FILENO, &t);
        struct termios tmp = t;
        tmp.c_cc[VMIN]  = 0;
        tmp.c_cc[VTIME] = timeout_ms / 100;
        tcsetattr(STDIN_FILENO, TCSANOW, &tmp);
        int n = static_cast<int>(::read(STDIN_FILENO, &out, 1));
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
        return n;
    }

    KeyEvent parse_escape() {
        unsigned char c2 = 0;
        if (read_timeout(c2) <= 0)
            return TERM_KEV(KEY_ESC);

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

        // Alt + 문자
        if (c2 >= 0x20 && c2 < 0x7F)
            return TERM_KEV_CH(static_cast<uint32_t>(c2), Modifier::ALT,
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

        char final_ch = buf[len - 1];
        std::string params(buf, static_cast<std::size_t>(len - 1));

        // SGR 마우스: ESC [ < Cb ; Cx ; Cy M/m
        if (!params.empty() && params[0] == '<') {
            std::string p = params.substr(1);
            int cb = 0, cx = 0, cy = 0;
            auto p1 = p.find(';');
            auto p2 = (p1 != std::string::npos) ? p.find(';', p1+1) : std::string::npos;
            if (p1 != std::string::npos && p2 != std::string::npos) {
                cb = std::stoi(p.substr(0, p1));
                cx = std::stoi(p.substr(p1+1, p2-p1-1));
                cy = std::stoi(p.substr(p2+1));
            }
            MouseEvent me;
            me.x      = cx;
            me.y      = cy;
            me.motion = (cb & 32) != 0;
            int btn_raw = cb & ~32;
            if      (btn_raw == 64) me.btn = MouseBtn::WHEEL_UP;
            else if (btn_raw == 65) me.btn = MouseBtn::WHEEL_DOWN;
            else if (final_ch == 'm') me.btn = MouseBtn::RELEASE;
            else {
                switch (btn_raw & 3) {
                    case 0: me.btn = MouseBtn::LEFT;   break;
                    case 1: me.btn = MouseBtn::MIDDLE; break;
                    case 2: me.btn = MouseBtn::RIGHT;  break;
                    default: me.btn = MouseBtn::RELEASE; break;
                }
            }
            KeyEvent ke;
            ke.key   = KEY_MOUSE;
            ke.mouse = me;
            return ke;
        }

        // 수정자 비트 (xterm: 1;Mod)
        uint8_t mod = Modifier::NONE;
        auto semi = params.find(';');
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
        if (c == 0x7F) return TERM_KEV(KEY_BACKSPACE);
        if (c == 0x0D || c == 0x0A) return TERM_KEV_CH(KEY_ENTER, Modifier::NONE, '\n');
        if (c == 0x09) return TERM_KEV_CH(KEY_TAB,   Modifier::NONE, '\t');

        uint32_t k = ctrl_to_key(c);
        if (k != KEY_NONE)
            return TERM_KEV_M(k, Modifier::CTRL);

        // 매핑 없는 Ctrl 코드 → 문자 그대로
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

// ─── 팩토리 구현 ────────────────────────────────────────────────────

inline std::unique_ptr<InputDriver> make_input_driver() {
    return std::make_unique<PosixInput>();
}

} // namespace term
#endif // !_WIN32
