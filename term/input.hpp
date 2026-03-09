#pragma once
/**
 * input.hpp — 키 입력 플랫폼 추상화  (TDD §10)
 *
 * 플랫폼별 키 입력을 공통 KeyEvent 구조체로 정규화한다.
 * 상위 레이어는 플랫폼을 인식하지 않는다.
 */

#include <cstdint>
#include <memory>

namespace term {

// ═══════════════════════════════════════════════════════════════════════
//  내부 키 코드 (플랫폼 독립)
// ═══════════════════════════════════════════════════════════════════════

enum Key : uint32_t {
    KEY_NONE      = 0,

    // ── 이동 / 편집 키 ──────────────────────────────────────────────
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

    // ── 펑션 키 ────────────────────────────────────────────────────
    KEY_F1  = 0x1100,
    KEY_F2,  KEY_F3,  KEY_F4,  KEY_F5,
    KEY_F6,  KEY_F7,  KEY_F8,  KEY_F9,
    KEY_F10, KEY_F11, KEY_F12,
    KEY_MENU,   ///< F10 / Alt+F 메뉴 키

    // ── Ctrl + 알파벳 키 ─────────────────────────────────────────────
    // Ctrl+H(Backspace)/I(Tab)/J,M(Enter) 은 위에서 처리됨
    KEY_CTRL_A = 0x1200,  ///< Ctrl+A  (0x01) — 전체선택 / 행 시작
    KEY_CTRL_B,           ///< Ctrl+B  (0x02)
    KEY_CTRL_C,           ///< Ctrl+C  (0x03) — 복사 / 인터럽트
    KEY_CTRL_D,           ///< Ctrl+D  (0x04) — EOF / 삭제
    KEY_CTRL_E,           ///< Ctrl+E  (0x05) — 행 끝
    KEY_CTRL_F,           ///< Ctrl+F  (0x06) — 찾기
    KEY_CTRL_G,           ///< Ctrl+G  (0x07) — 취소
    KEY_CTRL_K,           ///< Ctrl+K  (0x0B) — 행 뒤 삭제
    KEY_CTRL_L,           ///< Ctrl+L  (0x0C) — 화면 갱신
    KEY_CTRL_N,           ///< Ctrl+N  (0x0E) — 다음 줄
    KEY_CTRL_O,           ///< Ctrl+O  (0x0F) — 열기
    KEY_CTRL_P,           ///< Ctrl+P  (0x10) — 이전 줄
    KEY_CTRL_Q,           ///< Ctrl+Q  (0x11) — 종료
    KEY_CTRL_R,           ///< Ctrl+R  (0x12) — 바꾸기
    KEY_CTRL_S,           ///< Ctrl+S  (0x13) — 저장
    KEY_CTRL_T,           ///< Ctrl+T  (0x14) — 전치
    KEY_CTRL_U,           ///< Ctrl+U  (0x15) — 행 앞 삭제
    KEY_CTRL_V,           ///< Ctrl+V  (0x16) — 붙여넣기
    KEY_CTRL_W,           ///< Ctrl+W  (0x17) — 단어 삭제
    KEY_CTRL_X,           ///< Ctrl+X  (0x18) — 잘라내기
    KEY_CTRL_Y,           ///< Ctrl+Y  (0x19) — 재실행
    KEY_CTRL_Z,           ///< Ctrl+Z  (0x1A) — 실행취소

    // ── 시스템 이벤트 ───────────────────────────────────────────────
    KEY_RESIZE  = 0x1300,  ///< 터미널 리사이즈 이벤트 (SIGWINCH)
    KEY_MOUSE   = 0x1400,  ///< 마우스 이벤트 (MouseEvent 참조)
};

/// Ctrl 바이트(0x01~0x1A) → KEY_CTRL_X  변환 (Backspace/Tab/Enter 제외)
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

// ═══════════════════════════════════════════════════════════════════════
//  수정자 (비트 OR)
// ═══════════════════════════════════════════════════════════════════════

struct Modifier {
    static constexpr uint8_t NONE  = 0;
    static constexpr uint8_t CTRL  = 1 << 0;
    static constexpr uint8_t ALT   = 1 << 1;
    static constexpr uint8_t SHIFT = 1 << 2;
};

// ═══════════════════════════════════════════════════════════════════════
//  MouseEvent
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
//  KeyEvent — 모든 필드에 기본값 → 불완전 초기화 경고 없음
// ═══════════════════════════════════════════════════════════════════════

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

// ═══════════════════════════════════════════════════════════════════════
//  InputDriver 추상 인터페이스
// ═══════════════════════════════════════════════════════════════════════

class InputDriver {
public:
    virtual ~InputDriver() = default;
    virtual KeyEvent read_key() = 0;
    virtual void     setup()    {}
    virtual void     teardown() {}
};

std::unique_ptr<InputDriver> make_input_driver();

} // namespace term


// ═══════════════════════════════════════════════════════════════════════
//  플랫폼별 구현 포함
// ═══════════════════════════════════════════════════════════════════════

#if defined(_WIN32) || defined(_WIN64)
#  include "windows_input.hpp"
#else
#  include "posix_input.hpp"
#endif
