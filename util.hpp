#pragma once

/**
 * util.hpp — 하위호환 shim
 *
 * 기존 코드를 수정하지 않고 encode.hpp + console.hpp 로 분리한 결과를
 * 그대로 노출합니다.  새 코드는 직접 include 권장:
 *
 *   #include "encode.hpp"   // 인코딩·유니코드
 *   #include "console.hpp"  // 콘솔·포맷팅
 */

#ifndef UTIL_HPP
#define UTIL_HPP

#include "encode.hpp"
#include "console.hpp"

#endif // UTIL_HPP