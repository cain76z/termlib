#pragma once
/**
 * @file ansi_renderer.hpp
 * @brief 배치 출력 엔진  (TDD §8)
 *
 * `AnsiScreen` + `AnsiOptimizer` → ANSI 바이트 스트림 → `Terminal::write()`
 *
 * ## 역할 분리
 *
 * | 클래스 | 파일 | 역할 |
 * |--------|------|------|
 * | `Terminal`     | `term_info.hpp`       | 터미널 초기화·제어·정보 조회 |
 * | `AnsiScreen`   | `ansi_screen.hpp`     | 논리 셀 버퍼 관리 |
 * | `AnsiOptimizer`| `ansi_optimizer.hpp`  | diff 최소 이스케이프 생성 |
 * | `AnsiRenderer` | `ansi_renderer.hpp`   | 위 세 요소를 조합한 렌더 루프 |
 *
 * ## 사용 예시
 *
 * ```cpp
 * term::Terminal   term;                          // 터미널 초기화
 * term::AnsiScreen screen(term.cols(), term.rows()); // 버퍼 생성
 * term::AnsiRenderer renderer(screen, term);      // 렌더러 연결
 *
 * term.enter_alt_screen();
 * term.show_cursor(false);
 *
 * // ... screen 에 셀 쓰기 ...
 *
 * renderer.render();       // dirty 셀만 출력
 * renderer.render_full();  // 전체 재출력
 * renderer.handle_resize(); // SIGWINCH 후 크기 갱신
 * ```
 *
 * ## 의존 방향
 * ```
 * platform.hpp
 *   └── term_info.hpp      (Terminal, ColorLevel, TermSize, 자유함수)
 *   └── ansi_screen.hpp
 *         └── ansi_optimizer.hpp
 *               └── ansi_renderer.hpp   ← 이 파일
 * ```
 *
 * 구현부가 모두 클래스 본체 안에 위치한다.
 */
#include "term/term_info.hpp"
#include "term/ansi_optimizer.hpp"

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  AnsiRenderer  (TDD §8)
//  AnsiScreen + AnsiOptimizer 를 Terminal 을 통해 출력하는 렌더 루프
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief ANSI 터미널 배치 출력 엔진
 *
 * `AnsiScreen`(논리 버퍼)의 dirty 셀을 `AnsiOptimizer`로 최소 이스케이프 시퀀스로
 * 변환하여 `Terminal`을 통해 실제 터미널에 씁니다.
 *
 * ### 생성자
 * ```cpp
 * term::Terminal   term;
 * term::AnsiScreen screen(80, 24);
 * term::AnsiRenderer renderer(screen, term);
 * ```
 *
 * ### 렌더 루프
 * ```cpp
 * // dirty 셀만 출력 (일반 루프)
 * renderer.render();
 *
 * // 전체 화면 강제 재출력 (크기 변경 등)
 * renderer.render_full();
 *
 * // SIGWINCH 수신 후 터미널 크기 갱신 + 전체 재출력
 * renderer.handle_resize();
 * ```
 */
class AnsiRenderer {
public:
    /**
     * @brief 렌더러를 생성합니다.
     *
     * @param screen  렌더 대상 논리 화면 버퍼 (수명이 렌더러보다 길어야 함)
     * @param term    출력을 담당할 터미널 (수명이 렌더러보다 길어야 함)
     */
    explicit AnsiRenderer(AnsiScreen& screen, Terminal& term)
        : screen_(screen), term_(term)
    {}

    // 복사/이동 금지 (레퍼런스 멤버 소유)
    AnsiRenderer(const AnsiRenderer&)            = delete;
    AnsiRenderer& operator=(const AnsiRenderer&) = delete;
    AnsiRenderer(AnsiRenderer&&)                 = delete;
    AnsiRenderer& operator=(AnsiRenderer&&)      = delete;

    // ── 렌더링 ────────────────────────────────────────────────────────────

    /**
     * @brief dirty 셀만 출력합니다 (증분 렌더).
     *
     * `screen_.any_dirty()` 가 false 이면 아무 것도 출력하지 않습니다.
     * 렌더 후 스냅샷을 갱신하고 dirty 플래그를 초기화합니다.
     */
    void render() {
        if (!screen_.any_dirty()) return;
        std::string out = optimizer_.optimize(screen_);
        screen_.snapshot();
        screen_.clear_dirty();
        term_.write(out);
        term_.flush();
    }

    /**
     * @brief 화면 전체를 강제로 재출력합니다.
     *
     * 이전 프레임과 diff 없이 모든 셀을 출력합니다.
     * 크기 변경 직후, 초기화 시, 외부 화면 오염 복구 등에 사용합니다.
     */
    void render_full() {
        screen_.invalidate_all();
        std::string out = optimizer_.full_redraw(screen_);
        screen_.snapshot();
        screen_.clear_dirty();
        term_.write(out);
        term_.flush();
    }

    /**
     * @brief 터미널 크기 변경(SIGWINCH)을 처리합니다.
     *
     * 현재 터미널 크기를 다시 조회하여 `AnsiScreen`을 리사이즈하고,
     * 전체 화면을 재출력합니다.
     */
    void handle_resize() {
        auto [nc, nr] = term_.size();
        screen_.resize(nc, nr);
        render_full();
    }

    // ── 접근자 ────────────────────────────────────────────────────────────

    /** @brief 연결된 Terminal 을 반환합니다. */
    [[nodiscard]] Terminal&       terminal()  noexcept { return term_; }
    [[nodiscard]] const Terminal& terminal()  const noexcept { return term_; }

    /** @brief 연결된 AnsiScreen 을 반환합니다. */
    [[nodiscard]] AnsiScreen&       screen()  noexcept { return screen_; }
    [[nodiscard]] const AnsiScreen& screen()  const noexcept { return screen_; }

    // ── 편의 위임 (Terminal → 자주 쓰는 제어는 렌더러에서도 바로 호출 가능) ──

    /** @brief `Terminal::color_level()` 위임 */
    [[nodiscard]] ColorLevel color_level() const noexcept { return term_.color_level(); }
    /** @brief `Terminal::vt_enabled()` 위임 */
    [[nodiscard]] bool       vt_enabled()  const noexcept { return term_.vt_enabled(); }

private:
    AnsiScreen&   screen_;
    Terminal&     term_;
    AnsiOptimizer optimizer_;
};

} // namespace term