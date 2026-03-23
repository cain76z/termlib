#pragma once
/**
 * ansi_screen.hpp — Cell · AnsiScreen  (TDD §4.3, §5)
 *
 * 의존 방향:
 *   ansi_string.hpp (Color · Attr · AnsiString) → 이 파일
 *
 * Color · Attr 는 ansi_string.hpp 에 정의되어 있다.
 * 이 파일은 ansi_string.hpp 를 include 하며, 역방향 include 는 없다.
 *
 * 구현부는 모두 클래스 본체 안에 위치한다.
 */
#include "term/uni_string.hpp"
#include "term/ansi_string.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  Cell  (TDD §4.3)
//  데이터 전용 구조체 — 구현이 간단하므로 선언 그대로 완결됨.
// ═══════════════════════════════════════════════════════════════════════════

struct Cell {
    std::string grapheme;
    Color       fg;
    Color       bg;
    uint16_t    attr  = 0;
    uint8_t     width = 1;

    bool operator==(const Cell& o) const noexcept {
        return width == o.width && attr == o.attr &&
               fg == o.fg && bg == o.bg && grapheme == o.grapheme;
    }
    bool operator!=(const Cell& o) const noexcept { return !(*this == o); }

    bool is_blank() const noexcept {
        return (grapheme.empty() || grapheme == " ") &&
               fg.type == Color::Type::Default &&
               bg.type == Color::Type::Default && attr == 0;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  AnsiScreen  (TDD §5)
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class AnsiScreen {
public:
    // ── 생성 ──────────────────────────────────────────────────────────────
    explicit AnsiScreen(int cols, int rows)
        : cols_(cols), rows_(rows)
        , buf_(cols * rows)
        , prev_buf_(cols * rows)
        , dirty_(cols * rows, true)
        , row_dirty_(rows, true)      // [OPT-02] 행 단위 dirty 요약
        , any_dirty_(true)
    {
        Cell blank; blank.grapheme = " "; blank.width = 1;
        std::fill(buf_.begin(),      buf_.end(),      blank);
        std::fill(prev_buf_.begin(), prev_buf_.end(), Cell{});
    }

    // ── 크기 조회 ─────────────────────────────────────────────────────────
    [[nodiscard]] int cols() const noexcept { return cols_; }
    [[nodiscard]] int rows() const noexcept { return rows_; }

    // ── 리사이즈 ──────────────────────────────────────────────────────────
    void resize(int new_cols, int new_rows) {
        if (new_cols == cols_ && new_rows == rows_) return;

        Cell blank; blank.grapheme = " "; blank.width = 1;
        std::vector<Cell> new_buf(new_cols * new_rows, blank);

        int copy_cols = std::min(cols_, new_cols);
        int copy_rows = std::min(rows_, new_rows);
        for (int r = 0; r < copy_rows; ++r)
            for (int c = 0; c < copy_cols; ++c)
                new_buf[r * new_cols + c] = buf_[r * cols_ + c];

        cols_      = new_cols;
        rows_      = new_rows;
        buf_       = std::move(new_buf);
        prev_buf_.assign(new_cols * new_rows, Cell{});
        dirty_.assign(new_cols * new_rows, true);
        row_dirty_.assign(new_rows, true);
        any_dirty_ = true;

        // [BUG-03] 리사이즈 후 커서가 화면 밖이 되지 않도록 클램프
        if (new_cols > 0) cur_x_ = std::min(cur_x_, new_cols - 1);
        if (new_rows > 0) cur_y_ = std::min(cur_y_, new_rows - 1);
        cur_x_ = std::max(cur_x_, 0);
        cur_y_ = std::max(cur_y_, 0);
    }

    // ── 셀 쓰기 ───────────────────────────────────────────────────────────
    void put_cell(int x, int y, const Cell& cell) {
        if (!in_bounds(x, y)) return;
        int i = idx(x, y);

        // 기존 셀 처리: 2열 문자 잔재 정리
        const Cell& existing = buf_[i];
        if (existing.width == 0 && x > 0) {
            clear_cell_at(idx(x - 1, y), y);
        } else if (existing.width == 2 && x + 1 < cols_) {
            clear_cell_at(idx(x + 1, y), y);
        }

        buf_[i] = cell;
        mark_dirty(i, y);

        // 새 2열 문자 → 오른쪽에 플레이스홀더 삽입
        if (cell.width == 2 && x + 1 < cols_) {
            Cell ph;
            ph.grapheme = "";
            ph.fg       = cell.fg;
            ph.bg       = cell.bg;
            ph.attr     = cell.attr;
            ph.width    = 0;
            int pi = idx(x + 1, y);
            buf_[pi] = ph;
            mark_dirty(pi, y);
        }
    }

    // ── AnsiString 일괄 적용 ──────────────────────────────────────────────
    void put(const AnsiString& s) {
        int x = (s.dest_x() >= 0) ? s.dest_x() : cur_x_;
        int y = (s.dest_y() >= 0) ? s.dest_y() : cur_y_;

        for (const auto& seg : s.segments()) {
            using T = AnsiString::Segment::Type;
            if (seg.type == T::Move) {
                x = seg.x; y = seg.y;
            } else if (seg.type == T::Text) {
                auto clusters = UniString::split(seg.text);
                for (const auto& g : clusters) {
                    if (y < 0 || y >= rows_) goto done;
                    // 화면 오른쪽 경계 초과 시 줄바꿈하지 않고 해당 행 출력 중단
                    // (wrapping 이 허용되면 다음 행 내용이 밀려 화면이 깨짐)
                    if (x >= cols_) break;
                    Cell c;
                    c.grapheme = g.bytes;
                    c.width    = static_cast<uint8_t>(
                                 g.width < 0 ? 1 : g.width > 2 ? 2 : g.width);
                    c.fg   = seg.fg;
                    c.bg   = seg.bg;
                    c.attr = seg.attr;
                    // 2열 문자가 마지막 열에 걸치면 공백으로 대체
                    if (x + c.width > cols_) {
                        Cell sp; sp.grapheme = " "; sp.width = 1;
                        sp.fg = c.fg; sp.bg = c.bg; sp.attr = c.attr;
                        put_cell(x, y, sp);
                        break;
                    }
                    put_cell(x, y, c);
                    x += c.width;
                }
            }
        }
    done:
        cur_x_ = x;
        cur_y_ = y;
    }

    // ── 영역 초기화 ───────────────────────────────────────────────────────
    void clear(int x = 0, int y = 0, int w = -1, int h = -1) {
        if (w < 0) w = cols_ - x;
        if (h < 0) h = rows_ - y;
        Cell blank; blank.grapheme = " "; blank.width = 1;
        // [MIN-01] 실제 화면 안 셀만 dirty 처리
        bool any_cleared = false;
        for (int r = y; r < y + h && r < rows_; ++r) {
            if (r < 0) continue;
            for (int c = x; c < x + w && c < cols_; ++c) {
                if (c < 0) continue;
                int i = idx(c, r);
                buf_[i]       = blank;
                dirty_[i]     = true;
                row_dirty_[r] = true;
                any_cleared   = true;
            }
        }
        if (any_cleared) any_dirty_ = true;
    }

    // ── 전체 강제 더티 ────────────────────────────────────────────────────
    void invalidate_all() {
        std::fill(dirty_.begin(),     dirty_.end(),     true);
        std::fill(row_dirty_.begin(), row_dirty_.end(), true);
        any_dirty_ = true;
    }

    // ── dirty 조회 ────────────────────────────────────────────────────────
    [[nodiscard]] bool dirty_bit(int x, int y) const noexcept {
        if (!in_bounds(x, y)) return false;
        return dirty_[idx(x, y)];
    }
    [[nodiscard]] bool any_dirty() const noexcept { return any_dirty_; }
    /// [OPT-02] 행 단위 dirty 빠른 확인
    [[nodiscard]] bool row_dirty(int y) const noexcept {
        if (y < 0 || y >= rows_) return false;
        return row_dirty_[y];
    }

    // ── dirty 초기화 ──────────────────────────────────────────────────────
    void clear_dirty() {
        std::fill(dirty_.begin(),     dirty_.end(),     false);
        std::fill(row_dirty_.begin(), row_dirty_.end(), false);
        any_dirty_ = false;
    }

    // ── 셀 접근 ───────────────────────────────────────────────────────────
    [[nodiscard]] Cell& cell_at(int x, int y) {
        assert(in_bounds(x, y)); return buf_[idx(x, y)];
    }
    [[nodiscard]] const Cell& cell_at(int x, int y) const {
        assert(in_bounds(x, y)); return buf_[idx(x, y)];
    }
    [[nodiscard]] bool in_bounds_pub(int x, int y) const noexcept { return in_bounds(x, y); }
    [[nodiscard]] const Cell& prev_cell_at(int x, int y) const {
        assert(in_bounds(x, y)); return prev_buf_[idx(x, y)];
    }

    // ── 스냅샷 (prev_buf_ ← buf_ 복사) ───────────────────────────────────
    // [주의] swap 방식(OPT-01)은 부분 업데이트 시 2프레임 전 내용이 buf_에
    //        남아 다이얼로그 배경 뒤로 구버전 셀이 비치는 버그를 유발함.
    //        copy 방식으로 고정: buf_는 항상 "현재 그릴 버퍼"를 유지한다.
    void snapshot() { prev_buf_ = buf_; }

    /**
     * sync_buffer() — prev_buf_(현재 터미널 상태) → buf_ 역방향 동기화
     *
     * run_dialog 진입 시 호출한다. invalidate_all() 만으로는 buf_ 내
     * 다이얼로그 외부 영역이 prev_buf_와 달라질 수 있어 옵티마이저가
     * 낡은 셀을 터미널에 출력하는 잔상 버그를 유발한다.
     *
     * sync_buffer() 후 buf_ == prev_buf_ 가 보장되므로:
     *  - 다이얼로그 rect 외부: 동일 → 옵티마이저 출력 생략 ✓
     *  - 다이얼로그 rect 내부: dlg.render() 후 달라짐 → 정상 출력 ✓
     */
    void sync_buffer() { buf_ = prev_buf_; }

    // ── 영역 저장 / 복원 (다이얼로그 배경 보존용) ─────────────────────────
    /**
     * save_region()    — 지정 영역의 셀을 벡터로 반환 (다이얼로그 열기 전)
     * restore_region() — save_region() 결과를 원위치 복원 후 dirty 표시
     *
     * 사용 패턴:
     *   auto bg = screen.save_region(x, y, w, h);
     *   ... 다이얼로그 이벤트 루프 ...
     *   screen.restore_region(x, y, w, h, bg);
     *   renderer.render_full();   // 터미널에 복원 출력
     */
    [[nodiscard]] std::vector<Cell>
    save_region(int x, int y, int w, int h) const {
        std::vector<Cell> saved;
        saved.reserve(static_cast<size_t>(w * h));
        for (int r = y; r < y + h; ++r)
            for (int c = x; c < x + w; ++c)
                saved.push_back(in_bounds(c, r) ? buf_[idx(c, r)] : Cell{});
        return saved;
    }

    void restore_region(int x, int y, int w, int h,
                        const std::vector<Cell>& saved) {
        int si = 0;
        for (int r = y; r < y + h; ++r) {
            for (int c = x; c < x + w; ++c, ++si) {
                if (!in_bounds(c, r) || si >= (int)saved.size()) continue;
                int bi = idx(c, r);
                buf_[bi] = saved[si];
                mark_dirty(bi, r);
            }
        }
    }

    // ── 커서 ──────────────────────────────────────────────────────────────
    [[nodiscard]] int cursor_x() const noexcept { return cur_x_; }
    [[nodiscard]] int cursor_y() const noexcept { return cur_y_; }
    void set_cursor(int x, int y) noexcept { cur_x_ = x; cur_y_ = y; }

private:
    int cols_ = 0, rows_ = 0;
    std::vector<Cell> buf_;
    std::vector<Cell> prev_buf_;
    std::vector<bool> dirty_;
    std::vector<bool> row_dirty_;   ///< [OPT-02] 행 단위 dirty 요약 비트
    bool              any_dirty_ = false;
    int cur_x_ = 0, cur_y_ = 0;

    [[nodiscard]] int  idx(int x, int y) const noexcept { return y * cols_ + x; }
    [[nodiscard]] bool in_bounds(int x, int y) const noexcept {
        return x >= 0 && x < cols_ && y >= 0 && y < rows_;
    }

    // [OPT-02] dirty 마킹: 셀 + 행 요약 + 전체 플래그 동시 갱신
    void mark_dirty(int i, int row) {
        dirty_[i]       = true;
        row_dirty_[row] = true;
        any_dirty_      = true;
    }

    void clear_cell_at(int i, int row) {
        Cell blank; blank.grapheme = " "; blank.width = 1;
        buf_[i] = blank;
        mark_dirty(i, row);
    }
};

} // namespace term