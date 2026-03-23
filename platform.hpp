#pragma once
/**
 * platform.hpp — 플랫폼 감지 매크로  (TDD §12.1)
 * TERM_ 접두사 → TERM_
 */

#if defined(_WIN32) || defined(_WIN64)
#  define TERM_PLATFORM_WINDOWS 1
#  define TERM_PLATFORM_NAME "Windows"
#elif defined(__APPLE__) && defined(__MACH__)
#  define TERM_PLATFORM_MACOS   1
#  define TERM_PLATFORM_POSIX   1
#  define TERM_PLATFORM_NAME "macOS"
#elif defined(__linux__)
#  define TERM_PLATFORM_LINUX   1
#  define TERM_PLATFORM_POSIX   1
#  define TERM_PLATFORM_NAME "Linux"
#else
#  define TERM_PLATFORM_POSIX   1
#  define TERM_PLATFORM_NAME "Unknown POSIX"
#endif

#if defined(__GNUC__) || defined(__clang__)
#  define TERM_LIKELY(x)      __builtin_expect(!!(x), 1)
#  define TERM_UNLIKELY(x)    __builtin_expect(!!(x), 0)
#  define TERM_FORCE_INLINE   __attribute__((always_inline)) inline
#else
#  define TERM_LIKELY(x)      (x)
#  define TERM_UNLIKELY(x)    (x)
#  define TERM_FORCE_INLINE   inline
#endif

#if defined(TERM_PLATFORM_WINDOWS)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// ── encode.hpp / console.hpp 호환 별칭 ───────────────────
// encode.hpp 는 PLATFORM_WINDOWS(0/1) 매크로를 사용한다.
// platform.hpp 를 먼저 include 하면 아래 별칭이 정의되므로
// encode.hpp 는 자체 플랫폼 감지 블록을 생략할 수 있다.
#if defined(TERM_PLATFORM_WINDOWS)
#  ifndef PLATFORM_WINDOWS
#    define PLATFORM_WINDOWS 1
#  endif
#else
#  ifndef PLATFORM_WINDOWS
#    define PLATFORM_WINDOWS 0
#  endif
#endif
