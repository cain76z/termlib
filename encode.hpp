#pragma once

/**
 * @file encode.hpp
 * @brief 문자 인코딩 및 유니코드 처리 유틸리티 (C++17 단일 헤더)
 *
 * 이 헤더는 크로스 플랫폼(Windows, Linux) 환경에서 동작하는
 * 문자열 인코딩 변환 및 유니코드 분석 기능을 제공합니다.
 *
 * @section features 주요 기능
 * - **코드페이지 관리**: `codepage` 열거형을 통한 다양한 인코딩 지원 및 이름 파싱.
 * - **인코딩 변환**: UTF-8, ANSI(로컬), wstring 간의 상호 변환.
 *   (Windows: WinAPI 사용, Linux: iconv 사용)
 * - **자동 감지**: BOM(Byte Order Mark) 및 UTF-8 유효성 검사를 통한 인코딩 자동 감지.
 * - **유니코드 분석**: 문자의 시각적 폭(Visual Width) 계산 및 터미널 정렬 지원.
 *
 * @note C++17 이상 필요.
 * @warning `std::codecvt`의 depreciation 경고를 내부적으로 억제합니다.
 */

// =========================================================
// Warning suppression (deprecated codecvt 등)
// =========================================================
#if defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 4996) // std::codecvt 등의 deprecated 경고 억제
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// platform.hpp 가 PLATFORM_WINDOWS(0/1), WIN32_LEAN_AND_MEAN, NOMINMAX,
// windows.h include 를 모두 처리한다.
// platform.hpp 없이 encode.hpp 만 단독 사용하는 경우를 위해
// platform.hpp 가 없을 때는 직접 감지한다.
#if __has_include("platform.hpp")
#  include "platform.hpp"
#elif defined(_WIN32) || defined(_WIN64)
#  ifndef PLATFORM_WINDOWS
#    define PLATFORM_WINDOWS 1
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  ifndef PLATFORM_WINDOWS
#    define PLATFORM_WINDOWS 0
#  endif
#endif

// POSIX: iconv (인코딩 변환)
#if !PLATFORM_WINDOWS
#  include <iconv.h>
#  include <clocale>
#endif

namespace util {

// =========================================================
// 1. codepage — 콘솔/파일 코드페이지 열거형
// =========================================================

/**
 * @brief 지원하는 코드페이지 목록을 정의하는 열거형
 *
 * 시스템의 로캘과 독립적으로 특정 인코딩을 지정할 때 사용합니다.
 */
enum class codepage {
    UTF8,     ///< UTF-8 (BOM 없음)
    UTF16LE,  ///< UTF-16 Little Endian
    UTF16BE,  ///< UTF-16 Big Endian
    CP437,    ///< DOS 라틴어 (미국/서유럽)
    CP850,    ///< DOS 라틴어 1 (서유럽)
    CP932,    ///< 일본어 (Shift-JIS)
    CP936,    ///< 중국어 간체 (GBK)
    CP949,    ///< 한국어 (EUC-KR/통합 완성형)
    CP950,    ///< 중국어 번체 (Big5)
    ANSI,     ///< 시스템 기본 ANSI 코드페이지 (Windows) 또는 로캘 설정 (Linux)
    UNKNOWN   ///< 알 수 없는 인코딩
};

/**
 * @brief codepage 열거값을 사람이 읽기 쉬운 문자열로 변환합니다.
 * @param cp 코드페이지 열거값
 * @return 코드페이지 이름 문자열 (예: "UTF-8", "CP949")
 */
inline const char* codepage_name(codepage cp) {
    switch (cp) {
        case codepage::UTF8:   return "UTF-8";
        case codepage::CP437:  return "CP437";
        case codepage::CP850:  return "CP850";
        case codepage::CP932:  return "CP932";
        case codepage::CP936:  return "CP936";
        case codepage::CP949:  return "CP949";
        case codepage::CP950:  return "CP950";
        case codepage::ANSI:   return "ANSI";
        default:               return "UNKNOWN";
    }
}

/**
 * @brief 인코딩 이름 문자열을 정규화합니다.
 *
 * 알파벳과 숫자만 남기고 모두 소문자로 변환하여 비교하기 쉽게 만듭니다.
 * (예: "UTF-8" -> "utf8", "EUC-KR" -> "euckr")
 *
 * @param s 원본 인코딩 이름
 * @return 정규화된 문자열
 */
inline std::string normalize_encoding_name(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

/**
 * @brief 문자열을 codepage 열거값으로 파싱합니다.
 *
 * "UTF-8", "euc-kr", "cp949" 등의 다양한 표현을 인식합니다.
 *
 * @param name 인코딩 이름 문자열
 * @return 대응하는 codepage 값. 알 수 없는 경우 codepage::UNKNOWN 반환.
 */
inline codepage parse_codepage(std::string_view name) {
    const auto key = normalize_encoding_name(name);
    if (key == "utf8"  || key == "utf")                          return codepage::UTF8;
    if (key == "utf16le" || key == "utf16")                      return codepage::UTF16LE;
    if (key == "utf16be")                                        return codepage::UTF16BE;
    if (key == "cp437" || key == "ibm437")                       return codepage::CP437;
    if (key == "cp850")                                          return codepage::CP850;
    if (key == "cp932" || key == "shiftjis" || key == "sjis")   return codepage::CP932;
    if (key == "cp936" || key == "gbk")                         return codepage::CP936;
    if (key == "cp949" || key == "euckr" || key == "windows949") return codepage::CP949;
    if (key == "cp950" || key == "big5")                         return codepage::CP950;
    if (key == "ansi"  || key == "acp" || key == "system")      return codepage::ANSI;
    return codepage::UNKNOWN;
}

#if PLATFORM_WINDOWS
/**
 * @brief (Windows 전용) util::codepage를 Windows API 코드페이지 ID(UINT)로 변환합니다.
 * @param cp 변환할 코드페이지
 * @return Windows 코드페이지 ID (예: 65001, 949)
 */
inline UINT to_windows_codepage(codepage cp) {
    switch (cp) {
        case codepage::UTF8:  return 65001;
        case codepage::CP437: return 437;
        case codepage::CP850: return 850;
        case codepage::CP932: return 932;
        case codepage::CP936: return 936;
        case codepage::CP949: return 949;
        case codepage::CP950: return 950;
        case codepage::ANSI:  return GetACP(); // 시스템 기본 ANSI
        default:              return 65001;    // 기본적으로 UTF-8 반환
    }
}

/**
 * @brief (Windows 전용) Windows API 코드페이지 ID(UINT)를 util::codepage로 변환합니다.
 * @param cp Windows 코드페이지 ID
 * @return 대응하는 util::codepage 값
 */
inline codepage from_windows_codepage(UINT cp) {
    switch (cp) {
        case 65001: return codepage::UTF8;
        case 437:   return codepage::CP437;
        case 850:   return codepage::CP850;
        case 932:   return codepage::CP932;
        case 936:   return codepage::CP936;
        case 949:   return codepage::CP949;
        case 950:   return codepage::CP950;
        default:    return (cp == GetACP()) ? codepage::ANSI : codepage::UNKNOWN;
    }
}
#endif

/**
 * @brief 현재 콘솔 출력 환경의 코드페이지를 조회합니다.
 *
 * Windows에서는 `GetConsoleOutputCP()`를 사용하고,
 * Linux에서는 `setlocale` 정보를 기반으로 추정합니다.
 *
 * @return 감지된 콘솔 코드페이지
 */
inline codepage get_console_codepage() {
#if PLATFORM_WINDOWS
    return from_windows_codepage(GetConsoleOutputCP());
#else
    const char* loc = std::setlocale(LC_CTYPE, nullptr);
    if (!loc) return codepage::UNKNOWN;
    std::string s(loc);
    if (s.find("UTF-8")   != std::string::npos ||
        s.find("utf8")    != std::string::npos)  return codepage::UTF8;
    if (s.find("EUC-KR")  != std::string::npos ||
        s.find("eucKR")   != std::string::npos)  return codepage::CP949;
    if (s.find("SJIS")    != std::string::npos ||
        s.find("Shift_JIS")!=std::string::npos)  return codepage::CP932;
    if (s.find("GBK")     != std::string::npos)  return codepage::CP936;
    if (s.find("Big5")    != std::string::npos)  return codepage::CP950;
    return codepage::ANSI;
#endif
}

// =========================================================
// 2. 플랫폼별 저수준 변환 (내부 구현)
// =========================================================

#if !PLATFORM_WINDOWS

namespace detail {
    /**
     * @brief (Linux 전용) iconv 라이브러리를 사용하여 string을 wstring으로 변환합니다.
     * @param input 입력 바이트 문자열
     * @param from_enc 원본 인코딩 이름 (iconv 형식, 예: "UTF-8", "EUC-KR")
     * @return 변환된 와이드 문자열. 실패 시 빈 문자열 반환.
     */
    inline std::wstring iconv_to_wstring(const std::string& input,
                                         const char* from_enc)
    {
        iconv_t cd = iconv_open("WCHAR_T", from_enc);
        if (cd == (iconv_t)-1) return L"";

        size_t in_bytes  = input.size();
        size_t out_bytes = (input.size() + 1) * sizeof(wchar_t);
        std::wstring out(out_bytes / sizeof(wchar_t), L'\0');

        // iconv는 입력/출력 버퍼 포인터를 수정하므로 복사본 사용
        char* in_buf  = const_cast<char*>(input.data());
        char* out_buf = reinterpret_cast<char*>(out.data());

        if (iconv(cd, &in_buf, &in_bytes, &out_buf, &out_bytes) == (size_t)-1) {
            iconv_close(cd);
            return L""; // 변환 실패
        }
        iconv_close(cd);
        // 남은 공간(변환되지 않은 부분) 제거
        out.resize(out.size() - (out_bytes / sizeof(wchar_t)));
        return out;
    }

    /**
     * @brief (Linux 전용) iconv 라이브러리를 사용하여 wstring을 string으로 변환합니다.
     * @param ws 입력 와이드 문자열
     * @param to_enc 대상 인코딩 이름 (iconv 형식)
     * @return 변환된 바이트 문자열. 실패 시 빈 문자열 반환.
     */
    inline std::string wstring_to_iconv(const std::wstring& ws,
                                        const char* to_enc)
    {
        iconv_t cd = iconv_open(to_enc, "WCHAR_T");
        if (cd == (iconv_t)-1) return "";

        size_t in_bytes  = ws.size() * sizeof(wchar_t);
        size_t out_bytes = in_bytes * 4 + 8; // 충분한 버퍼 할당
        std::string out(out_bytes, '\0');

        char* in_buf  = reinterpret_cast<char*>(const_cast<wchar_t*>(ws.data()));
        char* out_buf = out.data();

        if (iconv(cd, &in_buf, &in_bytes, &out_buf, &out_bytes) == (size_t)-1) {
            iconv_close(cd);
            return "";
        }
        iconv_close(cd);
        out.resize(out.size() - out_bytes);
        return out;
    }
} // namespace detail

#endif // !PLATFORM_WINDOWS

// =========================================================
// 3. 공개 인코딩 변환 API
// =========================================================

/**
 * @brief UTF-8 바이트 문자열을 wstring으로 변환합니다.
 * @param str UTF-8 인코딩된 입력 문자열
 * @return 변환된 와이드 문자열
 */
inline std::wstring utf8_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
#if PLATFORM_WINDOWS
    int n = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(),
                                nullptr, 0);
    std::wstring ws(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(),
                        ws.data(), n);
    return ws;
#else
    return detail::iconv_to_wstring(str, "UTF-8");
#endif
}

/**
 * @brief wstring을 UTF-8 바이트 문자열로 변환합니다.
 * @param ws 입력 와이드 문자열
 * @return UTF-8 인코딩된 문자열
 */
inline std::string wstring_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
#if PLATFORM_WINDOWS
    int n = WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, ws.data(), (int)ws.size(),
                        s.data(), n, nullptr, nullptr);
    return s;
#else
    return detail::wstring_to_iconv(ws, "UTF-8");
#endif
}

/**
 * @brief ANSI(로컬) 바이트 문자열을 wstring으로 변환합니다.
 *
 * Windows에서는 CP_ACP(시스템 기본)를 사용합니다.
 * Linux에서는 현재 로캘 설정을 따릅니다.
 *
 * @param str ANSI 인코딩된 입력 문자열
 * @return 변환된 와이드 문자열
 */
inline std::wstring ansi_to_wstring(const std::string& str) {
    if (str.empty()) return L"";
#if PLATFORM_WINDOWS
    int n = MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(),
                                nullptr, 0);
    std::wstring ws(n, 0);
    MultiByteToWideChar(CP_ACP, 0, str.data(), (int)str.size(),
                        ws.data(), n);
    return ws;
#else
    return detail::iconv_to_wstring(str, ""); // 빈 문자열은 iconv에서 현재 로캘 의미
#endif
}

/**
 * @brief wstring을 ANSI(로컬) 바이트 문자열로 변환합니다.
 * @param ws 입력 와이드 문자열
 * @return ANSI 인코딩된 문자열
 */
inline std::string wstring_to_ansi(const std::wstring& ws) {
    if (ws.empty()) return "";
#if PLATFORM_WINDOWS
    int n = WideCharToMultiByte(CP_ACP, 0, ws.data(), (int)ws.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_ACP, 0, ws.data(), (int)ws.size(),
                        s.data(), n, nullptr, nullptr);
    return s;
#else
    return detail::wstring_to_iconv(ws, "");
#endif
}

/**
 * @brief wstring을 ASCII 범위 내에서 소문자로 변환합니다.
 *
 * 영어 대문자(A-Z)만 소문자로 변환하며, 다른 언어나 심볼은 그대로 유지합니다.
 * 로캘 영향을 받지 않는 안전한 변환입니다.
 *
 * @param s 입력 문자열
 * @return 소문자로 변환된 문자열
 */
inline std::wstring to_lower_ascii(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'A' && c <= L'Z') c += L'a' - L'A';
    }
    return s;
}

/**
 * @brief wstring을 ASCII 범위 내에서 대문자로 변환합니다.
 * @param s 입력 문자열
 * @return 대문자로 변환된 문자열
 */
inline std::wstring to_upper_ascii(std::wstring s) {
    for (auto& c : s) {
        if (c >= L'a' && c <= L'z') c -= L'a' - L'A';
    }
    return s;
}

// =========================================================
// 4. Encoding — 파일/스트림 인코딩 감지 & 변환
// =========================================================

/**
 * @brief 바이트 스트림의 인코딩 형식을 나타내는 열거형
 */
enum class Encoding {
    UNKNOWN,    ///< 알 수 없음
    ASCII,      ///< 순수 ASCII (0x00~0x7F)
    UTF8,       ///< UTF-8 (BOM 없음)
    UTF8_BOM,   ///< UTF-8 with BOM
    UTF16_LE,   ///< UTF-16 Little Endian
    UTF16_BE,   ///< UTF-16 Big Endian
    ANSI        ///< ANSI (로컬 인코딩)
};

/**
 * @brief 문자열이 UTF-8 BOM으로 시작하는지 확인합니다.
 * @param s 검사할 문자열 뷰
 * @return BOM이 있으면 true
 */
inline bool has_utf8_bom(std::string_view s) {
    return s.size() >= 3 &&
           (unsigned char)s[0] == 0xEF &&
           (unsigned char)s[1] == 0xBB &&
           (unsigned char)s[2] == 0xBF;
}

/**
 * @brief 문자열이 UTF-16 LE BOM으로 시작하는지 확인합니다.
 * @param s 검사할 문자열 뷰
 * @return BOM이 있으면 true
 */
inline bool has_utf16le_bom(std::string_view s) {
    return s.size() >= 2 &&
           (unsigned char)s[0] == 0xFF &&
           (unsigned char)s[1] == 0xFE;
}

/**
 * @brief 문자열이 UTF-16 BE BOM으로 시작하는지 확인합니다.
 * @param s 검사할 문자열 뷰
 * @return BOM이 있으면 true
 */
inline bool has_utf16be_bom(std::string_view s) {
    return s.size() >= 2 &&
           (unsigned char)s[0] == 0xFE &&
           (unsigned char)s[1] == 0xFF;
}

/**
 * @brief 바이트 스트림의 인코딩을 자동으로 감지합니다.
 *
 * 감지 순서:
 * 1. BOM(Byte Order Mark) 확인.
 * 2. UTF-8 유효성 검사 (올바른 바이트 시퀀스인지).
 * 3. ASCII 범위 확인.
 * 4. 그 외 ANSI로 간주.
 *
 * @param s 검사할 문자열 뷰
 * @return 감지된 Encoding 타입
 */
inline Encoding detect_encoding(std::string_view s) {
    if (s.empty())              return Encoding::ASCII;
    if (has_utf8_bom(s))        return Encoding::UTF8_BOM;
    if (has_utf16le_bom(s))     return Encoding::UTF16_LE;
    if (has_utf16be_bom(s))     return Encoding::UTF16_BE;

    bool ascii = true;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = s[i];
        if (c <= 0x7F) { ++i; continue; } // ASCII 범위
        ascii = false;

        // UTF-8 시퀀스 길이 추정
        int n = (c & 0xE0) == 0xC0 ? 2 :
                (c & 0xF0) == 0xE0 ? 3 :
                (c & 0xF8) == 0xF0 ? 4 : 0;

        // 유효하지 않은 UTF-8 시퀀스이면 ANSI로 판단
        if (!n || i + n > s.size()) return Encoding::ANSI;
        for (int j = 1; j < n; ++j) {
            if ((s[i + j] & 0xC0) != 0x80) return Encoding::ANSI;
        }
        i += n;
    }
    return ascii ? Encoding::ASCII : Encoding::UTF8;
}

/**
 * @brief 바이트 문자열을 wstring으로 변환합니다. 인코딩 자동 감지를 지원합니다.
 *
 * UTF-16의 경우 BOM을 제거하고 서로게이트 쌍(Surrogate Pair)을 처리합니다.
 *
 * @param str 입력 바이트 문자열
 * @param enc 지정된 인코딩 (기본값: UNKNOWN, 자동 감지)
 * @return 변환된 와이드 문자열
 */
inline std::wstring convert_to_wstring(const std::string& str,
                                       Encoding enc = Encoding::UNKNOWN)
{
    if (enc == Encoding::UNKNOWN) enc = detect_encoding(str);

    if (enc == Encoding::UTF8_BOM) return utf8_to_wstring(str.substr(3));
    if (enc == Encoding::UTF8 || enc == Encoding::ASCII) {
        return utf8_to_wstring(str);
    }
    if (enc == Encoding::ANSI) return ansi_to_wstring(str);

    // UTF-16 LE / BE 바이트열 직접 해석
    if (enc == Encoding::UTF16_LE || enc == Encoding::UTF16_BE) {
        const bool le = (enc == Encoding::UTF16_LE);
        if (str.size() < 2) return L"";

        std::wstring result;
        result.reserve((str.size() - 2) / 2);

        // BOM(2바이트) 건너뛰고 처리
        for (size_t i = 2; i + 1 < str.size(); i += 2) {
            auto lo  = (unsigned char)str[le ? i     : i + 1];
            auto hi  = (unsigned char)str[le ? i + 1 : i    ];
            auto wc  = (uint16_t)(lo | (hi << 8));

            // Windows(wchar_t=2)와 Linux(wchar_t=4) 모두 처리
            if constexpr (sizeof(wchar_t) == 4) {
                // Linux: 서로게이트 쌍을 단일 코드포인트로 결합
                if (wc >= 0xD800 && wc <= 0xDBFF && i + 3 < str.size()) {
                    auto lo2 = (unsigned char)str[le ? i+2 : i+3];
                    auto hi2 = (unsigned char)str[le ? i+3 : i+2];
                    auto wc2 = (uint16_t)(lo2 | (hi2 << 8));
                    if (wc2 >= 0xDC00 && wc2 <= 0xDFFF) {
                        uint32_t cp = ((wc - 0xD800u) << 10) +
                                      (wc2 - 0xDC00u) + 0x10000u;
                        result.push_back(static_cast<wchar_t>(cp));
                        i += 2; // 다음 워드 건너뜀
                        continue;
                    }
                }
            }
            result.push_back(static_cast<wchar_t>(wc));
        }
        return result;
    }
    return L"";
}

/**
 * @brief wstring을 지정된 인코딩의 바이트 문자열로 변환합니다.
 *
 * UTF-16 변환 시 BOM을 자동으로 추가합니다.
 *
 * @param ws 입력 와이드 문자열
 * @param enc 대상 인코딩 (기본값: UTF8)
 * @return 변환된 바이트 문자열
 */
inline std::string convert_from_wstring(const std::wstring& ws,
                                        Encoding enc = Encoding::UTF8)
{
    switch (enc) {
        case Encoding::UTF8_BOM:
            return "\xEF\xBB\xBF" + wstring_to_utf8(ws);
        case Encoding::UTF8:
        case Encoding::ASCII:
            return wstring_to_utf8(ws);
        case Encoding::ANSI:
            return wstring_to_ansi(ws);
        case Encoding::UTF16_LE: {
            std::string out = "\xFF\xFE"; // BOM 추가
            out.reserve(2 + ws.size() * 2);
            for (wchar_t c : ws) {
                out.push_back(static_cast<char>( c        & 0xFF));
                out.push_back(static_cast<char>((c >> 8)  & 0xFF));
            }
            return out;
        }
        case Encoding::UTF16_BE: {
            std::string out = "\xFE\xFF"; // BOM 추가
            out.reserve(2 + ws.size() * 2);
            for (wchar_t c : ws) {
                out.push_back(static_cast<char>((c >> 8)  & 0xFF));
                out.push_back(static_cast<char>( c        & 0xFF));
            }
            return out;
        }
        default:
            return wstring_to_utf8(ws);
    }
}

/**
 * @brief `convert_to_wstring`의 짧은 별칭(Alias) 함수.
 * @param s 입력 문자열
 * @param enc 인코딩 (기본값: 자동 감지)
 * @return 변환된 wstring
 */
inline std::wstring to_wstring(const std::string& s,
                               Encoding enc = Encoding::UNKNOWN) {
    return convert_to_wstring(s, enc);
}

/**
 * @brief `convert_from_wstring`의 짧은 별칭(Alias) 함수.
 * @param ws 입력 wstring
 * @param enc 인코딩 (기본값: UTF8)
 * @return 변환된 string
 */
inline std::string from_wstring(const std::wstring& ws,
                                Encoding enc = Encoding::UTF8) {
    return convert_from_wstring(ws, enc);
}

#if PLATFORM_WINDOWS
/**
 * @brief (Windows 전용) std::u16string을 std::wstring으로 변환.
 */
inline std::wstring    utf16_to_wstring(const std::u16string& u) { return {u.begin(), u.end()}; }

/**
 * @brief (Windows 전용) std::wstring을 std::u16string으로 변환.
 */
inline std::u16string  wstring_to_utf16(const std::wstring&   w) { return {w.begin(), w.end()}; }

/**
 * @brief (Windows 전용) std::u16string을 UTF-8 string으로 변환.
 */
inline std::string     utf16_to_utf8   (const std::u16string& u) { return wstring_to_utf8(utf16_to_wstring(u)); }

/**
 * @brief (Windows 전용) UTF-8 string을 std::u16string으로 변환.
 */
inline std::u16string  utf8_to_utf16   (const std::string&    s) { return wstring_to_utf16(utf8_to_wstring(s)); }
#endif

// =========================================================
// 5. 유니코드 시각 폭 (Visual Width)
// =========================================================

namespace detail {
    /**
     * @brief 유니코드 코드포인트 범위를 나타내는 내부 구조체
     */
    struct URange { uint32_t start, end; };

    // 전각(Full-width) 문자 범위 데이터 (한글, 한자, 이모지 등)
    static constexpr URange FULL_WIDTH_RANGES[] = {
        {0x1100,0x11FF},{0x231A,0x231B},{0x2329,0x232A},{0x23E9,0x23EC},
        {0x23F0,0x23F3},{0x25FD,0x25FE},{0x2614,0x2615},{0x2648,0x2653},
        {0x267F,0x267F},{0x2693,0x2693},{0x26A1,0x26A1},{0x26AA,0x26AB},
        {0x26BD,0x26BE},{0x26C4,0x26C5},{0x26CE,0x26CE},{0x26D4,0x26D4},
        {0x26EA,0x26EA},{0x26F2,0x26F3},{0x26F5,0x26FA},{0x26FD,0x26FD},
        {0x2705,0x2705},{0x270A,0x270B},{0x2728,0x2728},{0x274C,0x274E},
        {0x2753,0x2755},{0x2757,0x2757},{0x2795,0x2797},{0x27B0,0x27BF},
        {0x2B1B,0x2B1C},{0x2B50,0x2B50},{0x2B55,0x2B55},{0x2E80,0xA4CF},
        {0xAC00,0xD7A3},{0xF900,0xFAFF},{0xFE10,0xFE19},{0xFE30,0xFE6F},
        {0xFF00,0xFF60},{0xFFE0,0xFFE6},{0x1F000,0x1F0FF},{0x1F100,0x1F1FF},
        {0x1F200,0x1F2FF},{0x1F300,0x1F64F},{0x1F680,0x1F6FF},{0x1F700,0x1F7FF},
        {0x1F800,0x1F8FF},{0x1F900,0x1F9FF},{0x1FA00,0x1FAFF},{0x20000,0x3FFFD}
    };

    // 결합 문자(Combining) 범위 데이터 — 시각 폭 0
    static constexpr URange COMBINING_RANGES[] = {
        {0x0300,0x036F},{0x0483,0x0489},{0x0591,0x05BD},{0x05BF,0x05BF},
        {0x05C1,0x05C2},{0x05C4,0x05C5},{0x05C7,0x05C7},{0x0610,0x061A},
        {0x064B,0x065F},{0x0670,0x0670},{0x06D6,0x06DC},{0x06DF,0x06E4},
        {0x06E7,0x06E8},{0x06EA,0x06ED},{0x0711,0x0711},{0x0730,0x074A},
        {0x07A6,0x07B0},{0x07EB,0x07F3},{0x0816,0x0819},{0x081B,0x0823},
        {0x0825,0x0827},{0x0829,0x082D},{0x0859,0x085B},{0x08D3,0x0903},
        {0x093A,0x093C},{0x093E,0x094F},{0x0951,0x0957},{0x0962,0x0963},
        {0x0981,0x0983},{0x09BC,0x09BC},{0x09BE,0x09C4},{0x09C7,0x09C8},
        {0x09CB,0x09CD},{0x09D7,0x09D7},{0x0A01,0x0A03},{0x0A3C,0x0A3C},
        {0x0A3E,0x0A42},{0x0A47,0x0A48},{0x0A4B,0x0A4D},{0x0A51,0x0A51},
        {0x0A70,0x0A71},{0x0A75,0x0A75},{0x200B,0x200F},{0x202A,0x202E},
        {0x2060,0x2064},{0x2066,0x206F},{0x20D0,0x20FF},{0xFE00,0xFE0F},
        {0xFE20,0xFE2F}
    };

    /**
     * @brief 주어진 코드포인트가 특정 범위 배열에 속하는지 이진 탐색으로 확인합니다.
     * @tparam N 배열 크기
     * @param ranges 범위 배열
     * @param cp 검사할 코드포인트
     * @return 범위 내에 있으면 true
     */
    template <size_t N>
    inline bool in_ranges(const URange (&ranges)[N], uint32_t cp) {
        auto it = std::lower_bound(
            std::begin(ranges), std::end(ranges), cp,
            [](const URange& r, uint32_t v) { return r.end < v; });
        return it != std::end(ranges) && cp >= it->start;
    }
} // namespace detail

/**
 * @brief 해당 코드포인트가 결합 문자(Combining Character)인지 확인합니다.
 *
 * 결합 문자는 기본 문자와 결합되어 표시되므로 단독 시각 폭을 갖지 않습니다.
 *
 * @param cp 유니코드 코드포인트
 * @return 결합 문자이면 true
 */
inline bool is_combining(uint32_t cp) {
    return detail::in_ranges(detail::COMBINING_RANGES, cp);
}

/**
 * @brief 해당 코드포인트가 전각 문자(Full-width Character)인지 확인합니다.
 *
 * 전각 문자는 터미널 등에서 2칸의 너비를 차지합니다. (예: 한글, 한자, 이모지)
 *
 * @param cp 유니코드 코드포인트
 * @return 전각 문자이면 true
 */
inline bool is_full_width(uint32_t cp) {
    return detail::in_ranges(detail::FULL_WIDTH_RANGES, cp);
}

/**
 * @brief wstring의 시각적 표시 폭을 계산합니다.
 *
 * 터미널이나 고정 폭 환경에서 문자열이 차지하는 칸 수를 반환합니다.
 * - ASCII: 1칸
 * - 한글/한자/이모지 등 전각 문자: 2칸
 * - 결합 문자: 0칸
 *
 * Windows 환경(wchar_t == 2byte)에서는 UTF-16 서로게이트 쌍을 처리하여
 * 올바른 코드포인트를 계산합니다.
 *
 * @param str 입력 와이드 문자열
 * @return 계산된 시각적 폭 (칸 수)
 */
inline int visual_width(const std::wstring& str) {
    int width = 0;
    const size_t len = str.size();

    for (size_t i = 0; i < len; ++i) {
        uint32_t cp = static_cast<uint32_t>(str[i]);

        // Windows(wchar_t=2) UTF-16 서로게이트 쌍 처리
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len) {
                uint32_t lo = static_cast<uint32_t>(str[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = ((cp - 0xD800u) << 10) + (lo - 0xDC00u) + 0x10000u;
                    ++i; // 서로게이트 쌍 건너뜀
                }
            }
        }

        if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) continue; // 제어 문자 제외
        if (is_combining(cp)) continue;                          // 결합 문자 제외
        width += is_full_width(cp) ? 2 : 1;
    }
    return width;
}

// =========================================================
// 6. aligned_text — 터미널 폭 기준 패딩 정렬
// =========================================================

/**
 * @brief 텍스트 정렬 방향을 지정하는 열거형
 */
enum class align { left, center, right, top, middle, bottom };

/**
 * @brief 주어진 너비에 맞춰 텍스트를 정렬하고 공백 패딩을 추가합니다.
 *
 * `visual_width`를 사용하여 실제 터미널 표시 너비를 기준으로 정렬합니다.
 *
 * @param msg 정렬할 메시지
 * @param width 목표 전체 너비
 * @param a 정렬 방향 (left, center, right)
 * @return 패딩이 추가된 문자열
 */
inline std::wstring aligned_text(const std::wstring& msg, int width, align a) {
    int msg_w = visual_width(msg);
    if (msg_w >= width) return msg;

    int left_pad  = 0;
    int right_pad = 0;

    switch (a) {
        case align::left:
            right_pad = width - msg_w;
            break;
        case align::center:
            left_pad  = (width - msg_w) / 2;
            right_pad = width - msg_w - left_pad;
            break;
        case align::right:
            left_pad  = width - msg_w;
            break;
        default:
            break;
    }

    std::wstring result;
    result.reserve(msg.size() + left_pad + right_pad);
    result.append(left_pad, L' ');
    result += msg;
    result.append(right_pad, L' ');
    return result;
}

} // namespace util

#if defined(_MSC_VER)
    #pragma warning(pop)
#elif defined(__GNUC__) || defined(__clang__)
    #pragma GCC diagnostic pop
#endif