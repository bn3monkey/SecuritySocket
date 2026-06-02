# macOS 지원 가이드

## 개요

SecuritySocket은 Windows와 Linux를 주 대상으로 개발되어 있으며, macOS에서 빌드하려면 몇 가지 코드 수정이 필요합니다.
macOS는 POSIX 호환 플랫폼이므로 Linux 코드 경로를 대부분 공유할 수 있으나, 일부 플랫폼 분기가 `__linux__`만 처리하여 macOS를 누락시키고 있습니다.

---

## 현재 상태 요약

| 영역 | 상태 | 비고 |
|---|---|---|
| 소켓 생성 / 연결 | 호환 | POSIX `socket()`, `connect()` 공통 사용 |
| 논블로킹 모드 / 타임아웃 | 호환 | `fcntl()`, `timeval` POSIX 표준 |
| 소켓 이벤트 (`poll`) | **컴파일 오류** | `__linux__` 가드로 macOS 미포함 |
| errno 에러 매핑 | **컴파일 오류** | `<errno.h>` include가 macOS 미포함 |
| SIGPIPE 방지 | **런타임 오류** | `MSG_NOSIGNAL` macOS 미지원, `SO_NOSIGPIPE` 필요 |
| CMake -fPIC | 누락 | Linux 전용으로 설정됨 |
| VSCode IntelliSense | 누락 | macOS 구성 없음 |

---

## 필수 수정 사항

### 1. `src/implementation/SocketEvent.hpp` — `<poll.h>` 미포함 및 `_handle` 멤버 누락

**문제:**
- [SocketEvent.hpp:10](src/implementation/SocketEvent.hpp#L10): `#elif __linux__`로 가드되어 macOS에서 `<poll.h>`가 포함되지 않아 `pollfd` 타입을 인식하지 못함
- [SocketEvent.hpp:77](src/implementation/SocketEvent.hpp#L77): `SocketMultiEventListener::_handle` 멤버가 `_WIN32`와 `__linux__` 분기에만 선언되어, macOS에서는 해당 멤버 자체가 존재하지 않음

**수정:**
```cpp
// 변경 전
#elif __linux__
#include <poll.h>
#endif

// 변경 후
#elif defined(__linux__) || defined(__APPLE__)
#include <poll.h>
#endif
```

```cpp
// SocketMultiEventListener 내부 (변경 전)
#if defined(_WIN32)
    std::vector<pollfd> _handle;
#elif defined __linux__
    std::vector<pollfd> _handle;
#endif

// 변경 후
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
    std::vector<pollfd> _handle;
#endif
```

---

### 2. `src/implementation/SocketEvent_Linux.cpp` — macOS에서 이벤트 구현 전체 누락

**문제:**
- [SocketEvent_Linux.cpp:1](src/implementation/SocketEvent_Linux.cpp#L1): 파일 전체가 `#if defined(__linux__)` 가드로 감싸져 있어, macOS에서는 `SocketEventListener`와 `SocketMultiEventListener`의 구현이 링크 시 없어짐

**수정:**
```cpp
// 변경 전
#if defined(__linux__)

// 변경 후
#if defined(__linux__) || defined(__APPLE__)
```

`poll()`은 macOS에서도 POSIX 표준으로 동일하게 지원되므로 구현 내용 변경 없이 가드만 확장하면 됩니다.

---

### 3. `src/implementation/SocketResult.hpp` — `<errno.h>` 미포함

**문제:**
- [SocketResult.hpp:9](src/implementation/SocketResult.hpp#L9): `#elif defined __linux__`로 가드되어 macOS에서는 `<errno.h>`가 포함되지 않음. 결과적으로 `EACCES`, `ECONNREFUSED` 등 errno 상수가 정의되지 않아 컴파일 오류 발생

**수정:**
```cpp
// 변경 전
#elif defined __linux__
#include <errno.h>

// 변경 후
#elif defined(__linux__) || defined(__APPLE__)
#include <errno.h>
```

---

### 4. `src/implementation/ClientActiveSocket.cpp` — SIGPIPE 처리

**문제:**
- [ClientActiveSocket.cpp:98](src/implementation/ClientActiveSocket.cpp#L98): `MSG_NOSIGNAL`은 Linux 전용 플래그로 macOS에서 지원되지 않음
- macOS에서 상대방이 연결을 끊은 상태에서 `send()`를 호출하면 `SIGPIPE` 시그널이 발생하여 프로세스가 종료될 수 있음
- [ClientActiveSocket.cpp:374-379](src/implementation/ClientActiveSocket.cpp#L374): 개발자가 이미 `SO_NOSIGPIPE`를 이용한 해결책을 주석으로 남겨 두었음

**현재 주석 처리된 코드 (소켓 생성 시 설정 필요):**
```cpp
/*
#if !defined(_WIN32) && !defined(__linux__)
    int sigpipe{ 1 };
    setsockopt(_socket, SOL_SOCKET, SO_NOSIGPIPE, (void*)(&sigpipe), sizeof(sigpipe));
#endif
*/
```

**수정 방향:**
- 소켓 생성 직후(`ClientActiveSocket` 생성자 내 소켓 유효성 검사 통과 시점)에 해당 주석을 해제하여 활성화
- `send()` 호출 부분의 플래그 분기에도 명시적으로 macOS 처리 추가:
  ```cpp
  #ifdef __linux__
      ret = send(_socket, buffer, size, MSG_NOSIGNAL);
  #else
      ret = send(_socket, static_cast<const char*>(buffer), static_cast<int32_t>(size), 0);
  #endif
  ```
  macOS는 `#else` 경로를 타므로 `SO_NOSIGPIPE`를 소켓 옵션으로 설정해 두면 동일하게 동작함

---

### 5. `CMakeLists.txt` — `-fPIC` 플래그 누락

**문제:**
- [CMakeLists.txt:23](CMakeLists.txt#L23): `-fPIC`(Position Independent Code)이 Linux에만 설정됨
- macOS에서도 공유 라이브러리(`.dylib`) 빌드 시 `-fPIC`이 필요할 수 있음

**수정:**
```cmake
# 변경 전
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message("Apply -fPIC in linux environment ")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

# 변경 후
if (UNIX AND NOT WIN32)
    message("Apply -fPIC in unix environment (Linux/macOS)")
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
```

---

### 6. `src/implementation/SocketAddress.cpp` — `<netinet/in.h>` 미포함 (낮은 우선순위)

**현황:**
- [SocketAddress.cpp:5](src/implementation/SocketAddress.cpp#L5): `<netinet/in.h>`가 `#ifdef __linux__` 안에만 포함됨
- macOS에서는 `AF_INET`, `SOCK_STREAM` 등이 `<sys/socket.h>`에 정의되므로 현재는 대부분 동작하나, 명시적으로 포함하는 것이 안전함

**수정:**
```cpp
// 변경 전
#ifdef __linux__
#include <netinet/in.h>
#endif

// 변경 후
#if defined(__linux__) || defined(__APPLE__)
#include <netinet/in.h>
#endif
```

---

## 빌드 환경 설정

### 필수 도구 설치

```bash
# Xcode Command Line Tools (컴파일러, SDK 포함)
xcode-select --install

# Homebrew (패키지 관리자)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# CMake
brew install cmake

# Git (FetchContent 의존성 다운로드에 필요)
brew install git
```

> OpenSSL은 CMakeLists.txt의 `FetchContent`를 통해 [bn3monkey/openssl-cmake](https://github.com/bn3monkey/openssl-cmake)에서 자동으로 빌드되므로 별도 설치 불필요합니다.

### 빌드 명령

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

TLS를 비활성화하고 빌드하려면:

```bash
cmake -B build -DSECURITYSOCKET_USING_TLS=OFF
cmake --build build
```

---

## VSCode IntelliSense 설정

`.vscode/c_cpp_properties.json`에 macOS 구성을 추가해야 합니다.

```json
{
    "name": "macOS",
    "includePath": [
        "${workspaceFolder}/**"
    ],
    "defines": [
        "__APPLE__",
        "SECURITYSOCKET_EXPORTS"
    ],
    "macFrameworkPath": [
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/System/Library/Frameworks"
    ],
    "compilerPath": "/usr/bin/clang++",
    "cStandard": "c17",
    "cppStandard": "c++17",
    "intelliSenseMode": "macos-clang-x64"
}
```

---

## 수정 우선순위 요약

| 우선순위 | 파일 | 수정 내용 |
|---|---|---|
| 1 (컴파일 오류) | [SocketEvent.hpp:10](src/implementation/SocketEvent.hpp#L10) | `__linux__` → `__linux__ \|\| defined(__APPLE__)` |
| 1 (컴파일 오류) | [SocketEvent.hpp:77](src/implementation/SocketEvent.hpp#L77) | `_handle` 멤버 macOS 분기 추가 |
| 1 (컴파일 오류) | [SocketEvent_Linux.cpp:1](src/implementation/SocketEvent_Linux.cpp#L1) | `__linux__` → `__linux__ \|\| defined(__APPLE__)` |
| 1 (컴파일 오류) | [SocketResult.hpp:9](src/implementation/SocketResult.hpp#L9) | `<errno.h>` include macOS 추가 |
| 2 (런타임 오류) | [ClientActiveSocket.cpp:22-38](src/implementation/ClientActiveSocket.cpp#L22) | `SO_NOSIGPIPE` 소켓 옵션 활성화 |
| 3 (빌드 경고) | [CMakeLists.txt:23](CMakeLists.txt#L23) | `-fPIC` 조건을 `UNIX AND NOT WIN32`로 확장 |
| 4 (낮음) | [SocketAddress.cpp:5](src/implementation/SocketAddress.cpp#L5) | `<netinet/in.h>` macOS 분기 추가 |
