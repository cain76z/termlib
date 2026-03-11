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
 */
#include "platform.hpp"
#include "term_info.hpp"
#include "util.hpp"
#include "unicode_width_table.hpp"
#include "unicode_grapheme_table.hpp"
#include "uni_string.hpp"
#include "ansi_screen.hpp"
#include "ansi_string.hpp"
#include "ansi_optimizer.hpp"
#include "ansi_renderer.hpp"
#include "spinner.hpp"
#include "ansi_tui.hpp"
#include "ansi_tui_dlg.hpp"
#include "input.hpp"
