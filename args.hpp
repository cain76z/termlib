#ifndef ARGS_HPP
#define ARGS_HPP
#pragma once

/**
 * args.hpp — 커맨드라인 인자 파서 (C++17, 단일 헤더)
 *
 * ── 옵션 형식 ──────────────────────────────────────────────────────────────
 *   --flag              값 없는 플래그   → has("--flag") == true
 *   --key=value         = 구분 값        → get("--key") == "value"  (항상 동작)
 *   --key value         공백 구분 값     → value_args에 "--key" 등록 필요
 *   -f                  단일 대시 플래그 → has("-f") == true
 *   --                  이후 모든 인자를 파일로 처리
 *
 * ── 공백 구분 값 등록 ──────────────────────────────────────────────────────
 *   ArgsParseOptions opts;
 *   opts.value_args = {L"--output", L"--threads", L"-o"};
 *   Args args(argc, argv, opts);
 *   // args.get(L"--output") → "result.txt"   (app --output result.txt)
 *
 *   value_args가 비어있으면 모든 옵션을 플래그로 처리.
 *   공백 구분 값을 쓰려면 반드시 명시적으로 등록해야 함.
 *   (이전 버전의 휴리스틱 파싱 제거 — 파일 인자 흡수 버그 수정)
 *
 * ── 파일 인자 ──────────────────────────────────────────────────────────────
 *   옵션이 아닌 인자는 모두 파일로 처리.
 *   와일드카드(*, ?) 자동 확장, 디렉토리 자동 전개, 중복 제거 지원.
 *
 * ── 확장자 필터 ────────────────────────────────────────────────────────────
 *   opts.include_extensions = {L".cpp", L".hpp"};  // 코드에서 직접 지정
 *   --include-ext=.cpp,.hpp                         // 커맨드라인에서 지정
 *   --exclude-ext=.tmp,.bak
 *   -I=.cpp   -E=.tmp  (단축형)
 */

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "encode.hpp"   // util::to_wstring, util::to_lower_ascii, util::wstring_to_utf8
#include "fnutil.hpp"   // fnutil::glob_ex, fnutil::glob_options

#ifdef _WIN32
    #include <windows.h>
    #include <shellapi.h>
#endif

// =========================================================
//  파싱 옵션
// =========================================================

struct ArgsParseOptions {
    // ── 파일 처리 ─────────────────────────────────────────
    bool deduplicate_files   = true;   ///< 중복 파일 제거 (절대 경로 기준)
    bool verify_exists       = false;  ///< 파일 존재 여부 확인 (false면 비존재 파일도 허용)
    bool throw_on_error      = false;  ///< 에러 발생 시 예외 던지기
    bool expand_directories  = true;   ///< 디렉토리를 파일 목록으로 자동 전개

    // ── 확장자 필터 (점 포함, 소문자, 예: L".cpp") ────────
    std::vector<std::wstring> include_extensions;  ///< 이 확장자만 포함 (빈 경우 모두 포함)
    std::vector<std::wstring> exclude_extensions;  ///< 이 확장자는 항상 제외

    // ── 공백 구분 값을 가지는 옵션 목록 ──────────────────
    /// "--output", "--threads" 등을 등록하면 "--output result.txt" 형태로 파싱.
    /// 비어있으면 모든 옵션을 플래그로 처리. "=" 형식은 항상 동작.
    std::set<std::wstring> value_args;
};

// =========================================================
//  Args
// =========================================================

class Args {
public:
    using path         = std::filesystem::path;
    using ParseOptions = ArgsParseOptions;  ///< 하위 호환 별칭

    Args(int argc, char* argv[], ArgsParseOptions opts = {})
        : opts_(std::move(opts))
    {
        collect_raw_args(argc, argv);
        parse_options();
        apply_cmdline_ext_filters();
        expand_files();
    }

    // ── 옵션 조회 ─────────────────────────────────────────

    /// 옵션 존재 여부 ("--flag", "-f" 등)
    bool has(const std::wstring& key) const {
        return options_.count(key) > 0;
    }

    /// 옵션 값 반환 (없거나 플래그이면 def 반환)
    std::wstring get(const std::wstring& key,
                     const std::wstring& def = L"") const
    {
        auto it = options_.find(key);
        if (it == options_.end() || !it->second) return def;
        return *it->second;
    }

    /// 정수 변환 (변환 실패 시 def)
    int get_int(const std::wstring& key, int def = 0) const {
        const auto val = get(key);
        if (val.empty()) return def;
        try { return std::stoi(val); } catch (...) { return def; }
    }

    /// 실수 변환 (변환 실패 시 def)
    double get_double(const std::wstring& key, double def = 0.0) const {
        const auto val = get(key);
        if (val.empty()) return def;
        try { return std::stod(val); } catch (...) { return def; }
    }

    /// bool 변환
    /// - 플래그만 있으면 (값 없음) → true
    /// - "true"/"1"/"yes"/"on"   → true
    /// - "false"/"0"/"no"/"off"  → false
    /// - 그 외                    → def
    bool get_bool(const std::wstring& key, bool def = false) const {
        auto it = options_.find(key);
        if (it == options_.end()) return def;
        if (!it->second) return true;  // 플래그만 있는 경우

        const auto val = util::to_lower_ascii(*it->second);
        if (val == L"true"  || val == L"1" || val == L"yes" || val == L"on")  return true;
        if (val == L"false" || val == L"0" || val == L"no"  || val == L"off") return false;
        return def;
    }

    // ── 파일 목록 조회 ────────────────────────────────────

    /// 전체 파일 목록
    const std::vector<path>& files() const { return files_; }

    /// 특정 확장자만 필터링 (단일)
    std::vector<path> files(const std::wstring& ext,
                            bool case_sensitive = false) const
    {
        return filter_by_ext(files_, {ext}, case_sensitive);
    }

    /// 특정 확장자만 필터링 (복수)
    std::vector<path> files(const std::vector<std::wstring>& exts,
                            bool case_sensitive = false) const
    {
        return filter_by_ext(files_, exts, case_sensitive);
    }

    /// 특정 확장자만 필터링 (initializer_list)
    std::vector<path> files(std::initializer_list<std::wstring> exts,
                            bool case_sensitive = false) const
    {
        return filter_by_ext(files_, std::vector<std::wstring>(exts), case_sensitive);
    }

    // ── 디버깅용 ──────────────────────────────────────────

    /// 파싱 전 원본 인자 목록 (argv[1] 이후)
    const std::vector<std::wstring>& raw() const { return raw_args_; }

    /// 모든 옵션 맵 (nullopt = 값 없는 플래그)
    const std::unordered_map<std::wstring, std::optional<std::wstring>>&
    all_options() const { return options_; }

// =========================================================
//  private
// =========================================================
private:
    ArgsParseOptions   opts_;
    std::vector<std::wstring>  raw_args_;
    std::unordered_map<std::wstring, std::optional<std::wstring>> options_;
    std::vector<path>  files_;

    // ── 1. argv → raw_args_ ───────────────────────────────

    void collect_raw_args(int argc, char* argv[]) {
#ifdef _WIN32
        // Windows: CommandLineToArgvW로 정확한 유니코드 획득
        int wargc = 0;
        LPWSTR* wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
        if (!wargv) {
            if (opts_.throw_on_error)
                throw std::runtime_error("Failed to parse command line");
            return;
        }
        for (int i = 1; i < wargc; ++i)
            raw_args_.emplace_back(wargv[i]);
        LocalFree(wargv);
#else
        // Linux/macOS: 시스템 인코딩(보통 UTF-8) → wstring
        std::setlocale(LC_ALL, "");
        for (int i = 1; i < argc; ++i) {
            std::string arg(argv[i]);
            auto ws = util::to_wstring(arg);
            if (ws.empty() && !arg.empty())
                ws = util::ansi_to_wstring(arg);  // 폴백
            raw_args_.emplace_back(std::move(ws));
        }
#endif
    }

    // ── 2. 옵션과 파일 분리 ───────────────────────────────

    void parse_options() {
        for (size_t i = 0; i < raw_args_.size(); ++i) {
            const auto& arg = raw_args_[i];
            if (arg.empty()) continue;

            // "--" → 이후 모든 인자를 파일로 처리
            if (arg == L"--") {
                for (size_t j = i + 1; j < raw_args_.size(); ++j)
                    files_.emplace_back(raw_args_[j]);
                break;
            }

            if (arg[0] == L'-') {
                // "--key=value" 형식 — 항상 동작, value_args 등록 불필요
                auto eq = arg.find(L'=');
                if (eq != std::wstring::npos) {
                    options_[arg.substr(0, eq)] = arg.substr(eq + 1);
                    continue;
                }

                // "--key value" 형식 — value_args에 등록된 경우에만
                if (opts_.value_args.count(arg) &&
                    i + 1 < raw_args_.size() &&
                    !raw_args_[i + 1].empty())
                {
                    options_[arg] = raw_args_[++i];
                    continue;
                }

                // 플래그
                options_[arg] = std::nullopt;
            }
            else {
                // 옵션이 아닌 인자 → 파일
                files_.emplace_back(arg);
            }
        }
    }

    // ── 3. 커맨드라인에서 확장자 필터 옵션 적용 ──────────

    void apply_cmdline_ext_filters() {
        auto append_exts = [&](const std::wstring& key,
                               std::vector<std::wstring>& target)
        {
            if (has(key)) {
                auto parsed = split_extensions(get(key));
                target.insert(target.end(), parsed.begin(), parsed.end());
            }
        };

        append_exts(L"--include-ext", opts_.include_extensions);
        append_exts(L"-I",            opts_.include_extensions);
        append_exts(L"--exclude-ext", opts_.exclude_extensions);
        append_exts(L"-E",            opts_.exclude_extensions);
    }

    // ── 4. 파일 확장 (glob / 디렉토리 전개) ──────────────

    void expand_files() {
        if (files_.empty()) return;

        std::vector<path>             expanded;
        std::unordered_set<std::wstring> seen;
        const auto glob_opt = make_glob_options();

        for (const auto& p : files_) {
            try {
                expand_single(p, expanded, seen, glob_opt);
            } catch (...) {
                if (opts_.throw_on_error) throw;
            }
        }
        files_ = std::move(expanded);
    }

    void expand_single(const path& p,
                       std::vector<path>& out,
                       std::unordered_set<std::wstring>& seen,
                       const fnutil::glob_options& glob_opt)
    {
        const auto ws = p.wstring();
        const bool has_wild = ws.find(L'*') != std::wstring::npos ||
                              ws.find(L'?') != std::wstring::npos;

        if (has_wild) {
            // 와일드카드 → glob_ex
            // glob_ex는 매칭 실패 시 원본 경로를 반환하므로 걸러냄
            auto matches = fnutil::glob_ex(p, glob_opt);
            if (matches.size() == 1 && matches[0] == p) return; // 매칭 실패
            for (const auto& m : matches)
                add_if_valid(m, out, seen);
        }
        else if (opts_.expand_directories &&
                 std::filesystem::is_directory(p))
        {
            // 디렉토리 → 내부 파일 전개
            for (const auto& m : fnutil::glob_ex(p / L"*", glob_opt))
                add_if_valid(m, out, seen);
        }
        else {
            add_if_valid(p, out, seen);
        }
    }

    void add_if_valid(const path& p,
                      std::vector<path>& out,
                      std::unordered_set<std::wstring>& seen)
    {
        if (!passes_ext_filter(p)) return;

        if (opts_.verify_exists) {
            std::error_code ec;
            if (!std::filesystem::exists(p, ec)) {
                if (opts_.throw_on_error)
                    throw std::runtime_error(
                        "File not found: " + util::wstring_to_utf8(p.wstring()));
                return;
            }
        }

        if (opts_.deduplicate_files) {
            std::wstring key;
            try {
                key = std::filesystem::absolute(p).wstring();
#ifdef _WIN32
                key = util::to_lower_ascii(key); // Windows: 대소문자 무시
#endif
            } catch (...) {
                key = p.wstring();
            }
            if (!seen.insert(key).second) return; // 이미 추가됨
        }

        out.push_back(p);
    }

    // ── 헬퍼: 확장자 필터 ────────────────────────────────

    /// 파일이 include/exclude 필터를 통과하는지 확인
    bool passes_ext_filter(const path& p) const {
        std::error_code ec;
        if (std::filesystem::is_directory(p, ec)) return true;

        const auto ext = util::to_lower_ascii(p.extension().wstring());

        for (const auto& ex : opts_.exclude_extensions)
            if (ext == ex) return false;

        if (opts_.include_extensions.empty()) return true;

        for (const auto& in : opts_.include_extensions)
            if (ext == in) return true;

        return false;
    }

    /// 확장자 목록으로 파일 필터링
    static std::vector<path> filter_by_ext(
        const std::vector<path>& files,
        const std::vector<std::wstring>& exts,
        bool case_sensitive)
    {
        if (exts.empty()) return files;

        // 정규화된 확장자 목록 구성 (점 추가, 대소문자 통일)
        std::vector<std::wstring> norm;
        norm.reserve(exts.size());
        for (auto e : exts) {
            if (!case_sensitive) e = util::to_lower_ascii(e);
            if (!e.empty() && e[0] != L'.') e = L"." + e;
            norm.push_back(std::move(e));
        }

        std::vector<path> result;
        for (const auto& f : files) {
            auto fext = f.extension().wstring();
            if (!case_sensitive) fext = util::to_lower_ascii(fext);
            if (std::find(norm.begin(), norm.end(), fext) != norm.end())
                result.push_back(f);
        }
        return result;
    }

    // ── 헬퍼: 확장자 문자열 파싱 ─────────────────────────

    /// ".cpp,.hpp" 또는 "cpp;hpp cpp" → {L".cpp", L".hpp"}
    static std::vector<std::wstring> split_extensions(const std::wstring& s) {
        std::vector<std::wstring> result;
        std::wstring cur;

        auto flush = [&]{
            if (cur.empty()) return;
            if (cur[0] != L'.') cur = L"." + cur;
            result.push_back(util::to_lower_ascii(cur));
            cur.clear();
        };

        for (wchar_t c : s) {
            if (c == L',' || c == L';' || c == L' ') flush();
            else cur += c;
        }
        flush();
        return result;
    }

    // ── 헬퍼: glob 옵션 구성 ─────────────────────────────

    fnutil::glob_options make_glob_options() const {
        fnutil::glob_options opt;
        opt.recursive           = get_bool(L"--recursive") ||
                                  get_bool(L"-R") ||
                                  get_bool(L"-r");
        opt.ignoreCase          = !(has(L"--case-sensitive") || has(L"-C"));
        opt.include_directories = get_bool(L"--include-dirs") || get_bool(L"-D");
        opt.absolute            = !get_bool(L"--relative");
        return opt;
    }
};

#endif // ARGS_HPP