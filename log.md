# ansi_terminal 라이브러리 — 프로젝트 진행 로그

## 프로젝트 개요

헤더 온리(header-only) C++20 ANSI 터미널 TUI 라이브러리.
별도 `.cpp` 없이 `#include "term.hpp"` 한 줄로 전체 기능 사용 가능.

---

## 전체 파일 구조 및 의존 방향 (최신)

```
platform.hpp                    ← TERM_PLATFORM_*, PLATFORM_WINDOWS 별칭, windows.h
  ├── encode.hpp                 ← 인코딩 변환, 시각폭, codepage  (namespace util)
  │     └── [iconv.h — POSIX only]
  ├── str.hpp                    ← 문자열 유틸  (namespace strutil, alias str::)
  │     └── conf.hpp             ← INI 설정 파서  (class ConfReader)
  ├── term_shared.hpp            ← 공유 SIGWINCH 상태
  │     ├── term_info.hpp        ← Terminal RAII  (namespace term)
  │     └── input.hpp            ← InputDriver  (namespace term)
  ├── fnutil.hpp                 ← 파일시스템 + 고속 열거  (namespace fnutil)
  │     └── args.hpp             ← CLI 인자 파서  (class Args)
  └── util.hpp                   ← 경로·포맷 유틸  (namespace term::util)

ansi_string.hpp   (Color, Attr, AnsiString)  — namespace term
  └── ansi_screen.hpp            (Cell, AnsiScreen)
        └── ansi_optimizer.hpp   (diff 최소 이스케이프)
              └── ansi_renderer.hpp (렌더 루프)

uni_string.hpp    (UniString — UAX#29 완전 구현)
unicode_width_table.hpp
unicode_grapheme_table.hpp

ansi_tui.hpp      (Widget, Button, CheckBox, ProgressBar …)
  └── ansi_tui_dlg.hpp (FileOpenDialog, FileSaveDialog, SelectDialog, run_dialog)

spinner.hpp       (Spinner 위젯 + 프리셋)
term.hpp          ← 단일 포함 헤더 (전체 포함)
```

---

## 변경 이력

### [MERGE-02] 통합 2단계 완료 (최신)

#### 1. platform.hpp — PLATFORM_WINDOWS 별칭 추가

encode.hpp 는 `PLATFORM_WINDOWS` 매크로를 사용한다.
기존 term 라이브러리는 `TERM_PLATFORM_WINDOWS` 를 사용해 충돌이 발생했다.

해결: platform.hpp 하단에 아래 별칭 블록 추가.

```cpp
#if defined(TERM_PLATFORM_WINDOWS)
#  ifndef PLATFORM_WINDOWS
#    define PLATFORM_WINDOWS 1
#  endif
#else
#  ifndef PLATFORM_WINDOWS
#    define PLATFORM_WINDOWS 0
#  endif
#endif
```

`ifndef` 가드를 사용하므로 encode.hpp 가 독립 include 될 때 자체 감지와 충돌하지 않는다.

#### 2. encode.hpp — platform.hpp 통합

- 자체 플랫폼 감지 블록 제거
- `__has_include("platform.hpp")` 로 유무 감지:
  - 있으면: `#include "platform.hpp"` 로 위임
  - 없으면: 기존 자체 감지 폴백 (단독 사용 호환 유지)
- POSIX iconv: `#if !PLATFORM_WINDOWS` 블록에서 별도 include 유지
- 중복 `windows.h` include 방지

#### 3. util.hpp (term) — console.hpp 기능 흡수

console.hpp 의 두 포맷 함수를 `term::util` 네임스페이스에 추가.

| 함수 | 설명 |
|------|------|
| `ftime2str(fs::file_time_type, format)` | 파일 시각 → 날짜 문자열 |
| `sec2str(double, format)` | 초 → 시간 문자열 |

`#include <ctime>` 추가.

#### 4. term.hpp — 신규 헤더 6개 추가, 레이어 구조화

```
레이어 1: platform.hpp
레이어 2: encode.hpp, str.hpp
레이어 3: term_shared.hpp, term_info.hpp, util.hpp
레이어 4: fnutil.hpp
레이어 5: conf.hpp, args.hpp
레이어 6: unicode_*_table.hpp, uni_string.hpp
레이어 7: ansi_*.hpp, spinner.hpp, ansi_tui*.hpp
레이어 8: input.hpp
```

---

### [MERGE-01] 통합 1단계 완료

- str.hpp: `namespace str` → `namespace strutil` (alias str:: 유지)
- fastls.hpp → fnutil.hpp 흡수: 중복 자연 정렬 제거, FNUTIL_WINDOWS 통일

---

### [FIX-CONFLICT] term_info + input 충돌 해결

| 항목 | 해결 방법 |
|------|-----------|
| SIGWINCH 이중 핸들러 | `shared_detail::ensure_sigwinch_handler()` 1회 설치 |
| SA_RESTART 불일치 | 공유 핸들러 SA_RESTART 없음으로 통일 |
| Raw mode 이중 관리 | `InputOptions::manage_raw_mode` 소유권 명시 |

---

## 현재 파일 목록 (22개)

### 기존 유지
`term_shared.hpp`, `term_info.hpp`, `input.hpp`,
`ansi_string.hpp`, `ansi_screen.hpp`, `ansi_optimizer.hpp`, `ansi_renderer.hpp`,
`ansi_tui.hpp`, `ansi_tui_dlg.hpp`, `spinner.hpp`,
`uni_string.hpp`, `unicode_width_table.hpp`, `unicode_grapheme_table.hpp`

### 수정됨
| 파일 | 변경 내용 |
|------|-----------|
| `platform.hpp` | PLATFORM_WINDOWS 별칭 추가 |
| `encode.hpp` | platform.hpp 통합, __has_include 폴백 |
| `util.hpp` | ftime2str, sec2str 추가, ctime include |
| `term.hpp` | 신규 헤더 6개 include, 레이어 구조화 |

### 신규 추가
`str.hpp`, `fnutil.hpp`(fastls 흡수), `conf.hpp`, `args.hpp`

### 폐기
`fastls.hpp` (fnutil.hpp 흡수), `console.hpp` (util.hpp + term_info.hpp 흡수)

---

## API 요약 (신규 추가분)

### strutil:: (str.hpp)
```cpp
strutil::trim(sv)           // string_view 반환 (zero-copy)
strutil::to_lower(sv)       // string 반환
strutil::split(sv, delim)   // vector<string>
strutil::split_comma(sv)    // 콤마 분리 + trim + 빈 토큰 제거
strutil::strip_comment(sv)  // # ; 주석 제거
strutil::join(range, delim)
strutil::to_int(sv)         // optional<int>
strutil::to_double(sv)      // optional<double>
strutil::to_bool(sv)        // optional<bool>
// 하위 호환: namespace str = strutil
```

### ConfReader (conf.hpp)
```cpp
ConfReader conf;                         // 실행파일명.conf 자동 탐색
ConfReader conf("app.conf");
conf.get("section", "key", "default");  // 문자열
conf.getInt("key", 0);
conf.getBool("key", false);
conf.getList("key");                     // vector<string>, 콤마 분리
conf.hasSection("db");
conf.hasKey("db", "host");
```

### fnutil:: 신규 추가분 (fnutil.hpp)
```cpp
// 고속 열거 (Linux: getdents64, Windows: FindFirstFileEx)
fnutil::list_names("/dir", {".cpp"});        // vector<string>, stat 없음
fnutil::list("/dir", {".hpp"});              // vector<fs::path>, stat 없음
fnutil::list_stat("/dir", {}, true);         // vector<fnutil::entry> (재귀)
fnutil::count("/dir", {".txt"});             // size_t

// 단일 자연 정렬 구현 (구 fastls::detail::natural_less 흡수)
fnutil::naturalLess("file2", "file10", true); // true
```

### Args (args.hpp)
```cpp
Args args(argc, argv);
args.has(L"--verbose");          // bool
args.get(L"--output");           // wstring
args.get_int(L"--threads", 4);   // int
args.get_bool(L"--dry-run");     // bool
args.files();                    // vector<fs::path>
args.files({L".cpp", L".hpp"});  // 확장자 필터
```

### term::util:: 추가분 (util.hpp)
```cpp
term::util::ftime2str(fs::last_write_time(p));       // "2024-03-15 09:42"
term::util::ftime2str(ftime, "%Y/%m/%d");
term::util::sec2str(75.5);                           // "01:15"
term::util::sec2str(3661, "%H:%M:%S");               // "01:01:01"
```

---

## 빌드 요구사항

- C++20 이상
- Linux / macOS / Windows
- `g++ -std=c++20` 또는 `clang++ -std=c++20`
- Linux encode.hpp: glibc 포함 환경은 링크 불필요. 그 외 `-liconv`

---

## 미완료 / 향후 작업

- [x] 전체 헤더 컴파일 테스트 (Linux — GCC 13, -Wall -Wextra -Wpedantic 경고 0)
- [x] encode.hpp `util::` 와 `term::util::` 충돌 없음 검증 완료
      (현재 서로 다른 네임스페이스로 실제 충돌 없음)
- [x] readme.md 신규 API 섹션 추가 (strutil, encode, fnutil, ConfReader, Args)

---

### [FIX-COMPILE] 컴파일 경고/에러 수정

컴파일 테스트(`-Wall -Wextra -Wpedantic`) 결과 발견된 3가지 문제 수정:

| # | 파일 | 문제 | 수정 |
|---|------|------|------|
| 1 | `fnutil.hpp` | `naturalLess` string/string_view 이중 오버로드 → `const char*` 모호성 | `string_view` 단일 구현으로 통합 |
| 2 | `fnutil.hpp` | `linux_dirent64::d_name[]` C99 유연 배열 `-Wpedantic` 경고 | `d_name[1]` 로 교체 |
| 3 | `util.hpp` | `duration2str` `chrono::count()` 반환 타입이 `long` → `%lld` 불일치 | `static_cast<long long>` 명시적 캐스트 |
| 4 | `fnutil.hpp` | `list_names/list/list_stat/count` string/fs::path 이중 오버로드 → `const char*` 모호성 | `fs::path` 단일 진입점으로 통합 |
| 5 | `conf.hpp` | `get(section, key, default)` 3-param 기본값이 2-param 오버로드와 충돌 | 3-param에서 기본값 제거 |

최종 빌드: `g++ -std=c++20 -Wall -Wextra -Wpedantic -O2` 경고/에러 0개, 34개 API 테스트 모두 통과.

---

### [REFACTOR-01] term::util 해체 완료

**배경**: `term::util` 네임스페이스가 성격이 다른 두 종류의 함수를 혼합하고 있었음.
- 경로/파일시스템 함수 → `fnutil`에 있어야 자연스러움
- 문자열 변환 함수 → `strutil`에 있어야 자연스러움
- `ConfReader::executableConfPath()` → `fnutil::get_executable_conf()` 와 동일한 기능이 중복 구현됨

**변경 내용:**

#### str.hpp — 숫자/시간 변환 5개 추가
| 함수 | 설명 |
|------|------|
| `strutil::size2str(bytes, precision)` | 바이트 → "1.5 KB" |
| `strutil::time2str(seconds, compact)` | 초 → "1h 23m 45s" / "1:23:45" |
| `strutil::sec2str(seconds, format)` | 초 → "%H:%M:%S" 포맷 |
| `strutil::duration2str(nanoseconds)` | chrono → "2.50s" / "150.0ms" |
| `strutil::timestamp2str(time_point, fmt)` | system_clock → 날짜 문자열 |

include 추가: `<chrono>`, `<cstdint>`, `<cstdio>`, `<ctime>`

#### fnutil.hpp — 경로/설정 함수 9개 이동 + 개선
| 함수 | 변경 |
|------|------|
| `get_executable_path()` | util 버전(dynamic buffer, weakly_canonical)으로 교체 |
| `get_executable_conf(ext, create, contents)` | util에서 이동 |
| `get_portable_config(name, create, contents)` | util에서 이동 |
| `get_home_config_dir()` | util에서 이동 |
| `get_home_config(name, create, contents)` | util에서 이동 |
| `get_cache_dir()` | util에서 이동 |
| `get_log_dir()` | util에서 이동 |
| `normalize_extension(ext)` | util에서 이동 |
| `create_file_if_missing(path, contents)` | util에서 이동 |
| `ftime2str(file_time_type, format)` | util에서 이동 (fs:: 의존이므로 fnutil이 적합) |

include 추가: `<chrono>`, `<ctime>`, `<fstream>`, `<limits.h>`, macOS `<mach-o/dyld.h>`

#### conf.hpp — 중복 구현 제거
- private `executableConfPath()` 완전 제거
- 기본 생성자: `fnutil::get_executable_conf("conf")` 위임
- 의존: `#include "fnutil.hpp"` 추가, `windows.h`/`limits.h` 직접 include 제거

#### util.hpp — 57줄 폐기 shim으로 교체
```cpp
namespace term::util {
    using fnutil::get_executable_path;    // 경로 함수들 위임
    using fnutil::get_executable_conf;
    // ... 9개 fnutil 위임 ...
    using strutil::size2str;              // 변환 함수들 위임
    // ... 4개 strutil 위임 ...
}
```
기존 `term::util::size2str()` 등의 호출 코드는 shim으로 컴파일 유지.

#### term.hpp — `#include "util.hpp"` 제거
fnutil과 str이 이미 include되므로 util.hpp 불필요.

**런타임 테스트**: 50개 항목 전부 통과 (`-Wall -Wextra -Wpedantic` 경고/에러 0)
