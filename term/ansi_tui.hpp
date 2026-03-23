#pragma once
/**
 * ansi_tui.hpp — TUI 컴포넌트  (TDD §9)
 *
 * 구현부가 모두 클래스 본체 안에 위치한다.
 * KeyEvent 완전 정의가 필요하므로 input.hpp 를 파일 상단에서 include 한다.
 */
#include "term/ansi_screen.hpp"
#include "term/ansi_string.hpp"
#include "term/uni_string.hpp"
#include "term/input.hpp"      // KeyEvent, MouseEvent 완전 정의 (구현부에서 필요)
#include <algorithm>
#include <cstdio>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace term {

// ═══════════════════════════════════════════════════════════════════════════
//  테두리 문자 셋  (TDD §9.2)
// ═══════════════════════════════════════════════════════════════════════════

struct BorderChars {
    std::string_view tl,tr,bl,br;    // 모서리
    std::string_view h,v;             // 수평/수직
    std::string_view title_l,title_r; // 제목 구분
};

inline constexpr BorderChars kBorderSingle = {"┌","┐","└","┘","─","│","┤","├"};
inline constexpr BorderChars kBorderDouble = {"╔","╗","╚","╝","═","║","╣","╠"};
inline constexpr BorderChars kBorderRound  = {"╭","╮","╰","╯","─","│","┤","├"};
inline constexpr BorderChars kBorderBold   = {"┏","┓","┗","┛","━","┃","┫","┣"};

// ═══════════════════════════════════════════════════════════════════════════
//  Widget 기반 클래스  (TDD §9.1)
//  구현부가 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class Widget {
public:
    struct Rect { int x=0,y=0,w=0,h=0; };
    struct Theme {
        Color fg_normal      = Color::default_color();
        Color bg_normal      = Color::default_color();
        Color fg_focused     = Color::from_index(14); // bright cyan
        Color bg_focused     = Color::default_color();
        Color fg_disabled    = Color::from_index(8);  // dark gray
        Color bg_disabled    = Color::default_color();
        Color border_normal  = Color::from_index(7);
        Color border_focused = Color::from_index(14);
    };

    virtual ~Widget() = default;
    virtual void render(AnsiScreen& screen) = 0;
    virtual bool handle_key(const KeyEvent&)    { return false; }
    virtual bool handle_mouse(const MouseEvent&){ return false; }

    void set_rect(Rect r)     { rect_    = r; }
    Rect rect() const         { return rect_; }
    void set_focused(bool v)  { focused_ = v; }
    bool focused() const      { return focused_; }
    void set_enabled(bool v)  { enabled_ = v; }
    bool enabled() const      { return enabled_; }
    void set_visible(bool v)  { visible_ = v; }
    bool visible() const      { return visible_; }
    void set_theme(const Theme& t){ theme_ = t; }

protected:
    Rect  rect_;
    bool  focused_  = false;
    bool  enabled_  = true;
    bool  visible_  = true;
    Theme theme_;

    enum class BorderStyle { None, Single, Double, Round, Bold };

    void fill_bg(AnsiScreen& screen, const Color& bg) const {
        for (int r = rect_.y; r < rect_.y + rect_.h && r < screen.rows(); ++r) {
            AnsiString s;
            s.xy(rect_.x, r).bg(bg);
            s.text(std::string(static_cast<size_t>(rect_.w), ' ')).reset();
            screen.put(s);
        }
    }

    void render_border(AnsiScreen& screen,
                       BorderStyle style = BorderStyle::Single,
                       std::string_view title = "") const {
        if (style == BorderStyle::None) return;
        const BorderChars* bc = &kBorderSingle;
        if      (style == BorderStyle::Double) bc = &kBorderDouble;
        else if (style == BorderStyle::Round)  bc = &kBorderRound;
        else if (style == BorderStyle::Bold)   bc = &kBorderBold;

        Color bc_color = focused_ ? theme_.border_focused : theme_.border_normal;
        int x = rect_.x, y = rect_.y, w = rect_.w, h = rect_.h;

        // 상단 선
        {
            AnsiString s;
            s.xy(x, y).fg(bc_color).text(bc->tl);
            if (!title.empty()) {
                // ┤/├ 같은 Junction 문자는 동아시아 환경에서 2칸으로 렌더될 수 있어
                // 제목 너비 계산이 어긋난다. 공백만 사용한다:
                // center section = ' '(1) + title + ' '(1) = title_w + 2
                int title_w  = (int)UniString::display_width(title);
                int center_w = title_w + 2;
                int avail    = w - 2;          // 양 모서리(tl/tr) 제외한 너비
                // 제목이 너무 길면 클리핑
                if (center_w > avail) {
                    title    = UniString::clip(title, avail - 2);
                    title_w  = (int)UniString::display_width(title);
                    center_w = title_w + 2;
                }
                int left  = (avail - center_w) / 2;
                int right = avail - left - center_w;
                for (int i = 0; i < left;  ++i) s.text(bc->h);
                s.text(" ").text(title).text(" ");
                for (int i = 0; i < right; ++i) s.text(bc->h);
            } else {
                for (int i = 0; i < w - 2; ++i) s.text(bc->h);
            }
            s.text(bc->tr).reset();
            screen.put(s);
        }
        // 수직 선
        for (int r = y + 1; r < y + h - 1; ++r) {
            AnsiString s, s2;
            s.xy(x, r).fg(bc_color).text(bc->v).reset();
            screen.put(s);
            s2.xy(x + w - 1, r).fg(bc_color).text(bc->v).reset();
            screen.put(s2);
        }
        // 하단 선
        {
            AnsiString s;
            s.xy(x, y + h - 1).fg(bc_color).text(bc->bl);
            for (int i = 0; i < w - 2; ++i) s.text(bc->h);
            s.text(bc->br).reset();
            screen.put(s);
        }
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  Button  (TDD §9.3.1)
// ═══════════════════════════════════════════════════════════════════════════

class Button : public Widget {
public:
    enum class Style { Normal, Primary, Danger };

    explicit Button(std::string_view label = "OK") : label_(label) {}

    void set_label(std::string_view l) { label_    = l; }
    void set_style(Style s)            { style_    = s; }
    void set_on_click(std::function<void()> cb) { on_click_ = std::move(cb); }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        AnsiString s;
        s.xy(rect_.x, rect_.y);
        Color fg = enabled_ ? theme_.fg_normal : theme_.fg_disabled;
        Color bg = theme_.bg_normal;
        if (!enabled_) {
            s.fg(fg).bg(bg).text("( ").text(label_).text(" )").reset();
        } else if (pressed_) {
            s.reverse().text("[ ").text(label_).text(" ]").reset();
        } else if (focused_) {
            s.fg(theme_.fg_focused).bold().text("┃ ").text(label_).text(" ┃").reset();
        } else {
            s.fg(fg).bg(bg).text("[ ").text(label_).text(" ]").reset();
        }
        screen.put(s);
    }

    bool handle_key(const KeyEvent& ev) override {
        if (!focused_ || !enabled_) return false;
        if (ev.key == KEY_ENTER || ev.key == ' ') {
            pressed_ = true;
            if (on_click_) on_click_();
            pressed_ = false;
            return true;
        }
        return false;
    }

private:
    std::string           label_;
    Style                 style_   = Style::Normal;
    std::function<void()> on_click_;
    bool                  pressed_ = false;
};

// ═══════════════════════════════════════════════════════════════════════════
//  CheckBox  (TDD §9.3.2)
// ═══════════════════════════════════════════════════════════════════════════

class CheckBox : public Widget {
public:
    explicit CheckBox(std::string_view label = "", bool checked = false)
        : label_(label), checked_(checked) {}

    [[nodiscard]] bool is_checked() const { return checked_; }
    void set_checked(bool v) { checked_ = v; }
    void set_on_change(std::function<void(bool)> cb) { on_change_ = std::move(cb); }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        AnsiString s;
        s.xy(rect_.x, rect_.y);
        if (focused_) s.fg(theme_.fg_focused);
        s.text(checked_ ? "[x] " : "[ ] ").text(label_).reset();
        screen.put(s);
    }

    bool handle_key(const KeyEvent& ev) override {
        if (!focused_ || !enabled_) return false;
        if (ev.key == KEY_ENTER || ev.key == ' ') {
            checked_ = !checked_;
            if (on_change_) on_change_(checked_);
            return true;
        }
        return false;
    }

private:
    std::string               label_;
    bool                      checked_ = false;
    std::function<void(bool)> on_change_;
};

// ═══════════════════════════════════════════════════════════════════════════
//  ProgressBar  (TDD §9.3.3)
// ═══════════════════════════════════════════════════════════════════════════

class ProgressBar : public Widget {
public:
    enum class Style { Block, Smooth, Thin, Ascii };

    void set_value(double v)    { value_     = (v<0.0?0.0:v>1.0?1.0:v); }
    void set_style(Style s)     { style_     = s; }
    void set_label_format(std::string_view f) { label_fmt_ = f; }
    void set_colors(Color filled, Color empty){ filled_=filled; empty_=empty; }
    [[nodiscard]] double value() const { return value_; }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        int bar_w = rect_.w;
        if (bar_w <= 0) return;

        char lbuf[16];
        std::snprintf(lbuf, sizeof(lbuf), label_fmt_.c_str(), value_ * 100.0);
        int lw = UniString::display_width(lbuf);
        int pw = bar_w - lw - 1;
        if (pw <= 0) pw = bar_w;

        AnsiString s;
        s.xy(rect_.x, rect_.y);

        if (style_ == Style::Smooth) {
            static constexpr std::string_view kBlocks[] = {
                " ","▏","▎","▍","▌","▋","▊","▉","█"
            };
            double filled    = value_ * pw;
            int    full_cells = static_cast<int>(filled);
            int    partial   = static_cast<int>((filled - full_cells) * 8);
            for (int i = 0; i < pw; ++i) {
                if (i < full_cells)       s.fg(filled_).text("█");
                else if (i == full_cells) s.fg(filled_).text(kBlocks[partial]);
                else                      s.fg(empty_).text("░");
            }
        } else if (style_ == Style::Block) {
            int filled_w = static_cast<int>(value_ * pw + 0.5);
            s.fg(filled_);
            for (int i = 0; i < pw; ++i) s.text(i < filled_w ? "█" : "░");
        } else if (style_ == Style::Thin) {
            int filled_w = static_cast<int>(value_ * pw + 0.5);
            s.fg(filled_);
            for (int i = 0; i < pw; ++i) s.text(i < filled_w ? "─" : "·");
        } else {
            int filled_w = static_cast<int>(value_ * pw + 0.5);
            s.text("[");
            for (int i = 0; i < pw - 2; ++i) s.text(i < filled_w ? "=" : " ");
            s.text("]");
        }
        s.fg(Color::default_color()).text(" ").text(lbuf).reset();
        screen.put(s);
    }

private:
    double      value_     = 0.0;
    Style       style_     = Style::Smooth;
    Color       filled_    = Color::from_index(2);
    Color       empty_     = Color::from_index(8);
    std::string label_fmt_ = "%.0f%%";
};

// ═══════════════════════════════════════════════════════════════════════════
//  Label  (읽기 전용 텍스트)
// ═══════════════════════════════════════════════════════════════════════════

class Label : public Widget {
public:
    explicit Label(std::string_view text = "") : text_(text) {}
    void set_text(std::string_view t) { text_ = t; }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        AnsiString s;
        s.xy(rect_.x, rect_.y)
         .fg(theme_.fg_normal)
         .text(UniString::clip(text_, rect_.w))
         .reset();
        screen.put(s);
    }

private:
    std::string text_;
};

// ═══════════════════════════════════════════════════════════════════════════
//  Panel  (TDD §9.3.4)
// ═══════════════════════════════════════════════════════════════════════════

class Panel : public Widget {
public:
    explicit Panel(std::string_view title = "") : title_(title) {}

    void set_title(std::string_view t)  { title_  = t; }
    void set_border(BorderStyle s)      { border_ = s; }
    void add_child(std::unique_ptr<Widget> child) {
        children_.push_back(std::move(child));
    }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        fill_bg(screen, theme_.bg_normal);
        render_border(screen, border_, title_);
        for (auto& c : children_) c->render(screen);
    }

    bool handle_key(const KeyEvent& ev) override {
        if (focused_child_ >= 0 &&
            focused_child_ < static_cast<int>(children_.size()))
            return children_[focused_child_]->handle_key(ev);
        return false;
    }

private:
    std::string                          title_;
    BorderStyle                          border_        = BorderStyle::Single;
    std::vector<std::unique_ptr<Widget>> children_;
    int                                  focused_child_ = -1;
};

// ═══════════════════════════════════════════════════════════════════════════
//  TextInput  (TDD §9.3.6)
// ═══════════════════════════════════════════════════════════════════════════

class TextInput : public Widget {
public:
    explicit TextInput(std::string_view placeholder = "")
        : placeholder_(placeholder) {}

    [[nodiscard]] const std::string& value() const { return value_; }

    void set_value(std::string_view v) {
        value_      = v;
        cursor_pos_ = static_cast<int>(UniString::to_utf32(v).size());
    }
    void set_placeholder(std::string_view p) { placeholder_ = p; }
    void set_max_length(int n)               { max_length_  = n; }
    void set_password_mode(bool v)           { password_    = v; }
    void set_on_change(std::function<void(std::string_view)> cb) { on_change_=std::move(cb); }
    void set_on_submit(std::function<void(std::string_view)> cb) { on_submit_=std::move(cb); }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        int w = rect_.w;
        AnsiString s;
        s.xy(rect_.x, rect_.y);
        if (focused_) s.fg(theme_.fg_focused).underline();

        if (value_.empty() && !placeholder_.empty()) {
            s.fg(Color::from_index(8)).text(UniString::clip(placeholder_, w));
        } else {
            std::string display = value_;
            if (password_) display = std::string(UniString::to_utf32(value_).size(), '*');

            auto clusters = UniString::split(display);
            int start_idx = 0, skipped_cols = 0;
            for (int i = 0; i < (int)clusters.size(); ++i) {
                if (skipped_cols + clusters[i].width <= scroll_off_) {
                    skipped_cols += clusters[i].width;
                    start_idx = i + 1;
                } else break;
            }
            std::string visible_text;
            int vis_w = 0;
            for (int i = start_idx; i < (int)clusters.size() && vis_w < w - 1; ++i) {
                visible_text += clusters[i].bytes;
                vis_w        += clusters[i].width;
            }
            s.text(visible_text);
            int pad = w - vis_w;
            for (int i = 0; i < pad; ++i) s.text(" ");
        }
        s.reset();
        screen.put(s);
    }

    bool handle_key(const KeyEvent& ev) override {
        if (!focused_ || !enabled_) return false;
        if (ev.key == KEY_LEFT)      { cursor_left();   return true; }
        if (ev.key == KEY_RIGHT)     { cursor_right();  return true; }
        if (ev.key == KEY_HOME)      { cursor_home();   return true; }
        if (ev.key == KEY_END)       { cursor_end();    return true; }
        if (ev.key == KEY_BACKSPACE) { delete_before(); return true; }
        if (ev.key == KEY_DEL)       { delete_after();  return true; }
        if (ev.key == KEY_ENTER) {
            if (on_submit_) on_submit_(value_);
            return true;
        }
        if (ev.is_printable()) { insert_char(ev.ch); return true; }
        return false;
    }

private:
    std::string value_, placeholder_;
    int         cursor_pos_  = 0;
    int         scroll_off_  = 0;
    int         max_length_  = -1;
    bool        password_    = false;
    std::function<void(std::string_view)> on_change_, on_submit_;

    void cursor_left()  { if (cursor_pos_ > 0) --cursor_pos_; }
    void cursor_right() {
        if (cursor_pos_ < (int)UniString::to_utf32(value_).size()) ++cursor_pos_;
    }
    void cursor_home() { cursor_pos_ = 0; }
    void cursor_end()  { cursor_pos_ = (int)UniString::to_utf32(value_).size(); }

    void insert_char(char32_t cp) {
        if (max_length_ > 0 && (int)UniString::to_utf32(value_).size() >= max_length_) return;
        auto u32 = UniString::to_utf32(value_);
        u32.insert(u32.begin() + cursor_pos_, cp);
        value_ = UniString::to_utf8(u32);
        ++cursor_pos_;
        if (on_change_) on_change_(value_);
    }
    void delete_before() {
        if (cursor_pos_ <= 0) return;
        auto u32 = UniString::to_utf32(value_);
        u32.erase(u32.begin() + cursor_pos_ - 1);
        value_ = UniString::to_utf8(u32);
        --cursor_pos_;
        if (on_change_) on_change_(value_);
    }
    void delete_after() {
        auto u32 = UniString::to_utf32(value_);
        if (cursor_pos_ >= (int)u32.size()) return;
        u32.erase(u32.begin() + cursor_pos_);
        value_ = UniString::to_utf8(u32);
        if (on_change_) on_change_(value_);
    }
};

// ═══════════════════════════════════════════════════════════════════════════
//  StatusBar  (TDD §9.3.9)
// ═══════════════════════════════════════════════════════════════════════════

class StatusBar : public Widget {
public:
    struct Section {
        std::string text;
        Color       fg = Color::default_color();
        Color       bg = Color::default_color();
        int         min_width = 0;
        enum class Align { Left, Center, Right } align = Align::Left;
    };

    void set_sections(std::vector<Section> s) { sections_ = std::move(s); }
    void set_section_text(int i, std::string_view t) {
        if (i >= 0 && i < (int)sections_.size()) sections_[i].text = t;
    }

    void render(AnsiScreen& screen) override {
        if (!visible_) return;
        int x = rect_.x, y = rect_.y, total_w = rect_.w;

        AnsiString bg_s;
        bg_s.xy(x, y).bg(Color::from_index(0));
        bg_s.text(std::string(static_cast<size_t>(total_w), ' ')).reset();
        screen.put(bg_s);

        int used = 0;
        for (auto& sec : sections_) {
            if (used >= total_w) break;
            int avail = total_w - used;
            int w     = std::max(sec.min_width, UniString::display_width(sec.text));
            w = std::min(w, avail);

            std::string txt = UniString::clip(sec.text, w);
            int tw = UniString::display_width(txt);
            int pad_l = 0, pad_r = 0;
            using A = Section::Align;
            if (sec.align == A::Center) { pad_l = (w-tw)/2; pad_r = w-tw-pad_l; }
            else if (sec.align == A::Right) { pad_l = w-tw; }
            else { pad_r = w-tw; }

            AnsiString s;
            s.xy(x + used, y).fg(sec.fg).bg(sec.bg);
            for (int i = 0; i < pad_l; ++i) s.text(" ");
            s.text(txt);
            for (int i = 0; i < pad_r; ++i) s.text(" ");
            s.reset();
            screen.put(s);
            used += w;
        }
    }

private:
    std::vector<Section> sections_;
};

// ═══════════════════════════════════════════════════════════════════════════
//  MessageBox  (TDD §9.3.5)
// ═══════════════════════════════════════════════════════════════════════════

class MessageBox : public Widget {
public:
    enum class Type    { Info, Warning, Error, Question };
    enum class Buttons { OK, OKCancel, YesNo, YesNoCancel };
    enum class Result  { OK, Cancel, Yes, No };

    explicit MessageBox(std::string_view title   = "Notice",
                        std::string_view message = "",
                        Type    type = Type::Info,
                        Buttons btns = Buttons::OK)
        : title_(title), message_(message), type_(type), btns_(btns) {}

    void render(AnsiScreen& screen) override {
        if (!visible_) return;

        int sw = screen.cols(), sh = screen.rows();
        int inner_w = std::min(sw - 4, 50);
        auto lines  = wrap_message(inner_w - 2);
        int box_h   = 2 + 1 + (int)lines.size() + 1 + 3;

        int bx = (sw - inner_w) / 2;
        int by = (sh - box_h)   / 2;
        rect_ = {bx, by, inner_w, box_h};

        // 배경
        for (int r = by; r < by + box_h; ++r) {
            AnsiString s;
            s.xy(bx, r).bg(Color::from_index(0));
            s.text(std::string(static_cast<size_t>(inner_w), ' ')).reset();
            screen.put(s);
        }

        render_border(screen, BorderStyle::Round, title_);

        std::string_view icon =
            type_ == Type::Info    ? "ℹ " :
            type_ == Type::Warning ? "⚠ " :
            type_ == Type::Error   ? "✖ " : "? ";

        for (int i = 0; i < (int)lines.size(); ++i) {
            AnsiString s;
            s.xy(bx + 2, by + 2 + i);
            if (i == 0) s.text(icon);
            s.text(lines[i]).reset();
            screen.put(s);
        }

        auto blabels = button_labels();
        int btn_y = by + box_h - 2;
        int total_btn_w = 0;
        for (auto& bl : blabels) total_btn_w += (int)bl.size() + 4;
        total_btn_w += (int)blabels.size() - 1;

        int btn_x = bx + (inner_w - total_btn_w) / 2;
        for (int i = 0; i < (int)blabels.size(); ++i) {
            AnsiString s;
            s.xy(btn_x, btn_y);
            if (i == selected_) s.reverse().bold();
            s.text("[ ").text(blabels[i]).text(" ]").reset();
            screen.put(s);
            btn_x += (int)blabels[i].size() + 4 + 1;
        }
    }

    bool handle_key(const KeyEvent& ev) override {
        if (closed_) return false;
        auto blabels = button_labels();
        if (ev.key == KEY_LEFT || ev.key == KEY_RIGHT) {
            int dir = (ev.key == KEY_RIGHT) ? 1 : -1;
            selected_ = (selected_ + dir + (int)blabels.size()) % (int)blabels.size();
            return true;
        }
        if (ev.key == KEY_ENTER || ev.key == ' ') {
            std::string_view lbl = blabels[selected_];
            if      (lbl == "OK")     result_ = Result::OK;
            else if (lbl == "Cancel") result_ = Result::Cancel;
            else if (lbl == "Yes")    result_ = Result::Yes;
            else if (lbl == "No")     result_ = Result::No;
            closed_ = true;
            return true;
        }
        if (ev.key == KEY_ESC) { result_ = Result::Cancel; closed_ = true; return true; }
        return false;
    }

    [[nodiscard]] Result result() const { return result_; }
    [[nodiscard]] bool   closed() const { return closed_; }

private:
    std::string title_, message_;
    Type        type_;
    Buttons     btns_;
    Result      result_   = Result::OK;
    bool        closed_   = false;
    int         selected_ = 0;

    std::vector<std::string> wrap_message(int max_w) const {
        std::vector<std::string> lines;
        auto clusters = UniString::split(message_);
        std::string cur;
        int cur_w = 0;
        for (const auto& g : clusters) {
            if (g.bytes == "\n") { lines.push_back(cur); cur.clear(); cur_w=0; continue; }
            if (cur_w + g.width > max_w) { lines.push_back(cur); cur.clear(); cur_w=0; }
            cur   += g.bytes;
            cur_w += g.width;
        }
        if (!cur.empty()) lines.push_back(cur);
        return lines;
    }

    std::vector<std::string> button_labels() const {
        switch (btns_) {
        case Buttons::OK:          return {"OK"};
        case Buttons::OKCancel:    return {"OK","Cancel"};
        case Buttons::YesNo:       return {"Yes","No"};
        case Buttons::YesNoCancel: return {"Yes","No","Cancel"};
        }
        return {"OK"};
    }
};

} // namespace term