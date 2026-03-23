#pragma once
/**
 * term.hpp — 단일 포함 헤더  (ansi_terminal 라이브러리)
 *
 * 이 헤더 하나만 include 하면 라이브러리 전체를 사용할 수 있습니다.
 * 별도의 .cpp 파일이나 매크로 정의가 필요 없습니다.
 *
 *   #include <term/term.hpp>
 *
 * Build:
 *   g++ -std=c++20 -I include myapp.cpp -o myapp
 *
 * ## 포함 구성 요소
 *
 * | 헤더 | 네임스페이스 | 역할 |
 * |------|-------------|------|
 * | platform.hpp       | —             | 플랫폼 매크로 |
 * | encode.hpp         | util::        | 인코딩 변환, 시각 폭 |
 * | str.hpp            | strutil::     | 문자열 유틸 + 숫자/시간 변환 |
 * | term_shared.hpp    | shared_detail | 공유 SIGWINCH 상태 |
 * | term_info.hpp      | term::        | Terminal RAII |
 * | fnutil.hpp         | fnutil::      | 파일시스템, 경로, 앱 디렉터리, 고속 열거 |
 * | conf.hpp           | —             | INI 설정 파서 |
 * | args.hpp           | —             | CLI 인자 파서 |
 * | unicode_*_table    | term::detail  | UAX#29/EAW 테이블 |
 * | uni_string.hpp     | term::        | UniString (UAX#29 완전 구현) |
 * | ansi_string.hpp    | term::        | Color, Attr, AnsiString |
 * | ansi_screen.hpp    | term::        | Cell, AnsiScreen |
 * | ansi_optimizer.hpp | term::        | diff → ANSI 이스케이프 |
 * | ansi_renderer.hpp  | term::        | 렌더 루프 |
 * | spinner.hpp        | term::        | Spinner 위젯 |
 * | ansi_tui.hpp       | term::        | TUI 위젯 (Button, CheckBox …) |
 * | ansi_tui_dlg.hpp   | term::        | 다이얼로그 + run_dialog |
 * | input.hpp          | term::        | InputDriver (키/마우스) |
 * | util.hpp           | term::util    | 폐기 shim (fnutil+strutil로 위임) |
 *
 * ## term_info + input 동시 사용 충돌 해결
 *
 * term_shared.hpp 가 공유 SIGWINCH 상태를 제공하여
 * term_info.hpp + input.hpp 동시 사용 시 발생하는 충돌을 방지합니다.
 * 자세한 내용: term_shared.hpp, input.hpp(InputOptions) 참조
 */

// ── 레이어 1: 플랫폼 기반 ──────────────────────────────────
#include "platform.hpp"

// ── 레이어 2: 인코딩·문자열 유틸 ──────────────────────────
// encode.hpp 는 platform.hpp 의 PLATFORM_WINDOWS 별칭을 사용한다.
#include "encode.hpp"
#include "str.hpp"

// ── 레이어 3: 터미널 제어 ──────────────────────────────────
#include "term/term_shared.hpp"          // 공유 SIGWINCH 상태 (충돌 방지) — 가장 먼저
#include "term/term_info.hpp"

// ── 레이어 4: 파일시스템 유틸 ─────────────────────────────
#include "fnutil.hpp"

// ── 레이어 5: 앱 레벨 유틸 ────────────────────────────────
#include "conf.hpp"
#include "args.hpp"

// ── 레이어 6: 유니코드 ────────────────────────────────────
#include "term/unicode_width_table.hpp"
#include "term/unicode_grapheme_table.hpp"
#include "term/uni_string.hpp"

// ── 레이어 7: TUI 렌더링 ──────────────────────────────────
#include "term/ansi_screen.hpp"
#include "term/ansi_string.hpp"
#include "term/ansi_optimizer.hpp"
#include "term/ansi_renderer.hpp"
#include "term/spinner.hpp"
#include "term/ansi_tui.hpp"
#include "term/ansi_tui_dlg.hpp"

// ── 레이어 8: 입력 ────────────────────────────────────────
#include "term/input.hpp"