#ifndef TUI_HPP
#define TUI_HPP

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <iomanip>
#include <cmath>
#include <sstream>

#include "ansi.hpp"
#include "util.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace tui {

    // util::align을 그대로 사용
    using util::align;

    // ==========================================
    // 유틸리티 함수
    // ==========================================


    // 시간 포맷팅 (util::sec2str 사용)
    inline std::wstring format_time(double seconds) {
        if (std::isinf(seconds) || std::isnan(seconds) || seconds < 0) {
            return L"00:00";
        }
        return util::utf8_to_wstring(util::sec2str(seconds));
    }

    // ==========================================
    // 안전한 wstring 출력 (util 사용)
    // ==========================================
    inline void print(const std::wstring& wstr) {
        std::cout << util::wstring_to_utf8(wstr);
    }

    // ==========================================
    // 컴포넌트: Progress Bar
    // ==========================================
    inline std::string progress_bar(
            double percent,
            const std::string& message,
            int width = 40,
            bool show_percentage = true,
            const std::string& done_color = "green",
            const std::string& remain_color = "bright_black"
        ) {
            if (percent < 0) percent = 0;
            if (percent > 100) percent = 100;
            double progress = percent / 100.0;
            
            std::wstring msg;
            if (show_percentage) {
                std::wstringstream ss;
                ss << util::utf8_to_wstring(message) << L" " << std::fixed << std::setprecision(1) << percent << L"%";
                msg = ss.str();
            } else {
                msg = util::utf8_to_wstring(message);
            }
            
            // 전체 텍스트 라인 생성 (정렬됨)
            std::wstring line = util::aligned_text(msg, width, util::align::center);
            
            // 진행된 길이 계산
            size_t filled_len = static_cast<size_t>(progress * line.length());
            
            // 버퍼에 진행 바 구성
            std::string result = ansi::buffer()
                .clear()
                .text("[")
                .bg(done_color)
                .fg("white")
                .text(util::wstring_to_utf8(line.substr(0, filled_len)))
                .bg(remain_color)
                .fg("white")
                .text(util::wstring_to_utf8(line.substr(filled_len)))
                .reset()
                .text("]")
                .str();
            return result;
        }

    // 메시지가 오버레이된 진행률 바
    inline void print_progress_bar(
        double percent,
        const std::wstring& message,
        int width = 40,
        bool show_percentage = true,
        const std::string& done_color = "green",
        const std::string& remain_color = "bright_black"
    ) {
        std::string bar = progress_bar(
            percent,
            util::wstring_to_utf8(message),
            width,
            show_percentage,
            done_color,
            remain_color
        );
        print(util::utf8_to_wstring(bar));
    }

    // ==========================================
    // 컴포넌트: Box (Border)
    // ==========================================

    struct border_style {
        wchar_t tl, tr, bl, br, h, v; // TopLeft, TopRight, BottomLeft, BottomRight, Horizontal, Vertical
    };

    namespace border {
        constexpr border_style single = { L'┌', L'┐', L'└', L'┘', L'─', L'│' };
        constexpr border_style double_line = { L'╔', L'╗', L'╚', L'╝', L'═', L'║' };
        constexpr border_style round = { L'╭', L'╮', L'╰', L'╯', L'─', L'│' };
        constexpr border_style heavy = { L'┏', L'┓', L'┗', L'┛', L'━', L'┃' };
        constexpr border_style ascii = { L'+', L'+', L'+', L'+', L'-', L'|' };
    }

    // 박스 그리기 함수 (ansi 라이브러리 사용)
    inline void draw_box(
        int x, int y, 
        int width, int height, 
        const std::wstring& title = L"", 
        const std::string& color = "white",
        const border_style& style = border::round
    ) {
        // 상단 테두리
        ansi::buffer()
            .xy(x, y)
            .fg(color)
            .text(util::wstring_to_utf8(std::wstring(1, style.tl)))
            .flush();
        
        if (!title.empty()) {
            int title_w = util::visual_width(title);
            int available_w = width - 2;
            if (title_w > available_w) {
                print(std::wstring(available_w, style.h)); 
            } else {
                int left_pad = (available_w - title_w) / 2;
                int right_pad = available_w - title_w - left_pad;
                print(std::wstring(left_pad, style.h));
                print(title);
                print(std::wstring(right_pad, style.h));
            }
        } else {
            print(std::wstring(width - 2, style.h));
        }
        print(std::wstring(1, style.tr));

        // 중간 내용 (양쪽 세로 라인)
        for (int i = 1; i < height - 1; ++i) {
            ansi::buffer()
                .xy(x, y + i)
                .fg(color)
                .text(util::wstring_to_utf8(std::wstring(1, style.v)))
                .flush();
            
            ansi::buffer()
                .xy(x + width - 1, y + i)
                .fg(color)
                .text(util::wstring_to_utf8(std::wstring(1, style.v)))
                .flush();
        }

        // 하단 테두리
        ansi::buffer()
            .xy(x, y + height - 1)
            .fg(color)
            .text(util::wstring_to_utf8(std::wstring(1, style.bl)))
            .text(util::wstring_to_utf8(std::wstring(width - 2, style.h)))
            .text(util::wstring_to_utf8(std::wstring(1, style.br)))
            .reset()
            .flush();
    }

    // ==========================================
    // 컴포넌트: Table
    // ==========================================

    class table {
        struct column {
            std::wstring header;
            int width;
            align alignment;
        };
        std::vector<column> cols_;
        std::vector<std::vector<std::wstring>> rows_;
        int spacing_ = 2;

    public:
        table& add_column(const std::wstring& name, int width, align a = align::left) {
            cols_.push_back({name, width, a});
            return *this;
        }

        table& add_row(const std::vector<std::wstring>& row_data) {
            rows_.push_back(row_data);
            return *this;
        }

        void print_table(int start_x, int start_y, const std::string& header_fg = "bright_cyan") {
            int current_y = start_y;

            // 헤더 출력 (ansi::buffer 사용)
            ansi::buffer().xy(start_x, current_y).flush();
            
            for (const auto& col : cols_) {
                ansi::buffer()
                    .fg(header_fg)
                    .style("bold")
                    .text(util::wstring_to_utf8(util::aligned_text(col.header, col.width, align::center)))
                    .reset()
                    .flush();
                print(std::wstring(spacing_, L' '));
            }
            current_y++;
            
            // 구분선 (ansi::buffer 사용)
            ansi::buffer().xy(start_x, current_y).fg("bright_black").flush();
            
            for (const auto& col : cols_) {
                print(std::wstring(col.width, L'─'));
                print(std::wstring(spacing_, L' '));
            }
            
            std::cout << ansi::reset();
            current_y++;

            // 데이터 출력
            for (const auto& row : rows_) {
                std::cout << ansi::xy(start_x, current_y);
                for (size_t i = 0; i < cols_.size(); ++i) {
                    std::wstring val = (i < row.size()) ? row[i] : L"";
                    print(util::aligned_text(val, cols_[i].width, cols_[i].alignment));
                    print(std::wstring(spacing_, L' '));
                }
                current_y++;
            }
        }
    };

    // ==========================================
    // 컴포넌트: Input Prompt
    // ==========================================

    inline std::wstring prompt(const std::wstring& question, const std::string& color = "bright_yellow") {
        // ansi::buffer를 사용한 색상 프롬프트
        ansi::buffer()
            .fg("green")
            .text("? ")
            .fg(color)
            .style("bold")
            .text(util::wstring_to_utf8(question))
            .reset()
            .text(" ")
            .flush();
        
        std::wstring input;
        std::getline(std::wcin, input);
        return input;
    }

} // namespace tui

#endif // TUI_HPP