# termlib

the terminal command line libs

언어: C++ (100%)

이 저장소는 터미널 UI 구성, ANSI 렌더링/최적화, 유니코드/그레이프헴 처리 및 오디오 재생(BASS 라이브러리 래퍼) 등을 포함한 여러 유틸리티 헤더 파일들의 모음입니다. 플랫폼에 따라 POSIX / Windows 입력 처리를 제공하며, TUI(Dialog) 구성요소, 스피너, 문자열/유니코드 도움 함수 등 다양하게 포함되어 있습니다.

## 주요 목적
- 터미널(콘솔) 기반 UI/렌더링 도구 제공
- 유니코드 폭(문자 폭) 및 그레이프헴(그랩헴) 처리 제공
- ANSI 시퀀스 최적화 및 렌더러 제공
- 오디오 재생을 위한 un4seen BASS C++ 래퍼 제공
- 커맨드라인 인자 처리, 로깅, 인코딩, 문자열 유틸리티 등 부가 기능 제공

---

## 의존성(주요)
- C++17 이상 권장
- (옵션) un4seen BASS 라이브러리 및 해당 플러그인들 — `bass3.hpp` 사용 시 필요
  - Windows에서 BASS를 사용하는 코드가 포함되어 있습니다 (`windows.h` 사용).
  - BASS 플러그인(DLL)들이 있는 경우, 적절한 경로에 두고 로드됩니다.
- 빌드 예시: 일반적인 헤더-온리 조합이지만, BASS 사용 시 BASS 라이브러리 링크가 필요합니다.

---

## 저장소 파일(주요 헤더)
다음은 저장소에 포함된 주요 파일/디렉터리 목록과 간단한 설명입니다.

Top-level headers:
- `ansi.hpp` — (ANSI 관련) 콘솔 출력/색상/시퀀스 관련 유틸리티 제공 (ANSI 제어 시퀀스 처리 등).
- `args.hpp` — 커맨드라인 인자 파싱 유틸리티 (간단한 옵션/플래그 처리 기능 제공).
- `bass3.hpp` — un4seen BASS 오디오 라이브러리와 플러그인을 다루는 C++ 래퍼. 포맷 자동 로더, 트래커 모듈, MIDI 사운드폰트 관리, Song 클래스 등 포함.
- `conf.hpp` — 설정/구성 관련 헬퍼(설정 파일/값 처리 등).
- `console.hpp` — 콘솔 제어 관련 함수 및 유틸리티(커서, 색상, 화면 조작 등).
- `encode.hpp` — 인코딩/디코딩 관련 유틸리티(예: base64, url-encoding 등 — 구현에 따라 다름).
- `fnutil.hpp` — 범용 함수 유틸리티(함수형 헬퍼, 래퍼 등).
- `logger.hpp` — 로그 출력을 위한 간단한 로거(로그 레벨, 포맷 등).
- `str.hpp` — 문자열 관련 헬퍼(트림, 분할, 포맷 등).
- `tui.hpp` — 터미널 UI(콘트롤/위젯) 관련 상위 유틸리티(간단한 TUI 구축용).
- `util.hpp` — 기타 유틸리티(작은 헬퍼 함수들)

`term/` 디렉터리(터미널 관련 세부 구현):
- `term/ansi_optimizer.hpp` — ANSI 시퀀스(출력 스트림) 최적화 기능(중복 시퀀스 병합 등).
- `term/ansi_renderer.hpp` — 실제 ANSI 렌더링 엔진(문자열 → 콘솔 출력 렌더링 로직).
- `term/ansi_screen.hpp` — 스크린 버퍼 모델(화면 버퍼 관리, 부분 렌더링).
- `term/ansi_string.hpp` — ANSI 시퀀스를 포함하는 문자열의 조작과 측정(길이, 서브스트링 등).
- `term/ansi_tui.hpp` — TUI 기본 구성요소(위젯/레이아웃/렌더 흐름).
- `term/ansi_tui_dlg.hpp` — 다이얼로그/폼 같은 고수준 TUI 컴포넌트 모음.
- `term/input.hpp` — 추상 입력 인터페이스(키/마우스 이벤트 등).
- `term/posix_input.hpp` — POSIX (Unix/Linux/macOS) 용 입력 처리 구현.
- `term/windows_input.hpp` — Windows 콘솔 입력 처리 구현.
- `term/platform.hpp` — 플랫폼별 정의/유틸(컴파일 조건 등).
- `term/spinner.hpp` — 콘솔 로딩 스피너/프로그레스 표시 유틸리티.
- `term/term.hpp` — 공통 터미널 래퍼(간략한 플랫폼 선택 등).
- `term/uni_string.hpp` — 유니코드 문자열 유틸리티(그레이프헴/문자 폭 연산과 관련된 함수).
- `term/unicode_grapheme_table.hpp` — 그레이프헴(segment) 테이블/데이터(문자 길이 계산에 사용하는 표).
- `term/unicode_width_table.hpp` — 유니코드 문자 폭(예: 이모지, 동아시아 문자 폭 등) 테이블 데이터.
- `term/util.hpp` — term 공간의 보조 유틸리티 함수

> 주: 실제 각 헤더의 세부 함수/클래스는 파일 내용을 확인해 사용하세요. (특히 `bass3.hpp` 처럼 구현이 상세한 헤더는 구체적 API를 제공합니다.)

---

## 상세 사용법 (예제)

아래 예제들은 저장소에 있는 헤더의 일반적인 사용 패턴을 보여 줍니다. 실제 API 시그니처는 각 헤더를 열어 확인하세요.

1) `bass3.hpp` (BASS 래퍼) — 실제 코드 예제

```cpp
#include "bass3.hpp"
#include <iostream>

int main() {
    // 1) BASS 초기화 (디바이스: -1 = 기본, 샘플레이트 44100)
    if (!bass::init(-1, 44100, 0)) {
        std::wcerr << L"Failed to init BASS: " << bass::get_error_string() << std::endl;
        return 1;
    }

    // 2) 사운드폰트(SF2) 로드(필요한 경우)
    // bass::load_soundfont(L"example.sf2");

    // 3) Song 객체로 파일 로드 및 재생
    bass::Song song(L"example.mp3");
    if (!song.is_valid()) {
        std::wcerr << L"Failed to load file: " << bass::get_error_string() << std::endl;
        bass::free();
        return 1;
    }

    song.play(true); // 재생 시작(처음부터)

    // (간단 대기 루프)
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 정리
    song.stop();
    bass::free();
    return 0;
}
```
- 제공 기능(파일 `bass3.hpp` 기준):
  - `bass::init(device, freq, flags)` — BASS 초기화
  - `bass::free()` — 리소스 해제
  - `bass::load_soundfont(path)` — MIDI용 사운드폰트 로드
  - `bass::Song` 클래스 — 파일 로드, play/stop/pause, 볼륨, 위치, FFT 등
  - `bass::extensions()` / `bass::descriptions()` — 지원 확장자/설명 조회
  - `bass::get_error_string(code)` — BASS 에러 문자열

> 주의: 위 예제에서는 `std::this_thread::sleep_for`를 사용하므로 `<thread>`와 `<chrono>`가 필요합니다.

2) 터미널(ANSI / TUI) 관련 사용 예시 (패턴)
- 기본 패턴:
  - `#include "term/ansi_screen.hpp"` 및 `#include "term/ansi_renderer.hpp"`
  - 스크린 버퍼를 만들고, 렌더러로 텍스트 및 위젯을 그린 다음 화면에 반영합니다.
- 입력 처리:
  - `#include "term/input.hpp"`와 플랫폼별 구현(`posix_input.hpp` 또는 `windows_input.hpp`)을 이용하여 키 입력을 폴링하거나 이벤트 루프를 구성합니다.
- TUI 다이얼로그:
  - `ansi_tui.hpp` / `ansi_tui_dlg.hpp`의 위젯/다이얼로그 빌더를 사용해 빠르게 폼/메뉴를 구성할 수 있습니다.
- 유니코드/문자폭:
  - `uni_string.hpp`, `unicode_width_table.hpp`, `unicode_grapheme_table.hpp`를 사용하여 유니코드 문자(특히 이모지/복합문자)의 실제 화면 폭(너비)을 계산하여 정렬/레이아웃에 반영하세요.

3) `logger.hpp` (간단 사용 예)
```cpp
#include "logger.hpp"

int main() {
    // 간단히 로그 레벨 설정 및 출력 (구체적인 API는 헤더 확인)
    // logger::init(...);
    // logger::info("프로그램 시작");
    // logger::error("문제 발생: %s", message.c_str());
}
```

4) `args.hpp`, `conf.hpp`, `str.hpp`, `encode.hpp` 등
- `args.hpp`: 커맨드라인 인자 파싱(옵션/플래그/값)을 제공하는 유틸리티입니다. `--help` 처리 및 기본값 지정에 사용하세요.
- `conf.hpp`: 간단한 설정 파일 로드/저장 혹은 런타임 구성 접근 헬퍼를 제공합니다.
- `str.hpp`: 문자열 트림, 분할, 포맷, 변환 등 반복되는 문자열 작업을 편리하게 해줍니다.
- `encode.hpp`: 인코딩/디코딩 관련 헬퍼(예: base64) — 네트워크/데이터 직렬화 시 편리합니다.

---

## 빌드/컴파일 가이드(예시)
- 헤더 온리(대부분 헤더 파일만 사용)인 경우:
  - g++/clang++: `g++ -std=c++17 -I./ -O2 your_program.cpp -o your_program`
- `bass3.hpp` 등을 사용하고 BASS에 링크해야 할 경우 (Windows 예시):
  - 링커에 BASS 라이브러리 추가 및 DLL 배포 필요
  - Visual Studio 프로젝트에서 BASS 라이브러리(.lib) 링크, DLL은 실행 폴더에 두기

---

## 테스트 및 예제
- 저장소에는 예제 프로그램이 포함되어 있지 않다면, 간단한 테스트 파일을 만들어서 각 모듈(예: TUI 렌더링, BASS 재생, logger, args) 동작을 확인하세요.
- BASS 관련 테스트 시에는 실제 오디오 디바이스와 플러그인 DLL, 사운드폰트 등이 필요할 수 있습니다.

---

## 기여
- 버그 리포트, 사용성 개선, 예제 코드 추가 환영합니다.
- PR을 보낼 때는 관련 헤더의 간단한 예제(작동 확인 가능한 코드)와 플랫폼(Windows/Linux/macOS)을 명시해 주세요.

---

## 라이선스
- (저장소에 명시된 라이선스가 있으면 여기에 적으세요. 현재 README 초안에는 라이선스 정보가 없습니다.)