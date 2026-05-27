#!/usr/bin/env bash
# =============================================================================
# generate-build-script.sh
# -----------------------------------------------------------------------------
# MSVC + CMake 빌드 스크립트(build.sh)를 자동 생성한다.
#
# 자동 탐색 항목:
#   1) 프로젝트 루트          : 스크립트 상위에서 CMakeLists.txt 가 있는 디렉토리
#   2) VsDevCmd.bat           : "Program Files\Microsoft Visual Studio" 하위 검색
#                                (Launch-VsDevShell.ps1 은 일부 VS 18 Insiders
#                                빌드에서 깨져있음 — vswhere PowerShell 호출 실패.
#                                underlying batch 인 VsDevCmd.bat 는 안정적이라
#                                cmd /c 로 직접 호출한다.)
#   3) cmake.exe              : VS 번들 cmake (Common7\IDE\CommonExtensions\
#                                Microsoft\CMake\CMake\bin) 우선, 없으면 PATH
#                                fallback. 절대 경로로 build.sh 에 박는다.
#   4) CMake 빌드 디렉토리    : <project_root>/out/build/*/CMakeCache.txt 검색
#   5) 아키텍처               : 기본값 amd64 (인자로 override 가능)
#
# 출력:
#   - 기본 출력 경로 : <project_root>/script/build.sh
#     (이 경로는 .gitignore 로 제외되므로 머신마다 다른 절대 경로가 들어가도 안전)
#   - --output 옵션  : 임의 경로로 변경 가능
#
# 설계 의도(Claude 친화):
#   - 표준 출력의 모든 메시지에 [INFO]/[OK]/[WARN]/[ERROR] 접두사를 둔다.
#   - 인터랙티브 입력이 전혀 없다(에이전트가 그대로 실행 가능).
#   - 마지막 한 줄에 결과 경로만 단독 출력하여 파싱을 쉽게 한다.
#   - set -euo pipefail 로 즉시 실패시켜 무한 진행을 방지한다.
# =============================================================================

set -euo pipefail

# ----- 사용법 ----------------------------------------------------------------
usage() {
    cat <<'USAGE'
Usage: generate-build-script.sh [options]

  --output <path>      생성될 build.sh 경로 (기본: <project_root>/script/build.sh)
  --arch <arch>        대상 아키텍처 (기본: amd64; 예: amd64, x86, arm64)
  --vs-path <path>     VsDevCmd.bat 경로 직접 지정 (자동검색 건너뜀)
  --cmake-path <path>  cmake.exe 경로 직접 지정 (자동검색 건너뜀)
  --build-dir <path>   CMake 빌드 디렉토리 직접 지정 (자동검색 건너뜀)
  --project-root <p>   프로젝트 루트 직접 지정 (자동검색 건너뜀)
  -h, --help           이 도움말 표시

예시:
  ./script/generate-build-script.sh
  ./script/generate-build-script.sh --output ./script/build.sh
  ./script/generate-build-script.sh --cmake-path '/c/Program Files/CMake/bin/cmake.exe'
  ./script/generate-build-script.sh --build-dir /c/repo/out/build/x64-Release
USAGE
}

# ----- 기본값 ----------------------------------------------------------------
# OUTPUT 은 빈 문자열로 두고, 프로젝트 루트 검색이 끝난 뒤 기본 경로를 결정한다.
# (기본 경로 = <project_root>/script/build.sh — .gitignore 에 의해 추적되지 않음)
OUTPUT=""
ARCH="amd64"
VS_PATH=""        # VsDevCmd.bat 경로 — 비어있으면 자동검색
CMAKE_PATH=""     # cmake.exe 경로 — 비어있으면 자동검색
BUILD_DIR=""      # 비어있으면 자동검색
PROJECT_ROOT=""   # 비어있으면 자동검색

# ----- 인자 파싱 -------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)        OUTPUT="$2";       shift 2 ;;
        --arch)          ARCH="$2";         shift 2 ;;
        --vs-path)       VS_PATH="$2";      shift 2 ;;
        --cmake-path)    CMAKE_PATH="$2";   shift 2 ;;
        --build-dir)     BUILD_DIR="$2";    shift 2 ;;
        --project-root)  PROJECT_ROOT="$2"; shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "[ERROR] 알 수 없는 인자: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# ----- 경로 변환 헬퍼 --------------------------------------------------------
# msys / git-bash 의 /c/foo/bar 같은 경로를 Windows 형식으로 변환한다.
# - to_win_fwd : 슬래시 형식 (예: c:/foo/bar) — CMake 인자에 적합.
# - to_win_bs  : 백슬래시 형식 (예: C:\foo\bar) — cmd.exe 호출에 친숙.
to_win_fwd() {
    if command -v cygpath >/dev/null 2>&1; then
        # cygpath -w 는 백슬래시를 주므로 슬래시로 다시 치환한다.
        cygpath -w "$1" | tr '\\' '/'
    else
        # cygpath 가 없을 때의 대체: /c/... -> c:/...
        echo "$1" | sed -E 's|^/([a-zA-Z])/|\1:/|'
    fi
}
to_win_bs() {
    if command -v cygpath >/dev/null 2>&1; then
        cygpath -w "$1"
    else
        # cygpath 가 없을 때의 대체: /c/foo/bar -> C:\foo\bar
        echo "$1" | sed -E 's|^/([a-zA-Z])/|\U\1:\\|;s|/|\\|g'
    fi
}

# ----- 1. 프로젝트 루트 검색 -------------------------------------------------
if [[ -z "$PROJECT_ROOT" ]]; then
    # 이 스크립트가 위치한 디렉토리에서 위로 올라가며 CMakeLists.txt 를 찾는다.
    SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
    DIR="$SCRIPT_DIR"
    while [[ -n "$DIR" && "$DIR" != "/" ]]; do
        if [[ -f "$DIR/CMakeLists.txt" ]]; then
            PROJECT_ROOT="$DIR"
            break
        fi
        DIR="$(dirname "$DIR")"
    done
fi
if [[ -z "$PROJECT_ROOT" || ! -f "$PROJECT_ROOT/CMakeLists.txt" ]]; then
    echo "[ERROR] 프로젝트 루트(CMakeLists.txt 보유 디렉토리)를 찾지 못했습니다." >&2
    echo "        --project-root <path> 로 직접 지정하세요." >&2
    exit 1
fi
echo "[INFO] 프로젝트 루트 : $PROJECT_ROOT"

# 프로젝트 루트가 정해진 시점에 OUTPUT 기본값을 채운다.
if [[ -z "$OUTPUT" ]]; then
    OUTPUT="$PROJECT_ROOT/script/build.sh"
fi

# ----- 2. VsDevCmd.bat 검색 --------------------------------------------------
# Launch-VsDevShell.ps1 은 일부 VS 빌드 (특히 18 Insiders) 에서 vswhere 호출이
# 깨져있어 `\Common was unexpected at this time.` 류 에러로 실패한다. underlying
# batch 인 VsDevCmd.bat 는 안정적이므로 직접 cmd /c 로 호출한다.
if [[ -z "$VS_PATH" ]]; then
    SEARCH_ROOTS=(
        "/c/Program Files/Microsoft Visual Studio"
        "/c/Program Files (x86)/Microsoft Visual Studio"
    )
    for root in "${SEARCH_ROOTS[@]}"; do
        [[ -d "$root" ]] || continue
        # maxdepth 5: <root>/<버전>/<에디션>/Common7/Tools/VsDevCmd.bat
        # sort -V 로 버전 자연 정렬 후 가장 최신 것을 채택한다.
        found="$(find "$root" -maxdepth 5 -name 'VsDevCmd.bat' 2>/dev/null | sort -V | tail -n 1)"
        if [[ -n "$found" ]]; then
            VS_PATH="$found"
            break
        fi
    done
fi
if [[ -z "$VS_PATH" || ! -f "$VS_PATH" ]]; then
    echo "[ERROR] VsDevCmd.bat 를 찾지 못했습니다." >&2
    echo "        Visual Studio 가 설치되어 있는지 확인하거나 --vs-path 로 지정하세요." >&2
    exit 1
fi
echo "[INFO] VsDevCmd      : $VS_PATH"

# ----- 2-1. cmake.exe 검색 ---------------------------------------------------
# VS 번들 cmake 가 일반적으로 동작 검증된 버전이라 우선 채택한다. VS 번들이
# 없으면 system PATH 에서 찾고, 그것도 없으면 사용자 지정을 요구한다.
if [[ -z "$CMAKE_PATH" ]]; then
    # 후보 1: VS install 의 번들 cmake.
    #   Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
    #   VsDevCmd.bat 의 grandparent 인 VS install root 에서 위 경로를 합친다.
    if [[ -n "$VS_PATH" ]]; then
        # VsDevCmd.bat 의 위치는 <VS_root>/Common7/Tools/VsDevCmd.bat
        # → <VS_root> 는 두 단계 위.
        VS_ROOT="$(dirname "$(dirname "$(dirname "$VS_PATH")")")"
        cand="$VS_ROOT/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
        if [[ -f "$cand" ]]; then
            CMAKE_PATH="$cand"
        fi
    fi
fi
if [[ -z "$CMAKE_PATH" ]]; then
    # 후보 2: PATH 에서 찾기.
    if command -v cmake >/dev/null 2>&1; then
        CMAKE_PATH="$(command -v cmake)"
    fi
fi
if [[ -z "$CMAKE_PATH" || ! -f "$CMAKE_PATH" ]]; then
    echo "[ERROR] cmake.exe 를 찾지 못했습니다." >&2
    echo "        VS 번들 cmake (Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin)" >&2
    echo "        도 PATH 에도 없습니다. --cmake-path 로 절대 경로를 지정하세요." >&2
    exit 1
fi
echo "[INFO] cmake         : $CMAKE_PATH"

# ----- 3. 빌드 디렉토리 검색 -------------------------------------------------
# 1) --build-dir 로 명시 받았으면 그 값을 그대로 채택. 디렉토리가 없으면 생성.
#    CMakeCache.txt 부재는 OK — build.sh 의 do_configure 가 fresh 구성한다.
# 2) 자동탐색 모드: out/build/<config>/CMakeCache.txt 중 가장 최근 mtime 채택.
#    cache 가 하나도 없으면 표준 기본값 (out/build/x64-Debug) 으로 fallback.
if [[ -z "$BUILD_DIR" ]]; then
    candidates=()
    for d in "$PROJECT_ROOT"/out/build/*/; do
        [[ -f "${d}CMakeCache.txt" ]] && candidates+=("${d%/}")
    done
    if [[ ${#candidates[@]} -gt 0 ]]; then
        BUILD_DIR="$(ls -1dt "${candidates[@]}" | head -n 1)"
    else
        # cache 가 모두 비어있을 때 — --rebuild 직후 등 — 표준 경로로 fallback.
        BUILD_DIR="$PROJECT_ROOT/out/build/x64-Debug"
        echo "[WARN] 활성 cache 없음 — 표준 경로로 fallback: $BUILD_DIR"
    fi
fi
if [[ ! -d "$BUILD_DIR" ]]; then
    mkdir -p "$BUILD_DIR"
    echo "[INFO] 빌드 디렉토리 생성: $BUILD_DIR"
fi
echo "[INFO] 빌드 디렉토리 : $BUILD_DIR"
echo "[INFO] 아키텍처      : $ARCH"

# ----- 3-1. CMake 캐시에서 reconfigure 에 필요한 값 추출 ---------------------
# cache 가 있으면 generator / build type 을 추출, 없으면 표준 기본값 (Ninja / Debug).
# build.sh 가 cache 분실 시 do_configure 로 재생성할 때 이 값을 사용한다.
read_cache() {
    # CMakeCache.txt 항목 형식:  KEY:TYPE=VALUE
    local key="$1"
    [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || return 0
    grep -E "^${key}:[A-Z]+=" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null \
        | head -n 1 \
        | sed -E "s/^${key}:[A-Z]+=//"
}
CMAKE_GENERATOR_VALUE="$(read_cache CMAKE_GENERATOR)"
CMAKE_BUILD_TYPE_VALUE="$(read_cache CMAKE_BUILD_TYPE)"
[[ -z "$CMAKE_GENERATOR_VALUE"  ]] && CMAKE_GENERATOR_VALUE="Ninja"
[[ -z "$CMAKE_BUILD_TYPE_VALUE" ]] && CMAKE_BUILD_TYPE_VALUE="Debug"
echo "[INFO] 제너레이터    : $CMAKE_GENERATOR_VALUE"
echo "[INFO] 빌드 타입     : $CMAKE_BUILD_TYPE_VALUE"

# ----- 4. Windows 형식 경로로 변환 -------------------------------------------
# cmd 안에서 호출할 인자 (VsDevCmd.bat / cmake.exe) 는 백슬래시 형식이 자연스럽고,
# CMake --build / -S / -B 인자 (빌드/소스 디렉토리) 는 슬래시 형식을 그대로 받는다.
VS_PATH_WIN="$(to_win_bs "$VS_PATH")"
CMAKE_PATH_WIN="$(to_win_bs "$CMAKE_PATH")"
BUILD_DIR_WIN="$(to_win_fwd "$BUILD_DIR")"
PROJECT_ROOT_WIN="$(to_win_fwd "$PROJECT_ROOT")"

# ----- 5. 출력 디렉토리 준비 -------------------------------------------------
OUTPUT_DIR="$(dirname "$OUTPUT")"
if [[ ! -d "$OUTPUT_DIR" ]]; then
    mkdir -p "$OUTPUT_DIR"
    echo "[INFO] 출력 디렉토리 생성 : $OUTPUT_DIR"
fi

# ----- 6. build.sh 생성 ------------------------------------------------------
# heredoc 안에서:
#   - ${VS_PATH_WIN}, ${BUILD_DIR_WIN}, ${ARCH} 는 *지금* 치환된다(생성기 변수).
#   - \$VAR 는 백슬래시 이스케이프되어 *생성된 파일 안에* 그대로 $VAR 로 남는다
#     (즉, 생성된 build.sh 가 실행될 때 그 시점에서 확장된다).
GEN_TIME="$(date '+%Y-%m-%d %H:%M:%S')"
cat > "$OUTPUT" <<EOF
#!/usr/bin/env bash
# =============================================================================
# build.sh — MSVC + CMake 빌드 스크립트
# -----------------------------------------------------------------------------
# 이 파일은 generate-build-script.sh 에 의해 자동 생성되었습니다.
# 직접 수정하지 말고, 환경이 변경되면 generate-build-script.sh 를 다시 실행하세요.
# 생성 시각: ${GEN_TIME}
#
# 동작 (PS quoting 우회 — 임시 .bat 파일 패턴):
#   1) 임시 .bat 파일을 생성한다. 그 안에서:
#      a) PATH 를 system minimal (System32 + WindowsPowerShell) 로 sanitize.
#         ※ 호출 측 bash 의 PATH 가 ssh-agent 출력 등으로 오염되어 있어도
#           VsDevCmd.bat 의 PATH 파싱이 깨지지 않도록 하기 위함.
#      b) call VsDevCmd.bat — MSVC 환경변수를 같은 cmd 세션에 주입.
#      c) cmake.exe (절대 경로) 로 configure / build 호출.
#   2) cmd.exe //c 로 그 .bat 파일을 한 번에 실행 (인자 0 — 인자 quoting 문제 zero).
#   3) 종료 후 .bat 파일 삭제.
#
# 인자:
#   (없음)        \$BUILD_DIR 에 cache 가 있으면 그대로 빌드, 없으면 자동 재구성 후 빌드.
#   --clean       \$BUILD_DIR 내용 전체 삭제(캐시 포함). 빌드는 하지 않는다.
#   --rebuild     --clean 후 재구성 + 빌드를 한 번에 수행.
#   -h, --help    이 도움말.
#
# 새 소스 파일이 추가되었는데 file(GLOB ...) 결과가 캐시되어 빌드에 반영되지
# 않는 상황에서는 --rebuild 로 캐시를 초기화하면 안전하게 잡힌다.
# =============================================================================

set -euo pipefail

# ----- 빌드 환경 (자동 검색 결과) -------------------------------------------
VS_DEV_CMD='${VS_PATH_WIN}'
CMAKE_PATH='${CMAKE_PATH_WIN}'
BUILD_DIR='${BUILD_DIR_WIN}'
PROJECT_ROOT='${PROJECT_ROOT_WIN}'
ARCH='${ARCH}'
CMAKE_GENERATOR_NAME='${CMAKE_GENERATOR_VALUE}'
CMAKE_BUILD_TYPE_NAME='${CMAKE_BUILD_TYPE_VALUE}'

# ----- 인자 파싱 -------------------------------------------------------------
MODE="build"   # build | clean | rebuild
while [[ \$# -gt 0 ]]; do
    case "\$1" in
        --clean)   MODE="clean";   shift ;;
        --rebuild) MODE="rebuild"; shift ;;
        -h|--help)
            sed -n '2,/^# ===/p' "\$0" | sed 's/^# \\{0,1\\}//'
            exit 0
            ;;
        *) echo "[BUILD][ERROR] 알 수 없는 인자: \$1" >&2; exit 2 ;;
    esac
done

# ----- 진단 출력 -------------------------------------------------------------
# Claude/에이전트가 로그를 파싱하기 쉽도록 [BUILD] 접두사로 통일한다.
echo "[BUILD] 시작        \$(date '+%Y-%m-%d %H:%M:%S')"
echo "[BUILD] Mode        \$MODE"
echo "[BUILD] VsDevCmd    \$VS_DEV_CMD"
echo "[BUILD] CMake       \$CMAKE_PATH"
echo "[BUILD] BuildDir    \$BUILD_DIR"
echo "[BUILD] ProjectRoot \$PROJECT_ROOT"
echo "[BUILD] Arch        \$ARCH"
echo "[BUILD] Generator   \$CMAKE_GENERATOR_NAME"
echo "[BUILD] BuildType   \$CMAKE_BUILD_TYPE_NAME"

# ----- 헬퍼: clean / configure / build --------------------------------------
# clean: 빌드 디렉토리 자체는 유지하고 안의 내용물(.시작 파일 포함)만 비운다.
#   rm -rf "\$BUILD_DIR" 후 재생성하지 않는 이유 — VS/IDE 가 디렉토리 inode 를
#   감시하는 경우가 있어 가능한 한 디렉토리 자체는 보존한다.
do_clean() {
    if [[ -d "\$BUILD_DIR" ]]; then
        echo "[BUILD] Clean       \$BUILD_DIR"
        find "\$BUILD_DIR" -mindepth 1 -maxdepth 1 -exec rm -rf {} +
    else
        echo "[BUILD] Clean       (빌드 디렉토리 없음 — 건너뜀)"
        mkdir -p "\$BUILD_DIR"
    fi
}

# 임시 .bat 파일을 만들어 cmd.exe 한 번에 실행하는 헬퍼.
#   - PATH 를 system minimal 로 sanitize (호출 측 bash 의 PATH 오염 무력화).
#   - VsDevCmd.bat 환경 주입 후 cmake.exe 호출까지 한 cmd 세션에서.
#   - .bat 파일은 trap 으로 종료 시 자동 삭제.
#
# \$1 = cmake 호출 라인 (예: '"\$CMAKE_PATH" -S ... -B ...')
run_in_vs_env() {
    local cmake_cmd="\$1"
    local bat_file
    bat_file="\$(mktemp --suffix=.bat)"
    # heredoc — 안에서 \$VAR 는 즉시 확장되어 .bat 안에 박히고,
    # %errorlevel% 같은 cmd-side 변수는 그대로 유지된다.
    cat > "\$bat_file" <<BATEOF
@echo off
set PATH=C:\\Windows\\System32;C:\\Windows;C:\\Windows\\System32\\WindowsPowerShell\\v1.0
call "\$VS_DEV_CMD" -arch=\$ARCH -host_arch=\$ARCH -no_logo
if errorlevel 1 exit /b %errorlevel%
\$cmake_cmd
exit /b %errorlevel%
BATEOF
    local win_bat
    win_bat="\$(cygpath -w "\$bat_file")"
    local rc=0
    cmd.exe //c "\$win_bat" 2>&1 || rc=\$?
    rm -f "\$bat_file" 2>/dev/null || true
    return \$rc
}

do_configure() {
    echo "[BUILD] Configure   cmake -G '\$CMAKE_GENERATOR_NAME' -DCMAKE_BUILD_TYPE=\$CMAKE_BUILD_TYPE_NAME"
    run_in_vs_env "\"\$CMAKE_PATH\" -S \"\$PROJECT_ROOT\" -B \"\$BUILD_DIR\" -G \"\$CMAKE_GENERATOR_NAME\" -DCMAKE_BUILD_TYPE=\$CMAKE_BUILD_TYPE_NAME"
}

do_build() {
    echo "[BUILD] Build       cmake --build '\$BUILD_DIR'"
    run_in_vs_env "\"\$CMAKE_PATH\" --build \"\$BUILD_DIR\""
}

# ----- 모드 디스패치 ---------------------------------------------------------
case "\$MODE" in
    clean)
        do_clean
        ;;
    rebuild)
        do_clean
        do_configure
        do_build
        ;;
    build)
        # cache 가 비어 있으면 자동 재구성 — --clean 직후 무인 실행에서도 동작.
        if [[ ! -f "\$BUILD_DIR/CMakeCache.txt" ]]; then
            echo "[BUILD] Cache 없음 — 자동 재구성"
            do_configure
        fi
        do_build
        ;;
esac

echo "[BUILD] 완료        \$(date '+%Y-%m-%d %H:%M:%S')"
EOF

chmod +x "$OUTPUT" 2>/dev/null || true

# ----- 7. 마무리 -------------------------------------------------------------
echo "[OK]   빌드 스크립트 생성 완료"
# 마지막 줄은 결과 경로 단독 — 자동화 도구가 손쉽게 잘라쓰도록 한다.
echo "$OUTPUT"
