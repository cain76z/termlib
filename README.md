# ansi_terminal

C++20 헤더 온리(header-only) ANSI 터미널 TUI 라이브러리.

`.cpp` 없이 `#include "term.hpp"` 한 줄로 전체 기능을 사용할 수 있습니다.  
POSIX(Linux/macOS)와 Windows(Win32 콘솔)를 모두 지원합니다.

```
┌────────────────────────────────────────────────────────────┐
│  파일 탐색기                             [F5 새로고침]     │
├──────────────────────────────────────────────────────────  │
│  📁 src/          2024-01-15   <DIR>                       │
│  📁 include/      2024-01-15   <DIR>                       │
│▶ 📄 README.md     2024-01-15   4.2 KB                      │
│  📄 CMakeLists.txt 2024-01-15  1.1 KB                      │
├────────────────────────────────────────────────────────────│
│  [확인]  [취소]                                            │
└────────────────────────────────────────────────────────────┘
```

---

## 목차

1. [특징](#특징)
2. [빌드 요구사항](#빌드-요구사항)
3. [빠른 시작](#빠른-시작)
4. [핵심 개념](#핵심-개념)
5. [컴포넌트 레퍼런스](#컴포넌트-레퍼런스)
   - [Color / Attr](#color--attr)
   - [AnsiString](#ansistring)
   - [AnsiScreen](#ansiscreen)
   - [AnsiOptimizer / AnsiRenderer](#ansioptimizerAnsirenderer)
   - [Terminal](#terminal)
   - [InputDriver](#inputdriver)
   - [Widget 시스템](#widget-시스템)
   - [Spinner](#spinner)
   - [UniString](#unistring)
   - [strutil — 문자열 유틸](#strutil--문자열-유틸)
   - [encode — 인코딩 변환](#encode--인코딩-변환)
   - [fnutil — 파일시스템 유틸](#fnutil--파일시스템-유틸)
   - [ConfReader — 설정 파일](#confreader--설정-파일)
   - [Args — CLI 인자 파서](#args--cli-인자-파서)
6. [예제](#예제)
   - [Hello World](#예제-1-hello-world)
   - [색상 그라데이션](#예제-2-색상-그라데이션)
   - [키 입력 이벤트 루프](#예제-3-키-입력-이벤트-루프)
   - [진행 표시줄](#예제-4-진행-표시줄)
   - [스피너 애니메이션](#예제-5-스피너-애니메이션)
   - [파일 탐색 다이얼로그](#예제-6-파일-탐색-다이얼로그)
   - [전체 TUI 앱](#예제-7-전체-tui-앱)
7. [충돌 방지: term\_info + input 함께 사용](#충돌-방지-term_info--input-함께-사용)
8. [파일 구조](#파일-구조)

---

## 특징

- **헤더 온리** — `.cpp` 불필요, 헤더 경로만 추가하면 바로 사용
- **크로스 플랫폼** — Linux, macOS, Windows 동일 API
- **TrueColor / 256색 / 8색** 자동 감지
- **증분 렌더링** — dirty 셀만 출력해 깜빡임 없는 화면 갱신
- **완전한 Unicode 지원** — UAX #29 그래핌 클러스터, CJK 전각 문자, 이모지
- **RAII 설계** — `Terminal` 소멸 시 raw mode, alt screen, cursor 자동 복원
- **소유권 명시** — `InputOptions` 로 raw mode 중복 관리 충돌 방지
- **문자열 유틸** — `strutil::` 네임스페이스로 trim/split/join/타입 변환/숫자·시간 포맷
- **인코딩 변환** — UTF-8 ↔ wstring ↔ ANSI, BOM 자동 감지, 시각 폭 계산
- **파일시스템** — 경로 분해·glob·자연 정렬·OS 표준 디렉터리·고속 열거 통합
- **INI 설정 파일** — `ConfReader` 로 섹션/키/값 파싱, 콤마 리스트 지원
- **CLI 인자 파서** — `Args` 로 플래그/값/파일 분리, 와일드카드 자동 확장

---

## 빌드 요구사항

| 항목 | 최솟값 |
|------|--------|
| C++ 표준 | C++20 |
| 컴파일러 | GCC 11+ / Clang 13+ / MSVC 19.29+ |
| 플랫폼 | Linux, macOS, Windows 10 1903+ |

```bash
# 단일 파일 빌드 (헤더 경로만 추가)
g++ -std=c++20 -I include myapp.cpp -o myapp
```

---

## 폴더 구조

include/
  ├── args.hpp
  ├── conf.hpp
  ├── encode.hpp
  ├── fnutil.hpp
  ├── platform.hpp
  ├── str.hpp
  ├── term.hpp
  └── term/
        ├── ansi_optimizer.hpp
        ├── ansi_renderer.hpp
        ├── ansi_screen.hpp
        ├── ansi_string.hpp
        ├── ansi_tui.hpp
        ├── ansi_tui_dlg.hpp
        ├── input.hpp
        ├── spinner.hpp
        ├── term_info.hpp
        ├── term_shared.hpp
        ├── unicode_grapheme_table.hpp
        ├── unicode_width_table.hpp
        ├── uni_string.hpp
        └── util.hpp



## 빠른 시작

```cpp
#include "term.hpp"

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);

    auto input = term::make_input_driver();
    input->setup();

    bool running = true;
    while (running) {
        // 화면 그리기
        term::AnsiString s;
        s.xy(2, 2)
         .fg("bright_cyan").bold()
         .text("Hello, Terminal!")
         .reset();
        screen.put(s);

        renderer.render();

        // 입력 처리
        auto ev = input->read_key(16);
        if (ev.key == term::KEY_ESC || ev.key == term::KEY_CTRL_C)
            running = false;
        if (ev.key == term::KEY_RESIZE)
            renderer.handle_resize();
    }

    return 0;
}
```

---

## 핵심 개념

### 렌더 파이프라인

```
사용자 코드
  │  AnsiString 으로 셀 속성 기술
  ▼
AnsiScreen            ← 논리 버퍼 (현재 프레임)
  │  dirty 셀 추적
  ▼
AnsiOptimizer         ← diff 계산 → 최소 ANSI 이스케이프 생성
  │
  ▼
Terminal::write()     ← 출력 버퍼 누적
Terminal::flush()     ← 단일 syscall 로 전송
```

### 좌표계

모든 좌표는 **0-based** (column 0, row 0 이 좌상단).  
ANSI 시퀀스 내부의 1-based 변환은 라이브러리가 자동 처리합니다.

---

## 컴포넌트 레퍼런스

### Color / Attr

```cpp
// 색상 생성 방법
Color c1 = Color::default_color();           // 터미널 기본색
Color c2 = Color::from_index(196);           // 256색 팔레트
Color c3 = Color::from_rgb(255, 128, 0);     // TrueColor
Color c4 = Color::from_hex("#FF8000");       // 16진수 문자열
Color c5 = Color::parse("bright_cyan");      // 이름

// 텍스트 속성 플래그 (비트 OR 조합 가능)
uint16_t attr = Attr::BOLD | Attr::UNDERLINE;
```

지원 색상 이름: `black`, `red`, `green`, `yellow`, `blue`, `magenta`, `cyan`, `white`,  
`bright_black`(=`gray`), `bright_red`, `bright_green`, `bright_yellow`,  
`bright_blue`, `bright_magenta`, `bright_cyan`, `bright_white`, `orange`, `purple`, `pink`

---

### AnsiString

화면에 출력할 내용을 **메서드 체이닝**으로 기술하는 빌더 클래스.

```cpp
term::AnsiString s;
s.xy(5, 3)                          // 커서를 (5열, 3행) 으로 이동
 .fg("bright_yellow").bold()        // 전경색 + 굵게
 .bg(Color::from_rgb(30, 30, 60))   // 배경색 TrueColor
 .text("Score: ")
 .fg(255, 200, 0)                   // 색상 변경
 .text("9999")
 .reset();                          // 모든 속성 초기화

screen.put(s);                      // AnsiScreen 에 적용
```

| 메서드 | 설명 |
|--------|------|
| `.xy(x, y)` | 커서 절대 이동 (0-based) |
| `.fg(...)` | 전경색 (string/RGB/index/Color) |
| `.bg(...)` | 배경색 |
| `.bold()` / `.italic()` / `.underline()` | 속성 ON |
| `.bold(false)` | 속성 OFF |
| `.text(sv)` | UTF-8 텍스트 추가 |
| `.reset()` | SGR 0 — 전체 초기화 |

---

### AnsiScreen

논리 셀 버퍼. 직접 접근하거나 `AnsiString` 을 통해 쓸 수 있습니다.

```cpp
term::AnsiScreen screen(80, 24);   // cols, rows

// AnsiString 으로 쓰기 (권장)
screen.put(ansi_string);

// 영역 초기화
screen.clear();                    // 전체
screen.clear(10, 5, 30, 10);       // x, y, w, h

// 다이얼로그용 영역 저장/복원
auto saved = screen.save_region(x, y, w, h);
// ... 다이얼로그 표시 ...
screen.restore_region(x, y, w, h, saved);

// 리사이즈
screen.resize(new_cols, new_rows);

// 커서 위치 추적
screen.set_cursor(col, row);
int cx = screen.cursor_x();
```

---

### AnsiOptimizer / AnsiRenderer

`AnsiRenderer` 가 `AnsiScreen` + `AnsiOptimizer` + `Terminal` 을 통합합니다.  
직접 사용할 일은 거의 없습니다.

```cpp
term::AnsiRenderer renderer(screen, term);

renderer.render();          // dirty 셀만 증분 출력
renderer.render_full();     // 전체 화면 강제 재출력
renderer.handle_resize();   // SIGWINCH 후 크기 갱신 + 전체 재출력
```

---

### Terminal

RAII 터미널 관리 객체. 소멸자가 모든 상태를 역순으로 복원합니다.

```cpp
term::Terminal term;

// 정보 조회
auto [cols, rows] = term.size();
term::ColorLevel lvl = term.color_level();  // None/Basic8/Index256/TrueColor

// 화면 제어
term.enter_alt_screen();        // 대체 화면 진입 (소멸 시 자동 탈출)
term.leave_alt_screen();        // 수동 탈출
term.show_cursor(false);        // 커서 숨기기 (소멸 시 자동 복원)
term.clear_screen();
term.set_title("My App");       // 터미널 창 제목

// Raw mode (input.hpp 와 함께 사용 시 InputDriver 에 맡기는 것을 권장)
term.enter_raw_mode();          // 소멸 시 자동 해제

// 출력 버퍼
term.write("\x1b[1;1H");        // 버퍼에 누적
term.flush();                   // 실제 터미널 전송

// SIGWINCH (단독 사용 시만; InputDriver 와 함께면 KEY_RESIZE 이벤트로 처리)
term.on_resize([&](int c, int r) { screen.resize(c, r); });
// while (running) { term.poll_resize(); }
```

#### 소멸자 복원 순서

```
1. flush()              — 남은 버퍼 전송
2. reset_scroll_region()— 스크롤 영역 초기화
3. leave_raw_mode()     — termios / ConsoleMode 복원
4. show_cursor(true)    — 커서 표시 복원
5. leave_alt_screen()   — 원본 화면 복원
```

---

### InputDriver

플랫폼 독립 키/마우스/리사이즈 이벤트 수신.

```cpp
// 기본 생성
auto input = term::make_input_driver();
input->setup();

// 옵션 지정
term::InputOptions opts;
opts.manage_raw_mode = false;  // Terminal::enter_raw_mode() 가 이미 설정한 경우
opts.enable_mouse    = false;  // 마우스 추적 비활성화
auto input2 = term::make_input_driver(opts);

// 블로킹 읽기
term::KeyEvent ev = input->read_key();

// 타임아웃 읽기 (애니메이션 루프용)
term::KeyEvent ev = input->read_key(16);  // 최대 16ms 대기
if (ev.key == term::KEY_NONE) {
    // 타임아웃 — 애니메이션 갱신
}
```

#### 주요 키 코드

| 키 코드 | 설명 |
|---------|------|
| `KEY_ESC` | Escape |
| `KEY_ENTER` | Enter / Return |
| `KEY_TAB` | Tab |
| `KEY_BACKSPACE` | Backspace |
| `KEY_UP/DOWN/LEFT/RIGHT` | 방향키 |
| `KEY_HOME` / `KEY_END` | Home / End |
| `KEY_PGUP` / `KEY_PGDN` | PageUp / PageDown |
| `KEY_F1` ~ `KEY_F12` | 기능키 |
| `KEY_CTRL_A` ~ `KEY_CTRL_Z` | Ctrl 조합 |
| `KEY_RESIZE` | 터미널 크기 변경 (SIGWINCH) |
| `KEY_MOUSE` | 마우스 이벤트 → `ev.mouse` 참조 |

```cpp
// 마우스 이벤트 처리
if (ev.key == term::KEY_MOUSE) {
    auto& m = ev.mouse;
    // m.x, m.y (1-based), m.btn (LEFT/RIGHT/MIDDLE/WHEEL_UP/WHEEL_DOWN)
}

// 수정자 키 확인
if (ev.has_ctrl()) { /* Ctrl 눌림 */ }
if (ev.has_shift()) { /* Shift 눌림 */ }
if (ev.has_alt())  { /* Alt 눌림 */ }

// 출력 가능 문자
if (ev.is_printable()) {
    std::string ch = term::UniString::cp_to_utf8(ev.ch);
}
```

---

### Widget 시스템

모든 위젯은 `Widget` 을 상속하며 `AnsiScreen` 에 렌더됩니다.

#### 공통 인터페이스

```cpp
widget.set_rect({x, y, w, h});   // 위치와 크기
widget.set_focused(true);         // 포커스
widget.set_enabled(false);        // 비활성화
widget.set_visible(false);        // 숨기기
widget.render(screen);            // 화면에 그리기
widget.handle_key(ev);            // 키 이벤트 전달
```

#### Button

```cpp
term::Button btn("저장");
btn.set_rect({10, 5, 12, 1});
btn.set_style(term::Button::Style::Primary);
btn.set_on_click([&]() {
    save_file();
});
btn.set_focused(true);
btn.render(screen);

// 키 이벤트로 클릭 (Enter 또는 Space)
btn.handle_key(ev);
```

#### CheckBox

```cpp
term::CheckBox chk("자동 저장", /*checked=*/true);
chk.set_rect({5, 8, 20, 1});
chk.set_on_change([](bool checked) {
    set_auto_save(checked);
});
```

#### ProgressBar

```cpp
term::ProgressBar bar;
bar.set_rect({2, 10, 40, 1});
bar.set_style(term::ProgressBar::Style::Smooth);  // Block/Smooth/Thin/Ascii
bar.set_colors(
    term::Color::from_index(2),   // 채워진 부분 (초록)
    term::Color::from_index(8)    // 비어있는 부분 (회색)
);
bar.set_value(0.75);   // 0.0 ~ 1.0
bar.render(screen);
```

#### TextInput

```cpp
term::TextInput input("파일명 입력...");  // placeholder
input.set_rect({5, 12, 30, 1});
input.set_max_length(64);
input.set_on_submit([&](std::string_view val) {
    process(val);
});

// 키 이벤트로 편집
input.handle_key(ev);

// 현재 값 조회
std::string val = input.value();
```

#### Label / Panel / StatusBar / MessageBox

```cpp
// Label
term::Label lbl("상태: 정상");
lbl.set_rect({2, 20, 40, 1});

// Panel (자식 위젯 컨테이너)
term::Panel panel("설정");
panel.set_rect({5, 3, 50, 15});
panel.set_border(term::Widget::BorderStyle::Round);
panel.add_child(std::make_unique<term::CheckBox>("옵션 A"));

// MessageBox
term::MessageBox mb(
    "경고",
    "파일이 이미 존재합니다.\n덮어쓰시겠습니까?",
    term::MessageBox::Type::Warning,
    term::MessageBox::Buttons::YesNo
);
mb.render(screen);
mb.handle_key(ev);
if (mb.closed()) {
    bool yes = (mb.result() == term::MessageBox::Result::Yes);
}
```

#### 다이얼로그 — `run_dialog`

모달 이벤트 루프를 한 줄로 실행합니다. 배경 자동 저장/복원.

```cpp
// 파일 열기
term::FileDialogOptions fopts;
fopts.title        = "파일 열기";
fopts.initial_path = ".";
fopts.filters      = {"*.txt", "*.md", "*.cpp"};

term::FileOpenDialog dlg(fopts);
term::run_dialog(dlg, screen, renderer, *input);

if (dlg.confirmed()) {
    auto path = dlg.selected_path();  // std::optional<fs::path>
}

// 파일 저장
term::FileSaveDialog sdlg;
sdlg.set_filename("output.txt");
term::run_dialog(sdlg, screen, renderer, *input);
if (sdlg.confirmed()) {
    auto save_path = sdlg.save_path();
}

// 항목 선택
term::SelectDialogOptions sopts;
sopts.title        = "테마 선택";
sopts.multi_select = false;

term::SelectDialog sel({"Dark", "Light", "Solarized", "Monokai"}, sopts);
term::run_dialog(sel, screen, renderer, *input);
if (sel.confirmed()) {
    auto item = sel.selected_item();   // std::optional<std::string>
}
```

---

### Spinner

```cpp
// 프리셋으로 생성
term::Spinner sp(term::spinner::DOTS);
sp.set_rect({2, 5});
sp.set_label("로딩 중...");
sp.set_color(term::Color::from_index(14));  // bright cyan

// 애니메이션 루프
while (loading) {
    auto ev = input->read_key(80);   // 80ms 타임아웃
    sp.tick();
    if (sp.needs_render()) sp.render(screen);
    renderer.render();
}
```

사용 가능한 프리셋: `LINE`, `DOTS`, `MINI_DOTS`, `PULSE`, `POINTS`,  
`GLOBE`, `MOON`, `CLOCK`, `MONKEY`, `STAR`, `HAMBURGER`,  
`GROW_V`, `GROW_H`, `ARROW`, `TRIANGLE`, `CIRCLE`, `BOUNCE`

---

### UniString

Unicode 문자열 처리 유틸리티.

```cpp
// 터미널 출력 너비 계산 (CJK 전각=2, 이모지=2, ASCII=1, 결합문자=0)
int w = term::UniString::display_width("안녕하세요");  // 10
int w2 = term::UniString::display_width("Hello🎉");    // 7

// 그래핌 클러스터로 분리
auto clusters = term::UniString::split("안녕");
// clusters[0].bytes = "안", clusters[0].width = 2
// clusters[1].bytes = "녕", clusters[1].width = 2

// 너비 기준 클리핑 (경계에 걸치는 문자 자동 처리)
std::string clipped = term::UniString::clip("긴 한국어 문자열", 10);
std::string clipped2 = term::UniString::clip("long text...", 8, "…");  // 말줄임표

// 열 기준 부분 추출
std::string sub = term::UniString::get_sub_string("ABCDE", 1, 3); // "BCD"

// UTF-8 ↔ UTF-32 변환
std::u32string u32 = term::UniString::to_utf32("한글");
std::string    u8  = term::UniString::to_utf8(u32);
```

---

### strutil — 문자열 유틸

`str.hpp` | 네임스페이스 `strutil::` (하위 호환 alias: `str::`)

```cpp
// 공백 제거 (zero-copy view 반환)
std::string_view sv = strutil::trim("  hello  ");   // "hello"
std::string      s  = strutil::strip("  hello  ");  // "hello" (소유 반환)
strutil::ltrim("  hi");   // 왼쪽만
strutil::rtrim("hi  ");   // 오른쪽만

// 대소문자 변환
std::string lo = strutil::to_lower("HELLO");   // "hello"
std::string up = strutil::to_upper("world");   // "WORLD"

// 분리
auto v1 = strutil::split("a::b::c", "::");     // {"a","b","c"} (빈 토큰 포함, 트림 없음)
auto v2 = strutil::split("a, b, c", ',');      // {"a","b","c"} (트림, 빈 토큰 제거)
auto v3 = strutil::split_comma("mp3, ogg, wav"); // {"mp3","ogg","wav"}

// 결합
std::string j = strutil::join(v3, " | ");      // "mp3 | ogg | wav"

// 주석 제거 (INI/conf 파일용)
std::string val = strutil::strip_comment("value # inline comment"); // "value "

// 타입 변환 (실패 시 std::nullopt, 예외 없음)
auto i = strutil::to_int("42");          // std::optional<int>{42}
auto d = strutil::to_double("3.14");     // std::optional<double>{3.14}
auto b = strutil::to_bool("true");       // std::optional<bool>{true}
// "yes"/"1"/"on" → true | "no"/"0"/"off" → false

// 검색
bool has = strutil::contains("hello world", "world"); // true

// ── 숫자 / 시간 → 문자열 변환 ─────────────────────────────

// 바이트 크기
strutil::size2str(0);           // "0 B"
strutil::size2str(1536);        // "1.5 KB"
strutil::size2str(2097152, 2);  // "2.00 MB"

// 초 → HH:MM:SS (포맷 토큰: %H %M %S %Z)
strutil::sec2str(75.0);                    // "01:15"
strutil::sec2str(3661.0, "%H:%M:%S");      // "01:01:01"
strutil::sec2str(1.23,   "%M:%S.%Z");      // "00:01.23"

// 초 → 자연어
strutil::time2str(3661.0);        // "1h 1m 1s"
strutil::time2str(3661.0, true);  // "1:01:01"

// chrono::nanoseconds → 자동 단위
strutil::duration2str(std::chrono::milliseconds(2500)); // "2.50s"
strutil::duration2str(std::chrono::microseconds(150));  // "150.0ms"
strutil::duration2str(std::chrono::nanoseconds(800));   // "800ns"

// time_point → 날짜 문자열
strutil::timestamp2str(std::chrono::system_clock::now());           // "2024-03-15 09:42:00"
strutil::timestamp2str(std::chrono::system_clock::now(), "%Y/%m/%d"); // "2024/03/15"

// 하위 호환 alias
str::trim("  x  ");  // strutil::trim 과 동일
```

---

### encode — 인코딩 변환

`encode.hpp` | 네임스페이스 `util::`

```cpp
// UTF-8 ↔ wstring
std::wstring ws  = util::utf8_to_wstring("안녕하세요");
std::string  u8  = util::wstring_to_utf8(ws);

// ANSI(로컬) ↔ wstring
std::wstring wa  = util::ansi_to_wstring(ansi_str);
std::string  a   = util::wstring_to_ansi(ws);

// 대소문자 변환 (ASCII 범위, 로캘 독립)
std::wstring lo  = util::to_lower_ascii(L"HELLO");   // L"hello"
std::wstring up  = util::to_upper_ascii(L"world");   // L"WORLD"

// 인코딩 자동 감지 (BOM / UTF-8 유효성 검사)
util::Encoding enc = util::detect_encoding(data);
// Encoding::UTF8 / UTF8_BOM / UTF16_LE / UTF16_BE / ANSI / ASCII

// 파일 내용을 wstring으로 변환 (인코딩 자동 처리)
std::wstring content = util::convert_to_wstring(raw_bytes);

// 코드페이지
util::codepage cp   = util::parse_codepage("euc-kr");    // codepage::CP949
util::codepage ccp  = util::get_console_codepage();
std::string    name = util::codepage_name(cp);            // "CP949"

// 유니코드 시각 폭 (터미널 정렬용, wstring 기준)
int w = util::visual_width(L"안녕");   // 4 (CJK 전각 × 2)
int w2 = util::visual_width(L"AB");   // 2

// 정렬 패딩
std::wstring aligned = util::aligned_text(L"제목", 20, util::align::center);
```

> **UniString vs visual_width**: `term::UniString::display_width()`는 UTF-8 문자열 + UAX#29 완전 구현 버전이고, `util::visual_width()`는 wstring 기반 빠른 버전입니다. TUI 렌더링에는 UniString을, 인코딩 변환 파이프라인에는 util을 사용하세요.

---

### fnutil — 파일시스템 유틸

`fnutil.hpp` | 네임스페이스 `fnutil::`

#### 자연 정렬

```cpp
// 숫자 구간을 수치로 비교 ("file2" < "file10")
bool r = fnutil::naturalLess("file2", "file10");         // true
bool r2 = fnutil::naturalLess("File", "file", true);     // false (ic=true, 동등)

// fs::path 벡터 정렬
std::vector<fs::path> files = /* ... */;
fnutil::sort(files.begin(), files.end(),
             fnutil::flag::naturalName | fnutil::flag::ignoreCase);
```

#### Glob / 파일 탐색

```cpp
// 단순 와일드카드 매칭
auto cpp_files = fnutil::glob("/src", "*.cpp");   // vector<fs::path>

// 경로+패턴 통합 (재귀, 정렬 포함)
fnutil::glob_options opt;
opt.recursive   = true;
opt.ignoreCase  = true;
opt.absolute    = true;
auto all = fnutil::glob_ex("/project/*.hpp", opt);
```

#### 경로 분해

```cpp
auto parts = fnutil::split(fs::path("/home/user/file.txt"));
// parts.drive = ""    (POSIX) / "C:" (Windows)
// parts.dir   = "home/user"
// parts.name  = "file"
// parts.ext   = ".txt"

auto joined = fnutil::join({parts.drive, parts.dir, "newfile.md"});
```

#### wstring 래퍼

```cpp
std::wstring ext  = fnutil::get_extension(L"image.PNG");   // L"png"
std::wstring stem = fnutil::get_stem(L"archive.tar.gz");   // L"archive.tar"
std::wstring dir  = fnutil::get_directory(L"/a/b/c.txt");  // L"/a/b"
bool exists = fnutil::file_exists(L"config.ini");
bool is_dir = fnutil::directory_exists(L"/tmp");
std::intmax_t sz = fnutil::get_file_size(L"data.bin");     // -1 on error
```

#### 앱 경로 / OS 표준 디렉터리

```cpp
// 현재 실행 파일 경로 (dynamic buffer, weakly_canonical)
fs::path exe = fnutil::get_executable_path();   // e.g. /usr/bin/myapp

// 실행파일명 기반 설정 파일
fs::path cfg = fnutil::get_executable_conf();          // /usr/bin/myapp.conf
fs::path ini = fnutil::get_executable_conf("ini");     // /usr/bin/myapp.ini
fs::path cfg2 = fnutil::get_executable_conf("conf", /*create=*/true, "# defaults\n");

// Portable 설정 (./<app>/.config/<name>)
fs::path p = fnutil::get_portable_config("settings.ini");
fs::path p2 = fnutil::get_portable_config("settings.ini", /*create=*/true);

// OS 표준 경로 (앱 이름은 실행파일 stem 자동 사용)
// Windows: %APPDATA%/<app>  macOS: ~/Library/Application Support/<app>
// Linux:   $XDG_CONFIG_HOME/<app>  또는  ~/.config/<app>
fs::path cfgdir = fnutil::get_home_config_dir();
fs::path cfgf   = fnutil::get_home_config("app.ini");
fs::path cfgf2  = fnutil::get_home_config("app.ini", /*create=*/true, "[main]\n");

// Windows: %LOCALAPPDATA%/<app>/cache  macOS: ~/Library/Caches/<app>
// Linux:   $XDG_CACHE_HOME/<app>  또는  ~/.cache/<app>
fs::path cache = fnutil::get_cache_dir();

// Windows: %LOCALAPPDATA%/<app>/log  macOS: ~/Library/Logs/<app>
// Linux:   $XDG_STATE_HOME/<app>/log  또는  ~/.local/state/<app>/log
fs::path logs = fnutil::get_log_dir();

// 파일 시각 → 날짜 문자열 (fs::file_time_type 전용)
std::string mtime = fnutil::ftime2str(fs::last_write_time(p));  // "2024-03-15 09:42"
std::string mtime2 = fnutil::ftime2str(fs::last_write_time(p), "%Y/%m/%d");

// 확장자 정규화 / 파일 생성
std::string ext2 = fnutil::normalize_extension("conf");  // ".conf"
fnutil::create_file_if_missing("app.conf", "# config\n");
```

#### 고속 디렉터리 열거

Linux에서 `getdents64` 직접 syscall, Windows에서 `FindFirstFileEx + FIND_FIRST_EX_LARGE_FETCH` 를 사용해 `std::filesystem::directory_iterator` 대비 2~5배 빠릅니다.

```cpp
// 파일명만 — stat 없음 (최고 속도)
std::vector<std::string> names = fnutil::list_names("/music", {".mp3", ".flac"});

// 전체 경로
std::vector<fs::path> paths = fnutil::list("/src", {".cpp", ".hpp"});

// 재귀 탐색
fnutil::ls_options opt;
opt.extensions   = {".jpg", ".png"};
opt.recursive    = true;
opt.sort_natural = true;
auto images = fnutil::list("/photos", opt);

// 크기·날짜 포함 (파일별 stat 1회 추가)
auto entries = fnutil::list_stat("/downloads");
for (const auto& e : entries) {
    // e.name, e.path, e.size (bytes), e.mtime (unix timestamp), e.is_dir
}

// 파일 수만 카운트 (메모리 할당 최소)
std::size_t n = fnutil::count("/var/log", {".log"}, /*recursive=*/true);
```

---

### ConfReader — 설정 파일

`conf.hpp` | `str.hpp` 의존

INI 스타일 설정 파일 파서입니다. `#`·`;` 인라인 주석, 백슬래시 줄 이음, 따옴표 자동 제거, 섹션/키 대소문자 무시를 지원합니다.

```ini
# app.conf
window_x = 100
volume   = 0.8

[database]
host = localhost
port = 5432

audio_exts = mp3, ogg, \
             wav, flac

path = "C:/My Music"
```

```cpp
// 실행파일명.conf 자동 탐색
ConfReader conf;

// 경로 직접 지정
ConfReader conf("config/app.conf");
ConfReader conf("app.conf", /*throwOnMissing=*/true);

// 값 조회
std::string host  = conf.get("database", "host", "localhost");
int         port  = conf.getInt("database", "port", 5432);
double      vol   = conf.getDouble("volume", 1.0);
bool        debug = conf.getBool("debug", false);

// 루트 키 (섹션 없음)
int wx = conf.getInt("window_x", 0);

// 콤마 리스트
auto exts = conf.getList("audio_exts");  // {"mp3","ogg","wav","flac"}

// 존재 확인
if (conf.hasSection("database")) { /* ... */ }
if (conf.hasKey("database", "host")) { /* ... */ }

// 상태
bool ok = conf.isLoaded();
std::string path = conf.filePath();
```

---

### Args — CLI 인자 파서

`args.hpp` | `encode.hpp` + `fnutil.hpp` 의존

wstring 기반 CLI 인자 파서입니다. 플래그/값/파일 자동 분리, 와일드카드 확장, 디렉터리 전개, 확장자 필터, 중복 제거를 지원합니다.

```cpp
// 기본 사용
Args args(argc, argv);

// 플래그 확인 (--flag, -f)
if (args.has(L"--verbose")) { /* ... */ }
if (args.has(L"-v"))        { /* ... */ }

// 값 조회 (--key=value 또는 --key value)
std::wstring output = args.get(L"--output", L"result.txt");
int threads = args.get_int(L"--threads", 4);
bool dry    = args.get_bool(L"--dry-run");

// 공백 구분 값은 명시적 등록 필요
ArgsParseOptions opts;
opts.value_args = {L"--output", L"--threads", L"-o"};
Args args2(argc, argv, opts);

// 파일 목록 (와일드카드·디렉터리 자동 확장)
for (const auto& f : args.files()) {
    // fs::path
}

// 확장자 필터
auto cpp = args.files({L".cpp", L".hpp"});   // .cpp/.hpp 만
auto txt = args.files(L".txt");               // 단일 확장자

// 코드에서 확장자 필터 지정
opts.include_extensions = {L".cpp", L".hpp"};
opts.exclude_extensions = {L".bak"};

// 커맨드라인에서 지정
//   --include-ext=.cpp,.hpp   또는  -I=.cpp
//   --exclude-ext=.bak        또는  -E=.bak

// 재귀 탐색 (--recursive / -r / -R)
//   myapp --recursive /src *.cpp

// 디버깅
auto raw = args.raw();          // 원본 wstring 목록
auto all = args.all_options();  // 전체 옵션 맵
```

---

## 예제

### 예제 1: Hello World

```cpp
#include "term.hpp"

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);

    auto input = term::make_input_driver();
    input->setup();

    // 중앙에 텍스트 출력
    std::string_view msg = "Hello, Terminal! (ESC 로 종료)";
    int msg_w = term::UniString::display_width(msg);
    int x = (term.cols() - msg_w) / 2;
    int y = term.rows() / 2;

    term::AnsiString s;
    s.xy(x, y)
     .fg("bright_cyan").bold()
     .text(msg)
     .reset();
    screen.put(s);

    renderer.render();

    while (true) {
        auto ev = input->read_key();
        if (ev.key == term::KEY_ESC) break;
        if (ev.key == term::KEY_RESIZE) renderer.handle_resize();
    }

    return 0;
}
```

---

### 예제 2: 색상 그라데이션

```cpp
#include "term.hpp"

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);

    auto input = term::make_input_driver();
    input->setup();

    int cols = term.cols();
    int rows = term.rows();

    // TrueColor 그라데이션 바
    for (int y = 0; y < rows; ++y) {
        term::AnsiString s;
        s.xy(0, y);
        for (int x = 0; x < cols; ++x) {
            uint8_t r = static_cast<uint8_t>(255.0 * x / cols);
            uint8_t g = static_cast<uint8_t>(255.0 * y / rows);
            uint8_t b = static_cast<uint8_t>(128);
            s.bg(r, g, b).text(" ");
        }
        s.reset();
        screen.put(s);
    }

    // 가운데 텍스트
    term::AnsiString label;
    label.xy(cols/2 - 8, rows/2)
         .fg(255,255,255).bold()
         .bg(0,0,0)
         .text("  TrueColor 그라데이션  ")
         .reset();
    screen.put(label);

    renderer.render();

    while (input->read_key().key != term::KEY_ESC) {}

    return 0;
}
```

---

### 예제 3: 키 입력 이벤트 루프

```cpp
#include "term.hpp"
#include <deque>

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);

    auto input = term::make_input_driver();
    input->setup();

    std::deque<std::string> log;

    auto add_log = [&](std::string msg) {
        log.push_front(std::move(msg));
        if ((int)log.size() > term.rows() - 4)
            log.pop_back();
    };

    bool running = true;
    while (running) {
        // UI 그리기
        screen.clear();

        term::AnsiString title;
        title.xy(0, 0).fg("bright_white").bold()
             .text("키 이벤트 뷰어  (ESC: 종료, F1: 화면 초기화)")
             .reset();
        screen.put(title);

        for (int i = 0; i < (int)log.size(); ++i) {
            term::AnsiString row;
            row.xy(2, i + 2).text(log[i]).reset();
            screen.put(row);
        }

        renderer.render();

        // 입력 처리
        auto ev = input->read_key();

        if (ev.key == term::KEY_ESC)    { running = false; continue; }
        if (ev.key == term::KEY_RESIZE) { renderer.handle_resize(); continue; }
        if (ev.key == term::KEY_F1)     { log.clear(); continue; }

        // 키 이름 변환
        std::string desc;
        if (ev.is_printable()) {
            desc = "문자: '" + term::UniString::cp_to_utf8(ev.ch) + "'";
        } else {
            switch (ev.key) {
            case term::KEY_ENTER:     desc = "Enter";     break;
            case term::KEY_BACKSPACE: desc = "Backspace"; break;
            case term::KEY_TAB:       desc = "Tab";       break;
            case term::KEY_UP:        desc = "↑";         break;
            case term::KEY_DOWN:      desc = "↓";         break;
            case term::KEY_LEFT:      desc = "←";         break;
            case term::KEY_RIGHT:     desc = "→";         break;
            case term::KEY_HOME:      desc = "Home";      break;
            case term::KEY_END:       desc = "End";       break;
            case term::KEY_MOUSE:
                desc = "마우스 (" + std::to_string(ev.mouse.x)
                     + "," + std::to_string(ev.mouse.y) + ")";
                break;
            default:
                desc = "키코드: 0x" + std::to_string(ev.key);
            }
        }

        if (ev.has_ctrl())  desc = "Ctrl+" + desc;
        if (ev.has_alt())   desc = "Alt+"  + desc;
        if (ev.has_shift()) desc = "Shift+" + desc;

        add_log(desc);
    }

    return 0;
}
```

---

### 예제 4: 진행 표시줄

```cpp
#include "term.hpp"
#include <thread>
#include <chrono>

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);

    auto input = term::make_input_driver();
    input->setup();

    // 스무스 진행 표시줄
    term::ProgressBar bar;
    bar.set_rect({4, term.rows()/2, term.cols() - 8, 1});
    bar.set_style(term::ProgressBar::Style::Smooth);
    bar.set_colors(
        term::Color::from_rgb(100, 220, 120),  // 초록
        term::Color::from_index(238)            // 어두운 회색
    );

    // 얇은 진행 표시줄
    term::ProgressBar thin_bar;
    thin_bar.set_rect({4, term.rows()/2 + 2, term.cols() - 8, 1});
    thin_bar.set_style(term::ProgressBar::Style::Thin);
    thin_bar.set_colors(
        term::Color::from_rgb(80, 160, 255),
        term::Color::from_index(238)
    );

    for (int i = 0; i <= 100; ++i) {
        double v = i / 100.0;
        bar.set_value(v);
        thin_bar.set_value(v);

        screen.clear();

        // 제목
        term::AnsiString title;
        title.xy(4, term.rows()/2 - 2)
             .fg("bright_white").text("작업 진행 중... ")
             .fg("bright_yellow").bold().text(std::to_string(i) + "%")
             .reset();
        screen.put(title);

        bar.render(screen);
        thin_bar.render(screen);

        renderer.render();

        // 취소 체크 (논블로킹)
        auto ev = input->read_key(30);
        if (ev.key == term::KEY_ESC) break;
        if (ev.key == term::KEY_RESIZE) renderer.handle_resize();
    }

    // 완료 메시지
    term::AnsiString done;
    done.xy(4, term.rows()/2 + 4)
        .fg("bright_green").bold().text("✓ 완료!")
        .reset();
    screen.put(done);
    renderer.render();

    input->read_key();  // 아무 키나 대기
    return 0;
}
```

---

### 예제 5: 스피너 애니메이션

```cpp
#include "term.hpp"
#include <thread>
#include <future>

// 백그라운드 작업 시뮬레이션
std::string do_work() {
    std::this_thread::sleep_for(std::chrono::seconds(3));
    return "완료";
}

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);

    auto input = term::make_input_driver();
    input->setup();

    // 여러 스피너 동시 표시
    struct Task {
        std::string name;
        term::Spinner spinner;
        bool done = false;
    };

    std::vector<Task> tasks = {
        {"패키지 다운로드", term::Spinner(term::spinner::DOTS)},
        {"의존성 확인",     term::Spinner(term::spinner::MOON)},
        {"컴파일",          term::Spinner(term::spinner::BOUNCE)},
    };

    for (int i = 0; i < (int)tasks.size(); ++i) {
        tasks[i].spinner.set_rect({4, 5 + i * 2});
        tasks[i].spinner.set_label(tasks[i].name);
        tasks[i].spinner.set_color(term::Color::from_index(14));
    }

    // 순차 완료 시뮬레이션
    auto start = std::chrono::steady_clock::now();
    bool running = true;

    while (running) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();

        // 순서대로 완료 처리
        if (elapsed > 1.5) tasks[0].done = true;
        if (elapsed > 2.5) tasks[1].done = true;
        if (elapsed > 3.5) { tasks[2].done = true; running = false; }

        screen.clear();

        term::AnsiString title;
        title.xy(2, 3).fg("bright_white").bold()
             .text("설치 진행 중...").reset();
        screen.put(title);

        for (int i = 0; i < (int)tasks.size(); ++i) {
            auto& t = tasks[i];
            if (t.done) {
                term::AnsiString ok;
                ok.xy(4, 5 + i * 2)
                  .fg("bright_green").text("✓ ")
                  .fg(term::Color::default_color()).text(t.name)
                  .reset();
                screen.put(ok);
            } else {
                t.spinner.tick();
                t.spinner.render(screen);
            }
        }

        renderer.render();

        auto ev = input->read_key(50);
        if (ev.key == term::KEY_ESC) break;
        if (ev.key == term::KEY_RESIZE) renderer.handle_resize();
    }

    // 완료
    term::AnsiString done;
    done.xy(2, 12).fg("bright_cyan").bold()
        .text("✓ 모든 작업이 완료되었습니다.").reset();
    screen.put(done);
    renderer.render();

    input->read_key();
    return 0;
}
```

---

### 예제 6: 파일 탐색 다이얼로그

```cpp
#include "term.hpp"

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);

    auto input = term::make_input_driver();
    input->setup();

    // 배경 화면
    for (int y = 0; y < term.rows(); ++y) {
        term::AnsiString bg;
        bg.xy(0, y)
          .bg(term::Color::from_rgb(20, 20, 35))
          .text(std::string(term.cols(), ' '))
          .reset();
        screen.put(bg);
    }

    term::AnsiString hint;
    hint.xy(2, 1).fg(term::Color::from_index(8))
        .text("F2: 파일 열기   F3: 파일 저장   ESC: 종료")
        .reset();
    screen.put(hint);
    renderer.render();

    std::string status;
    bool running = true;

    while (running) {
        // 상태 표시
        if (!status.empty()) {
            term::AnsiString st;
            st.xy(2, term.rows() - 2)
              .fg(term::Color::from_index(10))
              .text(status).reset();
            screen.put(st);
            renderer.render();
        }

        auto ev = input->read_key();
        if (ev.key == term::KEY_ESC)    { running = false; continue; }
        if (ev.key == term::KEY_RESIZE) { renderer.handle_resize(); continue; }

        // F2: 파일 열기
        if (ev.key == term::KEY_F2) {
            term::FileDialogOptions opts;
            opts.title   = "파일 열기";
            opts.filters = {"*.cpp", "*.hpp", "*.txt", "*.md"};

            term::FileOpenDialog dlg(opts);
            term::run_dialog(dlg, screen, renderer, *input);

            if (dlg.confirmed()) {
                auto p = dlg.selected_path();
                status = "열기: " + (p ? p->string() : "없음");
            } else {
                status = "취소됨";
            }
            continue;
        }

        // F3: 파일 저장
        if (ev.key == term::KEY_F3) {
            term::FileDialogOptions opts;
            opts.title = "파일 저장";

            term::FileSaveDialog dlg(opts);
            dlg.set_filename("output.txt");
            term::run_dialog(dlg, screen, renderer, *input);

            if (dlg.confirmed()) {
                auto p = dlg.save_path();
                status = "저장: " + (p ? p->string() : "없음");
            } else {
                status = "취소됨";
            }
            continue;
        }
    }

    return 0;
}
```

---

### 예제 7: 전체 TUI 앱

탭 이동, 위젯 포커스, 다이얼로그를 조합한 실용적인 TUI 앱 패턴.

```cpp
#include "term.hpp"

int main() {
    term::Terminal term;
    term::AnsiScreen screen(term.cols(), term.rows());
    term::AnsiRenderer renderer(screen, term);

    term.enter_alt_screen();
    term.show_cursor(false);
    term.set_title("설정 관리자");

    auto input = term::make_input_driver();
    input->setup();

    // 위젯 구성
    term::TextInput name_input("이름 입력");
    term::CheckBox  dark_mode("다크 모드", true);
    term::CheckBox  auto_save("자동 저장", false);
    term::Button    save_btn("저장");
    term::Button    cancel_btn("취소");
    term::ProgressBar usage_bar;

    // 레이아웃 설정
    auto layout = [&]() {
        int cx = term.cols(), cy = term.rows();
        name_input.set_rect ({4, 4,  cx - 8, 1});
        dark_mode.set_rect  ({4, 7,  30, 1});
        auto_save.set_rect  ({4, 9,  30, 1});
        save_btn.set_rect   ({4, 12, 12, 1});
        cancel_btn.set_rect ({18, 12, 12, 1});
        usage_bar.set_rect  ({4, 15, cx - 8, 1});
        usage_bar.set_value(0.62);
        usage_bar.set_style(term::ProgressBar::Style::Smooth);
    };
    layout();

    // 포커스 순환 관리
    enum Focus { NAME, DARK, AUTO, SAVE, CANCEL, FOCUS_COUNT };
    int focus = NAME;

    auto update_focus = [&]() {
        name_input.set_focused(focus == NAME);
        dark_mode.set_focused (focus == DARK);
        auto_save.set_focused (focus == AUTO);
        save_btn.set_focused  (focus == SAVE);
        cancel_btn.set_focused(focus == CANCEL);
    };
    update_focus();

    // 상태바
    term::StatusBar sbar;
    sbar.set_sections({
        {"Tab: 다음  Shift+Tab: 이전  Enter: 선택  ESC: 취소",
         term::Color::from_index(7), term::Color::from_index(0), 0,
         term::StatusBar::Section::Align::Left},
        {"설정 관리자 v1.0",
         term::Color::from_index(8), term::Color::from_index(0), 20,
         term::StatusBar::Section::Align::Right},
    });

    bool running = true;
    while (running) {
        // 화면 렌더링
        screen.clear();

        // 패널 테두리 (수동)
        term::AnsiString border;
        border.xy(2, 2).fg(term::Color::from_index(14))
              .text("╭─── 기본 설정 ").reset();
        screen.put(border);

        // 레이블
        auto put_label = [&](int x, int y, std::string_view text) {
            term::AnsiString s;
            s.xy(x, y).fg(term::Color::from_index(11)).text(text).reset();
            screen.put(s);
        };
        put_label(4, 3,  "이름:");
        put_label(4, 6,  "테마:");
        put_label(4, 8,  "저장:");
        put_label(4, 14, "디스크 사용량:");

        // 위젯 렌더
        name_input.render(screen);
        dark_mode.render(screen);
        auto_save.render(screen);
        save_btn.render(screen);
        cancel_btn.render(screen);
        usage_bar.render(screen);

        // 상태바
        sbar.set_rect({0, term.rows() - 1, term.cols(), 1});
        sbar.render(screen);

        renderer.render();

        // 입력 처리
        auto ev = input->read_key();

        if (ev.key == term::KEY_RESIZE) {
            renderer.handle_resize();
            layout();
            continue;
        }

        if (ev.key == term::KEY_ESC) { running = false; continue; }

        // 포커스 이동
        if (ev.key == term::KEY_TAB) {
            focus = (focus + (ev.has_shift() ? FOCUS_COUNT - 1 : 1)) % FOCUS_COUNT;
            update_focus();
            continue;
        }

        // 현재 포커스 위젯에 키 전달
        switch (focus) {
        case NAME:   name_input.handle_key(ev); break;
        case DARK:   dark_mode.handle_key(ev);  break;
        case AUTO:   auto_save.handle_key(ev);  break;
        case SAVE:
            if (save_btn.handle_key(ev)) {
                // 저장 처리
                term::MessageBox mb("알림", "설정이 저장되었습니다.",
                                    term::MessageBox::Type::Info,
                                    term::MessageBox::Buttons::OK);
                term::run_dialog(mb, screen, renderer, *input);
                running = false;
            }
            break;
        case CANCEL:
            if (cancel_btn.handle_key(ev)) running = false;
            break;
        }
    }

    return 0;
}
```

---

## 충돌 방지: term\_info + input 함께 사용

`term_info.hpp` 와 `input.hpp` 를 동시에 include 하면 세 가지 잠재적 충돌이 있었습니다.  
`term_shared.hpp` 와 `InputOptions` 로 완전히 해결되었습니다.

| 충돌 | 해결 방법 |
|------|-----------|
| SIGWINCH 이중 핸들러 | `shared_detail::g_sigwinch_flag` 공유, `ensure_sigwinch_handler()` 1회 설치 |
| SA_RESTART 불일치 | 공유 핸들러가 SA_RESTART 없음으로 통일 |
| Raw mode 이중 관리 | `InputOptions::manage_raw_mode` 로 소유권 명시 |

```
권장 조합:
  Terminal      — 출력 전담 (write, flush, alt screen, cursor)
  InputDriver   — 입력 전담 (raw mode, SIGWINCH, 키/마우스)
  ↓
  중복 없음, 복원 순서 보장
```

---

## 파일 구조

| 파일 | 네임스페이스 | 역할 |
|------|-------------|------|
| `term.hpp` | — | 단일 포함 헤더 (전체 라이브러리) |
| `platform.hpp` | — | 플랫폼 감지 매크로 (`TERM_PLATFORM_*`, `PLATFORM_WINDOWS`) |
| `encode.hpp` | `util::` | 인코딩 변환, 시각 폭, codepage, BOM 감지 |
| `str.hpp` | `strutil::` | 문자열 유틸 + 숫자/시간/크기 포맷 변환, alias `str::` |
| `term_shared.hpp` | `shared_detail` | 공유 SIGWINCH 상태 (충돌 방지) |
| `term_info.hpp` | `term::` | Terminal RAII, ColorLevel, get_terminal_size |
| `input.hpp` | `term::` | InputDriver, KeyEvent, MouseEvent, InputOptions |
| `fnutil.hpp` | `fnutil::` | 자연 정렬, glob, 경로 분해, 앱 디렉터리, 고속 파일 열거 |
| `conf.hpp` | — | ConfReader — INI 설정 파일 파서 |
| `args.hpp` | — | Args — CLI 인자 파서 |
| `ansi_string.hpp` | `term::` | Color, Attr, AnsiString (빌더) |
| `ansi_screen.hpp` | `term::` | Cell, AnsiScreen (논리 버퍼) |
| `ansi_optimizer.hpp` | `term::` | diff → 최소 ANSI 이스케이프 생성 |
| `ansi_renderer.hpp` | `term::` | 렌더 루프 (Screen + Optimizer + Terminal) |
| `ansi_tui.hpp` | `term::` | Widget, Button, CheckBox, ProgressBar, TextInput, Panel, StatusBar, MessageBox |
| `ansi_tui_dlg.hpp` | `term::` | FileOpenDialog, FileSaveDialog, SelectDialog, run_dialog |
| `spinner.hpp` | `term::` | Spinner 위젯 + 프리셋 |
| `uni_string.hpp` | `term::` | UniString (UAX#29 완전 구현, 너비 계산, 클리핑) |
| `unicode_width_table.hpp` | `term::detail` | Unicode 15.1 EastAsianWidth 테이블 (자동생성) |
| `unicode_grapheme_table.hpp` | `term::detail` | Unicode 15.1 GraphemeBreak 테이블 (자동생성) |
| `util.hpp` | `term::util` | **폐기 shim** — fnutil/strutil로 위임, 하위 호환 전용 |

---

## 라이선스

MIT License