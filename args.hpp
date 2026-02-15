#ifndef ARGS_HPP
#define ARGS_HPP
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <filesystem>
#include <stdexcept>
#include <algorithm>

#include "util.hpp"
#include "fnutil.hpp"

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
#endif

// 파싱 옵션 구조체
struct ArgsParseOptions {
    bool deduplicate_files = true;      // 중복 파일 제거
    bool verify_exists = false;          // 파일 존재 여부 확인
    bool throw_on_error = false;         // 에러 발생 시 예외 던지기
    bool expand_directories = true;      // 디렉토리를 파일 목록으로 확장
    
    // 확장자 필터 (소문자로 변환되어 비교됨, 점 포함 예: L".cpp")
    std::vector<std::wstring> include_extensions;  // 이 확장자만 포함 (비어있으면 모두 포함)
    std::vector<std::wstring> exclude_extensions;  // 이 확장자는 제외
};

class Args {
public:
    using path = std::filesystem::path;
    using ParseOptions = ArgsParseOptions;  // 하위 호환성을 위한 별칭

    Args(int argc, char* argv[], ParseOptions opts = {})
        : parse_opts(opts)
    {
        parse_raw_args(argc, argv);
        expand_files();
    }

    // 옵션 존재 여부 확인
    bool has(const std::wstring& opt) const {
        return options.find(opt) != options.end();
    }

    // 옵션 값 가져오기
    std::wstring get(
        const std::wstring& opt,
        const std::wstring& def = L"") const
    {
        auto it = options.find(opt);
        if (it == options.end() || !it->second)
            return def;
        return *it->second;
    }

    // 정수형 옵션 값 가져오기
    int get_int(
        const std::wstring& opt,
        int def = 0) const
    {
        auto val = get(opt);
        if (val.empty()) return def;
        
        try {
            return std::stoi(val);
        } catch (...) {
            return def;
        }
    }

    // bool 옵션 값 가져오기 (--flag 또는 --flag=true/false)
    bool get_bool(
        const std::wstring& opt,
        bool def = false) const
    {
        auto it = options.find(opt);
        if (it == options.end())
            return def;

        if (!it->second) // 값 없이 플래그만 있으면 true
            return true;

        auto val = util::to_lower_ascii(*it->second);
        if (val == L"true" || val == L"1" || val == L"yes")
            return true;
        if (val == L"false" || val == L"0" || val == L"no")
            return false;

        return def;
    }

    // 파일 목록 가져오기
    const std::vector<path>& files() const {
        return file_args;
    }

    // 특정 확장자 필터링 (vector 버전)
    std::vector<path> files(
        const std::vector<std::wstring>& extensions,
        bool case_sensitive = false) const 
    {
        return filter_by_extensions(file_args, extensions, case_sensitive);
    }

    // 특정 확장자 필터링 (initializer_list 버전)
    std::vector<path> files(
        std::initializer_list<std::wstring> extensions,
        bool case_sensitive = false) const 
    {
        return filter_by_extensions(file_args, std::vector<std::wstring>(extensions), case_sensitive);
    }

    // 특정 확장자 필터링 (단일 확장자)
    std::vector<path> files(
        const std::wstring& extension,
        bool case_sensitive = false) const 
    {
        return filter_by_extensions(file_args, {extension}, case_sensitive);
    }

    // 원본 인자 가져오기 (디버깅용)
    const std::vector<std::wstring>& raw() const {
        return raw_args;
    }

    // 모든 옵션 가져오기
    const std::unordered_map<std::wstring, std::optional<std::wstring>>& 
    all_options() const {
        return options;
    }

private:
    ParseOptions parse_opts;
    std::vector<std::wstring> raw_args;
    std::unordered_map<std::wstring, std::optional<std::wstring>> options;
    std::vector<path> file_args;

    // -----------------------------------------
    // 확장자로 파일 필터링 (헬퍼 함수)
    // -----------------------------------------
    std::vector<path> filter_by_extensions(
        const std::vector<path>& files,
        const std::vector<std::wstring>& extensions,
        bool case_sensitive = false) const 
    {
        if (extensions.empty()) {
            return files;  // 빈 리스트면 모든 파일 반환
        }

        // 확장자 정규화
        std::vector<std::wstring> normalized_exts;
        normalized_exts.reserve(extensions.size());
        for (const auto& ext : extensions) {
            auto normalized = ext;
            
            // 대소문자 무시 모드면 소문자로 변환
            if (!case_sensitive) {
                normalized = util::to_lower_ascii(normalized);
            }
            
            // 점이 없으면 추가
            if (!normalized.empty() && normalized[0] != L'.') {
                normalized = L"." + normalized;
            }
            
            normalized_exts.push_back(normalized);
        }

        // 필터링
        std::vector<path> result;
        for (const auto& file : files) {
            auto file_ext = file.extension().wstring();
            
            // 대소문자 무시 모드면 소문자로 변환
            if (!case_sensitive) {
                file_ext = util::to_lower_ascii(file_ext);
            }
            
            for (const auto& ext : normalized_exts) {
                if (file_ext == ext) {
                    result.push_back(file);
                    break;
                }
            }
        }

        return result;
    }

    // -----------------------------------------
    // 확장자 문자열 파싱 (.cpp,.hpp 형식)
    // -----------------------------------------
    std::vector<std::wstring> parse_extensions(const std::wstring& ext_str) const {
        std::vector<std::wstring> result;
        if (ext_str.empty()) return result;

        std::wstring current;
        for (wchar_t c : ext_str) {
            if (c == L',' || c == L';' || c == L' ') {
                if (!current.empty()) {
                    // 점이 없으면 추가
                    if (current[0] != L'.') {
                        current = L"." + current;
                    }
                    result.push_back(util::to_lower_ascii(current));
                    current.clear();
                }
            } else {
                current += c;
            }
        }
        
        if (!current.empty()) {
            if (current[0] != L'.') {
                current = L"." + current;
            }
            result.push_back(util::to_lower_ascii(current));
        }

        return result;
    }

    // -----------------------------------------
    // 확장자 필터 체크
    // -----------------------------------------
    bool should_include_file(const path& p) const {
        // 디렉토리는 필터링하지 않음
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) {
            return true;
        }

        auto ext = util::to_lower_ascii(p.extension().wstring());

        // 제외 확장자 체크
        for (const auto& excluded : parse_opts.exclude_extensions) {
            if (ext == excluded) {
                return false;
            }
        }

        // 포함 확장자 체크 (비어있으면 모두 포함)
        if (parse_opts.include_extensions.empty()) {
            return true;
        }

        for (const auto& included : parse_opts.include_extensions) {
            if (ext == included) {
                return true;
            }
        }

        return false;  // 포함 목록에 없으면 제외
    }

    // -----------------------------------------
    // argv → wstring + 옵션 분리
    // Windows/Linux 모두에서 올바른 인코딩 보장
    // -----------------------------------------
    void parse_raw_args(int argc, char* argv[]) {
#ifdef _WIN32
        // Windows: CommandLineToArgvW로 유니코드 직접 획득
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        
        if (!wargv) {
            if (parse_opts.throw_on_error)
                throw std::runtime_error("Failed to parse command line");
            return;
        }

        // argv[0]은 프로그램 이름이므로 제외
        for (int i = 1; i < wargc; ++i) {
            raw_args.emplace_back(wargv[i]);
        }

        LocalFree(wargv);
#else
        // Linux: 현재 로케일 확인 후 적절한 변환
        // 대부분의 현대 Linux는 UTF-8을 사용하지만, 
        // 다른 인코딩도 지원하기 위해 로케일 기반 변환 사용
        
        // 현재 콘솔 코드페이지 감지
        auto current_cp = util::get_console_codepage();
        
        // 시스템 로케일 설정 (인코딩 변환에 필요)
        std::setlocale(LC_ALL, "");
        
        for (int i = 1; i < argc; ++i) {
            // argv는 시스템 인코딩(보통 UTF-8)으로 되어 있음
            // util::to_wstring이 자동으로 감지하여 변환
            std::string arg(argv[i]);
            
            // 인코딩 자동 감지 및 변환
            auto warg = util::to_wstring(arg);
            
            if (warg.empty() && !arg.empty()) {
                // 변환 실패 시 ANSI로 재시도
                warg = util::ansi_to_wstring(arg);
            }
            
            raw_args.emplace_back(warg);
        }
#endif

        // 옵션 파싱
        parse_options();
    }

    // -----------------------------------------
    // 옵션과 파일 인자 분리
    // -----------------------------------------
    void parse_options() {
        for (size_t i = 0; i < raw_args.size(); ++i) {
            const auto& arg = raw_args[i];

            if (arg.empty())
                continue;

            // -- 또는 - 로 시작하는 옵션
            if (arg[0] == L'-') {
                // --option=value 형식 파싱
                auto eq_pos = arg.find(L'=');
                if (eq_pos != std::wstring::npos) {
                    auto key = arg.substr(0, eq_pos);
                    auto val = arg.substr(eq_pos + 1);
                    options[key] = val;
                    continue;
                }

                // 다음 인자가 값인지 확인
                if (i + 1 < raw_args.size() && 
                    !raw_args[i + 1].empty() &&
                    raw_args[i + 1][0] != L'-') 
                {
                    // --option value 형식
                    options[arg] = raw_args[i + 1];
                    ++i; // 다음 인자를 값으로 사용했으므로 건너뜀
                } 
                else {
                    // --flag 형식 (값 없는 플래그)
                    options[arg] = std::nullopt;
                }
            } 
            else {
                // 옵션이 아닌 일반 파일 인자
                file_args.emplace_back(arg);
            }
        }

        // 명령줄에서 확장자 필터 옵션 파싱
        parse_extension_filters();
    }

    // -----------------------------------------
    // 명령줄 옵션에서 확장자 필터 추출
    // -----------------------------------------
    void parse_extension_filters() {
        // 포함 확장자: --include-ext=.cpp,.hpp 또는 -I=.cpp
        if (has(L"--include-ext")) {
            auto exts = get(L"--include-ext");
            auto parsed = parse_extensions(exts);
            parse_opts.include_extensions.insert(
                parse_opts.include_extensions.end(),
                parsed.begin(), parsed.end());
        }
        if (has(L"-I")) {
            auto exts = get(L"-I");
            auto parsed = parse_extensions(exts);
            parse_opts.include_extensions.insert(
                parse_opts.include_extensions.end(),
                parsed.begin(), parsed.end());
        }

        // 제외 확장자: --exclude-ext=.tmp,.bak 또는 -E=.tmp
        if (has(L"--exclude-ext")) {
            auto exts = get(L"--exclude-ext");
            auto parsed = parse_extensions(exts);
            parse_opts.exclude_extensions.insert(
                parse_opts.exclude_extensions.end(),
                parsed.begin(), parsed.end());
        }
        if (has(L"-E")) {
            auto exts = get(L"-E");
            auto parsed = parse_extensions(exts);
            parse_opts.exclude_extensions.insert(
                parse_opts.exclude_extensions.end(),
                parsed.begin(), parsed.end());
        }
    }

    // -----------------------------------------
    // 옵션 → glob 옵션 변환
    // -----------------------------------------
    fnutil::glob_options make_glob_options() const {
        fnutil::glob_options opt;

        // 재귀 검색
        opt.recursive = get_bool(L"--recursive") || 
                        get_bool(L"-R") ||
                        get_bool(L"-r");

        // 대소문자 무시 (기본값 true)
        if (has(L"--case-sensitive") || has(L"-C")) {
            opt.ignoreCase = false;
        } else {
            opt.ignoreCase = true;
        }

        // 디렉토리 포함
        opt.include_directories = get_bool(L"--include-dirs") ||
                                  get_bool(L"-D");

        // 절대 경로 사용
        opt.absolute = !get_bool(L"--relative");

        return opt;
    }

    // -----------------------------------------
    // 파일 인자 확장 (glob / 디렉토리)
    // -----------------------------------------
    void expand_files() {
        if (file_args.empty())
            return;

        std::vector<path> expanded;
        std::unordered_set<std::wstring> seen; // 중복 제거용
        auto glob_opt = make_glob_options();

        for (const auto& p : file_args) {
            try {
                expand_single_path(p, expanded, seen, glob_opt);
            }
            catch (const std::exception& e) {
                if (parse_opts.throw_on_error)
                    throw;
                // 에러 무시하고 계속 진행
            }
        }

        file_args = std::move(expanded);
    }

    // -----------------------------------------
    // 단일 경로 확장
    // -----------------------------------------
    void expand_single_path(
        const path& p,
        std::vector<path>& expanded,
        std::unordered_set<std::wstring>& seen,
        const fnutil::glob_options& glob_opt)
    {
        const std::wstring ws = p.wstring();

        // 와일드카드 포함 → glob_ex
        bool has_wildcard = (ws.find(L'*') != std::wstring::npos ||
                            ws.find(L'?') != std::wstring::npos);

        if (has_wildcard) {
            auto matches = fnutil::glob_ex(p, glob_opt);
            
            for (const auto& match : matches) {
                // glob_ex가 실패하면 원본을 반환하는데,
                // 원본이 와일드카드를 포함하면 실제 파일이 아니므로 검증
                if (match == p && has_wildcard) {
                    // 매칭 실패 - 원본이 와일드카드면 스킵
                    if (parse_opts.verify_exists)
                        continue;
                }

                add_file_if_valid(match, expanded, seen);
            }
        }
        else if (std::filesystem::exists(p) && 
                 std::filesystem::is_directory(p) &&
                 parse_opts.expand_directories) 
        {
            // 디렉토리 → 내부 파일 확장
            auto matches = fnutil::glob_ex(p / L"*", glob_opt);
            
            for (const auto& match : matches) {
                add_file_if_valid(match, expanded, seen);
            }
        }
        else {
            // 일반 파일 또는 존재하지 않는 경로
            add_file_if_valid(p, expanded, seen);
        }
    }

    // -----------------------------------------
    // 유효한 파일만 추가 (중복 제거 포함)
    // -----------------------------------------
    void add_file_if_valid(
        const path& p,
        std::vector<path>& expanded,
        std::unordered_set<std::wstring>& seen)
    {
        // 확장자 필터 체크
        if (!should_include_file(p)) {
            return;  // 필터에 맞지 않으면 스킵
        }

        // 존재 여부 확인
        if (parse_opts.verify_exists) {
            std::error_code ec;
            if (!std::filesystem::exists(p, ec)) {
                if (parse_opts.throw_on_error) {
                    throw std::runtime_error(
                        "File not found: " + 
                        util::wstring_to_utf8(p.wstring()));
                }
                return; // 존재하지 않으면 스킵
            }
        }

        // 중복 제거
        if (parse_opts.deduplicate_files) {
            std::wstring canonical_path;
            
            try {
                // 절대 경로로 정규화하여 중복 체크
                canonical_path = std::filesystem::absolute(p).wstring();
            }
            catch (...) {
                // 정규화 실패 시 원본 경로 사용
                canonical_path = p.wstring();
            }

            // 이미 추가된 파일인지 확인
            if (seen.find(canonical_path) != seen.end())
                return;

            seen.insert(canonical_path);
        }

        expanded.push_back(p);
    }
};

#endif // ARGS_HPP