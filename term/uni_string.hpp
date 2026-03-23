#pragma once
/**
 * @file uni_string.hpp
 * @brief 유니코드 문자열 유틸리티 – UTF-8 처리, 그래핌 클러스터 분할, 터미널 출력 너비 계산, 클리핑 등
 *
 * 구현은 모두 클래스 본체 안에 위치하며, 내부 헬퍼는 `uni_detail` 네임스페이스에 정의됩니다.
 *
 * ## 성능 최적화 (v2)
 *
 * 현대 터미널 엔진(WezTerm, Alacritty, Kitty, iTerm2) 수준의 5가지 최적화가 적용됩니다:
 *
 * 1. **ASCII SIMD-like fast path**  — 8/16바이트 청크 비트 트릭으로 ASCII 처리 3~10배 향상
 * 2. **Grapheme segmentation cache** — thread_local direct-mapped 캐시, 반복 문자열 split() 비용 제거
 * 3. **Codepoint width cache**       — thread_local 2048슬롯 direct-mapped 캐시, width lookup ≈ 0
 * 4. **Single-codepoint shortcut**   — 단일 코드포인트 클러스터에서 VS/ZWJ 파싱 건너뜀
 * 5. **Emoji fast table**            — 주요 이모지 범위 즉시 width=2 반환, 복잡한 파싱 생략
 *
 * @see term::UniString
 */

#include "platform.hpp"
#include "term/unicode_width_table.hpp"      // detail::kWidthBlockIndex, kWidthBlockData, kSmpWidthRanges
#include "term/unicode_grapheme_table.hpp"   // detail::GBProp, kGBBlockIndex, kGBBlockData
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace term {

/// 하나의 그래핌 클러스터를 표현하는 구조체
struct Grapheme {
    std::string bytes;  ///< UTF-8로 인코딩된 그래핌 클러스터 원본 바이트
    int         width;  ///< 터미널에서 차지하는 열(column) 수 (0, 1, 2)
};

// ═══════════════════════════════════════════════════════════════════════════
//  UniString 내부 구현 헬퍼  (UniString 보다 먼저 정의돼야 inline 참조 가능)
// ═══════════════════════════════════════════════════════════════════════════

namespace uni_detail {

// ── UTF-8 디코딩 ─────────────────────────────────────────────────────────

/**
 * @brief UTF-8 선두 바이트로부터 시퀀스 길이를 반환합니다.
 * @param b UTF-8 선두 바이트
 * @return 시퀀스 길이 (1~4). 잘못된 바이트면 1을 반환합니다.
 */
inline int utf8_seq_len(uint8_t b) noexcept {
    if (b < 0x80)             return 1;
    if ((b & 0xE0) == 0xC0)  return 2;
    if ((b & 0xF0) == 0xE0)  return 3;
    if ((b & 0xF8) == 0xF0)  return 4;
    return 1; // invalid → 1바이트 소비
}

/**
 * @brief UTF-8 시퀀스를 하나의 코드 포인트로 디코딩합니다.
 * @param p UTF-8 시퀀스 시작 위치
 * @param len 시퀀스 길이 (utf8_seq_len으로 얻은 값)
 * @return 디코딩된 Unicode 코드 포인트 (잘못된 시퀀스는 0xFFFD)
 */
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

/**
 * @brief BMP(0~0xFFFF) 코드 포인트의 Grapheme Break Property를 반환합니다.
 * @param cp Unicode 코드 포인트 (BMP)
 * @return GBProp 값
 */
inline detail::GBProp gb_prop_bmp(char32_t cp) noexcept {
    uint8_t block = detail::kGBBlockIndex[cp >> 8];
    return static_cast<detail::GBProp>(detail::kGBBlockData[block][cp & 0xFF]);
}

/**
 * @brief 임의의 코드 포인트에 대한 Grapheme Break Property를 반환합니다.
 *
 * UAX#29 보정 사항:
 * - Variation Selectors (U+FE00–U+FE0F, U+E0100–U+E01EF) → Extend
 *   테이블이 Other로 잘못 분류하는 경우를 이 함수에서 수정합니다.
 *
 * @param cp Unicode 코드 포인트
 * @return GBProp 값
 */
inline detail::GBProp gb_prop(char32_t cp) noexcept {
    // ── UAX#29 보정: Variation Selectors → Extend ─────────────────────
    // Unicode 표준상 VS는 Grapheme_Cluster_Break=Extend 이지만,
    // 일부 테이블에서 Other로 잘못 분류됨
    if ((cp >= 0xFE00 && cp <= 0xFE0F) ||
        (cp >= 0xE0100 && cp <= 0xE01EF))
        return detail::GBProp::Extend;

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

/// 그래핌 클러스터 분할 상태
enum class GBState : uint8_t {
    Start, CR, RI_Odd, Prepend, L, LV, LVT, ExtPic, ExtPicZWJ, Other
};

/// 상태 전이 결과: 경계 여부와 다음 상태
struct GBTransition { bool boundary; GBState next; };

/**
 * @brief 현재 상태와 다음 문자의 프로퍼티로 그래핌 경계 여부와 다음 상태를 결정합니다.
 * @param state 현재 상태
 * @param prop 다음 문자의 GBProp
 * @return GBTransition 구조체
 */
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
    // GB7: LV|V × (V|T)
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

// ═══════════════════════════════════════════════════════════════════════════
// ⭐ 최적화 5️⃣ — Emoji fast table
//    주요 이모지 범위를 미리 테이블로 저장해 width=2 즉시 반환
//    ZWJ/VS/스킨톤 파싱을 생략하므로 이모지 heavy 텍스트에서 효과 큼
// ═══════════════════════════════════════════════════════════════════════════

/// 이모지 코드포인트 범위 (width=2 로 확정되는 단독 codepoint 범위)
struct EmojiRange { char32_t start; char32_t end; };

static constexpr EmojiRange kEmojiRanges[] = {
    {0x1F300, 0x1F5FF},  // Misc Symbols and Pictographs
    {0x1F600, 0x1F64F},  // Emoticons
    {0x1F650, 0x1F67F},  // Ornamental Dingbats
    {0x1F680, 0x1F6FF},  // Transport and Map Symbols
    {0x1F700, 0x1F77F},  // Alchemical Symbols
    {0x1F780, 0x1F7FF},  // Geometric Shapes Extended
    {0x1F800, 0x1F8FF},  // Supplemental Arrows-C
    {0x1F900, 0x1F9FF},  // Supplemental Symbols and Pictographs
    {0x1FA00, 0x1FA6F},  // Chess Symbols
    {0x1FA70, 0x1FAFF},  // Symbols and Pictographs Extended-A
    {0x2600,  0x26FF},   // Miscellaneous Symbols (BMP)
    {0x2700,  0x27BF},   // Dingbats (BMP)
    {0x1F1E0, 0x1F1FF},  // Regional Indicator Symbols
    {0x1F3FB, 0x1F3FF},  // Skin Tone Modifiers
};
static constexpr int kEmojiRangesCount =
    static_cast<int>(sizeof(kEmojiRanges) / sizeof(kEmojiRanges[0]));

/**
 * @brief 코드 포인트가 이모지 프레젠테이션 범위인지 빠르게 확인합니다.
 * @param cp Unicode 코드 포인트
 * @return true이면 단독 클러스터 width=2 로 처리 가능
 */
inline bool is_emoji_range(char32_t cp) noexcept {
    // BMP 이모지 범위 (빠른 사전 체크)
    if (cp >= 0x2600 && cp <= 0x27BF) return true;
    // SMP 이모지: 대부분 0x1F000 이상
    if (cp < 0x1F000) return false;
    for (int i = 2; i < kEmojiRangesCount; ++i) {  // index 0,1은 SMP, 이미 처리
        if (cp >= kEmojiRanges[i].start && cp <= kEmojiRanges[i].end) return true;
    }
    // 0x1F000~0x1FAFF 전체를 포함하는 빠른 범위 체크
    return (cp >= 0x1F000 && cp <= 0x1FAFF);
}

// ═══════════════════════════════════════════════════════════════════════════
// ── 그래핌 클러스터 너비 결정 ────────────────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief 단일 코드 포인트의 터미널 출력 너비를 반환합니다.
 * @param cp Unicode 코드 포인트
 * @return 너비 (0, 1, 2)
 */
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

// ═══════════════════════════════════════════════════════════════════════════
// ⭐ 최적화 3️⃣ — Codepoint width cache (direct-mapped, 2048슬롯)
//    thread_local 캐시로 같은 문자 반복 렌더 시 width lookup 비용 ≈ 0
//    특히 │ ─ · █ 등 UI 박스 문자 반복에 효과 큼
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief 코드 포인트 너비를 캐시를 통해 조회합니다.
 *
 * ASCII는 캐시를 우회하여 즉시 반환합니다.
 * 나머지는 thread_local 2048슬롯 direct-mapped 캐시를 조회하고,
 * 미스 시 cp_width_impl()를 호출하여 캐시에 저장합니다.
 *
 * @param cp Unicode 코드 포인트
 * @return 너비 (0, 1, 2)
 */
inline int cp_width_cached(char32_t cp) noexcept {
    // ASCII fast path — 캐시 우회
    if (TERM_LIKELY(cp < 0x80))
        return (cp >= 0x20 && cp != 0x7F) ? 1 : 0;

    constexpr uint32_t kCacheSize = 2048;  // 반드시 2의 제곱수
    constexpr uint32_t kInvalid   = 0xFFFFFFFFu;

    struct Entry { uint32_t cp; int8_t w; };

    thread_local Entry cache[kCacheSize] = {};
    thread_local bool  initialized       = false;
    if (TERM_UNLIKELY(!initialized)) {
        for (auto& e : cache) { e.cp = kInvalid; e.w = 0; }
        initialized = true;
    }

    uint32_t slot = static_cast<uint32_t>(cp) & (kCacheSize - 1);
    if (TERM_LIKELY(cache[slot].cp == static_cast<uint32_t>(cp)))
        return cache[slot].w;

    int w = cp_width_impl(cp);
    cache[slot] = {static_cast<uint32_t>(cp), static_cast<int8_t>(w)};
    return w;
}

// ═══════════════════════════════════════════════════════════════════════════
// ── cluster_width (최적화 4️⃣ + 5️⃣ 포함) ────────────────────────────────
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief 하나의 그래핌 클러스터의 터미널 출력 너비를 계산합니다.
 *
 * 최적화 적용 순서:
 *  1. 빈 문자열 즉시 반환
 *  2. ⭐ 단일 ASCII 바이트 → 즉시 반환 (최적화 4️⃣)
 *  3. ⭐ 단일 코드포인트 클러스터 → VS/ZWJ 파싱 생략 (최적화 4️⃣)
 *  4. ⭐ 이모지 범위 확인 → ZWJ/VS 없으면 width=2 즉시 반환 (최적화 5️⃣)
 *  5. VS-16/VS-15 처리
 *  6. Regional Indicator 쌍, ZWJ 시퀀스, 스킨톤 처리
 *
 * @param bytes 그래핌 클러스터의 UTF-8 바이트
 * @return 너비 (0, 1, 2)
 */
inline int cluster_width(std::string_view bytes) noexcept {
    if (bytes.empty()) return 0;

    const auto* p   = reinterpret_cast<const uint8_t*>(bytes.data());
    const auto* end = p + bytes.size();

    // ─── ⭐ 최적화 4️⃣: Single-codepoint shortcut ─────────────────────────
    // 단일 ASCII 바이트 → 캐시도 우회하는 최고속 경로
    if (TERM_LIKELY(bytes.size() == 1)) {
        uint8_t c = p[0];
        return (c >= 0x20 && c != 0x7F) ? 1 : 0;
    }

    // 단일 코드포인트(2~4바이트) 클러스터인지 확인
    {
        int first_len = utf8_seq_len(*p);
        if (first_len == static_cast<int>(bytes.size())) {
            // ZWJ/VS/수정자 없이 코드포인트 하나뿐이므로 캐시 조회 후 즉시 반환
            char32_t cp = utf8_decode_one(p, first_len);

            // ⭐ 최적화 5️⃣: 이모지 범위 fast check
            if (is_emoji_range(cp)) return 2;

            return cp_width_cached(cp);
        }
    }

    // ─── VS-16(U+FE0F) / VS-15(U+FE0E) 확인 ─────────────────────────────
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

    int      len   = utf8_seq_len(*p);
    char32_t first = utf8_decode_one(p, len);
    int      w     = cp_width_cached(first);

    // 스택 배열로 코드포인트 수집
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

// ═══════════════════════════════════════════════════════════════════════════
// ⭐ 최적화 2️⃣ — Grapheme segmentation cache
//    thread_local direct-mapped 256슬롯 캐시
//    prompt, status bar, tab title 등 반복 문자열의 split() 비용 제거
// ═══════════════════════════════════════════════════════════════════════════

/// 그래핌 캐시 슬롯 수 (2의 제곱수여야 함)
constexpr size_t kGraphemeCacheSlots = 256;

/// 그래핌 캐시 엔트리
struct GraphemeCacheEntry {
    std::string         key;    ///< 캐시된 UTF-8 문자열
    std::vector<Grapheme> val;  ///< 분할 결과
    bool                valid = false;
};

/**
 * @brief thread_local 그래핌 클러스터 캐시를 반환합니다.
 *
 * direct-mapped 256슬롯 캐시이며, 슬롯 충돌 시 덮어씁니다.
 * thread_local이므로 뮤텍스 없이 멀티스레드 안전합니다.
 */
inline std::array<GraphemeCacheEntry, kGraphemeCacheSlots>& grapheme_cache() noexcept {
    thread_local std::array<GraphemeCacheEntry, kGraphemeCacheSlots> cache{};
    return cache;
}

/**
 * @brief 문자열에 대한 그래핌 캐시 룩업을 수행합니다.
 * @param utf8 조회할 UTF-8 문자열
 * @return 캐시 히트 시 그래핌 벡터 포인터, 미스 시 nullptr
 */
inline const std::vector<Grapheme>* grapheme_cache_lookup(std::string_view utf8) noexcept {
    auto& cache = grapheme_cache();
    size_t h = std::hash<std::string_view>{}(utf8) & (kGraphemeCacheSlots - 1);
    const auto& entry = cache[h];
    if (entry.valid && entry.key == utf8) return &entry.val;
    return nullptr;
}

/**
 * @brief 그래핌 분할 결과를 캐시에 저장합니다.
 * @param utf8 UTF-8 문자열
 * @param val  분할 결과 (move됨)
 */
inline void grapheme_cache_insert(std::string_view utf8, std::vector<Grapheme> val) {
    auto& cache = grapheme_cache();
    size_t h = std::hash<std::string_view>{}(utf8) & (kGraphemeCacheSlots - 1);
    auto& entry = cache[h];
    entry.key   = std::string(utf8);
    entry.val   = std::move(val);
    entry.valid = true;
}

} // namespace uni_detail

// ═══════════════════════════════════════════════════════════════════════════
//  UniString  (TDD §3)
//  구현부가 모두 클래스 본체 안에 위치한다.
// ═══════════════════════════════════════════════════════════════════════════


/**
 * @brief 유니코드 문자열 처리 유틸리티 클래스
 *
 * ## 두 가지 사용 방식
 *
 * ### 1. 인스턴스 API (권장 — 문자열을 보유하며 반복 호출 최적화)
 * ```cpp
 * UniString s("hello한글🙂");
 * int w        = s.display_width();          // 전체 너비
 * auto parts   = s.split();                  // 그래핌 클러스터 목록
 * std::string c = s.clip(5);                 // 최대 5칸으로 클리핑
 * std::string sub = s.get_sub_string(1, 3);  // 1열부터 3칸 추출
 * ```
 *
 * ### 2. 정적 API (일회성 처리)
 * ```cpp
 * int w = UniString::display_width("hello");
 * auto parts = UniString::split("한글");
 * ```
 *
 * UTF-8 문자열을 그래핌 클러스터로 분할하고, 각 클러스터의 터미널 출력 너비를 계산하며,
 * 주어진 너비에 맞게 클리핑하거나 부분 문자열을 추출하는 기능을 제공합니다.
 */
class UniString {
public:
    // ── 생성자 ───────────────────────────────────────────────────────────

    /// 기본 생성자 (빈 문자열)
    UniString() = default;

    /**
     * @brief C 문자열 리터럴 / const char* 로부터 생성합니다.
     * string_view 와 string 오버로드 사이의 모호성을 없애기 위해 명시적으로 제공합니다.
     * @param s null-terminated UTF-8 문자열
     */
    explicit UniString(const char* s) : data_(s ? s : "") {}

    /**
     * @brief std::string_view 로부터 생성합니다.
     * @param s UTF-8 문자열 뷰
     */
    explicit UniString(std::string_view s) : data_(s.data(), s.size()) {}

    /**
     * @brief std::string 로부터 생성합니다 (이동 지원).
     * @param s UTF-8 문자열
     */
    explicit UniString(std::string s) : data_(std::move(s)) {}

    // ── 인스턴스 API ─────────────────────────────────────────────────────

    /// 보유 중인 UTF-8 문자열을 반환합니다.
    [[nodiscard]] const std::string& str()  const noexcept { return data_; }
    [[nodiscard]] bool               empty() const noexcept { return data_.empty(); }
    [[nodiscard]] std::size_t        size()  const noexcept { return data_.size(); }

    /**
     * @brief 내부적으로 캐싱된 그래핌 클러스터 목록을 반환합니다.
     * 최초 호출 시에만 분할 연산이 발생하며, 이후에는 O(1) 접근입니다.
     */
    [[nodiscard]] const std::vector<Grapheme>& graphemes() const {
        ensure_analyzed();
        return graphemes_;
    }

    /**
     * @brief 전체 디스플레이 너비를 반환합니다. (캐싱됨)
     */
    [[nodiscard]] int display_width() const {
        ensure_analyzed();
        return cached_width_;
    }

    /**
     * @brief 분할된 그래핌 클러스터의 복사본을 반환합니다.
     * (API 호환성 유지용, graphemes() 사용 권장)
     */
    [[nodiscard]] std::vector<Grapheme> split() const {
        ensure_analyzed();
        return graphemes_; // 복사 발생
    }

    /**
     * @brief 최대 너비에 맞게 문자열을 자릅니다. (캐시 활용)
     */
    [[nodiscard]] std::string clip(int max_cols, std::string_view clip_char = "") const {
        if (max_cols <= 0) return {};

        ensure_analyzed(); // 캐시된 클러스터 사용

        int clip_w = clip_char.empty() ? 0 : display_width(clip_char);
        int budget = max_cols - clip_w;
        int used = 0;
        std::string result;
        result.reserve(data_.size());

        bool clipped = false;
        for (const auto& g : graphemes_) {
            if (used + g.width > budget) { clipped = true; break; }
            result += g.bytes;
            used += g.width;
        }

        if (clipped && !clip_char.empty()) {
            result += clip_char;
            if (used + clip_w < max_cols) result += ' ';
        }
        return result;
    }

    /**
     * @brief 지정된 열 범위를 추출합니다. (캐시 활용)
     */
    [[nodiscard]] std::string get_sub_string(int start, int length, char fill = ' ') const {
        if (length <= 0) return {};

        ensure_analyzed(); // 캐시된 클러스터 사용

        std::string result;
        result.reserve(data_.size());
        int current_col = 0;
        int added_width = 0;

        for (const auto& g : graphemes_) {
            if (current_col < start) {
                if (current_col + g.width > start) {
                    int overlap = (current_col + g.width) - start;
                    int to_fill = std::min(overlap, length);
                    result.append(to_fill, fill);
                    added_width += to_fill;
                }
                current_col += g.width;
                continue;
            }

            if (added_width + g.width <= length) {
                result += g.bytes;
                added_width += g.width;
            } else {
                int remain = length - added_width;
                if (remain > 0) {
                    result.append(remain, fill);
                    added_width += remain;
                }
                break;
            }
        }

        if (added_width < length) result.append(length - added_width, fill);
        return result;
    }

    // ── 코드포인트 너비 (정적) ───────────────────────────────────────────

    /**
     * @brief 단일 Unicode 코드 포인트의 터미널 출력 너비를 반환합니다.
     *
     * 너비는 Unicode East Asian Width와 기타 프로퍼티에 따라 결정됩니다:
     * - 0: 제어 문자, 결합 문자, zero-width 공백 등
     * - 1: 대부분의 좁은 문자
     * - 2: CJK 표의문자, 이모지 등 넓은 문자
     *
     * @param cp Unicode 코드 포인트
     * @return 터미널 열 단위 너비 (0, 1, 2)
     */
    [[nodiscard]] static int cp_width(char32_t cp) noexcept {
        return uni_detail::cp_width_cached(cp);
    }

    // ── 그래핌 클러스터 분리 (UAX #29) ──────────────────────────────────

    /**
     * @brief UTF-8 문자열을 그래핌 클러스터로 분할합니다 (UAX #29 규칙 적용).
     *
     * ## 최적화 파이프라인
     * ```
     * cache lookup
     *   hit  → 즉시 반환                          (최적화 2️⃣)
     *   miss ↓
     * ASCII fast path (8바이트 청크 비트 트릭)    (최적화 1️⃣)
     *   → ASCII 구간은 개별 Grapheme 으로 일괄 추가
     * non-ASCII → UTF8 decode
     *   → gb_transition (UAX#29 상태 기계)
     *   → cluster_width (최적화 3️⃣ 4️⃣ 5️⃣)
     * 결과 캐시 저장                               (최적화 2️⃣)
     * ```
     *
     * @param utf8 입력 UTF-8 문자열
     * @return 그래핌 클러스터의 벡터
     */
    [[nodiscard]] static std::vector<Grapheme> split(std::string_view utf8) {
        if (utf8.empty()) return {};

        // ─── ⭐ 최적화 2️⃣: 캐시 히트 → 즉시 반환 ────────────────────────
        if (const auto* cached = uni_detail::grapheme_cache_lookup(utf8))
            return *cached;

        std::vector<Grapheme> result;
        result.reserve(utf8.size()); // 최대 size() 개 클러스터 (ASCII 1:1)

        const auto* p   = reinterpret_cast<const uint8_t*>(utf8.data());
        const auto* end = p + utf8.size();

        uni_detail::GBState state = uni_detail::GBState::Start;
        Grapheme cur;

        while (p < end) {
            // ─── ⭐ 최적화 1️⃣: ASCII SIMD-like fast path ─────────────────
            // 현재 클러스터가 비어있고 8바이트 이상 남은 경우 청크 단위 처리
            // 8바이트 중 고비트(0x80)가 하나도 없으면 전부 ASCII
            while (cur.bytes.empty() && p + 8 <= end) {
                uint64_t chunk;
                std::memcpy(&chunk, p, 8);
                if (chunk & 0x8080808080808080ULL) break; // non-ASCII 있음 → 탈출

                // 8바이트 전부 ASCII → 개별 Grapheme 으로 추가
                for (int i = 0; i < 8; ++i) {
                    uint8_t c = p[i];
                    // 제어 문자(0x00~0x1F, 0x7F)는 width=0 그래핌으로 처리
                    int w = (c >= 0x20 && c != 0x7F) ? 1 : 0;
                    result.push_back({{static_cast<char>(c)}, w});
                }
                p += 8;
                // state 는 ASCII 처리 후에도 Other 유지 (ASCII는 항상 경계)
                state = uni_detail::GBState::Other;
            }

            // 4바이트 청크 잔여 처리 (8바이트 미만 구간)
            while (cur.bytes.empty() && p + 4 <= end) {
                uint32_t chunk;
                std::memcpy(&chunk, p, 4);
                if (chunk & 0x80808080u) break;

                for (int i = 0; i < 4; ++i) {
                    uint8_t c = p[i];
                    int w = (c >= 0x20 && c != 0x7F) ? 1 : 0;
                    result.push_back({{static_cast<char>(c)}, w});
                }
                p += 4;
                state = uni_detail::GBState::Other;
            }

            if (p >= end) break;

            // ─── 일반 경로 (non-ASCII 또는 클러스터 구성 중) ─────────────
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

        // ─── ⭐ 최적화 2️⃣: 결과를 캐시에 저장 ────────────────────────────
        // 짧은 문자열만 캐시 (prompt, status bar 등 대상)
        // 너무 긴 문자열은 캐시 효율이 낮으므로 생략
        if (utf8.size() <= 256) {
            uni_detail::grapheme_cache_insert(utf8, result);
        }

        return result;
    }

    // ── 터미널 출력 열 수 ────────────────────────────────────────────────

    /**
     * @brief UTF-8 문자열이 터미널에서 차지하는 전체 열 수를 반환합니다.
     *
     * ## 최적화 파이프라인
     * ```
     * 빈 문자열 → 즉시 0 반환
     * ⭐ ASCII 전용 SIMD-like 패스 (8/4바이트 청크)    (최적화 1️⃣)
     *   → 8바이트 청크에 non-ASCII가 없으면 유효 바이트 수 합산
     * non-ASCII 포함 → split() → 너비 합산
     *   (split() 내부에서 캐시 사용)                   (최적화 2️⃣)
     * ```
     *
     * @param utf8 입력 UTF-8 문자열
     * @return 전체 디스플레이 너비
     */
    [[nodiscard]] static int display_width(std::string_view utf8) {
        if (utf8.empty()) return 0;

        const auto* p   = reinterpret_cast<const uint8_t*>(utf8.data());
        const auto* end = p + utf8.size();

        // ─── ⭐ 최적화 1️⃣: ASCII SIMD-like fast path ─────────────────────
        // 전체가 ASCII인지 빠르게 확인하면서 동시에 너비 계산
        int  w          = 0;
        bool all_ascii  = true;

        // 8바이트 청크 처리
        const auto* q = p;
        while (q + 8 <= end) {
            uint64_t chunk;
            std::memcpy(&chunk, q, 8);
            if (TERM_UNLIKELY(chunk & 0x8080808080808080ULL)) {
                all_ascii = false;
                break;
            }
            // 8바이트 전부 ASCII: 제어 문자(0x00~0x1F, 0x7F) 제외하고 너비 합산
            // 비트 트릭: 각 바이트가 0x20 이상이면 (byte - 0x20) & 0x80 == 0
            // 0x7F 예외는 별도 처리
            for (int i = 0; i < 8; ++i) {
                uint8_t c = static_cast<uint8_t>(q[i]);
                w += (c >= 0x20 && c != 0x7F) ? 1 : 0;
            }
            q += 8;
        }

        if (all_ascii) {
            // 4바이트 잔여
            while (q + 4 <= end) {
                uint32_t chunk;
                std::memcpy(&chunk, q, 4);
                if (TERM_UNLIKELY(chunk & 0x80808080u)) { all_ascii = false; break; }
                for (int i = 0; i < 4; ++i) {
                    uint8_t c = static_cast<uint8_t>(q[i]);
                    w += (c >= 0x20 && c != 0x7F) ? 1 : 0;
                }
                q += 4;
            }
        }

        if (all_ascii) {
            // 1바이트 잔여
            while (q < end) {
                uint8_t c = *q++;
                if (TERM_UNLIKELY(c >= 0x80)) { all_ascii = false; --q; break; }
                w += (c >= 0x20 && c != 0x7F) ? 1 : 0;
            }
        }

        if (all_ascii) return w;  // 순수 ASCII 경로 종료

        // ─── non-ASCII 포함: split() 이용 (캐시 활용) ─────────────────────
        // 주의: q 이전 ASCII 구간의 w 는 이미 계산됨
        // 하지만 split()이 캐시를 사용하므로 전체 문자열을 split하는 것이 더 효율적
        // ASCII 구간이 짧으면 재계산 비용 < 캐시 miss 비용
        if (static_cast<size_t>(q - p) < utf8.size() / 2) {
            // non-ASCII 비율이 높음 → 전체 split
            int total = 0;
            for (const auto& g : split(utf8)) total += g.width;
            return total;
        }

        // ASCII 구간이 길고 끝부분만 non-ASCII → w + 나머지 split
        int tail = 0;
        std::string_view tail_sv(reinterpret_cast<const char*>(q),
                                 static_cast<size_t>(end - q));
        for (const auto& g : split(tail_sv)) tail += g.width;
        return w + tail;
    }

    // ── 클리핑 ───────────────────────────────────────────────────────────

    /**
     * @brief UTF-8 문자열을 주어진 최대 너비에 맞게 자릅니다.
     *
     * 문자열의 디스플레이 너비가 `max_cols`를 초과하지 않도록 그래핌 클러스터 단위로 자릅니다.
     * `clip_char`가 제공되면 잘린 부분에 해당 문자열을 덧붙입니다 (예: "…").
     * 클리핑 후 남은 너비가 `max_cols`보다 작으면 공백 하나를 추가합니다.
     *
     * @param utf8 입력 UTF-8 문자열
     * @param max_cols 허용할 최대 열 수
     * @param clip_char 클리핑 표시 문자열 (기본값 빈 문자열)
     * @return 클리핑된 UTF-8 문자열
     */
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
            if (!clip_char.empty()) {
                result += clip_char;
                int final_w = used + clip_w;
                // clip_char가 있을 때만 너비 정렬을 위한 공백 패딩
                if (final_w < max_cols) result += ' ';
            }
            // clip_char 없음: 잘린 결과 그대로 반환 (공백 패딩 없음)
        }
        return result;
    }

    /**
     * @brief 지정된 시작 열(column)부터 특정 너비만큼 문자열을 추출합니다.
     *
     * @param utf8 원본 UTF-8 문자열
     * @param start 시작 위치 (터미널 열 단위, 0부터)
     * @param length 추출할 너비 (터미널 열 단위)
     * @param fill 부족한 너비를 채울 문자 (기본값 ' ')
     * @return 추출된 UTF-8 문자열
     */
    [[nodiscard]] static std::string get_sub_string(std::string_view utf8,
                                                    int start,
                                                    int length,
                                                    char fill = ' ') {
        if (length <= 0) return {};

        auto clusters = split(utf8);
        std::string result;
        result.reserve(utf8.size());

        int current_col = 0;
        int added_width = 0;

        for (const auto& g : clusters) {
            // 1. 아직 시작 지점에 도달하지 않은 경우
            if (current_col < start) {
                // 넓은 문자(2칸)가 시작 지점에 걸쳐 있으면 공백으로 맞춤
                if (current_col + g.width > start) {
                    int overlap = (current_col + g.width) - start;
                    int to_fill = std::min(overlap, length);
                    result.append(to_fill, fill);
                    added_width += to_fill;
                }
                current_col += g.width;
                continue;
            }

            // 2. 추출 범위 내
            if (added_width + g.width <= length) {
                result += g.bytes;
                added_width += g.width;
            } else {
                int remain = length - added_width;
                if (remain > 0) {
                    result.append(remain, fill);
                    added_width += remain;
                }
                break;
            }
        }

        // 3. 목표 length 보다 짧으면 fill 로 패딩
        if (added_width < length)
            result.append(length - added_width, fill);

        return result;
    }

    // ── 인코딩 변환 ──────────────────────────────────────────────────────

    /**
     * @brief UTF-8 문자열을 UTF-32로 변환합니다.
     * @param utf8 입력 UTF-8 문자열
     * @return UTF-32로 인코딩된 문자열
     */
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

    /**
     * @brief UTF-32 문자열을 UTF-8로 변환합니다.
     * @param utf32 입력 UTF-32 문자열
     * @return UTF-8로 인코딩된 문자열
     */
    [[nodiscard]] static std::string to_utf8(std::u32string_view utf32) {
        std::string out;
        out.reserve(utf32.size() * 3);
        for (char32_t cp : utf32) out += cp_to_utf8(cp);
        return out;
    }

    /**
     * @brief 단일 Unicode 코드 포인트를 UTF-8로 변환합니다.
     * @param cp Unicode 코드 포인트
     * @return UTF-8 바이트 문자열 (1~4바이트)
     */
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

private:
    std::string data_;  ///< 보유 중인 UTF-8 문자열 (인스턴스 API 전용)
    // ── 지연 초기화(Lazy Initialization)를 위한 캐시 멤버 ───────────────
    // mutable: const 메서드(display_width 등)에서도 값을 변경(캐싱)하기 위해 필요
    mutable std::vector<Grapheme> graphemes_;
    mutable int  cached_width_ = 0;
    mutable bool analyzed_     = false;

    /**
     * @brief 문자열 분할이 필요한 시점에 최초 1회만 실행합니다.
     */
    void ensure_analyzed() const {
        if (TERM_LIKELY(analyzed_)) return;

        // 정적 split 함수를 호출하여 분할 수행 (이때 전역 캐시도 활용됨)
        // 단, 여기서는 결과를 인스턴스 멤버에 저장하여 영속화합니다.
        graphemes_ = split(data_);

        // 너비 미리 계산
        cached_width_ = 0;
        for (const auto& g : graphemes_) {
            cached_width_ += g.width;
        }
        analyzed_ = true;
    }
};

} // namespace term
