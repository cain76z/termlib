#pragma once
/**
 * ansi_tui_dlg.hpp — 터미널 다이얼로그 위젯  (PRD Phase 3 확장)
 *
 * 구성:
 *   FileOpenDialog  — 파일/폴더 선택 다이얼로그
 *   FileSaveDialog  — 파일 저장 다이얼로그 (파일명 입력 + 덮어쓰기 확인)
 *   SelectDialog    — 항목 선택 다이얼로그 (단일/다중)
 *   run_dialog<T>() — 모달 이벤트 루프 헬퍼
 *
 * 의존 방향:
 *   ansi_tui.hpp (Widget, TextInput, MessageBox …)
 *   ansi_renderer.hpp (AnsiRenderer, InputDriver)
 *   <filesystem> (C++17/20)
 *
 * 구현부는 모두 클래스 본체 안에 위치한다.
 * 내부 헬퍼(dlg_detail)는 클래스 선언 앞에 배치한다.
 */
/*
                      ╚══════════════════════════════════╝

                             데모 항목을 선택하세요

                 [1]  FileOpenDialog  (단일 선택)                 ← 파일 하나 선
택
                 [2]  FileOpenDialog  (다중 선택)                 ← Space로 여러
 파일 선택       [1]  FileOpenDialog  (단일 선택)
                 [3]  FileSaveDialog                              ← 파일 저장 경
로 입력          [2]  FileOpenDialog  (다중 선택)
                 [4]  SelectDialog    (단일)                      ← 목록에서 항
목 택            [3]  FileSaveDialog
 선택
                 [4]  SelectDialog    (단일)                      ← 목록에서 여
러               [q]  종료                                        ← 프로그램 종
 항목 선택       [5]  SelectDialog    (다중)
                 [q]  종료                                        ← 프로그램 종
료               [q]  종료





                    ↑↓ 이동    Enter 실행    숫자 직접 선택    q 종료


*/
#include "ansi_tui.hpp"
#include "ansi_renderer.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace term {

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════════════════════════
//  다이얼로그 옵션 구조체
// ═══════════════════════════════════════════════════════════════════════════

struct FileDialogOptions {
    fs::path    initial_path  = ".";
    std::string title         = "파일 열기";
    bool        select_files  = true;
    bool        select_dirs   = false;
    bool        multi_select  = false;
    std::vector<std::string> filters;
    int dialog_w = 64;
    int dialog_h = 22;
};

struct SelectDialogOptions {
    std::string title        = "선택";
    bool        multi_select = false;
    int         initial_idx  = 0;
    std::vector<int> initial_selected;
    int dialog_w = 50;
    int dialog_h = 18;
};

// ═══════════════════════════════════════════════════════════════════════════
//  dlg_detail — 내부 렌더링 헬퍼  (클래스 선언 전에 정의해야 inline 참조 가능)
// ═══════════════════════════════════════════════════════════════════════════

namespace dlg_detail {

inline Widget::Rect calc_dialog_rect(const AnsiScreen& screen,
                                     int want_w, int want_h) {
    int sc = screen.cols(), sr = screen.rows();
    int w = (want_w <= 0) ? sc - 4 : std::min(want_w, sc - 2);
    int h = (want_h <= 0) ? sr - 4 : std::min(want_h, sr - 2);
    return { (sc - std::max(w, 20)) / 2,
             (sr - std::max(h,  8)) / 2,
              std::max(w, 20), std::max(h, 8) };
}

inline void fill_dialog_bg(AnsiScreen& screen, const Widget::Rect& r) {
    static const Color bg = Color::from_index(0);
    for (int row = r.y; row < r.y + r.h && row < screen.rows(); ++row) {
        AnsiString s;
        s.xy(r.x, row).bg(bg);
        s.text(std::string(static_cast<size_t>(r.w), ' ')).reset();
        screen.put(s);
    }
}

inline void draw_hline(AnsiScreen& screen,
                        int x, int y, int w,
                        std::string_view lc, std::string_view mid,
                        std::string_view rc, const Color& col) {
    AnsiString s;
    s.xy(x, y).fg(col).text(lc);
    for (int i = 0; i < w - 2; ++i) s.text(mid);
    s.text(rc).reset();
    screen.put(s);
}

inline void draw_button(AnsiScreen& screen, int x, int y,
                         std::string_view label, bool focused) {
    AnsiString s;
    s.xy(x, y);
    if (focused) s.fg(Color::from_index(14)).bold().text("┃ ").text(label).text(" ┃");
    else         s.text("[ ").text(label).text(" ]");
    s.reset();
    screen.put(s);
}

} // namespace dlg_detail

// ═══════════════════════════════════════════════════════════════════════════
//  FileOpenDialog
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class FileOpenDialog : public Widget {
public:
    explicit FileOpenDialog(const FileDialogOptions& opts = {})
        : opts_(opts)
    {
        if (opts_.multi_select && !opts_.select_files) opts_.multi_select = false;
        fs::path init = fs::absolute(opts_.initial_path);
        if (!fs::is_directory(init)) init = init.parent_path();
        try { init = fs::canonical(init); } catch (...) {}
        load_dir(init);
    }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;

        rect_ = dlg_detail::calc_dialog_rect(screen, opts_.dialog_w, opts_.dialog_h);
        int bx = rect_.x, by = rect_.y, bw = rect_.w, bh = rect_.h;

        dlg_detail::fill_dialog_bg(screen, rect_);
        render_border(screen, BorderStyle::Round, opts_.title);

        int ix = bx + 1, iw = bw - 2;

        render_path_bar(screen, ix, by + 1, iw);
        dlg_detail::draw_hline(screen, bx, by + 2, bw, "├","─","┤", theme_.border_normal);

        int list_y = by + 3;
        int list_h = bh - 7;
        if (list_h < 1) list_h = 1;
        render_list(screen, ix, list_y, iw, list_h);

        dlg_detail::draw_hline(screen, bx, by + bh - 4, bw, "├","─","┤", theme_.border_normal);
        render_buttons(screen, bx, by + bh - 3, bw, true, true);
        render_hint(screen, ix, by + bh - 2, iw);
    }

    bool handle_key(const KeyEvent& ev) override {
        if (closed_) return false;
        if (ev.key == KEY_ESC) { closed_ = true; return true; }

        if (ev.key == KEY_TAB) {
            focus_ = (focus_ + (ev.has_shift() ? 2 : 1)) % 3;
            return true;
        }

        if (focus_ == 0) {
            int list_h = std::max(1, rect_.h - 7);
            switch (ev.key) {
            case KEY_UP:        move_cursor(-1); return true;
            case KEY_DOWN:      move_cursor(+1); return true;
            case KEY_PGUP:      page_move(-1, list_h); return true;
            case KEY_PGDN:      page_move(+1, list_h); return true;
            case KEY_HOME:      cursor_ = 0; return true;
            case KEY_END:       cursor_ = std::max(0, (int)entries_.size()-1); return true;
            case KEY_ENTER:     enter_item(); return true;
            case KEY_BACKSPACE:
                if (cur_dir_.has_parent_path() && cur_dir_ != cur_dir_.root_path())
                    load_dir(cur_dir_.parent_path());
                return true;
            default:
                if (ev.key == ' ' && opts_.multi_select) { toggle_multi(cursor_); return true; }
                break;
            }
        } else if (focus_ == 1) {
            if (ev.key == KEY_ENTER || ev.key == ' ') { confirm(); return true; }
            if (ev.key == KEY_LEFT || ev.key == KEY_RIGHT) { focus_ = 2; return true; }
        } else {
            if (ev.key == KEY_ENTER || ev.key == ' ') { closed_ = true; return true; }
            if (ev.key == KEY_LEFT || ev.key == KEY_RIGHT) { focus_ = 1; return true; }
        }
        return false;
    }

    [[nodiscard]] bool closed()    const noexcept { return closed_; }
    [[nodiscard]] bool confirmed() const noexcept { return confirmed_; }

    [[nodiscard]] const std::vector<fs::path>& selected_paths() const noexcept {
        return selected_paths_;
    }
    [[nodiscard]] std::optional<fs::path> selected_path() const {
        if (selected_paths_.empty()) return std::nullopt;
        return selected_paths_.front();
    }

protected:
    FileDialogOptions opts_;

    struct FileEntry {
        fs::path    path;
        std::string name;
        bool        is_dir    = false;
        bool        is_parent = false;
        uintmax_t   size      = 0;
    };

    fs::path               cur_dir_;
    std::vector<FileEntry> entries_;
    int                    cursor_ = 0;
    int                    scroll_ = 0;
    std::vector<int>       multi_sel_;
    int                    focus_  = 0;   // 0=목록, 1=확인, 2=취소

    bool                   closed_    = false;
    bool                   confirmed_ = false;
    std::vector<fs::path>  selected_paths_;

    // ── 디렉터리 로드 ─────────────────────────────────────────────────────
    void load_dir(const fs::path& p) {
        cur_dir_ = p;
        entries_.clear();
        cursor_ = 0; scroll_ = 0;
        multi_sel_.clear();

        if (cur_dir_.has_parent_path() && cur_dir_ != cur_dir_.root_path()) {
            FileEntry pe;
            pe.path = cur_dir_.parent_path(); pe.name = "..";
            pe.is_dir = true; pe.is_parent = true;
            entries_.push_back(std::move(pe));
        }

        try {
            std::vector<FileEntry> dirs, files;
            for (const auto& ent : fs::directory_iterator(cur_dir_)) {
                FileEntry fe;
                fe.path   = ent.path();
                fe.name   = ent.path().filename().string();
                fe.is_dir = ent.is_directory();
                if (!fe.is_dir) {
                    if (!match_filter(fe.name)) continue;
                    try { fe.size = ent.file_size(); } catch (...) {}
                }
                (fe.is_dir ? dirs : files).push_back(std::move(fe));
            }
            auto ci_sort = [](const FileEntry& a, const FileEntry& b) {
                std::string la = a.name, lb = b.name;
                std::transform(la.begin(), la.end(), la.begin(), ::tolower);
                std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
                return la < lb;
            };
            std::sort(dirs.begin(),  dirs.end(),  ci_sort);
            std::sort(files.begin(), files.end(), ci_sort);
            for (auto& d : dirs)  entries_.push_back(std::move(d));
            for (auto& f : files) entries_.push_back(std::move(f));
        } catch (...) {}
    }

    bool match_filter(const std::string& name) const {
        if (opts_.filters.empty()) return true;
        for (const auto& pat : opts_.filters) {
            if (pat == "*" || pat == "*.*") return true;
            if (pat.size() >= 2 && pat[0] == '*') {
                std::string ext = pat.substr(1);
                if (name.size() >= ext.size() &&
                    name.substr(name.size() - ext.size()) == ext) return true;
            } else {
                if (name == pat) return true;
            }
        }
        return false;
    }

    static std::string format_size(uintmax_t sz) {
        char buf[16];
        if      (sz < 1024ULL)               std::snprintf(buf,sizeof(buf),"%uB",   (unsigned)sz);
        else if (sz < 1024ULL*1024)          std::snprintf(buf,sizeof(buf),"%.1fK", sz/1024.0);
        else if (sz < 1024ULL*1024*1024)     std::snprintf(buf,sizeof(buf),"%.1fM", sz/(1024.0*1024));
        else                                 std::snprintf(buf,sizeof(buf),"%.1fG", sz/(1024.0*1024*1024));
        return buf;
    }

    bool is_selectable(const FileEntry& e) const {
        if (e.is_parent) return false;
        if ( e.is_dir && opts_.select_dirs)  return true;
        if (!e.is_dir && opts_.select_files) return true;
        return false;
    }

    // ── 커서 이동 ─────────────────────────────────────────────────────────
    void move_cursor(int delta) {
        if (entries_.empty()) return;
        cursor_ = std::clamp(cursor_ + delta, 0, (int)entries_.size() - 1);
    }
    void page_move(int delta, int list_h) {
        if (entries_.empty()) return;
        cursor_ = std::clamp(cursor_ + delta * list_h, 0, (int)entries_.size() - 1);
    }

    void enter_item() {
        if (cursor_ < 0 || cursor_ >= (int)entries_.size()) return;
        const auto& e = entries_[cursor_];
        if (e.is_dir) {
            if (opts_.select_dirs && !e.is_parent && multi_sel_.empty()) {
                selected_paths_ = { e.path };
                confirmed_ = true; closed_ = true;
                return;
            }
            load_dir(e.path);
        } else if (opts_.select_files) {
            if (opts_.multi_select && !multi_sel_.empty()) confirm();
            else { selected_paths_ = { e.path }; confirmed_ = true; closed_ = true; }
        }
    }

    void toggle_multi(int idx) {
        if (!opts_.multi_select || idx < 0 || idx >= (int)entries_.size()) return;
        if (!is_selectable(entries_[idx])) return;
        auto it = std::find(multi_sel_.begin(), multi_sel_.end(), idx);
        if (it == multi_sel_.end()) multi_sel_.push_back(idx);
        else                        multi_sel_.erase(it);
    }

    void confirm() {
        if (opts_.multi_select && !multi_sel_.empty()) {
            for (int i : multi_sel_)
                if (i < (int)entries_.size()) selected_paths_.push_back(entries_[i].path);
        } else if (cursor_ >= 0 && cursor_ < (int)entries_.size()) {
            const auto& e = entries_[cursor_];
            if (is_selectable(e)) selected_paths_.push_back(e.path);
        }
        if (!selected_paths_.empty()) { confirmed_ = true; closed_ = true; }
    }

    // ── 렌더링 서브루틴 ───────────────────────────────────────────────────
    void render_path_bar(AnsiScreen& screen, int x, int y, int w) const {
        std::string_view label = "경로: ";
        int label_w = (int)UniString::display_width(label);
        int avail   = w - label_w;
        if (avail <= 0) return;
        std::string clipped = UniString::clip(cur_dir_.string(), avail, "…");
        AnsiString s;
        s.xy(x, y).fg(Color::from_index(14)).text(label)
                  .fg(Color::default_color()).text(clipped).reset();
        int used = label_w + (int)UniString::display_width(clipped);
        for (int i = used; i < w; ++i) s.text(" ");
        screen.put(s);
    }

    void render_list(AnsiScreen& screen, int lx, int ly, int lw, int lh) const {
        auto& scroll = const_cast<int&>(scroll_);
        if (cursor_ < scroll)       scroll = cursor_;
        if (cursor_ >= scroll + lh) scroll = cursor_ - lh + 1;
        scroll = std::max(scroll, 0);

        static const Color c_dir    = Color::from_index(12);
        static const Color c_sel    = Color::from_index(11);
        static const Color c_cur_bg = Color::from_index(4);
        static const Color c_cur_fg = Color::from_index(15);
        static const Color c_size   = Color::from_index(8);

        int n = (int)entries_.size();
        for (int row = 0; row < lh; ++row) {
            int idx      = scroll + row;
            int screen_y = ly + row;

            if (idx >= n) {
                // fill_dialog_bg(black) 와 동일한 bg — diff 가 이전 화면 글자를 SKIP 하지 않도록
                AnsiString s;
                s.xy(lx, screen_y)
                 .bg(Color::from_index(0))
                 .text(std::string(static_cast<size_t>(lw), ' '))
                 .reset();
                screen.put(s);
                continue;
            }

            const auto& e = entries_[idx];
            bool is_cursor = (idx == cursor_);
            bool is_msel   = opts_.multi_select &&
                             std::find(multi_sel_.begin(), multi_sel_.end(), idx) != multi_sel_.end();

            AnsiString s;
            s.xy(lx, screen_y);
            if (is_cursor)    s.bg(c_cur_bg).fg(c_cur_fg);
            else if (is_msel) s.fg(c_sel);
            else if (e.is_dir) s.fg(c_dir);

            std::string_view icon = e.is_parent ? "↑  " : e.is_dir ? "📁 " : "📄 ";
            s.text(icon);

            std::string size_str;
            int size_w = 0;
            if (!e.is_dir) { size_str = format_size(e.size); size_w = (int)size_str.size() + 1; }

            int icon_w = (int)UniString::display_width(std::string(icon));
            int name_w = std::max(4, lw - icon_w - size_w);

            std::string name_c = UniString::clip(e.name, name_w);
            s.text(name_c);

            int used = icon_w + (int)UniString::display_width(name_c);
            int pad  = lw - used - size_w;
            for (int i = 0; i < pad; ++i) s.text(" ");

            if (!size_str.empty()) {
                if (!is_cursor) s.fg(c_size);
                s.text(" ").text(size_str);
            }
            s.reset();
            screen.put(s);

            // 다중선택 ✓ 오버레이
            if (is_msel && !is_cursor) {
                AnsiString ck;
                ck.xy(lx, screen_y).fg(c_sel).text("✓").reset();
                screen.put(ck);
            }
        }
    }

    void render_buttons(AnsiScreen& screen,
                         int bx, int by, int total_w,
                         bool has_ok, bool has_cancel) const {
        static const std::string_view ok_lbl     = "확인";
        static const std::string_view cancel_lbl = "취소";
        int ok_w     = 2 + (int)UniString::display_width(ok_lbl)     + 2;
        int cancel_w = 2 + (int)UniString::display_width(cancel_lbl) + 2;
        int total_btn = (has_ok ? ok_w : 0) + (has_ok && has_cancel ? 2 : 0) + (has_cancel ? cancel_w : 0);
        int x = bx + (total_w - total_btn) / 2;
        if (has_ok) {
            dlg_detail::draw_button(screen, x, by, ok_lbl, focus_ == 1);
            x += ok_w + 2;
        }
        if (has_cancel)
            dlg_detail::draw_button(screen, x, by, cancel_lbl, focus_ == 2);
    }

    void render_hint(AnsiScreen& screen, int x, int y, int w) const {
        static const char* hint = "↑↓이동 Enter열기 BS상위 Space다중 Tab버튼 Esc취소";
        AnsiString s;
        s.xy(x, y).fg(Color::from_index(8)).text(UniString::clip(hint, w)).reset();
        screen.put(s);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  FileSaveDialog
//  FileOpenDialog 를 상속하여 탐색 로직을 재사용한다.
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class FileSaveDialog : public FileOpenDialog {
public:
    explicit FileSaveDialog(const FileDialogOptions& opts_in = {})
        : FileOpenDialog([&]{
            auto o = opts_in;
            if (o.title.empty()) o.title = "파일 저장";
            o.multi_select = false;
            o.select_files = true;
            return o;
          }())
        , filename_input_("파일명 입력...")
        , overwrite_mb_("덮어쓰기 확인",
                        "이미 존재하는 파일입니다.\n덮어쓰시겠습니까?",
                        MessageBox::Type::Warning,
                        MessageBox::Buttons::YesNo)
    {
        focus_ = 0;
    }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;

        if (overwrite_confirm_pending_) {
            FileOpenDialog::render(screen);
            overwrite_mb_.render(screen);
            return;
        }

        rect_ = dlg_detail::calc_dialog_rect(screen, opts_.dialog_w, opts_.dialog_h);
        int bx = rect_.x, by = rect_.y, bw = rect_.w, bh = rect_.h;

        dlg_detail::fill_dialog_bg(screen, rect_);
        render_border(screen, BorderStyle::Round, opts_.title);

        int ix = bx + 1, iw = bw - 2;

        render_path_bar(screen, ix, by + 1, iw);
        dlg_detail::draw_hline(screen, bx, by + 2, bw, "├","─","┤", theme_.border_normal);

        int list_y = by + 3;
        int list_h = bh - 9;
        if (list_h < 1) list_h = 1;
        render_list(screen, ix, list_y, iw, list_h);

        dlg_detail::draw_hline(screen, bx, by + bh - 6, bw, "├","─","┤", theme_.border_normal);

        // 파일명 입력
        {
            std::string_view lbl_text = "파일명: ";
            int lbl_w = (int)UniString::display_width(lbl_text);
            AnsiString lbl;
            lbl.xy(ix, by + bh - 5).fg(Color::from_index(14)).text(lbl_text).reset();
            screen.put(lbl);
            filename_input_.set_rect({ix + lbl_w, by + bh - 5, iw - lbl_w, 1});
            filename_input_.set_focused(focus_ == 1);
            filename_input_.render(screen);
        }

        dlg_detail::draw_hline(screen, bx, by + bh - 4, bw, "├","─","┤", theme_.border_normal);
        render_buttons(screen, bx, by + bh - 3, bw, true, true);

        AnsiString hint;
        hint.xy(ix, by + bh - 2)
            .fg(Color::from_index(8))
            .text(UniString::clip("↑↓이동 Enter폴더진입 BS상위 Tab이동 Esc취소", iw))
            .reset();
        screen.put(hint);
    }

    bool handle_key(const KeyEvent& ev) override {
        if (closed_) return false;

        if (overwrite_confirm_pending_) {
            overwrite_mb_.handle_key(ev);
            if (overwrite_mb_.closed()) {
                overwrite_confirm_pending_ = false;
                if (overwrite_mb_.result() == MessageBox::Result::Yes)
                    { confirmed_ = true; closed_ = true; }
            }
            return true;
        }

        if (ev.key == KEY_ESC) { closed_ = true; return true; }

        // TAB: 0=목록, 1=파일명, 2=확인, 3=취소
        if (ev.key == KEY_TAB) {
            focus_ = (focus_ + (ev.has_shift() ? 3 : 1)) % 4;
            return true;
        }

        if (focus_ == 0) {
            int list_h = std::max(1, rect_.h - 9);
            switch (ev.key) {
            case KEY_UP:        move_cursor(-1);  return true;
            case KEY_DOWN:      move_cursor(+1);  return true;
            case KEY_PGUP:      page_move(-1, list_h); return true;
            case KEY_PGDN:      page_move(+1, list_h); return true;
            case KEY_HOME:      cursor_ = 0; return true;
            case KEY_END:       cursor_ = std::max(0,(int)entries_.size()-1); return true;
            case KEY_BACKSPACE:
                if (cur_dir_.has_parent_path()) load_dir(cur_dir_.parent_path());
                return true;
            case KEY_ENTER:
                if (cursor_ < (int)entries_.size()) {
                    const auto& e = entries_[cursor_];
                    if (e.is_dir) load_dir(e.path);
                    else { filename_input_.set_value(e.name); focus_ = 1; }
                }
                return true;
            default: break;
            }
        } else if (focus_ == 1) {
            if (ev.key == KEY_ENTER) { check_overwrite_and_confirm(); return true; }
            filename_input_.handle_key(ev);
            return true;
        } else if (focus_ == 2) {
            if (ev.key == KEY_ENTER || ev.key == ' ') { check_overwrite_and_confirm(); return true; }
            if (ev.key == KEY_LEFT || ev.key == KEY_RIGHT) { focus_ = 3; return true; }
        } else {
            if (ev.key == KEY_ENTER || ev.key == ' ') { closed_ = true; return true; }
            if (ev.key == KEY_LEFT || ev.key == KEY_RIGHT) { focus_ = 2; return true; }
        }
        return false;
    }

    [[nodiscard]] std::optional<fs::path> save_path() const {
        if (!confirmed_) return std::nullopt;
        std::string fn = filename_input_.value();
        if (fn.empty()) return std::nullopt;
        return cur_dir_ / fn;
    }

    void set_filename(std::string_view name) { filename_input_.set_value(name); }

private:
    TextInput   filename_input_;
    bool        overwrite_confirm_pending_ = false;
    MessageBox  overwrite_mb_;

    void check_overwrite_and_confirm() {
        std::string fn = filename_input_.value();
        if (fn.empty()) return;
        fs::path target = cur_dir_ / fn;
        if (fs::exists(target)) { overwrite_confirm_pending_ = true; return; }
        confirmed_ = true; closed_ = true;
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  SelectDialog
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class SelectDialog : public Widget {
public:
    explicit SelectDialog(std::vector<std::string> items,
                          const SelectDialogOptions& opts = {})
        : opts_(opts), items_(std::move(items))
    {
        cursor_ = std::clamp(opts_.initial_idx, 0,
                             items_.empty() ? 0 : (int)items_.size() - 1);
        if (opts_.multi_select) {
            for (int i : opts_.initial_selected)
                if (i >= 0 && i < (int)items_.size() &&
                    std::find(selected_.begin(), selected_.end(), i) == selected_.end())
                    selected_.push_back(i);
        } else if (!opts_.initial_selected.empty()) {
            int i = opts_.initial_selected.front();
            if (i >= 0 && i < (int)items_.size()) selected_ = {i};
        }
    }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        rect_ = dlg_detail::calc_dialog_rect(screen, opts_.dialog_w, opts_.dialog_h);
        int bx = rect_.x, by = rect_.y, bw = rect_.w, bh = rect_.h;
        int ix = bx + 1, iw = bw - 2;

        dlg_detail::fill_dialog_bg(screen, rect_);
        render_border(screen, BorderStyle::Round, opts_.title);

        // 선택 상태 표시
        if (opts_.multi_select) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%d개 선택됨", (int)selected_.size());
            AnsiString s;
            s.xy(ix, by + 1).fg(Color::from_index(11)).text(buf);
            int used = (int)UniString::display_width(buf);
            for (int i = used; i < iw; ++i) s.text(" ");
            s.reset();
            screen.put(s);
        } else {
            std::string info = selected_.empty() ? "선택 없음" :
                               UniString::clip(items_[selected_.front()], iw - 7);
            AnsiString s;
            s.xy(ix, by + 1)
             .fg(Color::from_index(8)).text("선택: ")
             .fg(Color::from_index(14)).text(info).reset();
            screen.put(s);
        }

        dlg_detail::draw_hline(screen, bx, by + 2, bw, "├","─","┤", theme_.border_normal);

        int list_y = by + 3;
        int list_h = bh - 7;
        if (list_h < 1) list_h = 1;
        render_list(screen, ix, list_y, iw, list_h);

        dlg_detail::draw_hline(screen, bx, by + bh - 4, bw, "├","─","┤", theme_.border_normal);
        render_buttons(screen, bx, by + bh - 3, bw);
        render_hint(screen, ix, by + bh - 2, iw);
    }

    bool handle_key(const KeyEvent& ev) override {
        if (closed_) return false;
        if (ev.key == KEY_ESC) { closed_ = true; return true; }

        if (ev.key == KEY_TAB) {
            focus_ = (focus_ + (ev.has_shift() ? 2 : 1)) % 3;
            return true;
        }

        if (focus_ == 0) {
            int list_h = std::max(1, rect_.h - 6);
            switch (ev.key) {
            case KEY_UP:    move_cursor(-1); return true;
            case KEY_DOWN:  move_cursor(+1); return true;
            case KEY_PGUP:  page_move(-1, list_h); return true;
            case KEY_PGDN:  page_move(+1, list_h); return true;
            case KEY_HOME:  cursor_ = 0; return true;
            case KEY_END:   cursor_ = std::max(0,(int)items_.size()-1); return true;
            case KEY_ENTER:
                if (!opts_.multi_select) { toggle_select(cursor_); confirm(); }
                else { toggle_select(cursor_); move_cursor(+1); }
                return true;
            default:
                if (ev.key == ' ') {
                    toggle_select(cursor_);
                    if (!opts_.multi_select) confirm();
                    return true;
                }
                break;
            }
        } else if (focus_ == 1) {
            if (ev.key == KEY_ENTER || ev.key == ' ') { confirm(); return true; }
            if (ev.key == KEY_LEFT || ev.key == KEY_RIGHT) { focus_ = 2; return true; }
        } else {
            if (ev.key == KEY_ENTER || ev.key == ' ') { closed_ = true; return true; }
            if (ev.key == KEY_LEFT || ev.key == KEY_RIGHT) { focus_ = 1; return true; }
        }
        return false;
    }

    [[nodiscard]] bool closed()    const noexcept { return closed_; }
    [[nodiscard]] bool confirmed() const noexcept { return confirmed_; }

    [[nodiscard]] const std::vector<int>& selected_indices() const noexcept { return selected_; }

    [[nodiscard]] std::vector<std::string> selected_items() const {
        std::vector<std::string> out;
        for (int i : selected_) if (i < (int)items_.size()) out.push_back(items_[i]);
        return out;
    }
    [[nodiscard]] std::optional<int> selected_index() const {
        return selected_.empty() ? std::nullopt : std::optional<int>(selected_.front());
    }
    [[nodiscard]] std::optional<std::string> selected_item() const {
        auto idx = selected_index();
        return idx ? std::optional<std::string>(items_[*idx]) : std::nullopt;
    }

private:
    SelectDialogOptions      opts_;
    std::vector<std::string> items_;
    int                      cursor_    = 0;
    int                      scroll_    = 0;
    int                      focus_     = 0;
    std::vector<int>         selected_;
    bool                     closed_    = false;
    bool                     confirmed_ = false;

    void move_cursor(int delta) {
        if (items_.empty()) return;
        cursor_ = std::clamp(cursor_ + delta, 0, (int)items_.size() - 1);
    }
    void page_move(int delta, int list_h) {
        if (items_.empty()) return;
        cursor_ = std::clamp(cursor_ + delta * list_h, 0, (int)items_.size() - 1);
    }
    void toggle_select(int idx) {
        if (idx < 0 || idx >= (int)items_.size()) return;
        if (!opts_.multi_select) { selected_ = {idx}; return; }
        auto it = std::find(selected_.begin(), selected_.end(), idx);
        if (it == selected_.end()) selected_.push_back(idx);
        else                       selected_.erase(it);
    }
    void confirm() {
        if (selected_.empty() && !items_.empty()) toggle_select(cursor_);
        if (!selected_.empty()) { confirmed_ = true; closed_ = true; }
    }

    void render_list(AnsiScreen& screen, int lx, int ly, int lw, int lh) const {
        auto& scroll = const_cast<int&>(scroll_);
        if (cursor_ < scroll)       scroll = cursor_;
        if (cursor_ >= scroll + lh) scroll = cursor_ - lh + 1;
        scroll = std::max(scroll, 0);

        static const Color c_sel_fg = Color::from_index(11);
        static const Color c_cur_bg = Color::from_index(4);
        static const Color c_cur_fg = Color::from_index(15);

        int n = (int)items_.size();
        for (int row = 0; row < lh; ++row) {
            int idx      = scroll + row;
            int screen_y = ly + row;
            if (idx >= n) {
                // fill_dialog_bg(black) 와 동일한 bg
                AnsiString s;
                s.xy(lx, screen_y)
                 .bg(Color::from_index(0))
                 .text(std::string(static_cast<size_t>(lw), ' '))
                 .reset();
                screen.put(s);
                continue;
            }
            bool is_cursor = (idx == cursor_);
            bool is_sel    = std::find(selected_.begin(), selected_.end(), idx) != selected_.end();

            AnsiString s;
            s.xy(lx, screen_y);
            if (is_cursor) s.bg(c_cur_bg).fg(c_cur_fg);
            else if (is_sel) s.fg(c_sel_fg);

            s.text(opts_.multi_select ? (is_sel ? "  ✓ " : "    ")
                                      : (is_sel ? "  ● " : "  ○ "));
            int prefix_w = 4;
            int name_w   = lw - prefix_w;
            std::string name_c = UniString::clip(items_[idx], name_w);
            s.text(name_c);
            int used = prefix_w + (int)UniString::display_width(name_c);
            for (int i = used; i < lw; ++i) s.text(" ");
            s.reset();
            screen.put(s);
        }
    }

    void render_buttons(AnsiScreen& screen, int bx, int by, int tw) const {
        static const std::string_view ok_lbl     = "확인";
        static const std::string_view cancel_lbl = "취소";
        int ok_w     = 2 + (int)UniString::display_width(ok_lbl)     + 2;
        int cancel_w = 2 + (int)UniString::display_width(cancel_lbl) + 2;
        int x = bx + (tw - ok_w - 2 - cancel_w) / 2;
        dlg_detail::draw_button(screen, x, by, ok_lbl,     focus_ == 1);
        x += ok_w + 2;
        dlg_detail::draw_button(screen, x, by, cancel_lbl, focus_ == 2);
    }

    void render_hint(AnsiScreen& screen, int x, int y, int w) const {
        const char* hint = opts_.multi_select
            ? "↑↓이동 Space선택 Enter확인 Tab버튼 Esc취소"
            : "↑↓이동 Enter/Space선택 Tab버튼 Esc취소";
        AnsiString s;
        s.xy(x, y).fg(Color::from_index(8)).text(UniString::clip(hint, w)).reset();
        screen.put(s);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  run_dialog — 모달 이벤트 루프 헬퍼
// ═══════════════════════════════════════════════════════════════════════════

template<typename Dialog>
inline void run_dialog(Dialog& dlg, AnsiScreen& screen,
                       AnsiRenderer& renderer, InputDriver& input) {
    // ── 1단계: rect 확정 (화면에 쓰지 않고 위치/크기만 계산) ──────────────────
    // calc_dialog_rect() 는 screen 크기만 보고 rect를 계산한다.
    // Widget::calc_rect() 가 없으므로 render() 로 rect_ 를 세팅한 뒤
    // 저장 전에 buf_ 를 원상복구해야 순서가 맞는다.
    //
    // 더 간단한 방법: dlg 내부 calc_dialog_rect 호출 결과를 직접 재현
    //   dlg_detail::calc_dialog_rect(screen, opts_.dialog_w, opts_.dialog_h)
    // 하지만 opts_ 가 private 이므로, render() → rect() 순서로 취득한 뒤
    // 저장은 clear() 이전에 수행한다.
    //
    //  [정확한 순서]
    //  a) dlg.render(screen)  → rect_ 확정 + buf_ 에 다이얼로그 셀 기록
    //  b) r = dlg.rect()      → 위치/크기 취득
    //  c) saved_bg 저장 시    → buf_[r] 는 이미 다이얼로그 내용 (잘못됨)
    //
    //  [해결] render() 전에 rect 를 미리 알아야 함.
    //  dlg_detail::calc_dialog_rect() 는 public 이므로 직접 호출 가능.
    //  단, dialog_w/dialog_h 가 필요 → dlg.hint_size() 접근자를 Widget 에 추가하거나
    //  render() 후 restore + save 두 단계를 쓴다.
    //
    //  [가장 단순한 해결]
    //  render() 로 rect 취득 → 저장은 prev_buf_ 에서 읽음 (터미널 실제 내용)
    //  prev_buf_ 는 renderer.render() 를 마지막으로 호출한 시점의 화면 상태.

    // rect 확정
    dlg.render(screen);
    const Widget::Rect r = dlg.rect();

    // ── 2단계: 배경 저장 — prev_buf_ 기준 (터미널 실제 출력 내용) ─────────────
    // buf_ 는 이미 dlg.render() 로 덮어씌워졌으므로 prev_buf_ 에서 저장한다.
    // prev_buf_ = 마지막 renderer.render()/render_full() 직후의 화면 상태.
    std::vector<Cell> saved_bg;
    saved_bg.reserve(static_cast<size_t>(r.w * r.h));
    for (int ry = r.y; ry < r.y + r.h; ++ry)
        for (int cx = r.x; cx < r.x + r.w; ++cx)
            saved_bg.push_back(screen.in_bounds_pub(cx, ry)
                               ? screen.prev_cell_at(cx, ry) : Cell{});

    // ── 3단계: 다이얼로그 영역만 지우고 다이얼로그 렌더 → 증분 출력 ───────────
    screen.clear(r.x, r.y, r.w, r.h);
    dlg.render(screen);
    renderer.render();

    // ── 4단계: 이벤트 루프 ──────────────────────────────────────────────────
    while (!dlg.closed()) {
        dlg.render(screen);
        renderer.render();
        auto ev = input.read_key();
        if (ev.key == KEY_RESIZE) {
            renderer.handle_resize();
            dlg.render(screen);
            const Widget::Rect nr = dlg.rect();
            saved_bg.clear();
            saved_bg.reserve(static_cast<size_t>(nr.w * nr.h));
            for (int ry = nr.y; ry < nr.y + nr.h; ++ry)
                for (int cx = nr.x; cx < nr.x + nr.w; ++cx)
                    saved_bg.push_back(screen.in_bounds_pub(cx, ry)
                                       ? screen.prev_cell_at(cx, ry) : Cell{});
            screen.clear(nr.x, nr.y, nr.w, nr.h);
            dlg.render(screen);
            renderer.render_full();
        } else {
            dlg.handle_key(ev);
        }
    }

    // ── 5단계: 배경 복원 → 다이얼로그 잔상 제거 ─────────────────────────────
    screen.restore_region(r.x, r.y, r.w, r.h, saved_bg);
    renderer.render();
}

} // namespace term