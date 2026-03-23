#pragma once
/**
 * ansi_optimizer.hpp — ANSI 이스케이프 코드 최적화  (TDD §7)
 *
 * 구현부가 모두 클래스 본체 안에 위치한다.
 */
#include "term/ansi_screen.hpp"
#include <bit>
#include <string>

namespace term {

class AnsiOptimizer {
public:
    AnsiOptimizer() = default;

    /// dirty 셀만 출력하는 최적화된 바이트 스트림 생성
    [[nodiscard]] std::string optimize(const AnsiScreen& screen) {
        if (!screen.any_dirty()) return {};

        out_.clear();
        out_.reserve(static_cast<size_t>(screen.cols()) *
                     static_cast<size_t>(screen.rows()) * 20);  // [MIN-02] TrueColor 최악 대비
        RenderState st;

        for (int y = 0; y < screen.rows(); ++y) {
            // [OPT-02] 행 단위 dirty 빠른 확인
            if (!screen.row_dirty(y)) continue;

            int x = 0;
            while (x < screen.cols()) {
                if (!screen.dirty_bit(x, y)) { ++x; continue; }

                const Cell& cell = screen.cell_at(x, y);
                if (cell.width == 0) { ++x; continue; }  // placeholder

                // 이전 프레임과 동일하면 생략
                const Cell& prev = screen.prev_cell_at(x, y);
                if (cell == prev) { ++x; continue; }

                emit_move(x, y, st);
                emit_attr_diff(cell, st);
                emit_cell_text(cell);

                st.cur_x = x + std::max(1, static_cast<int>(cell.width));
                st.cur_y = y;
                x        = st.cur_x;
            }
        }

        if (st.attr != 0 || st.fg.type != Color::Type::Default ||
            st.bg.type != Color::Type::Default)
            out_ += "\x1b[0m";

        return std::move(out_);
    }

    /// 전체 화면 재출력 스트림 생성 (diff 없음)
    [[nodiscard]] std::string full_redraw(const AnsiScreen& screen) {
        out_.clear();
        out_.reserve(static_cast<size_t>(screen.cols()) *
                     static_cast<size_t>(screen.rows()) * 20);
        out_ += "\x1b[H";

        RenderState st;
        st.cur_x = 0; st.cur_y = 0;

        for (int y = 0; y < screen.rows(); ++y) {
            for (int x = 0; x < screen.cols(); ) {
                const Cell& cell = screen.cell_at(x, y);
                if (cell.width == 0) { ++x; continue; }

                emit_attr_diff(cell, st);
                emit_cell_text(cell);

                int step = std::max(1, static_cast<int>(cell.width));
                x       += step;
                st.cur_x = x;
                st.cur_y = y;
            }
            if (y + 1 < screen.rows()) {
                out_ += "\r\n";
                st.cur_x = 0;
                st.cur_y = y + 1;
            }
        }
        out_ += "\x1b[0m";
        return std::move(out_);
    }

private:
    struct RenderState {
        Color    fg     = Color::default_color();
        Color    bg     = Color::default_color();
        uint16_t attr   = 0;
        int      cur_x  = -1;
        int      cur_y  = -1;
    };

    std::string out_;

    // ── emit_move ─────────────────────────────────────────────────────────
    void emit_move(int x, int y, RenderState& st) {
        if (st.cur_x == x && st.cur_y == y) return;
        // [BUG-02] 공백 채우기 최적화 제거: 색상 오염 위험이 있으므로
        //          항상 절대 이동 시퀀스 사용
        out_ += "\x1b[";
        out_ += std::to_string(y + 1);
        out_ += ';';
        out_ += std::to_string(x + 1);
        out_ += 'H';
        st.cur_x = x;
        st.cur_y = y;
    }

    // ── emit_attr_diff ────────────────────────────────────────────────────
    void emit_attr_diff(const Cell& cell, RenderState& st) {
        bool fg_changed = (cell.fg != st.fg);
        bool bg_changed = (cell.bg != st.bg);
        uint16_t attr_on  = cell.attr & ~st.attr;
        uint16_t attr_off = st.attr  & ~cell.attr;

        if (!fg_changed && !bg_changed && attr_on == 0 && attr_off == 0) return;

        bool do_reset =
            (std::popcount(static_cast<unsigned>(attr_off)) >= 3) ||
            (attr_off != 0 && (fg_changed || bg_changed));

        if (do_reset) {
            out_ += "\x1b[0m";
            st.fg   = Color::default_color();
            st.bg   = Color::default_color();
            st.attr = 0;
            attr_on    = cell.attr;
            fg_changed = (cell.fg.type != Color::Type::Default);
            bg_changed = (cell.bg.type != Color::Type::Default);
            attr_off   = 0;
        }

        std::string params;
        params.reserve(32);

        for (int i = 0; i < 8; ++i)
            if (attr_on & (1 << i)) append_int_param(params, Attr::kSGR[i].on);

        if (!do_reset)
            for (int i = 0; i < 8; ++i)
                if (attr_off & (1 << i)) append_int_param(params, Attr::kSGR[i].off);

        if (fg_changed) append_color_fg(params, cell.fg);
        if (bg_changed) append_color_bg(params, cell.bg);

        if (!params.empty()) {
            out_ += "\x1b[";
            out_ += params;
            out_ += 'm';
        }

        st.fg   = cell.fg;
        st.bg   = cell.bg;
        st.attr = cell.attr;
    }

    // ── emit_cell_text ────────────────────────────────────────────────────
    void emit_cell_text(const Cell& cell) {
        if (cell.grapheme.empty() || cell.width == 0) out_ += ' ';
        else                                           out_ += cell.grapheme;
    }

    // ── 정적 SGR 파라미터 헬퍼 ───────────────────────────────────────────
    static void append_int_param(std::string& p, int v) {
        if (!p.empty()) p += ';';
        // [OPT-05] 스택 버퍼 itoa (0~255 범위)
        if (v >= 0 && v < 256) {
            char buf[4]; int n = 0;
            if (v >= 100) { buf[n++] = char('0' + v/100); v %= 100; buf[n++] = char('0' + v/10); v %= 10; }
            else if (v >= 10) { buf[n++] = char('0' + v/10); v %= 10; }
            buf[n++] = char('0' + v);
            p.append(buf, n);
        } else {
            p += std::to_string(v);
        }
    }

    static void append_color_fg(std::string& p, const Color& c) {
        switch (c.type) {
        case Color::Type::Default: append_int_param(p, 39); break;
        case Color::Type::Index256:
            if      (c.index <  8) append_int_param(p, 30 + c.index);
            else if (c.index < 16) append_int_param(p, 90 + c.index - 8);
            else { append_int_param(p,38); p+=";5;"; p+=std::to_string(c.index); }
            break;
        case Color::Type::TrueColor:
            append_int_param(p,38);
            p+=";2;"; p+=std::to_string(c.r);
            p+=';';   p+=std::to_string(c.g);
            p+=';';   p+=std::to_string(c.b);
            break;
        }
    }

    static void append_color_bg(std::string& p, const Color& c) {
        switch (c.type) {
        case Color::Type::Default: append_int_param(p, 49); break;
        case Color::Type::Index256:
            if      (c.index <  8) append_int_param(p, 40 + c.index);
            else if (c.index < 16) append_int_param(p, 100 + c.index - 8);
            else { append_int_param(p,48); p+=";5;"; p+=std::to_string(c.index); }
            break;
        case Color::Type::TrueColor:
            append_int_param(p,48);
            p+=";2;"; p+=std::to_string(c.r);
            p+=';';   p+=std::to_string(c.g);
            p+=';';   p+=std::to_string(c.b);
            break;
        }
    }
};

} // namespace term
