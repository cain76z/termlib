#pragma once
/**
 * uni_string.hpp — 유니코드 문자열 유틸리티  (TDD §3)
 *
 * UTF-8 → 그래핌 클러스터 분리, 터미널 출력 넓이 계산, 클리핑
 *
 * 구현부는 모두 클래스 본체 안에 위치한다.
 * 내부 헬퍼는 detail 네임스페이스에 정의한 후 클래스가 참조한다.
 */
#include "platform.hpp"
#include "unicode_width_table.hpp"      // detail::kWidthBlockIndex, kWidthBlockData, kSmpWidthRanges
#include "unicode_grapheme_table.hpp"   // detail::GBProp, kGBBlockIndex, kGBBlockData
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace term {

/// 그래핌 클러스터 하나
struct Grapheme {
    std::string bytes;  ///< UTF-8 원본 바이트
    int         width;  ///< 터미널 출력 열 수 (0=zero-width, 1=narrow, 2=wide)
};

// ═══════════════════════════════════════════════════════════════════════════
//  UniString 내부 구현 헬퍼  (UniString 보다 먼저 정의돼야 inline 참조 가능)
// ═══════════════════════════════════════════════════════════════════════════

namespace uni_detail {

// ── UTF-8 디코딩 ─────────────────────────────────────────────────────────

inline int utf8_seq_len(uint8_t b) noexcept {
    if (b < 0x80)             return 1;
    if ((b & 0xE0) == 0xC0)  return 2;
    if ((b & 0xF0) == 0xE0)  return 3;
    if ((b & 0xF8) == 0xF0)  return 4;
    return 1; // invalid → 1바이트 소비
}

inline char32_t utf8_decode_one(const uint8_t* p, int len) noexcept {
    if (len == 1) return static_cast<char32_t>(*p);
    char32_t cp = *p & (0x7F >> len);
    for (int i = 1; i < len; ++i) {
        if ((p[i] & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    return cp;
}

// ── 그래핌 분류 프로퍼티 ─────────────────────────────────────────────────

inline detail::GBProp gb_prop_bmp(char32_t cp) noexcept {
    uint8_t block = detail::kGBBlockIndex[cp >> 8];
    return static_cast<detail::GBProp>(detail::kGBBlockData[block][cp & 0xFF]);
}

inline detail::GBProp gb_prop(char32_t cp) noexcept {
    if (cp <= 0xFFFF) return gb_prop_bmp(cp);
    for (int i = 0; i < detail::kSmpGBRangesCount; ++i) {
        const auto& r = detail::kSmpGBRanges[i];
        if (cp >= r.start && cp <= r.end) return r.prop;
    }
    if ((cp >= 0x1F000 && cp <= 0x1FAFF) ||
        (cp >= 0x1FA00 && cp <= 0x1FA6F)) return detail::GBProp::ExtPic;
    return detail::GBProp::Other;
}

// ── UAX #29 상태 기계 ────────────────────────────────────────────────────

enum class GBState : uint8_t {
    Start, CR, RI_Odd, Prepend, L, LV, LVT, ExtPic, ExtPicZWJ, Other
};

struct GBTransition { bool boundary; GBState next; };

inline GBTransition gb_transition(GBState state, detail::GBProp prop) noexcept {
    using P = detail::GBProp;
    using S = GBState;

    if (state == S::CR) {
        if (prop == P::LF) return {false, S::Other};
        return {true, prop == P::CR ? S::CR : S::Other};
    }
    if (prop == P::CR)      return {true, S::CR};
    if (prop == P::LF)      return {true, S::Other};
    if (prop == P::Control) return {true, S::Other};

    // GB6: L × (L|V|LV|LVT)
    if (state == S::L) {
        if (prop == P::L || prop == P::V || prop == P::LV || prop == P::LVT)
            return {false, prop == P::L   ? S::L   :
                           prop == P::LV  ? S::LV  :
                           prop == P::LVT ? S::LVT : S::LV};
    }
    // GB7: LV|V × (V|T)   [BUG-04: LVT 는 T 만 허용, V 는 경계]
    if (state == S::LV) {
        if (prop == P::V) return {false, S::LV};
        if (prop == P::T) return {false, S::LVT};
    }
    // GB8: LVT|T × T
    if (state == S::LVT) {
        if (prop == P::T) return {false, S::LVT};
    }

    if (prop == P::Extend || prop == P::ZWJ) {
        GBState ns = (state == S::ExtPic && prop == P::ZWJ)
                     ? S::ExtPicZWJ : state;
        return {false, ns};
    }
    if (prop == P::SpacingMark) return {false, state};
    if (state == S::Prepend)    return {false,
        prop == P::L      ? S::L      :
        prop == P::LV     ? S::LV     :
        prop == P::LVT    ? S::LVT    :
        prop == P::ExtPic ? S::ExtPic : S::Other};

    if (state == S::ExtPicZWJ && prop == P::ExtPic)
        return {false, S::ExtPic};

    if (prop == P::RI) {
        if (state == S::RI_Odd) return {false, S::Other};
        return {true, S::RI_Odd};
    }

    GBState ns = prop == P::L       ? S::L       :
                 prop == P::LV      ? S::LV      :
                 prop == P::LVT     ? S::LVT     :
                 prop == P::ExtPic  ? S::ExtPic  :
                 prop == P::Prepend ? S::Prepend : S::Other;
    return {true, ns};
}

// ── 그래핌 클러스터 너비 결정 ────────────────────────────────────────────

// UniString::cp_width 전방 선언 (UniString 이 아래에 정의됨)
// cluster_width 는 UniString 정의 후 참조하므로 inline 람다 또는 전방 참조 필요
// → UniString 에 friend로 정의하거나, 별도 헬퍼로 split과 같은 위치에 배치
// 여기서는 cp_width_impl 로 중복 없이 직접 구현한다.

inline int cp_width_impl(char32_t cp) noexcept {
    if (TERM_LIKELY(cp < 0x80))
        return (cp < 0x20 || cp == 0x7F) ? 0 : 1;
    if (cp >= 0x80 && cp <= 0x9F) return 0;
    if ((cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0xE0100 && cp <= 0xE01EF)) return 0;
    if (cp == 0x200B || cp == 0x200C || cp == 0x200D ||
        cp == 0xFEFF || cp == 0x00AD) return 0;
    if ((cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F)) return 0;
    if (cp <= 0xFFFF) {
        uint8_t block = detail::kWidthBlockIndex[cp >> 8];
        return detail::kWidthBlockData[block][cp & 0xFF];
    }
    int lo = 0, hi = detail::kSmpWidthRangesCount - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        const auto& r = detail::kSmpWidthRanges[mid];
        if (cp < r.start)    hi = mid - 1;
        else if (cp > r.end) lo = mid + 1;
        else                 return r.width;
    }
    return 1;
}

inline int cluster_width(std::string_view bytes) noexcept {
    if (bytes.empty()) return 0;
    const auto* p   = reinterpret_cast<const uint8_t*>(bytes.data());
    const auto* end = p + bytes.size();

    // VS-16(U+FE0F) / VS-15(U+FE0E) 확인
    {
        const uint8_t* q = p;
        char32_t last_cp = 0;
        while (q < end) {
            int len = utf8_seq_len(*q);
            if (q + len <= end) last_cp = utf8_decode_one(q, len);
            q += (q + len <= end) ? len : 1;
        }
        if (last_cp == 0xFE0F) return 2;
        if (last_cp == 0xFE0E) {
            int len  = utf8_seq_len(*p);
            char32_t first = utf8_decode_one(p, len);
            return cp_width_impl(first) > 0 ? 1 : 0;
        }
    }

    int len   = utf8_seq_len(*p);
    char32_t first = utf8_decode_one(p, len);
    int w = cp_width_impl(first);

    // 스택 배열로 코드포인트 수집 [OPT-03]
    char32_t cps[32]; int n_cps = 0;
    for (const uint8_t* q = p; q < end && n_cps < 32; ) {
        int l = utf8_seq_len(*q);
        cps[n_cps++] = (q + l <= end) ? utf8_decode_one(q, l) : 0xFFFD;
        q += (q + l <= end) ? l : 1;
    }

    // Regional Indicator 쌍 (국기)
    if (n_cps >= 2 &&
        cps[0] >= 0x1F1E0 && cps[0] <= 0x1F1FF &&
        cps[1] >= 0x1F1E0 && cps[1] <= 0x1F1FF) return 2;

    // ZWJ 시퀀스
    for (int i = 0; i < n_cps; ++i)
        if (cps[i] == 0x200D) { w = 2; break; }

    // 스킨톤 수정자
    if (n_cps >= 2 && cps[1] >= 0x1F3FB && cps[1] <= 0x1F3FF) return 2;

    return w;
}

} // namespace uni_detail

// ═══════════════════════════════════════════════════════════════════════════
//  UniString  (TDD §3)
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════

class UniString {
public:
    UniString() = delete;

    // ── 코드포인트 너비 ──────────────────────────────────────────────────
    [[nodiscard]] static int cp_width(char32_t cp) noexcept {
        return uni_detail::cp_width_impl(cp);
    }

    // ── 그래핌 클러스터 분리 (UAX #29) ──────────────────────────────────
    [[nodiscard]] static std::vector<Grapheme> split(std::string_view utf8) {
        std::vector<Grapheme> result;
        if (utf8.empty()) return result;

        const auto* p   = reinterpret_cast<const uint8_t*>(utf8.data());
        const auto* end = p + utf8.size();

        uni_detail::GBState state = uni_detail::GBState::Start;
        Grapheme cur;

        while (p < end) {
            int      len = uni_detail::utf8_seq_len(*p);
            if (p + len > end) len = static_cast<int>(end - p);
            char32_t cp  = uni_detail::utf8_decode_one(p, len);
            if (cp == 0xFFFD && len > 1) len = 1;

            detail::GBProp     prop = uni_detail::gb_prop(cp);
            std::string_view   seq(reinterpret_cast<const char*>(p), len);

            if (cur.bytes.empty()) {
                cur.bytes.append(seq.data(), seq.size());
                state = uni_detail::gb_transition(uni_detail::GBState::Start, prop).next;
            } else {
                auto [boundary, next] = uni_detail::gb_transition(state, prop);
                if (boundary) {
                    cur.width = uni_detail::cluster_width(cur.bytes);
                    result.push_back(std::move(cur));
                    cur = Grapheme{};
                    cur.bytes.append(seq.data(), seq.size());
                    state = next;
                } else {
                    cur.bytes.append(seq.data(), seq.size());
                    state = next;
                }
            }
            p += len;
        }

        if (!cur.bytes.empty()) {
            cur.width = uni_detail::cluster_width(cur.bytes);
            result.push_back(std::move(cur));
        }
        return result;
    }

    // ── 터미널 출력 열 수 ────────────────────────────────────────────────
    [[nodiscard]] static int display_width(std::string_view utf8) {
        if (utf8.empty()) return 0;
        // 빠른 경로: 순수 ASCII
        bool all_ascii = true;
        for (uint8_t c : utf8)
            if (c >= 0x80) { all_ascii = false; break; }
        if (all_ascii) {
            int w = 0;
            for (uint8_t c : utf8)
                w += (c >= 0x20 && c != 0x7F) ? 1 : 0;
            return w;
        }
        int total = 0;
        for (const auto& g : split(utf8)) total += g.width;
        return total;
    }

    // ── 클리핑 ───────────────────────────────────────────────────────────
    [[nodiscard]] static std::string clip(std::string_view utf8,
                                          int max_cols,
                                          std::string_view clip_char = "") {
        if (max_cols <= 0) return {};
        int clip_w = clip_char.empty() ? 0 : display_width(clip_char);
        int budget = max_cols - clip_w;

        auto clusters = split(utf8);
        int  used     = 0;
        std::string result;
        result.reserve(utf8.size());

        bool clipped = false;
        for (const auto& g : clusters) {
            if (used + g.width > budget) { clipped = true; break; }
            result += g.bytes;
            used   += g.width;
        }

        if (clipped) {
            result += clip_char;
            int final_w = used + clip_w;
            if (final_w < max_cols) result += ' ';
        }
        return result;
    }

    // ── 인코딩 변환 ──────────────────────────────────────────────────────
    [[nodiscard]] static std::u32string to_utf32(std::string_view utf8) {
        std::u32string out;
        out.reserve(utf8.size());
        const auto* p   = reinterpret_cast<const uint8_t*>(utf8.data());
        const auto* end = p + utf8.size();
        while (p < end) {
            int      len = uni_detail::utf8_seq_len(*p);
            if (p + len > end) { out += 0xFFFD; break; }
            char32_t cp  = uni_detail::utf8_decode_one(p, len);
            out += cp;
            p   += len;
        }
        return out;
    }

    [[nodiscard]] static std::string to_utf8(std::u32string_view utf32) {
        std::string out;
        out.reserve(utf32.size() * 3);
        for (char32_t cp : utf32) out += cp_to_utf8(cp);
        return out;
    }

    [[nodiscard]] static std::string cp_to_utf8(char32_t cp) {
        char buf[5] = {};
        if (cp < 0x80) {
            buf[0] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            buf[0] = static_cast<char>(0xC0 | (cp >> 6));
            buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            buf[0] = static_cast<char>(0xE0 | (cp >> 12));
            buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            buf[0] = static_cast<char>(0xF0 | (cp >> 18));
            buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            buf[2] = static_cast<char>(0x80 | ((cp >> 6)  & 0x3F));
            buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
        }
        return buf;
    }
};

} // namespace term
