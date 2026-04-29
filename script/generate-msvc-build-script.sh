#!/usr/bin/env bash
# =============================================================================
# generate-build-script.sh
# -----------------------------------------------------------------------------
# MSVC + CMake 빌드 스크립트(build.sh)를 자동 생성한다.
#
# 자동 탐색 항목:
#   1) 프로젝트 루트          : 스크립트 상위에서 CMakeLists.txt 가 있는 디렉토리
#   2) Launch-VsDevShell.ps1  : "Program Files\Microsoft Visual Studio" 하위 검색
#   3) CMake 빌드 디렉토리    : <project_root>/out/build/*/CMakeCache.txt 검색
#   4) 아키텍처               : 기본값 amd64 (인자로 override 가능)
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
  --vs-path <path>     Launch-VsDevShell.ps1 경로 직접 지정 (자동검색 건너뜀)
  --build-dir <path>   CMake 빌드 디렉토리 직접 지정 (자동검색 건너뜀)
  --project-root <p>   프로젝트 루트 직접 지정 (자동검색 건너뜀)
  -h, --help           이 도움말 표시

예시:
  ./script/generate-build-script.sh
  ./script/generate-build-script.sh --output ./script/build.sh
  ./script/generate-build-script.sh --build-dir /c/repo/out/build/x64-Release
USAGE
}

# ----- 기본값 ----------------------------------------------------------------
# OUTPUT 은 빈 문자열로 두고, 프로젝트 루트 검색이 끝난 뒤 기본 경로를 결정한다.
# (기본 경로 = <project_root>/script/build.sh — .gitignore 에 의해 추적되지 않음)
OUTPUT=""
ARCH="amd64"
VS_PATH=""        # 비어있으면 자동검색
BUILD_DIR=""      # 비어있으면 자동검색
PROJECT_ROOT=""   # 비어있으면 자동검색

# ----- 인자 파싱 -------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --output)        OUTPUT="$2";       shift 2 ;;
        --arch)          ARCH="$2";         shift 2 ;;
        --vs-path)       VS_PATH="$2";      shift 2 ;;
        --build-dir)     BUILD_DIR="$2";    shift 2 ;;
        --project-root)  PROJECT_ROOT="$2"; shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        *) echo "[ERROR] 알 수 없는 인자: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# ----- 경로 변환 헬퍼 --------------------------------------------------------
# msys / git-bash 의 /c/foo/bar 같은 경로를 Windows 형식으로 변환한다.
# - to_win_fwd : 슬래시 형식 (예: c:/foo/bar) — CMake 인자에 적합.
# - to_win_bs  : 백슬래시 형식 (예: C:\foo\bar) — PowerShell 호출에 친숙.
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

# ----- 2. Launch-VsDevShell.ps1 검색 -----------------------------------------
if [[ -z "$VS_PATH" ]]; then
    # 후보 루트 — 일반적으로 64bit 머신에 설치되는 두 위치를 모두 확인한다.
    SEARCH_ROOTS=(
        "/c/Program Files/Microsoft Visual Studio"
        "/c/Program Files (x86)/Microsoft Visual Studio"
    )
    for root in "${SEARCH_ROOTS[@]}"; do
        [[ -d "$root" ]] || continue
        # maxdepth 5: <root>/<버전>/<에디션>/Common7/Tools/Launch-VsDevShell.ps1
        # sort -V 로 버전 자연 정렬 후 가장 최신 것을 채택한다.
        found="$(find "$root" -maxdepth 5 -name 'Launch-VsDevShell.ps1' 2>/dev/null | sort -V | tail -n 1)"
        if [[ -n "$found" ]]; then
            VS_PATH="$found"
            break
        fi
    done
fi
if [[ -z "$VS_PATH" || ! -f "$VS_PATH" ]]; then
    echo "[ERROR] Launch-VsDevShell.ps1 를 찾지 못했습니다." >&2
    echo "        Visual Studio 가 설치되어 있는지 확인하거나 --vs-path 로 지정하세요." >&2
    exit 1
fi
echo "[INFO] VsDevShell    : $VS_PATH"

# ----- 3. 빌드 디렉토리 검색 -------------------------------------------------
if [[ -z "$BUILD_DIR" ]]; then
    # out/build/<config>/ 중 CMakeCache.txt 가 있는 디렉토리를 모두 모은 뒤
    # 가장 최근에 수정된 것을 우선 채택한다(가장 최근에 구성된 빌드).
    candidates=()
    for d in "$PROJECT_ROOT"/out/build/*/; do
        [[ -f "${d}CMakeCache.txt" ]] && candidates+=("${d%/}")
    done
    if [[ ${#candidates[@]} -gt 0 ]]; then
        # mtime 내림차순으로 정렬하여 가장 최근 빌드 디렉토리를 선택.
        # ls -1dt 는 디렉토리 자체의 mtime 기준으로 정렬한다.
        BUILD_DIR="$(ls -1dt "${candidates[@]}" | head -n 1)"
    fi
fi
if [[ -z "$BUILD_DIR" || ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "[ERROR] CMake 빌드 디렉토리를 찾지 못했습니다." >&2
    echo "        먼저 CMake configure 를 실행하거나 --build-dir 로 지정하세요." >&2
    exit 1
fi
echo "[INFO] 빌드 디렉토리 : $BUILD_DIR"
echo "[INFO] 아키텍처      : $ARCH"

# ----- 3-1. CMake 캐시에서 reconfigure 에 필요한 값 추출 ---------------------
# --rebuild / cache 분실 후 재구성을 위해 generator 와 build type 을 보존한다.
# 추출 실패 시 합리적 기본값(Ninja / Debug) 으로 대체한다.
read_cache() {
    # CMakeCache.txt 항목 형식:  KEY:TYPE=VALUE
    # 예) CMAKE_GENERATOR:INTERNAL=Ninja
    local key="$1"
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
# PowerShell 호출 인자(VsDevShell)는 백슬래시 형식이 가독성이 좋고,
# CMake --build 인자(빌드/소스 디렉토리)는 슬래시 형식을 그대로 받는다.
VS_PATH_WIN="$(to_win_bs "$VS_PATH")"
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
# 동작:
#   1) PowerShell 로 Visual Studio 개발자 셸을 활성화한다.
#   2) 같은 PowerShell 세션에서 cmake configure(필요 시) / build 를 호출한다.
#   3) 표준 에러도 표준 출력으로 합쳐서 한 줄 단위로 보이도록 한다.
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
VS_DEV_SHELL='${VS_PATH_WIN}'
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
echo "[BUILD] VsDevShell  \$VS_DEV_SHELL"
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

# configure: cache 가 사라진 경우 generate-build-script.sh 시점에 추출한
# generator / build type 으로 재구성한다. cache 가 살아있으면 호출하지 않는다.
do_configure() {
    echo "[BUILD] Configure   cmake -G '\$CMAKE_GENERATOR_NAME' -DCMAKE_BUILD_TYPE=\$CMAKE_BUILD_TYPE_NAME"
    powershell.exe -ExecutionPolicy Bypass -Command "& '\$VS_DEV_SHELL' -Arch \$ARCH -HostArch \$ARCH -SkipAutomaticLocation | Out-Null; cmake -S '\$PROJECT_ROOT' -B '\$BUILD_DIR' -G '\$CMAKE_GENERATOR_NAME' -DCMAKE_BUILD_TYPE=\$CMAKE_BUILD_TYPE_NAME 2>&1"
}

# build:
# - Launch-VsDevShell.ps1 : 현 PowerShell 세션에 MSVC 도구체인 환경변수를 주입.
# - -SkipAutomaticLocation : 셸이 임의로 cwd 를 바꾸지 않도록 한다.
# - Out-Null               : 셸 활성화 메시지를 버려 출력을 cmake 로그에 집중.
# - 2>&1                   : 에러 출력도 stdout 으로 합쳐 한 줄 단위로 본다.
do_build() {
    echo "[BUILD] Build       cmake --build '\$BUILD_DIR'"
    powershell.exe -ExecutionPolicy Bypass -Command "& '\$VS_DEV_SHELL' -Arch \$ARCH -HostArch \$ARCH -SkipAutomaticLocation | Out-Null; cmake --build '\$BUILD_DIR' 2>&1"
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
