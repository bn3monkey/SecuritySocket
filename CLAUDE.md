# SecuritySocket — 프로젝트 지침

## MSVC 빌드 (Windows)

MSVC 빌드는 **항상 프로젝트 스크립트**를 사용한다. `d:\claude_config`의 전역 스크립트가 아니라 아래 절차를 따른다:

1. **빌드 스크립트 생성** — 머신별 절대 경로(VsDevCmd.bat / cmake.exe / 빌드 디렉토리)를 자동 탐색해 `script/build.sh`를 생성한다. `script/build.sh`는 `.gitignore`로 제외되므로 머신마다 안전하다.
   ```bash
   ./script/generate-msvc-build-script.sh
   ```
   환경(VS 경로 등)이 바뀌었거나 `build.sh`가 없으면 다시 실행한다.

2. **빌드** — 인자 없이 실행하면 기존 CMake 캐시로 **증분 빌드**한다.
   ```bash
   ./script/build.sh
   ```
   - `--clean` : 빌드 디렉토리 전체 삭제(빌드 안 함).
   - `--rebuild` : clean 후 재구성+빌드.

### 주의
- **`--rebuild`는 가급적 피한다.** 재구성 시 OpenSSL을 FetchContent로 다시 받는데, 빌드 스크립트가 PATH를 sanitize하면서 `git`이 없어 FetchContent가 깨진다. deps가 이미 받아진 상태에서는 **인자 없는 증분 빌드**가 안전하다.
- **새 소스 파일을 새 디렉토리에 추가**한 경우, `file(GLOB)` 결과가 캐시되어 빌드에 반영되지 않을 수 있다. 새 `.cpp`를 꼭 컴파일해야 하면 `CMakeLists.txt`를 touch하거나(권장) 부득이하면 `--rebuild`(위 FetchContent 주의 감안).
- 헤더 변경은 Ninja가 의존성으로 추적하므로 증분 빌드로 충분히 반영된다.

## 테스트 실행 (gtest)

테스트는 `securitysockettest` DLL(gtest 케이스)로 빌드되고, **실행 가능한 러너는 `test/msvc`** 의 별도 CMake 프로젝트(`SecuritySocketMSVCTest.exe`, `main.cpp` → `startSecuritySocketTest`)다. 절차:

1. `test/msvc`용 빌드 스크립트를 한 번 생성한다(기존 빌드 디렉토리 `test/msvc/out/build/x64-Debug`를 그대로 사용 → FetchContent 재실행 없음):
   ```bash
   ./script/generate-msvc-build-script.sh \
     --project-root /d/repo/SecuritySocket/test/msvc \
     --build-dir   /d/repo/SecuritySocket/test/msvc/out/build/x64-Debug \
     --output      ./script/build_msvctest.sh
   ./script/build_msvctest.sh
   ```
2. 러너 실행 (DLL이 같은 디렉토리에 있으므로 그 폴더에서 실행):
   ```bash
   cd test/msvc/out/build/x64-Debug && ./SecuritySocketMSVCTest.exe
   ```
   - 특정 스위트만: `./SecuritySocketMSVCTest.exe --gtest_filter='HttpRouter*'`
     (단, `startSecuritySocketTest`가 자체 기본 필터를 둘 수 있어 무시될 수 있다 — 그 경우 전체 실행 후 출력에서 해당 스위트 결과를 확인한다.)
   - TCP/TLS/WebSocket 스위트는 포트를 바인딩한다(`script/allow_test_port.ps1` 참고).

생성되는 `script/build*.sh`는 머신별 절대경로라 `.gitignore` 처리되어 있다.

## 코드 구조 메모
- `src/implementation/core/` : pool/arena/free_stack(memory), intrusive tree hook(hook), SegmentTrie(trie) 등 자료구조 기반.
- `src/implementation/http/HttpRouter.*` : 메서드별 `SegmentTrie` + per-method action 컨테이너 기반 라우터. 공개 표면(`MatchResult`/`PathParamView`/`Method`/`parseMethod`)은 `test/securitysockettest_http_router.cpp`의 계약이므로 유지한다.
