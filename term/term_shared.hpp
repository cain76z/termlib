#pragma once
/**
 * term_shared.hpp — term_info / input 공유 상태  (충돌 해결 §SIG)
 *
 * ## 해결하는 문제
 *
 * term_info.hpp + input.hpp 를 동시에 include 하면 세 가지 충돌이 발생한다:
 *
 * | 항목 | term_info | input | 충돌 |
 * |------|-----------|-------|------|
 * | SIGWINCH 플래그 | term_detail::g_sigwinch_flag | input_detail::g_sigwinch_flag | 2개 독립 운용 → 한쪽은 영원히 0 |
 * | SA_RESTART | SA_RESTART 포함 | SA_RESTART 제거 | 나중에 등록한 쪽이 이기고 한쪽 로직이 깨짐 |
 * | Raw mode | Terminal::enter_raw_mode() | PosixInput::setup() | 이중 tcsetattr → 복원 순서 파괴 |
 *
 * ## 해결 방법
 *
 * 1. 이 파일에 **단일 SIGWINCH 공유 상태**를 정의한다.
 * 2. `ensure_sigwinch_handler()` 를 최초 1회만 설치(no SA_RESTART)하도록 보장한다.
 * 3. `term_info.hpp` / `input.hpp` 모두 이 파일을 include 하여 같은 플래그를 참조한다.
 * 4. Raw mode 소유권은 `InputOptions::manage_raw_mode` 로 명시적으로 지정한다.
 *
 * ## 의존 방향
 * ```
 * platform.hpp
 *   └── term_shared.hpp ← term_info.hpp
 *                       ← input.hpp
 * ```
 *
 * 구현부가 모두 inline 으로 헤더에 위치한다.
 */
#include "platform.hpp"

#if defined(TERM_PLATFORM_POSIX)
#  include <atomic>
#  include <csignal>
#  include <functional>

namespace term::shared_detail {

// ─── SIGWINCH 공유 상태 ────────────────────────────────────────────────────

/// SIGWINCH 수신 플래그 — signal handler → 메인 스레드
/// term_info.hpp 와 input.hpp 가 동일한 인스턴스를 공유한다.
inline std::atomic<bool> g_sigwinch_flag{false};

/// 핸들러 중복 설치 방지 플래그
inline std::atomic<bool> g_sigwinch_installed{false};

/// 터미널 크기 변경 콜백 — Terminal::on_resize() 가 등록
inline std::function<void(int, int)> g_resize_callback;

/**
 * @brief SIGWINCH 핸들러를 **최초 1회**만 설치한다.
 *
 * ### SA_RESTART 없음 (의도적)
 * SA_RESTART 를 제거하면 SIGWINCH 수신 시 read() 가 EINTR 로 즉시 중단된다.
 * PosixInput::read_key() 는 EINTR 감지 후 루프 상단에서 플래그를 확인하여
 * KEY_RESIZE 를 즉시 반환할 수 있다.
 *
 * Terminal::poll_resize() 도 g_sigwinch_flag 를 직접 확인하므로 문제없다.
 *
 * ### 멀티 호출 안전
 * 이미 설치된 경우 no-op 이므로 term_info / input 양쪽에서 호출해도 안전하다.
 */
inline void ensure_sigwinch_handler() noexcept {
    if (g_sigwinch_installed.exchange(true)) return;  // 이미 설치됨 → skip

    struct sigaction sa{};
    sa.sa_handler = [](int) noexcept { g_sigwinch_flag.store(true); };
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // SA_RESTART 제거 — read() EINTR 허용 → KEY_RESIZE 즉시 반환
    ::sigaction(SIGWINCH, &sa, nullptr);
}

} // namespace term::shared_detail

#endif // TERM_PLATFORM_POSIX