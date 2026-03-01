/**
 * @file bass3.hpp
 * @brief un4seen BASS 오디오 라이브러리 및 확장 플러그인을 위한 C++ 래퍼(Wrapper)입니다.
 *
 * 이 헤더는 BASS 라이브러리와 다양한 플러그인(FLAC, MIDI, WMA 등)의 초기화, 로딩, 재생을 단순화합니다.
 * 확장자에 따른 적절한 로더 자동 선택, 트래커 모듈 지원, MIDI 사운드폰트 관리 등의 기능을 제공합니다.
 */

#ifndef BASS3_HPP
#define BASS3_HPP

#include "bass/bass.h"
#include "bass/bassalac.h"
#include "bass/bassape.h"
#include "bass/bassflac.h"
#include "bass/basshls.h"
#include "bass/bassmidi.h"
#include "bass/bassopus.h"
#include "bass/basswebm.h"
#include "bass/basswma.h"
#include "bass/basswv.h"
#include "bass/bassmix.h"
#include "bass/bass_fx.h"
#include "bass/bass_aac.h"
#include "bass/bass_ac3.h"
#include "bass/bass_mpc.h"
#include "bass/basszxtune.h"

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <algorithm>
#include <stdexcept>
#include <windows.h>    // MultiByteToWideChar 때문에 필수
#include <cwctype>      // std::towlower 때문에 필요

namespace bass {

/**
 * @brief 파일 경로와 플래그를 받아 스트림 핸들을 반환하는 로더 함수의 타입 정의입니다.
 * @param filepath 파일 경로 (Wide String)
 * @param flags BASS 스트림 생성 플래그
 * @return 생성된 스트림 핸들 (HSTREAM), 실패 시 0
 */
using LoaderFunc = std::function<HSTREAM(const std::wstring&, DWORD)>;

/**
 * @brief BASS 플러그인 정보를 담는 구조체입니다.
 */
struct PluginMap {
    std::string dll_name;           ///< 플러그인 DLL 파일 이름 (예: "bassflac.dll")
    std::vector<std::string> extensions; ///< 지원하는 파일 확장자 목록
    LoaderFunc loader;              ///< 해당 포맷을 로드할 때 사용할 사용자 정의 로더 함수 (선택 사항)
};

/**
 * @brief 지원하는 파일 포맷의 정보를 나타내는 구조체입니다.
 */
struct FormatDescription {
    std::wstring extension;   ///< 파일 확장자 (예: L"mp3")
    std::wstring description; ///< 포맷 설명 (예: L"MPEG Audio Layer 3")

    FormatDescription(const std::wstring& ext, const std::wstring& desc)
        : extension(ext), description(desc) {}
};

// 헤더 내에서 한 개의 인스턴스만 보장됩니다.
inline std::map<std::wstring, LoaderFunc> g_loaders;
inline std::vector<std::wstring> g_extensions;
inline std::vector<HPLUGIN> g_loaded_plugins;
inline HSOUNDFONT g_soundfont = 0;
inline bool g_initialized = false;

/**
 * @brief 기본 스트림 로더입니다. BASS 내장 포맷(MP3, OGG, WAV 등) 및 플러그인 통합 포맷을 로드합니다.
 * @param filepath 파일 경로
 * @param flags BASS 플래그
 * @return 스트림 핸들
 */
inline HSTREAM default_loader(const std::wstring& filepath, DWORD flags) {
    return BASS_StreamCreateFile(FALSE, filepath.c_str(), 0, 0, flags | BASS_UNICODE);
}

/**
 * @brief 트래커 모듈(MOD, XM, IT 등)을 위한 로더입니다. BASS_MusicLoad를 사용합니다.
 * @param filepath 파일 경로
 * @param flags BASS 플래그
 * @return 뮤직 핸들 (HSTREAM으로 캐스팅됨)
 */
inline HSTREAM music_loader(const std::wstring& filepath, DWORD flags) {
    return BASS_MusicLoad(FALSE, filepath.c_str(), 0, 0, flags | BASS_UNICODE, 0);
}

/**
 * @brief 시스템에 로드할 플러그인 목록과 설정을 반환합니다.
 *
 * 이 함수는 지원할 플러그인 DLL 이름, 파일 확장자, 그리고 특별한 로딩 로직이 필요한 경우
 * 커스텀 로더 함수를 정의하여 반환합니다.
 *
 * @return 플러그인 설정 벡터
 */
inline std::vector<PluginMap> get_plugins() {
    return {
        {"bass_aac.dll", {"aac","m4a","mp4"}, nullptr},
        {"bass_ac3.dll", {"ac3"}, nullptr},
        {"bassalac.dll", {"alac","m4a"}, nullptr},
        {"bassape.dll", {"ape"}, nullptr},
        {"bassflac.dll", {"flac"}, nullptr},
        {"bassmidi.dll", {"mid","midi","rmi"}, 
            [](const std::wstring& f, DWORD fl) { 
                return BASS_MIDI_StreamCreateFile(FALSE, f.c_str(), 0, 0, fl, 1); 
            }
        },
        {"bassopus.dll", {"opus"}, nullptr},
        {"basswebm.dll", {"webm","mkv","mka"}, nullptr},
        {"basswma.dll", {"wma"}, nullptr},
        {"basswv.dll", {"wv"}, nullptr},
        {"bass_mpc.dll", {"mpc"}, nullptr},
        {"basshls.dll", {"m3u8","hls"}, nullptr},
        {"bassmix.dll", {}, nullptr},
        {"bass_fx.dll", {}, nullptr},
        {"basszxtune.dll", {
            "$b","$m","2sf","ahx","as0","asc","ay","ayc","bin","cc3","chi","cop",
            "d","dmm","dsf","dsq","dst","esv","fdi","ftc","gam","gamplus","gbs","gsf","gtr","gym",
            "hes","hrm","hrp","hvl","kss","lzs","m","mod","msp","mtc","nsf","nsfe", 
            "p","pcd","psc","psf","psf2","psg","psm","pt1","pt2","pt3","rmt","rsn", 
            "s","s98","sap","scl","sid","spc","sqd","sqt","ssf","st1","st3","stc","stp","str","szx",
            "td0","tf0","tfc","tfd","tfe","tlz","tlzp","trd","trs","ts","usef","v2m","vgm","vgz","vtx","ym"}, nullptr}
        };
}

/**
 * @brief BASS 에러 코드를 설명하는 와이드 문자열을 반환합니다.
 * @param code BASS 에러 코드 (기본값: -1, 현재 발생한 에러 코드 자동 조회)
 * @return 에러 설명 문자열
 */
inline std::wstring get_error_string(int code = -1) {
    if (code == -1) code = BASS_ErrorGetCode();
    
    switch (code) {
        case BASS_OK: return L"정상";
        case BASS_ERROR_MEM: return L"메모리 부족";
        case BASS_ERROR_FILEOPEN: return L"파일을 열 수 없음";
        case BASS_ERROR_DRIVER: return L"드라이버를 사용할 수 없음";
        case BASS_ERROR_BUFLOST: return L"버퍼 손실";
        case BASS_ERROR_HANDLE: return L"잘못된 핸들";
        case BASS_ERROR_FORMAT: return L"지원되지 않는 포맷";
        case BASS_ERROR_POSITION: return L"잘못된 위치";
        case BASS_ERROR_INIT: return L"BASS_Init가 호출되지 않음";
        case BASS_ERROR_START: return L"시작 실패";
        case BASS_ERROR_ALREADY: return L"이미 초기화됨";
        case BASS_ERROR_NOCHAN: return L"사용 가능한 채널이 없음";
        case BASS_ERROR_ILLTYPE: return L"잘못된 타입";
        case BASS_ERROR_ILLPARAM: return L"잘못된 파라미터";
        case BASS_ERROR_NO3D: return L"3D 지원 없음";
        case BASS_ERROR_NOEAX: return L"EAX 지원 없음";
        case BASS_ERROR_DEVICE: return L"잘못된 디바이스 번호";
        case BASS_ERROR_NOPLAY: return L"재생 중이 아님";
        case BASS_ERROR_FREQ: return L"잘못된 샘플레이트";
        case BASS_ERROR_NOTFILE: return L"스트림이 파일이 아님";
        case BASS_ERROR_NOHW: return L"하드웨어 지원 없음";
        case BASS_ERROR_EMPTY: return L"빈 파일";
        case BASS_ERROR_NONET: return L"네트워크 연결 없음";
        case BASS_ERROR_CREATE: return L"생성 실패";
        case BASS_ERROR_NOFX: return L"효과를 사용할 수 없음";
        case BASS_ERROR_NOTAVAIL: return L"사용 불가";
        case BASS_ERROR_DECODE: return L"디코드 전용 채널";
        case BASS_ERROR_DX: return L"DirectX 에러";
        case BASS_ERROR_TIMEOUT: return L"타임아웃";
        case BASS_ERROR_FILEFORM: return L"잘못된 파일 형식";
        case BASS_ERROR_SPEAKER: return L"사용 불가능한 스피커";
        case BASS_ERROR_VERSION: return L"잘못된 BASS 버전";
        case BASS_ERROR_CODEC: return L"코덱을 사용할 수 없음";
        case BASS_ERROR_ENDED: return L"스트림 종료";
        case BASS_ERROR_BUSY: return L"사용 중";
        case BASS_ERROR_UNKNOWN: return L"알 수 없는 에러";
        default: return L"알 수 없는 에러 코드: " + std::to_wstring(code);
    }
}

/**
 * @brief UTF-8 문자열을 와이드 문자열(wstring)로 변환합니다.
 * @param str UTF-8 문자열
 * @return 변환된 wstring
 */
inline std::wstring to_wstring(const std::string& str) {
    if (str.empty()) return L"";
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, NULL, 0);
    std::wstring wstr(size_needed - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &wstr[0], size_needed);
    return wstr;
}

/**
 * @brief BASS 라이브러리와 플러그인을 초기화합니다.
 *
 * 오디오 디바이스를 초기화하고, 설정된 플러그인들을 로드하여
 * 지원 파일 포맷 맵(loader map)을 구축합니다.
 *
 * @param device 사용할 오디오 디바이스 번호 (-1: 기본 디바이스)
 * @param freq 출력 샘플 레이트 (기본값: 44100)
 * @param flags 초기화 플래그 (기본값: 0)
 * @return 초기화 성공 여부
 */
inline bool init(int device = -1, DWORD freq = 44100, DWORD flags = 0) {
    if (g_initialized) return true;
    
    if (!BASS_Init(device, freq, flags, 0, NULL)) {
        return false;
    }
    
    // BASS 내장 포맷 등록 (MP3, OGG, WAV, AIFF)
    std::vector<std::wstring> builtin_exts = {L"mp3", L"mp2", L"mp1", L"ogg", L"wav", L"aiff"};
    for (const auto& ext : builtin_exts) {
        g_loaders[ext] = default_loader;
        g_extensions.push_back(ext);
    }
    
    // 트래커 모듈 포맷 등록 (BASS_MusicLoad 사용)
    std::vector<std::wstring> tracker_exts = {
        L"mod", L"mtm", L"s3m", L"xm", L"it", L"umx", L"mo3",
        L"669", L"amf", L"dsm", L"far", L"gdm", L"med", L"okt",
        L"ptm", L"stm", L"ult", L"apun", L"imf", L"digi", L"dtm",
        L"psm", L"j2b", L"mdl", L"mt2", L"plm", L"rtm", L"st26",
        L"st3", L"wow"
    };
    for (const auto& ext : tracker_exts) {
        g_loaders[ext] = music_loader;
        g_extensions.push_back(ext);
    }
    
    // 플러그인 로드
    auto plugins = get_plugins();
    for (const auto& plugin : plugins) {
        HPLUGIN hplugin = BASS_PluginLoad(plugin.dll_name.c_str(), 0);
        
        if (hplugin) {
            g_loaded_plugins.push_back(hplugin);
            
            for (const auto& ext : plugin.extensions) {
                std::wstring wext = to_wstring(ext);
                
                // 전용 로더가 있으면 사용, 없으면 default_loader 사용 (플러그인 자동 처리)
                g_loaders[wext] = plugin.loader ? plugin.loader : default_loader;
                
                if (std::find(g_extensions.begin(), g_extensions.end(), wext) == g_extensions.end()) {
                    g_extensions.push_back(wext);
                }
            }
        }
    }
    
    g_initialized = true;
    return true;
}

/**
 * @brief BASS 라이브러리와 로드된 리소스를 해제합니다.
 *
 * 사운드폰트, 플러그인, BASS 엔진을 순차적으로 해제합니다.
 */
inline void free() {
    if (g_soundfont) {
        BASS_MIDI_FontFree(g_soundfont);
        g_soundfont = 0;
    }
    
    for (auto plugin : g_loaded_plugins) {
        BASS_PluginFree(plugin);
    }
    g_loaded_plugins.clear();
    
    if (g_initialized) {
        BASS_Free();
        g_initialized = false;
    }
    
    g_loaders.clear();
    g_extensions.clear();
}

/**
 * @brief 사용할 오디오 디바이스를 변경하고 시스템을 재초기화합니다.
 * @param device 새로운 디바이스 번호
 * @param freq 샘플 레이트
 * @param flags 초기화 플래그
 * @return 설정 성공 여부
 */
inline bool setup_device(int device, DWORD freq = 44100, DWORD flags = 0) {
    if (g_initialized) {
        free();
    }
    return init(device, freq, flags);
}

/**
 * @brief 특정 확장자에 대한 사용자 정의 로더를 설정합니다.
 * @param ext 파일 확장자 (예: L"custom")
 * @param loader 로더 함수
 */
inline void set_loader(const std::wstring& ext, LoaderFunc loader) {
    g_loaders[ext] = loader;
    if (std::find(g_extensions.begin(), g_extensions.end(), ext) == g_extensions.end()) {
        g_extensions.push_back(ext);
    }
}

/**
 * @brief 특정 확장자에 대한 로더 함수를 반환합니다.
 * @param ext 파일 확장자
 * @return 로더 함수 (없을 경우 기본 로더 반환)
 */
inline LoaderFunc get_loader(const std::wstring& ext) {
    auto it = g_loaders.find(ext);
    if (it != g_loaders.end()) {
        return it->second;
    }
    return default_loader;
}

/**
 * @brief MIDI 재생을 위한 사운드폰트(SF2)를 로드합니다.
 * @param path 사운드폰트 파일 경로
 * @return 로드 성공 여부
 */
inline bool load_soundfont(const wchar_t* path) {
    if (g_soundfont) {
        BASS_MIDI_FontFree(g_soundfont);
        g_soundfont = 0;
    }
    
    g_soundfont = BASS_MIDI_FontInit(path, BASS_UNICODE);
    return g_soundfont != 0;
}

/**
 * @brief 현재 지원하는 파일 확장자 목록을 반환합니다.
 * @return 확장자 문자열 벡터
 */
inline std::vector<std::wstring> extensions() {
    return g_extensions;
}

/**
 * @brief 지원하는 파일 포맷의 확장자와 설명 목록을 반환합니다.
 *
 * UI의 파일 필터나 정보 표시 등에 사용할 수 있습니다.
 *
 * @return 포맷 설명 벡터
 */
inline std::vector<FormatDescription> descriptions() {
    std::vector<FormatDescription> result;
    
    // BASS 내장 포맷
    result.emplace_back(L"mp3", L"MPEG Audio Layer 3");
    result.emplace_back(L"mp2", L"MPEG Audio Layer 2");
    result.emplace_back(L"mp1", L"MPEG Audio Layer 1");
    result.emplace_back(L"ogg", L"Ogg Vorbis");
    result.emplace_back(L"wav", L"Waveform Audio");
    result.emplace_back(L"aiff", L"Audio Interchange File Format");
    
    // 트래커 모듈 포맷 (BASS_MusicLoad)
    result.emplace_back(L"mod", L"Amiga ProTracker Module");
    result.emplace_back(L"mtm", L"MultiTracker Module");
    result.emplace_back(L"s3m", L"Scream Tracker 3 Module");
    result.emplace_back(L"xm", L"FastTracker 2 Extended Module");
    result.emplace_back(L"it", L"Impulse Tracker Module");
    result.emplace_back(L"umx", L"Unreal Music Package");
    result.emplace_back(L"mo3", L"Compressed Module");
    result.emplace_back(L"669", L"Composer 669 / UNIS 669 Module");
    result.emplace_back(L"amf", L"ASYLUM/Advanced Module Format");
    result.emplace_back(L"dsm", L"DSIK Internal Format");
    result.emplace_back(L"far", L"Farandole Composer Module");
    result.emplace_back(L"gdm", L"General DigiMusic Module");
    result.emplace_back(L"med", L"OctaMED Module");
    result.emplace_back(L"okt", L"Oktalyzer Module");
    result.emplace_back(L"ptm", L"PolyTracker Module");
    result.emplace_back(L"stm", L"Scream Tracker 2 Module");
    result.emplace_back(L"ult", L"UltraTracker Module");
    result.emplace_back(L"apun", L"APlayer Module");
    result.emplace_back(L"imf", L"Imago Orpheus Module");
    result.emplace_back(L"digi", L"DigiTrakker Module");
    result.emplace_back(L"dtm", L"Digital Tracker Module");
    result.emplace_back(L"j2b", L"Janus 2b Module");
    result.emplace_back(L"mdl", L"DigiTracker Module");
    result.emplace_back(L"mt2", L"MadTracker 2 Module");
    result.emplace_back(L"plm", L"Protracker Lightning Module");
    result.emplace_back(L"psm", L"Protracker Studio Module");
    result.emplace_back(L"rtm", L"RealTracker Module");
    result.emplace_back(L"st26", L"Scream Tracker 2.6 Module");
    result.emplace_back(L"st3", L"Scream Tracker 1-2 Module");
    result.emplace_back(L"wow", L"Grave Composer Module");
    
    // 플러그인 포맷
    result.emplace_back(L"aac", L"Advanced Audio Coding");
    result.emplace_back(L"m4a", L"MPEG-4 Audio / Apple Lossless");
    result.emplace_back(L"mp4", L"MPEG-4 Audio");
    result.emplace_back(L"ac3", L"Dolby Digital AC-3");
    result.emplace_back(L"alac", L"Apple Lossless Audio Codec");
    result.emplace_back(L"ape", L"Monkey's Audio");
    result.emplace_back(L"flac", L"Free Lossless Audio Codec");
    result.emplace_back(L"mid", L"MIDI Sequence");
    result.emplace_back(L"midi", L"MIDI Sequence");
    result.emplace_back(L"rmi", L"RIFF MIDI");
    result.emplace_back(L"opus", L"Opus Audio Codec");
    result.emplace_back(L"webm", L"WebM Audio");
    result.emplace_back(L"mkv", L"Matroska Video (Audio)");
    result.emplace_back(L"mka", L"Matroska Audio");
    result.emplace_back(L"wma", L"Windows Media Audio");
    result.emplace_back(L"wv", L"WavPack");
    result.emplace_back(L"mpc", L"Musepack");
    result.emplace_back(L"m3u8", L"HTTP Live Streaming");
    result.emplace_back(L"hls", L"HTTP Live Streaming");
    
    // ZXTune 플러그인 포맷 (레트로 게임/칩튠)
    result.emplace_back(L"$b", L"ZXTune Bank");
    result.emplace_back(L"$m", L"ZXTune Music");
    result.emplace_back(L"2sf", L"Nintendo DS Sound Format");
    result.emplace_back(L"ahx", L"Abyss' Highest eXperience");
    result.emplace_back(L"as0", L"Atari SAP Type 0");
    result.emplace_back(L"asc", L"ASCII Text Music");
    result.emplace_back(L"ay", L"Amstrad/ZX Spectrum AY");
    result.emplace_back(L"ayc", L"Amstrad CPC AY");
    result.emplace_back(L"bin", L"Binary Module");
    result.emplace_back(L"cc3", L"Chris Huelsbeck CC3");
    result.emplace_back(L"chi", L"Chip Tracker");
    result.emplace_back(L"cop", L"Coprocessor Music");
    result.emplace_back(L"d", L"Atari D Music");
    result.emplace_back(L"dmm", L"Digital Music Module");
    result.emplace_back(L"dsf", L"DreamStation Format");
    result.emplace_back(L"dsq", L"DSQ Module");
    result.emplace_back(L"dst", L"Digital Symphony Tracker");
    result.emplace_back(L"esv", L"ESV Tracker");
    result.emplace_back(L"fdi", L"FDI Disk Image");
    result.emplace_back(L"ftc", L"FTC Module");
    result.emplace_back(L"gam", L"Game Music");
    result.emplace_back(L"gamplus", L"Game Music Plus");
    result.emplace_back(L"gbs", L"Game Boy Sound System");
    result.emplace_back(L"gsf", L"Game Boy Advance Sound Format");
    result.emplace_back(L"gtr", L"Graoumf Tracker");
    result.emplace_back(L"gym", L"Genesis YM2612");
    result.emplace_back(L"hes", L"PC Engine Sound Format");
    result.emplace_back(L"hrm", L"HRM Module");
    result.emplace_back(L"hrp", L"HRP Module");
    result.emplace_back(L"hvl", L"Hively Tracker");
    result.emplace_back(L"kss", L"MSX Sound System");
    result.emplace_back(L"lzs", L"LZS Compressed");
    result.emplace_back(L"m", L"M Module");
    result.emplace_back(L"msp", L"Music Studio Pro");
    result.emplace_back(L"mtc", L"MTC Module");
    result.emplace_back(L"nsf", L"NES Sound Format");
    result.emplace_back(L"nsfe", L"NES Sound Format Extended");
    result.emplace_back(L"p", L"P Module");
    result.emplace_back(L"pcd", L"Photo CD Audio");
    result.emplace_back(L"psc", L"PSC Module");
    result.emplace_back(L"psf", L"PlayStation Sound Format");
    result.emplace_back(L"psf2", L"PlayStation 2 Sound Format");
    result.emplace_back(L"psg", L"PSG Module");
    result.emplace_back(L"pt1", L"Pro Tracker 1");
    result.emplace_back(L"pt2", L"Pro Tracker 2");
    result.emplace_back(L"pt3", L"Pro Tracker 3");
    result.emplace_back(L"rmt", L"Raster Music Tracker");
    result.emplace_back(L"rsn", L"RSN Archive");
    result.emplace_back(L"s", L"S Module");
    result.emplace_back(L"s98", L"S98 Sound Format");
    result.emplace_back(L"sap", L"Slight Atari Player");
    result.emplace_back(L"scl", L"Scream Tracker Clone");
    result.emplace_back(L"sid", L"Commodore 64 SID");
    result.emplace_back(L"spc", L"SNES SPC700");
    result.emplace_back(L"sqd", L"SQD Tracker");
    result.emplace_back(L"sqt", L"SQT Tracker");
    result.emplace_back(L"ssf", L"Saturn Sound Format");
    result.emplace_back(L"st1", L"Scream Tracker 1");
    result.emplace_back(L"stc", L"STC Tracker");
    result.emplace_back(L"stp", L"STP Tracker");
    result.emplace_back(L"str", L"STR Tracker");
    result.emplace_back(L"szx", L"ZX Spectrum SZX");
    result.emplace_back(L"td0", L"TD0 Disk Image");
    result.emplace_back(L"tf0", L"TF0 Module");
    result.emplace_back(L"tfc", L"TFC Tracker");
    result.emplace_back(L"tfd", L"TFD Module");
    result.emplace_back(L"tfe", L"TFE Module");
    result.emplace_back(L"tlz", L"TLZ Compressed");
    result.emplace_back(L"tlzp", L"TLZP Compressed");
    result.emplace_back(L"trd", L"TRD Disk Image");
    result.emplace_back(L"trs", L"TRS Module");
    result.emplace_back(L"ts", L"TS Module");
    result.emplace_back(L"usf", L"Nintendo 64 Sound Format");
    result.emplace_back(L"usef", L"USEF Module");
    result.emplace_back(L"v2m", L"Farbrausch V2 Module");
    result.emplace_back(L"vgm", L"Video Game Music");
    result.emplace_back(L"vgz", L"Video Game Music (Compressed)");
    result.emplace_back(L"vtx", L"VTX Module");
    result.emplace_back(L"ym", L"YM Chip Music");
    
    return result;
}


/**
 * @class Song
 * @brief 오디오 파일 또는 트래커 모듈을 관리하고 재생하는 클래스입니다.
 *
 * 파일 로딩, 재생 제어, 볼륨 조절, 위치 이동, FFT 데이터 조회 등의 기능을 캡슐화합니다.
 * 이동 의미론(Move Semantics)을 지원하여 리소스 소유권을 안전하게 이전할 수 있습니다.
 */
class Song {
private:
    HSTREAM stream_;          ///< 오디오 스트림 핸들
    std::wstring path_;       ///< 파일 경로
    std::vector<HFX> effects_;///< 적용된 FX 효과 핸들 목록
    bool is_music_;           ///< 트래커 모듈 여부 (BASS_MusicLoad 사용 여부)

    /**
     * @brief 경로에서 확장자를 추출하여 소문자로 변환합니다.
     * @param path 파일 경로
     * @return 소문자 확장자
     */
    std::wstring get_extension(const std::wstring& path) {
        size_t pos = path.find_last_of(L'.');
        if (pos != std::wstring::npos) {
            std::wstring ext = path.substr(pos + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(),
                [](wchar_t c) { return std::towlower(c); });
            return ext;
        }
        return L"";
    }

public:
    /**
     * @brief 기본 생성자. 초기화되지 않은 상태의 Song 객체를 생성합니다.
     */
    Song() : stream_(0), is_music_(false) {}
    
    /**
     * @brief 파일 경로를 받아 객체를 생성하고 로드합니다.
     * @param path 파일 경로
     * @param flags BASS 로딩 플래그
     */
    Song(const wchar_t* path, DWORD flags = 0) : stream_(0), path_(path) {
        load(path, flags);
    }

    /**
     * @brief 이동 생성자. 다른 Song 객체의 리소스를 가져옵니다.
     * @param other 이동할 객체
     */
    Song(Song&& other) noexcept {
        *this = std::move(other);
    }

    /**
     * @brief 이동 대입 연산자.
     * @param other 이동할 객체
     * @return 참조
     */
    Song& operator=(Song&& other) noexcept {
        if (this != &other) {
            cleanup();
            stream_ = other.stream_;
            is_music_ = other.is_music_;
            effects_ = std::move(other.effects_);
            path_ = std::move(other.path_);
            other.stream_ = 0;
        }
        return *this;
    }

    /**
     * @brief 소멸자. 할당된 리소스를 정리합니다.
     */
    ~Song() {
        if (stream_) {
            stop();
            cleanup();
        }
    }

    /**
     * @brief 현재 스트림과 관련된 모든 리소스를 해제합니다.
     */
    void cleanup() {
        if (stream_) {
            for (auto fx : effects_) {
                BASS_ChannelRemoveFX(stream_, fx);
            }
            if (is_music_) BASS_MusicFree(stream_);
            else BASS_StreamFree(stream_);
            stream_ = 0;
        }
    }

    /**
     * @brief 오디오 파일을 로드합니다.
     *
     * 확장자를 확인하여 적절한 로더를 선택하고, MIDI 파일인 경우 사운드폰트를 적용합니다.
     *
     * @param path 파일 경로
     * @param flags BASS 플래그
     * @return 로드 성공 여부
     */
    bool load(const wchar_t* path, DWORD flags = 0) {
        if (stream_) {
            cleanup();
        }
        
        path_ = path;
        std::wstring ext = get_extension(path);
        
        is_music_ = (ext == L"mod" || ext == L"xm" || ext == L"it" || ext == L"s3m" || ext == L"mtm" || ext == L"umx" || ext == L"mo3");

        auto loader = get_loader(ext);
        stream_ = loader(path_, flags);
        
        if (stream_ && g_soundfont && (ext == L"mid" || ext == L"midi" || ext == L"rmi")) {
            BASS_MIDI_StreamSetFonts(stream_, &g_soundfont, 1);
        }
        
        return stream_ != 0;
    }
    
    /** @brief 유효한 스트림이 로드되었는지 확인합니다. */
    bool is_valid() const { return stream_ != 0; }
    
    /**
     * @brief 재생을 시작합니다.
     * @param restart true면 처음부터 다시 재생, false면 이어서 재생
     * @return 성공 여부
     */
    bool play(bool restart = false) {
        if (!stream_) return false;
        return BASS_ChannelPlay(stream_, restart ? TRUE : FALSE) != 0;
    }
    
    /**
     * @brief 재생을 중지합니다. 위치는 초기화되지 않습니다.
     * @return 성공 여부
     */
    bool stop() {
        if (!stream_) return false;
        return BASS_ChannelStop(stream_) != 0;
    }
    
    /**
     * @brief 재생을 일시 정지합니다.
     * @return 성공 여부
     */
    bool pause() {
        if (!stream_) return false;
        return BASS_ChannelPause(stream_) != 0;
    }
    
    /**
     * @brief 일시 정지된 재생을 재개합니다.
     * @return 성공 여부
     */
    bool resume() {
        if (!stream_) return false;
        return BASS_ChannelPlay(stream_, FALSE) != 0;
    }
    
    /** @brief 현재 재생 중인지 확인합니다. */
    bool is_playing() const {
        if (!stream_) return false;
        return BASS_ChannelIsActive(stream_) == BASS_ACTIVE_PLAYING;
    }
    
    /** @brief 일시 정지 상태인지 확인합니다. */
    bool is_paused() const {
        if (!stream_) return false;
        return BASS_ChannelIsActive(stream_) == BASS_ACTIVE_PAUSED;
    }
    
    /**
     * @brief 현재 재생 위치를 초 단위로 반환합니다.
     * @return 현재 위치 (초)
     */
    double get_position() const {
        if (!stream_) return 0.0;
        QWORD pos = BASS_ChannelGetPosition(stream_, BASS_POS_BYTE);
        return BASS_ChannelBytes2Seconds(stream_, pos);
    }
    
    /**
     * @brief 재생 위치를 설정합니다.
     * @param seconds 이동할 시간 (초)
     * @return 성공 여부
     */
    bool set_position(double seconds) {
        if (!stream_) return false;
        QWORD pos = BASS_ChannelSeconds2Bytes(stream_, seconds);
        return BASS_ChannelSetPosition(stream_, pos, BASS_POS_BYTE) != 0;
    }
    
    /**
     * @brief 재생 위치를 이동합니다 (Seek).
     * @param seconds 이동할 시간 (초)
     * @return 성공 여부
     */
    bool seek(double seconds) {
        if (!stream_) return false;
        QWORD pos = BASS_ChannelSeconds2Bytes(stream_, seconds);
        return BASS_ChannelSetPosition(stream_, pos, BASS_POS_BYTE) != 0;
    }
    
    /**
     * @brief 전체 재생 길이를 초 단위로 반환합니다.
     * @return 전체 길이 (초)
     */
    double get_length() const {
        if (!stream_) return 0.0;
        QWORD len = BASS_ChannelGetLength(stream_, BASS_POS_BYTE);
        return BASS_ChannelBytes2Seconds(stream_, len);
    }
    
    /**
     * @brief 볼륨을 설정합니다.
     * @param volume 볼륨 (0.0 ~ 1.0)
     * @return 성공 여부
     */
    bool set_volume(float volume) {
        if (!stream_) return false;
        return BASS_ChannelSetAttribute(stream_, BASS_ATTRIB_VOL, volume) != 0;
    }
    
    /**
     * @brief 현재 볼륨을 반환합니다.
     * @return 볼륨 (0.0 ~ 1.0)
     */
    float get_volume() const {
        if (!stream_) return 0.0f;
        float vol = 0.0f;
        BASS_ChannelGetAttribute(stream_, BASS_ATTRIB_VOL, &vol);
        return vol;
    }
    
    /**
     * @brief 채널에 DSP/EFX 효과를 적용합니다.
     * @param type 효과 타입 (BASS_FX_DX8 등)
     * @param priority 우선순위
     * @return 효과 핸들 (HFX)
     */
    HFX set_fx(DWORD type, int priority = 0) {
        if (!stream_) return 0;
        HFX fx = BASS_ChannelSetFX(stream_, type, priority);
        if (fx) effects_.push_back(fx);
        return fx;
    }
    
    /**
     * @brief 적용된 효과를 제거합니다.
     * @param fx 제거할 효과 핸들
     * @return 성공 여부
     */
    bool remove_fx(HFX fx) {
        if (!stream_) return false;
        auto it = std::find(effects_.begin(), effects_.end(), fx);
        if (it != effects_.end()) {
            effects_.erase(it);
        }
        return BASS_ChannelRemoveFX(stream_, fx) != 0;
    }
    
    /**
     * @brief 이 스트림에 특정 사운드폰트를 설정합니다.
     * @param font 사운드폰트 핸들
     * @return 성공 여부
     */
    bool set_soundfont(HSOUNDFONT font) {
        if (!stream_) return false;
        return BASS_MIDI_StreamSetFonts(stream_, &font, 1) != 0;
    }
    
    /**
     * @brief FFT 데이터를 가져옵니다 (시각화용).
     * @param fft 데이터를 저장할 float 배열 포인터
     * @param length 데이터 길이 (예: BASS_DATA_FFT512)
     * @return 성공 여부
     */
    bool get_fft(float* fft, DWORD length) const {
        if (!stream_) return false;
        return BASS_ChannelGetData(stream_, fft, length) != (DWORD)-1;
    }
    
    /** @brief 원시 스트림 핸들을 반환합니다. */
    HSTREAM get_handle() const { return stream_; }
    
    /** @brief 로드된 파일 경로를 반환합니다. */
    const std::wstring& get_path() const { return path_; }
};

} // namespace bass

#endif // BASS3_HPP