#pragma once

/**
 * ConfReader - 단일 헤더 INI 스타일 설정 파일 리더
 *
 * 사용법:
 *   #include "conf.hpp"   // str.hpp가 같은 경로에 있어야 함
 *
 *   ConfReader conf;                     // 실행파일과 같은 이름의 .conf 자동 탐색
 *   ConfReader conf("C:/app/my.conf");   // 경로 직접 지정
 *
 * 설정 파일 형식:
 *   # 주석, ; 주석, 인라인 주석 모두 지원
 *
 *   window_x = 100          # 섹션 없는 루트 키
 *   volume   = 1.0
 *
 *   [database]
 *   host = localhost
 *
 *   audio_exts = mp3, ogg, \   # 백슬래시 줄 이음
 *                wav, flac
 *
 *   path = "C:/My Music"        # 따옴표 값 (자동 제거)
 */

#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <stdexcept>

#include "str.hpp"   // trim, to_lower, split_comma, strip_comment

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#  include <limits.h>
#endif

// ============================================================
//  ConfReader
// ============================================================
class ConfReader {
public:

    /// 섹션 없는 루트 키를 저장하는 가상 섹션 이름
    static constexpr const char* ROOT = "";

    // ----------------------------------------------------------
    // 생성자
    // ----------------------------------------------------------

    /// 실행파일과 같은 경로에서 확장자만 .conf 로 바꿔 읽음
    explicit ConfReader(bool throwOnMissing = false)
        : m_throwOnMissing(throwOnMissing)
    {
        load(executableConfPath());
    }

    /// 파일 경로 직접 지정
    explicit ConfReader(const std::string& filePath, bool throwOnMissing = false)
        : m_throwOnMissing(throwOnMissing)
    {
        load(filePath);
    }

    // ----------------------------------------------------------
    // 값 조회 (섹션/키 대소문자 구분 없음)
    // ----------------------------------------------------------

    /// [section] key 조회. 루트 키는 section = ConfReader::ROOT ("")
    const std::string& get(const std::string& section,
                           const std::string& key,
                           const std::string& defaultValue = EMPTY) const
    {
        auto secIt = m_data.find(str::to_lower(section));
        if (secIt == m_data.end()) return defaultValue;
        auto keyIt = secIt->second.find(str::to_lower(key));
        if (keyIt == secIt->second.end()) return defaultValue;
        return keyIt->second;
    }

    /// 루트 키 편의 오버로드
    const std::string& get(const std::string& key,
                           const std::string& defaultValue = EMPTY) const
    {
        return get(ROOT, key, defaultValue);
    }

    /// 정수 변환
    int getInt(const std::string& section,
               const std::string& key,
               int defaultValue = 0) const
    {
        return str::to_int(get(section, key, EMPTY)).value_or(defaultValue);
    }

    int getInt(const std::string& key, int defaultValue = 0) const {
        return getInt(ROOT, key, defaultValue);
    }

    /// 실수 변환
    double getDouble(const std::string& section,
                     const std::string& key,
                     double defaultValue = 0.0) const
    {
        return str::to_double(get(section, key, EMPTY)).value_or(defaultValue);
    }

    double getDouble(const std::string& key, double defaultValue = 0.0) const {
        return getDouble(ROOT, key, defaultValue);
    }

    /// bool 변환 (true / yes / 1 / on → true)
    bool getBool(const std::string& section,
                 const std::string& key,
                 bool defaultValue = false) const
    {
        const auto& raw = get(section, key, EMPTY);
        if (raw.empty()) return defaultValue;

        const auto lv = str::to_lower(raw);
        if (lv == "true" || lv == "yes" || lv == "1" || lv == "on") return true;
        if (lv == "false" || lv == "no"  || lv == "0" || lv == "off") return false;
        return defaultValue;
    }

    bool getBool(const std::string& key, bool defaultValue = false) const {
        return getBool(ROOT, key, defaultValue);
    }

    /// 콤마 구분 리스트 반환
    /// "mp3, ogg, wav" → {"mp3", "ogg", "wav"}
    std::vector<std::string> getList(const std::string& section,
                                     const std::string& key) const
    {
        return str::split_comma(get(section, key, EMPTY));
    }

    std::vector<std::string> getList(const std::string& key) const {
        return getList(ROOT, key);
    }

    /// 섹션 존재 여부
    bool hasSection(const std::string& section) const {
        return m_data.count(str::to_lower(section)) > 0;
    }

    /// 키 존재 여부
    bool hasKey(const std::string& section, const std::string& key) const {
        auto secIt = m_data.find(str::to_lower(section));
        if (secIt == m_data.end()) return false;
        return secIt->second.count(str::to_lower(key)) > 0;
    }

    bool hasKey(const std::string& key) const {
        return hasKey(ROOT, key);
    }

    bool isLoaded()              const { return m_loaded;   }
    const std::string& filePath() const { return m_filePath; }

    const std::unordered_map<std::string,
          std::unordered_map<std::string, std::string>>& data() const
    {
        return m_data;
    }

// ============================================================
//  private
// ============================================================
private:

    void load(const std::string& path)
    {
        m_filePath = path;
        m_loaded   = false;

        std::ifstream file(path);
        if (!file.is_open()) {
            if (m_throwOnMissing)
                throw std::runtime_error("ConfReader: 파일을 열 수 없습니다 -> " + path);
            return;
        }

        std::string currentSection = ROOT;
        std::string line;

        while (std::getline(file, line)) {

            // UTF-8 BOM 제거
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line = line.substr(3);
            }

            line = str::trim(line);

            // 빈 줄 / 주석 줄 건너뜀
            if (line.empty() || line[0] == '#' || line[0] == ';')
                continue;

            // 섹션 헤더 [section]
            if (line.front() == '[' && line.back() == ']') {
                currentSection = str::to_lower(
                    str::trim(line.substr(1, line.size() - 2)));
                m_data[currentSection]; // 섹션 보장
                continue;
            }

            // key = value
            const auto eqPos = line.find('=');
            if (eqPos == std::string::npos) continue;

            std::string key      = str::to_lower(str::trim(line.substr(0, eqPos)));
            std::string rawValue = str::trim(line.substr(eqPos + 1));

            // 백슬래시 줄 이음
            while (!rawValue.empty() && rawValue.back() == '\\') {
                rawValue.pop_back();
                rawValue = str::trim(rawValue);
                std::string nextLine;
                if (!std::getline(file, nextLine)) break;
                nextLine = str::trim(nextLine);
                if (nextLine.empty() || nextLine[0] == '#' || nextLine[0] == ';') break;
                rawValue += " " + nextLine;
            }

            std::string value = str::trim(str::strip_comment(rawValue));

            // 따옴표 자동 제거
            if (value.size() >= 2 &&
                ((value.front() == '"'  && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\'')))
            {
                value = value.substr(1, value.size() - 2);
            }

            if (!key.empty())
                m_data[currentSection][key] = std::move(value);
        }

        m_loaded = true;
    }

    // 실행파일 경로에서 확장자를 .conf 로 교체
    static std::string executableConfPath()
    {
        namespace fs = std::filesystem;

#ifdef _WIN32
        wchar_t wpath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, wpath, MAX_PATH) == 0) return "app.conf";
        fs::path p(wpath);
#else
        char buf[PATH_MAX] = {};
        const ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len <= 0) return "app.conf";
        fs::path p(std::string(buf, static_cast<size_t>(len)));
#endif
        p.replace_extension(".conf");
        auto u8 = p.u8string();
        return std::string(u8.begin(), u8.end());
    }

    // ----------------------------------------------------------
    // 멤버 변수
    // ----------------------------------------------------------
    using SectionMap = std::unordered_map<std::string, std::string>;
    using DataMap    = std::unordered_map<std::string, SectionMap>;

    DataMap     m_data;
    std::string m_filePath;
    bool        m_loaded         = false;
    bool        m_throwOnMissing = false;

    static inline const std::string EMPTY{};
};