#pragma once
/**
 * windows_input.hpp — Windows 키 입력 드라이버
 *
 * ReadConsoleInputW()로 INPUT_RECORD를 수신하여 KeyEvent로 변환한다.
 */

#if defined(_WIN32) || defined(_WIN64)
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>

namespace term {

class WindowsInput : public InputDriver {
public:
    WindowsInput()  = default;
    ~WindowsInput() override { teardown(); }

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

    KeyEvent read_key() override {
        INPUT_RECORD rec{};
        DWORD n = 0;
        while (true) {
            ReadConsoleInputW(stdin_handle_, &rec, 1, &n);
            if (rec.EventType == KEY_EVENT &&
                rec.Event.KeyEvent.bKeyDown) {
                return translate(rec.Event.KeyEvent);
            }
            if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
                KeyEvent ke;
                ke.key = KEY_RESIZE;
                return ke;
            }
        }
    }

private:
    HANDLE stdin_handle_ = INVALID_HANDLE_VALUE;
    DWORD  orig_mode_    = 0;

    KeyEvent translate(const KEY_EVENT_RECORD& ke) {
        uint8_t mod = Modifier::NONE;
        DWORD state = ke.dwControlKeyState;
        if (state & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) mod |= Modifier::CTRL;
        if (state & (LEFT_ALT_PRESSED  | RIGHT_ALT_PRESSED))  mod |= Modifier::ALT;
        if (state & SHIFT_PRESSED)                             mod |= Modifier::SHIFT;

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
            case VK_RETURN: return kev_ch(KEY_ENTER, '\n');
            case VK_TAB:    return kev_ch(KEY_TAB,   '\t');
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
            case VK_F10:    return kev(KEY_MENU);
            case VK_F11:    return kev(KEY_F11);
            case VK_F12:    return kev(KEY_F12);
            default: break;
        }

        // Ctrl + 알파벳 → KEY_CTRL_X
        if (mod & Modifier::CTRL) {
            // VK_A=0x41 .. VK_Z=0x5A
            if (vk >= 'A' && vk <= 'Z') {
                // Ctrl+A(0x01)..Z(0x1A) 변환
                unsigned char ctrl_byte = static_cast<unsigned char>(vk - 'A' + 1);
                uint32_t k = ctrl_to_key(ctrl_byte);
                if (k != KEY_NONE) return kev(k);
            }
        }

        // 일반 문자
        wchar_t wc = ke.uChar.UnicodeChar;
        if (wc >= 0x20) {
            char32_t cp = static_cast<char32_t>(wc);
            KeyEvent e; e.key = static_cast<uint32_t>(cp);
            e.modifier = mod; e.ch = cp;
            return e;
        }

        return KeyEvent{};  // KEY_NONE, 기본값
    }
};

inline std::unique_ptr<InputDriver> make_input_driver() {
    return std::make_unique<WindowsInput>();
}

} // namespace term
#endif // _WIN32
