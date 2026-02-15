/*
개선된 ANSI 터미널 컨트롤 라이브러리

사용 예시:
    ansi::buffer().xy(x,y).fg("#808080").bg("black").style("underline").text("test text 1 ").reset();
    ansi::buffer().xy(x,y).fg("#d0d0d0").bg("black").text("test text 2 ").reset();
    ansi::buffer().flush();

    std::cout << ansi::fg("bright_red") << "기본 이름\n" << ansi::reset();
    std::cout << ansi::fg(196)           << "256색\n" << ansi::reset();
    std::cout << ansi::fg(255, 0, 100)   << "True Color RGB\n" << ansi::reset();
    std::cout << ansi::fg("#ff0064")     << "#hex 형식\n" << ansi::reset();
    std::cout << ansi::fg("rgb(64,64,64)") << "rgb 형식\n" << ansi::reset();
    std::cout << ansi::style("bold") << "굵게\n" << ansi::reset();

API:
    xy(x,y)       : 커서 이동
    fg(color)     : 전경색 설정
    bg(color)     : 배경색 설정
    style(style)  : 스타일 설정 (bold, underline 등)
    strip()       : 중복 ANSI 시퀀스 제거
    flush()       : 출력
*/

#ifndef ANSI_HPP
#define ANSI_HPP

#include <string>
#include <optional>
#include <set>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace ansi {

    // 기본 색상 상수 (직접 사용 가능)
    namespace color {
        namespace fg {
            constexpr const char* black          = "\033[30m";
            constexpr const char* red            = "\033[31m";
            constexpr const char* green          = "\033[32m";
            constexpr const char* yellow         = "\033[33m";
            constexpr const char* blue           = "\033[34m";
            constexpr const char* magenta        = "\033[35m";
            constexpr const char* cyan           = "\033[36m";
            constexpr const char* white          = "\033[37m";
            constexpr const char* bright_black   = "\033[90m";
            constexpr const char* bright_red     = "\033[91m";
            constexpr const char* bright_green   = "\033[92m";
            constexpr const char* bright_yellow  = "\033[93m";
            constexpr const char* bright_blue    = "\033[94m";
            constexpr const char* bright_magenta = "\033[95m";
            constexpr const char* bright_cyan    = "\033[96m";
            constexpr const char* bright_white   = "\033[97m";
        }
        namespace bg {
            constexpr const char* black          = "\033[40m";
            constexpr const char* red            = "\033[41m";
            constexpr const char* green          = "\033[42m";
            constexpr const char* yellow         = "\033[43m";
            constexpr const char* blue           = "\033[44m";
            constexpr const char* magenta        = "\033[45m";
            constexpr const char* cyan           = "\033[46m";
            constexpr const char* white          = "\033[47m";
            constexpr const char* bright_black   = "\033[100m";
            constexpr const char* bright_red     = "\033[101m";
            constexpr const char* bright_green   = "\033[102m";
            constexpr const char* bright_yellow  = "\033[103m";
            constexpr const char* bright_blue    = "\033[104m";
            constexpr const char* bright_magenta = "\033[105m";
            constexpr const char* bright_cyan    = "\033[106m";
            constexpr const char* bright_white   = "\033[107m";
        }
    }

    // 기본 유틸리티 함수
    inline std::string reset() { return "\033[0m"; }
    inline std::string xy(int x, int y) { 
        return "\033[" + std::to_string(y + 1) + ";" + std::to_string(x + 1) + "H"; 
    }
    
    // 추가 커서 제어 함수
    inline std::string cursor_up(int n = 1) { return "\033[" + std::to_string(n) + "A"; }
    inline std::string cursor_down(int n = 1) { return "\033[" + std::to_string(n) + "B"; }
    inline std::string cursor_forward(int n = 1) { return "\033[" + std::to_string(n) + "C"; }
    inline std::string cursor_back(int n = 1) { return "\033[" + std::to_string(n) + "D"; }
    inline std::string clear_screen() { return "\033[2J"; }
    inline std::string clear_line() { return "\033[2K"; }
    inline std::string hide_cursor() { return "\033[?25l"; }
    inline std::string show_cursor() { return "\033[?25h"; }

    // 스타일 함수들 (네임스페이스 레벨)
    inline std::string style(const std::string& name_raw) {
        std::string name = name_raw;
        std::transform(name.begin(), name.end(), name.begin(), 
                      [](unsigned char c) { return std::tolower(c); });
        
        static const std::unordered_map<std::string, int> styles = {
            {"bold", 1}, {"faint", 2}, {"italic", 3}, {"underline", 4},
            {"blink", 5}, {"reverse", 7}, {"strike", 9}
        };
        
        auto it = styles.find(name);
        if (it != styles.end()) {
            return "\033[" + std::to_string(it->second) + "m";
        }
        return "";
    }

    // 개별 스타일 상수 (직접 사용 가능)
    namespace style_code {
        constexpr const char* bold      = "\033[1m";
        constexpr const char* faint     = "\033[2m";
        constexpr const char* italic    = "\033[3m";
        constexpr const char* underline = "\033[4m";
        constexpr const char* blink     = "\033[5m";
        constexpr const char* reverse   = "\033[7m";
        constexpr const char* strike    = "\033[9m";
    }

    // 색상 파싱 함수 (내부 사용)
    inline std::optional<std::string> get_color_code(const std::string& spec_raw, bool is_fg) {
        // trim
        std::string spec = spec_raw;
        spec.erase(0, spec.find_first_not_of(" \t\r\n"));
        spec.erase(spec.find_last_not_of(" \t\r\n") + 1);
        if (spec.empty()) return std::nullopt;

        std::string lower = spec;
        std::transform(lower.begin(), lower.end(), lower.begin(), 
                      [](unsigned char c) { return std::tolower(c); });

        // 기본 색상 이름
        static const std::unordered_map<std::string, int> names = {
            {"black", 0}, {"red", 1}, {"green", 2}, {"yellow", 3},
            {"blue", 4}, {"magenta", 5}, {"cyan", 6}, {"white", 7}
        };

        // bright_ 접두사 처리
        bool bright = false;
        if (lower.size() > 7 && lower.substr(0, 7) == "bright_") {
            bright = true;
            lower = lower.substr(7);
        }

        auto it = names.find(lower);
        if (it != names.end()) {
            int base = is_fg ? 30 : 40;
            int code = base + it->second + (bright ? 60 : 0);
            return std::to_string(code);
        }

        // 256색 (0-255)
        try {
            size_t pos;
            int idx = std::stoi(spec, &pos);
            if (pos == spec.size() && idx >= 0 && idx <= 255) {
                return (is_fg ? "38;5;" : "48;5;") + std::to_string(idx);
            }
        } catch (...) {}

        // #hex 형식
        if (!spec.empty() && spec[0] == '#') {
            std::string hex = spec.substr(1);
            // #RGB -> #RRGGBB 확장
            if (hex.size() == 3) {
                hex = {hex[0],hex[0],hex[1],hex[1],hex[2],hex[2]};
            }
            if (hex.size() == 6) {
                try {
                    int r = std::stoi(hex.substr(0,2), nullptr, 16);
                    int g = std::stoi(hex.substr(2,2), nullptr, 16);
                    int b = std::stoi(hex.substr(4,2), nullptr, 16);
                    return (is_fg ? "38;2;" : "48;2;") + 
                           std::to_string(r) + ";" + 
                           std::to_string(g) + ";" + 
                           std::to_string(b);
                } catch (...) {}
            }
        }

        // rgb(r,g,b) 형식
        if (lower.size() > 5 && lower.substr(0, 4) == "rgb(" && spec.back() == ')') {
            std::string inside = spec.substr(4, spec.size() - 5);
            std::vector<int> vals;
            std::stringstream ss(inside);
            std::string token;
            
            while (std::getline(ss, token, ',')) {
                // trim token
                token.erase(0, token.find_first_not_of(" \t"));
                token.erase(token.find_last_not_of(" \t") + 1);
                if (!token.empty()) {
                    try { 
                        vals.push_back(std::stoi(token)); 
                    } catch (...) {
                        return std::nullopt;
                    }
                }
            }
            
            if (vals.size() == 3) {
                for (int v : vals) {
                    if (v < 0 || v > 255) return std::nullopt;
                }
                return (is_fg ? "38;2;" : "48;2;") + 
                       std::to_string(vals[0]) + ";" + 
                       std::to_string(vals[1]) + ";" + 
                       std::to_string(vals[2]);
            }
        }

        return std::nullopt;
    }

    // 직접 스트리밍용 fg/bg 오버로드
    inline std::string fg(int index) { 
        if (index < 0 || index > 255) return "";
        return "\033[38;5;" + std::to_string(index) + "m"; 
    }
    
    inline std::string fg(int r, int g, int b) { 
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return "";
        return "\033[38;2;" + std::to_string(r) + ";" + 
               std::to_string(g) + ";" + std::to_string(b) + "m"; 
    }
    
    inline std::string fg(const std::string& spec) {
        auto code = get_color_code(spec, true);
        return code ? "\033[" + *code + "m" : "";
    }

    inline std::string bg(int index) { 
        if (index < 0 || index > 255) return "";
        return "\033[48;5;" + std::to_string(index) + "m"; 
    }
    
    inline std::string bg(int r, int g, int b) { 
        if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return "";
        return "\033[48;2;" + std::to_string(r) + ";" + 
               std::to_string(g) + ";" + std::to_string(b) + "m"; 
    }
    
    inline std::string bg(const std::string& spec) {
        auto code = get_color_code(spec, false);
        return code ? "\033[" + *code + "m" : "";
    }

    // 버퍼 시스템
    namespace detail {
        // 스레드 로컬 버퍼 (멀티스레드 안전성)
        inline std::string& buffer() { 
            thread_local std::string b; 
            return b; 
        }
        
        inline std::optional<std::string>& cur_fg() { 
            thread_local std::optional<std::string> c; 
            return c; 
        }
        
        inline std::optional<std::string>& cur_bg() { 
            thread_local std::optional<std::string> c; 
            return c; 
        }
        
        inline std::set<int>& cur_styles() { 
            thread_local std::set<int> s; 
            return s; 
        }
    }

    class buffer_proxy {
    public:
        buffer_proxy() = default;
        buffer_proxy(const buffer_proxy&) = delete;
        buffer_proxy& operator=(const buffer_proxy&) = delete;    
        
        buffer_proxy& xy(int x, int y) {
            detail::buffer() += ansi::xy(x, y);
            return *this;
        }

        buffer_proxy& fg(const std::string& spec) {
            auto code = get_color_code(spec, true);
            if (code && code != detail::cur_fg()) {
                detail::buffer() += "\033[" + *code + "m";
                detail::cur_fg() = code;
            }
            return *this;
        }
        
        buffer_proxy& fg(int index) { 
            return fg(std::to_string(index)); 
        }
        
        buffer_proxy& fg(int r, int g, int b) {
            std::ostringstream oss; 
            oss << "rgb(" << r << "," << g << "," << b << ")";
            return fg(oss.str());
        }

        buffer_proxy& bg(const std::string& spec) {
            auto code = get_color_code(spec, false);
            if (code && code != detail::cur_bg()) {
                detail::buffer() += "\033[" + *code + "m";
                detail::cur_bg() = code;
            }
            return *this;
        }
        
        buffer_proxy& bg(int index) { 
            return bg(std::to_string(index)); 
        }
        
        buffer_proxy& bg(int r, int g, int b) {
            std::ostringstream oss; 
            oss << "rgb(" << r << "," << g << "," << b << ")";
            return bg(oss.str());
        }

        buffer_proxy& style(const std::string& name_raw) {
            std::string name = name_raw;
            std::transform(name.begin(), name.end(), name.begin(), 
                          [](unsigned char c) { return std::tolower(c); });
            
            static const std::unordered_map<std::string, int> styles = {
                {"bold", 1}, {"faint", 2}, {"italic", 3}, {"underline", 4},
                {"blink", 5}, {"reverse", 7}, {"strike", 9}
            };
            
            auto it = styles.find(name);
            if (it != styles.end()) {
                if (detail::cur_styles().insert(it->second).second) {
                    detail::buffer() += "\033[" + std::to_string(it->second) + "m";
                }
            }
            return *this;
        }

        buffer_proxy& text(const std::string& t) {
            detail::buffer() += t;
            return *this;
        }

        buffer_proxy& reset() {
            detail::buffer() += "\033[0m";
            detail::cur_fg().reset();
            detail::cur_bg().reset();
            detail::cur_styles().clear();
            return *this;
        }

        // 개선된 strip() - ANSI 이스케이프 시퀀스 제거
        buffer_proxy& strip() {
            std::string& buf = detail::buffer();
            std::string clean;
            clean.reserve(buf.size());
            
            size_t i = 0;
            while (i < buf.size()) {
                if (buf[i] == '\033' && i + 1 < buf.size() && buf[i + 1] == '[') {
                    size_t j = i + 2;
                    // CSI 시퀀스 파싱
                    while (j < buf.size() && 
                           (std::isdigit(static_cast<unsigned char>(buf[j])) || 
                            buf[j] == ';' || buf[j] == '?')) {
                        ++j;
                    }
                    // 종료 문자 확인 (m, H, A-D, J, K 등)
                    if (j < buf.size() && std::isalpha(static_cast<unsigned char>(buf[j]))) {
                        i = j + 1;
                        continue;
                    }
                }
                clean += buf[i++];
            }
            
            buf = std::move(clean);
            return *this;
        }

        buffer_proxy& flush() {
            std::cout << detail::buffer();
            std::cout.flush();
            detail::buffer().clear();
            detail::cur_fg().reset();
            detail::cur_bg().reset();
            detail::cur_styles().clear();
            return *this;
        }
        
        // 버퍼 내용 반환 (출력하지 않고)
        std::string str() const {
            return detail::buffer();
        }
        
        // 버퍼 초기화
        buffer_proxy& clear() {
            detail::buffer().clear();
            detail::cur_fg().reset();
            detail::cur_bg().reset();
            detail::cur_styles().clear();
            return *this;
        }
    };

    inline buffer_proxy buffer() { return buffer_proxy(); }

} // namespace ansi

#endif // ANSI_HPP