#pragma once
/**
 * util.hpp — 공통 유틸리티  (TDD §11)
 */
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace term::util {

// ─── size2str ────────────────────────────────────────────────────────────
/// 1536 → "1.5 KB",  2097152 → "2.0 MB"
inline std::string size2str(uint64_t bytes, int precision = 1) {
    static constexpr const char* units[] = {"B","KB","MB","GB","TB","PB"};
    double val = static_cast<double>(bytes);
    int    ui  = 0;
    while (val >= 1024.0 && ui < 5) { val /= 1024.0; ++ui; }
    char buf[48];
    if (ui == 0)
        std::snprintf(buf, sizeof(buf), "%llu B",
                      static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buf, sizeof(buf), "%.*f %s", precision, val, units[ui]);
    return buf;
}

// ─── time2str ────────────────────────────────────────────────────────────
/// 초 → "1h 23m 45s" (compact=false) 또는 "01:23:45" (compact=true)
inline std::string time2str(double seconds, bool compact = false) {
    int h = static_cast<int>(seconds) / 3600;
    int m = (static_cast<int>(seconds) % 3600) / 60;
    int s = static_cast<int>(seconds) % 60;
    char buf[32];
    if (compact) {
        if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
        else       std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    } else {
        if (h > 0)      std::snprintf(buf, sizeof(buf), "%dh %dm %ds", h, m, s);
        else if (m > 0) std::snprintf(buf, sizeof(buf), "%dm %ds", m, s);
        else            std::snprintf(buf, sizeof(buf), "%ds", s);
    }
    return buf;
}

// ─── duration2str ────────────────────────────────────────────────────────
/// chrono::nanoseconds → "150ms", "2.5s", "1m 30s"
inline std::string duration2str(std::chrono::nanoseconds ns) {
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(ns).count();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(ns).count();
    auto  s = std::chrono::duration_cast<std::chrono::seconds>(ns).count();
    char buf[32];
    if      (us < 1000) std::snprintf(buf, sizeof(buf), "%lldns",   (long long)ns.count());
    else if (ms <    1) std::snprintf(buf, sizeof(buf), "%.1f\xce\xbcs", us / 1000.0);
    else if ( s <    1) std::snprintf(buf, sizeof(buf), "%.1fms",   ms / 1.0);
    else if ( s <   60) std::snprintf(buf, sizeof(buf), "%.2fs",    s  / 1.0);
    else                return time2str(static_cast<double>(s));
    return buf;
}

// ─── timestamp2str ───────────────────────────────────────────────────────
inline std::string timestamp2str(std::chrono::system_clock::time_point tp,
                                 std::string_view fmt = "%Y-%m-%d %H:%M:%S") {
    std::time_t tt = std::chrono::system_clock::to_time_t(tp);
    char buf[64] = {};
    std::strftime(buf, sizeof(buf), fmt.data(), std::localtime(&tt));
    return buf;
}

} // namespace term::util
