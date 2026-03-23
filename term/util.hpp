#pragma once
/**
 * util.hpp — 폐기(deprecated) shim
 *
 * 이 파일에 있던 함수들이 아래와 같이 이동됐습니다:
 *
 *   [경로 / 설정 / 파일]           → fnutil.hpp  (namespace fnutil)
 *     get_executable_path()
 *     get_executable_conf()
 *     get_portable_config()
 *     get_home_config_dir() / get_home_config()
 *     get_cache_dir()
 *     get_log_dir()
 *     normalize_extension()
 *     create_file_if_missing()
 *     ftime2str()
 *
 *   [숫자 / 시간 → 문자열]         → str.hpp     (namespace strutil)
 *     size2str()
 *     time2str()
 *     sec2str()
 *     duration2str()
 *     timestamp2str()
 *
 * 기존 코드가 term::util:: 로 참조하던 경우 아래 shim이
 * 컴파일을 유지시켜 주지만, 새 코드는 fnutil:: / strutil:: 을 직접 사용하세요.
 */
#ifndef _TERM_UTIL_HPP_
#define _TERM_UTIL_HPP_

#include "fnutil.hpp"
#include "str.hpp"

namespace term::util {

// 경로 함수 위임
using fnutil::get_executable_path;
using fnutil::get_executable_conf;
using fnutil::get_portable_config;
using fnutil::get_home_config_dir;
using fnutil::get_home_config;
using fnutil::get_cache_dir;
using fnutil::get_log_dir;
using fnutil::normalize_extension;
using fnutil::create_file_if_missing;
using fnutil::ftime2str;

// 문자열 변환 위임
using strutil::size2str;
using strutil::time2str;
using strutil::sec2str;
using strutil::duration2str;
using strutil::timestamp2str;

} // namespace term::util

#endif // _TERM_UTIL_HPP_
