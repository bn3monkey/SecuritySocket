# 이벤트 백엔드 교체 전략

## 개요

현재 `SocketEvent_Windows.cpp`(WSAPoll)와 `SocketEvent_Linux.cpp`(poll)를 고성능 백엔드로 교체한다.

| 플랫폼 | 현재 | 목표 |
|---|---|---|
| Windows | WSAPoll | **IOCP** |
| Linux (kernel < 5.1 or liburing 없음) | poll | **epoll** |
| Linux (kernel >= 5.1, liburing 있음) | poll | **io_uring** |
| macOS / Apple | (미지원) | **kqueue** |
| 기타 POSIX | — | poll (fallback) |

---

## 핵심 개념: readiness vs completion 불일치

### 두 모델의 동작

현재 서버 코드는 **readiness 모델**로 작성되어 있다.

```
// 단일 스레드가 모든 것을 처리
loop:
    events = wait()          // "누가 준비됐나?" 대기

    for each event:
        if READ:
            header = recv()  // 지금 읽음 (커널 → 유저 버퍼 복사)
            response = handler(header)
            modify(WRITE)

        if WRITE:
            send(response)   // 지금 씀
            modify(READ)
```

IOCP와 io_uring은 **completion 모델**이다.

```
// 초기화 시점에 I/O를 커널에 미리 제출
submit_recv(fd, buf)         // 커널에 맡기고 즉시 return, 스레드 해방

// 완료 통지 루프
loop:
    completion = dequeue()   // "recv 완료, 데이터 이미 buf에 있음" 대기

    if RECV_DONE:
        response = handler(completion.buf)  // 데이터 이미 있음
        submit_send(fd, response)

    if SEND_DONE:
        submit_recv(fd, buf) // 다음 요청 재제출
```

### 실질적 차이: wait가 몇 번인가

겉으로 보면 비슷해 보이지만 I/O 대기 횟수가 다르다.

```
Readiness:
  wait()    ← "소켓에 데이터 있음" 신호 대기        [대기 1]
  recv()    ← 실제 데이터 복사 (커널 → 유저 버퍼)   [대기 2]

Completion:
  dequeue() ← "recv 완료, 복사까지 끝남" 대기        [대기 1]
              (대기와 복사가 합쳐진 것)
```

syscall 횟수로 보면:

| | Readiness | Completion |
|---|---|---|
| I/O당 syscall | wait 1 + recv 1 = **2번** | submit 1 + dequeue 1 = **2번** |
| 데이터 복사 시점 | 앱이 recv() 호출 시 | 커널이 미리 처리 |
| 스레드 점유 | recv() 완료까지 점유 | submit 후 즉시 해방 |

syscall 횟수는 같지만 **스레드가 I/O를 기다리는 시간** 차이가 핵심이다.

### 언제 completion이 의미 있나

```
LAN 환경 기준 recv() 실제 블로킹 시간: ~50µs
스레드 컨텍스트 스위치 비용:           ~1-5µs
submit + dequeue 오버헤드:             ~2-10µs
```

| 연결 수 | 적합한 모델 | 이유 |
|---|---|---|
| < ~100 | Readiness | recv()가 워낙 빨리 끝나 스레드 해방 효과 없음. Completion 오버헤드가 더 클 수 있음 |
| ~100 ~ ~1000 | 둘 다 무방 | 트래픽 패턴에 따라 다름 |
| > ~1000 | Completion 권장 | 스레드가 wait()에 묶이는 시간이 누적됨 |
| > 10000 (C10K) | Completion 필수 | Readiness로는 스레드 풀 한계 도달 |

### 현재 SocketRequestServer와의 관계

[SocketRequestServer.cpp](src/implementation/SocketRequestServer.cpp)는 이미 상태머신을 사용하고 있다:

```
READING_HEADER → READING_PAYLOAD → WRITING_RESPONSE → FINISH_PROCESS
```

Completion 모델로 전환해도 이 상태 구조는 그대로 재사용할 수 있다.
`wait()` 결과를 처리하는 부분만 `dequeue()` 결과 처리로 교체하면 된다.
단, completion 모델은 **버퍼를 submit 시점에 미리 할당해서 커널에 넘겨야** 하므로
`SocketConnection`의 버퍼 관리와 `ObjectPool` 설계를 함께 변경해야 한다.

### 이번 구현의 목표

`SocketRequestServer::run()`의 루프 구조와 상태머신은 변경하지 않는다.
IOCP / io_uring은 **zero-byte overlapped 트릭** / **IORING_OP_POLL_ADD**를 통해
readiness 시맨틱을 흉내내어 현재 서버 구조와 호환되도록 한다.
완전한 completion 모델로의 전환은 별도 작업으로 분리한다.

---

## 1. `SocketEvent.hpp` 변경

### 문제

현재 private 멤버에 `pollfd`, `std::vector<pollfd>` 등 백엔드 타입이 직접 노출되어 있어
헤더에 `#ifdef` 범벅이 생기거나 macOS처럼 플랫폼이 누락되는 문제가 발생한다.

### 해결: 고정 크기 불투명 버퍼 (SocketContainer와 동일한 패턴)

```cpp
// SocketEventListener
private:
    static constexpr size_t HANDLE_SIZE = 64;
    alignas(8) char _handle[HANDLE_SIZE]{ 0 };
```

```cpp
// SocketMultiEventListener
private:
    int32_t _server_socket{ 0 };
    std::mutex _mtx;
    std::vector<SocketEventContext*> _contexts;
    static constexpr size_t HANDLE_SIZE = 128;
    alignas(8) char _handle[HANDLE_SIZE]{ 0 };
```

각 백엔드 `.cpp` 파일이 `_handle`을 자신의 타입으로 캐스팅해서 사용한다.
헤더에서 `<poll.h>`, `<winsock2.h>` include가 제거되어 헤더가 플랫폼 무관해진다.

---

## 2. 파일 구조

기존 두 파일을 삭제하고 백엔드별 파일로 분리한다.

```
src/implementation/
    SocketEvent.hpp               ← public API 동일, private만 변경
    SocketEvent_poll.cpp          ← 기존 두 파일 통합 (fallback / poll)
    SocketEvent_epoll.cpp         ← Linux epoll
    SocketEvent_iouring.cpp       ← Linux io_uring
    SocketEvent_iocp.cpp          ← Windows IOCP
    SocketEvent_kqueue.cpp        ← Apple kqueue
```

각 파일 상단의 플랫폼 가드:

```cpp
// SocketEvent_epoll.cpp
#if defined(__linux__) && defined(USE_EPOLL)

// SocketEvent_iouring.cpp
#if defined(__linux__) && defined(USE_IO_URING)

// SocketEvent_iocp.cpp
#if defined(_WIN32) && defined(USE_IOCP)

// SocketEvent_kqueue.cpp
#if defined(__APPLE__) && defined(USE_KQUEUE)

// SocketEvent_poll.cpp
#if defined(USE_POLL)
```

`USE_*` 매크로는 CMake가 `target_compile_definitions`로 주입한다.

---

## 3. 백엔드별 인터페이스 매핑

### 3-1. epoll (Linux)

readiness 모델이므로 poll과 구조가 같고 가장 단순하다.

| 메서드 | 구현 |
|---|---|
| `open()` | `epoll_create1(EPOLL_CLOEXEC)` → fd를 `_handle`에 저장 |
| `addEvent(context, type)` | `epoll_ctl(ADD)`, `ev.data.ptr = context` |
| `modifyEvent(context, type)` | `epoll_ctl(MOD)` — 현재 poll의 remove+add 패턴을 원자적으로 개선 |
| `removeEvent(context)` | `epoll_ctl(DEL)` |
| `wait(timeout_ms)` | `epoll_wait()` → `data.ptr`이 바로 `context*` (O(1), 현재 O(n) find_if 제거) |
| `close()` | `::close(epfd)` |

`SocketEventListener` (단일 소켓, BroadcastServer 사용): epoll 없이 `::poll()` 그대로 사용.
단일 fd에 epoll을 쓸 이유가 없다.

---

### 3-2. kqueue (macOS / Apple)

epoll과 구조가 유사하다. `kevent()` 하나로 등록과 대기를 모두 처리한다.

| 메서드 | 구현 |
|---|---|
| `open()` | `kqueue()` → fd를 `_handle`에 저장 |
| `addEvent(context, type)` | `EV_SET` + `kevent(kqfd, &change, 1, ...)`, `udata = context` |
| `modifyEvent(context, type)` | 기존 filter를 `EV_DELETE` 후 새 filter로 `EV_ADD` |
| `removeEvent(context)` | `EVFILT_READ`와 `EVFILT_WRITE` 모두 `EV_DELETE` 시도 (ENOENT 무시) |
| `wait(timeout_ms)` | `kevent(kqfd, nullptr, 0, eventlist, N, &ts)` → `udata`가 `context*` (O(1)) |
| `close()` | `::close(kqfd)` |

`SocketEventListener`: `::poll()` 사용 (epoll과 동일한 이유).

---

### 3-3. IOCP (Windows)

completion 모델이므로 readiness 인터페이스와 불일치가 발생한다.
**zero-byte overlapped 트릭**으로 readiness를 흉내낸다.

#### 핵심 아이디어

```
READ 등록   → WSARecv(길이 0버퍼, OVERLAPPED) 커널 제출
             커널: 소켓에 데이터 수신 → overlapped 완료
wait()      → GetQueuedCompletionStatus() → "읽기 준비됨" 신호로 변환
             서버: socket->read() 호출 → 데이터가 이미 있으므로 즉시 성공

WRITE 등록  → WSASend(길이 0버퍼, OVERLAPPED) 커널 제출
             커널: 송신 버퍼 공간 생기면 완료
             서버: socket->write() 호출 → 버퍼가 비어있으므로 즉시 성공

ACCEPT 등록 → AcceptEx(미리 할당한 소켓, OVERLAPPED) 커널 제출
             클라이언트 연결 시 완료 → context->accept_socket에 수락된 fd 저장
             서버: _socket->accept() 호출 → PassiveSocket이 이미 수락된 fd를 래핑
```

| 메서드 | 구현 |
|---|---|
| `open()` | `CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)` → HANDLE 저장 |
| `addEvent(context, type)` | ① `context->iocp_associated`가 false면 `CreateIoCompletionPort`로 소켓-IOCP 연결 (1회만) ② 이벤트 타입에 맞는 zero-byte overlapped 작업 제출 |
| `modifyEvent(context, type)` | 새 zero-byte overlapped 작업만 재제출 (IOCP 연결은 영구적) |
| `removeEvent(context)` | `_contexts`에서 제거. 소켓 close 시 IOCP가 자동으로 이벤트 중단 |
| `wait(timeout_ms)` | `GetQueuedCompletionStatus()` → completion_key가 `context*` → 즉시 zero-byte 작업 재제출 |
| `close()` | `CloseHandle(iocp_handle)` |

`SocketEventListener`: `WSAPoll` 그대로 유지. IOCP는 `SocketMultiEventListener`에만 적용.

#### PassiveSocket::accept() 수정 필요

```cpp
// context->accept_socket이 유효한 경우 (IOCP AcceptEx 완료)
// AcceptEx로 이미 수락된 소켓을 래핑하여 반환
// 그렇지 않은 경우 기존 ::accept() 호출
```

---

### 3-4. io_uring (Linux, kernel >= 5.1)

`IORING_OP_POLL_ADD`를 사용하면 zero-byte 트릭 없이 readiness 시맨틱을 얻을 수 있다.
io_uring 내부에서 poll을 실행하고 완료 시 알려주는 방식이다.

| 메서드 | 구현 |
|---|---|
| `open()` | `io_uring_queue_init(256, ring, 0)` → ring 포인터를 `_handle`에 저장 (heap 할당) |
| `addEvent(context, type)` | `io_uring_prep_poll_add(sqe, fd, POLLIN/POLLOUT)` → `sqe->user_data = context*` → `io_uring_submit()` |
| `modifyEvent(context, type)` | kernel 5.13+: `IORING_OP_POLL_UPDATE` / 5.1~5.12: cancel 후 재등록 |
| `removeEvent(context)` | `IORING_OP_ASYNC_CANCEL` (user_data로 타겟 지정) |
| `wait(timeout_ms)` | `io_uring_wait_cqe_timeout()` → `cqe->user_data`가 `context*` (O(1)) → `cqe->res` 비트로 이벤트 종류 판별 → `io_uring_cqe_seen()` 후 `poll_add` 재제출 (one-shot이므로 필수) |
| `close()` | `io_uring_queue_exit(ring)` → `delete ring` |

`SocketEventListener`: `::poll()` 사용.

#### 런타임 fallback

io_uring이 컴파일되었더라도 컨테이너 환경에서 seccomp 정책으로 차단될 수 있다.
`open()` 시점에 probe를 실행한다:

```cpp
// SocketEvent_iouring.cpp 내부
static bool probeIoUring() {
    io_uring ring;
    int ret = io_uring_queue_init(4, &ring, 0);
    if (ret < 0) return false;
    io_uring_queue_exit(&ring);
    return true;
}

SocketResult SocketMultiEventListener::open() {
    if (!probeIoUring()) {
        _using_fallback = true;
        // epoll fd로 초기화
        return openEpollFallback();
    }
    // io_uring 초기화
}
```

이후 모든 메서드는 `_using_fallback` 플래그로 분기한다.
별도의 fallback `.cpp` 파일 없이 하나의 파일에서 처리한다.

---

## 4. `SocketEventContext` 변경

`SocketConnection`이 `SocketEventContext`를 상속하므로 필드 추가는 모든 연결 객체 크기에 영향을 준다.
`ObjectPool<SocketConnection>`의 버퍼 크기도 함께 확인해야 한다.

```cpp
struct SocketEventContext {
#ifdef _WIN32
    OVERLAPPED overlapped;          // 32 bytes (기존)
    SOCKET accept_socket{ INVALID_SOCKET }; // AcceptEx용 미리 할당 소켓
    char accept_buffer[128]{ 0 };   // AcceptEx 로컬+리모트 주소 블록
    bool iocp_associated{ false };  // CreateIoCompletionPort 중복 호출 방지
#elif defined(__linux__)
    bool poll_in_flight{ false };   // io_uring 재등록 로직용 (epoll에서는 무해)
#endif
    int32_t fd{ -1 };
    SocketEventType type{ SocketEventType::UNDEFINED };
};
```

`accept_socket` / `accept_buffer`는 `SocketRequestServer::run()` 내의 `server_context`
로컬 변수에서만 실제로 사용된다. `SocketConnection` 인스턴스에서는 낭비 필드지만,
풀 크기 × 추가 바이트는 허용 범위 내다.

---

## 5. `CMakeLists.txt` 변경

### GLOB 대신 명시적 파일 제어

```cmake
option(SECURITYSOCKET_USE_IOCP "Use IOCP on Windows instead of WSAPoll" ON)

# 기존 파일 제거
list(REMOVE_ITEM securitysocket_source_files
    "${source_dir}/implementation/SocketEvent_Windows.cpp"
    "${source_dir}/implementation/SocketEvent_Linux.cpp")

# 백엔드 선택 및 파일 추가
if(WIN32)
    if(SECURITYSOCKET_USE_IOCP)
        list(APPEND securitysocket_source_files
            "${source_dir}/implementation/SocketEvent_iocp.cpp")
        target_compile_definitions(securitysocket PRIVATE USE_IOCP)
    else()
        list(APPEND securitysocket_source_files
            "${source_dir}/implementation/SocketEvent_poll.cpp")
        target_compile_definitions(securitysocket PRIVATE USE_POLL)
    endif()

elseif(APPLE)
    list(APPEND securitysocket_source_files
        "${source_dir}/implementation/SocketEvent_kqueue.cpp")
    target_compile_definitions(securitysocket PRIVATE USE_KQUEUE)

elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_path(LIBURING_INCLUDE_DIR liburing.h)
    find_library(LIBURING_LIBRARY NAMES uring)

    if(LIBURING_INCLUDE_DIR AND LIBURING_LIBRARY)
        execute_process(
            COMMAND uname -r
            OUTPUT_VARIABLE KERNEL_VERSION_STRING
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _ ${KERNEL_VERSION_STRING})
        set(KERNEL_MAJOR ${CMAKE_MATCH_1})
        set(KERNEL_MINOR ${CMAKE_MATCH_2})

        if((KERNEL_MAJOR GREATER 5) OR (KERNEL_MAJOR EQUAL 5 AND KERNEL_MINOR GREATER_EQUAL 1)
           OR SECURITYSOCKET_FORCE_IO_URING)
            message(STATUS "io_uring backend (kernel ${KERNEL_VERSION_STRING})")
            list(APPEND securitysocket_source_files
                "${source_dir}/implementation/SocketEvent_iouring.cpp")
            target_compile_definitions(securitysocket PRIVATE USE_IO_URING)
            target_include_directories(securitysocket PRIVATE ${LIBURING_INCLUDE_DIR})
            target_link_libraries(securitysocket ${LIBURING_LIBRARY})
        else()
            message(STATUS "epoll backend (kernel ${KERNEL_VERSION_STRING} < 5.1)")
            list(APPEND securitysocket_source_files
                "${source_dir}/implementation/SocketEvent_epoll.cpp")
            target_compile_definitions(securitysocket PRIVATE USE_EPOLL)
        endif()
    else()
        message(STATUS "epoll backend (liburing not found)")
        list(APPEND securitysocket_source_files
            "${source_dir}/implementation/SocketEvent_epoll.cpp")
        target_compile_definitions(securitysocket PRIVATE USE_EPOLL)
    endif()

else()
    # 기타 POSIX (fallback)
    list(APPEND securitysocket_source_files
        "${source_dir}/implementation/SocketEvent_poll.cpp")
    target_compile_definitions(securitysocket PRIVATE USE_POLL)
endif()
```

### -fPIC 조건 확장

```cmake
# 변경 전
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")

# 변경 후
if(UNIX AND NOT WIN32)
```

---

## 6. `SocketRequestServer` / `SocketBroadcastServer` 변경 범위

### SocketBroadcastServer

**변경 없음.**
`SocketEventListener` (단일 소켓)는 모든 플랫폼에서 poll/WSAPoll을 그대로 사용하므로
[SocketBroadcastServer.cpp](src/implementation/SocketBroadcastServer.cpp) 코드를 건드릴 필요가 없다.

### SocketRequestServer

**루프 구조 변경 없음.** 단, IOCP 백엔드에서 두 가지 사항을 확인해야 한다.

1. **`PassiveSocket::accept()` 수정**: IOCP AcceptEx 완료 후 `server_context.accept_socket`에
   수락된 fd가 저장되어 있으므로, `PassiveSocket::accept()`가 이를 감지해 래핑하도록 수정한다.
   [SocketRequestServer.cpp](src/implementation/SocketRequestServer.cpp)의 `run()` 루프 자체는 변경 없다.

2. **소켓 논블로킹 확인**: IOCP / io_uring에서 `wait()` 반환 후 즉시 `read()` / `write()`가
   성공하려면 `ServerActiveSocket`이 논블로킹 상태여야 한다.
   `PassiveSocket.cpp`, `ServerActiveSocket.cpp`에서 `O_NONBLOCK` / `FIONBIO` 설정 여부를 확인한다.

---

## 7. 구현 순서

복잡도 낮은 순서로 진행하되, 각 단계마다 기존 테스트 통과를 확인한다.

| 단계 | 작업 | 비고 |
|---|---|---|
| **1** | `SocketEvent.hpp` private를 불투명 버퍼로 교체 + `SocketEvent_poll.cpp` 통합 | 기존 플랫폼 회귀 없이 기반 확립 |
| **2** | `SocketEvent_epoll.cpp` | readiness 모델, 가장 단순 |
| **3** | `SocketEvent_kqueue.cpp` + macOS.md 수정 병행 | epoll과 구조 유사 |
| **4** | `SocketEvent_iocp.cpp` + `PassiveSocket::accept()` 수정 | zero-byte 트릭 검증 필요 |
| **5** | `SocketEvent_iouring.cpp` + runtime fallback | 커널 버전 분기, `POLL_UPDATE` 조건 컴파일 |

---

## 참고: 각 백엔드 핵심 API 요약

### epoll
```c
int epfd = epoll_create1(EPOLL_CLOEXEC);
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);   // ev.data.ptr = context
epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
int n = epoll_wait(epfd, events, MAX, timeout_ms);
// events[i].data.ptr → SocketEventContext*
```

### kqueue
```c
int kqfd = kqueue();
struct kevent change;
EV_SET(&change, fd, EVFILT_READ, EV_ADD, 0, 0, context);
kevent(kqfd, &change, 1, NULL, 0, NULL);
int n = kevent(kqfd, NULL, 0, events, MAX, &ts);
// events[i].udata → SocketEventContext*
```

### IOCP
```c
HANDLE iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
CreateIoCompletionPort((HANDLE)fd, iocp, (ULONG_PTR)context, 0); // 소켓 연결 (1회)
WSARecv(fd, &buf0, 1, &bytes, &flags, &context->overlapped, NULL); // zero-byte
GetQueuedCompletionStatus(iocp, &bytes, &key, &overlapped, timeout_ms);
// key → SocketEventContext*,  완료 후 즉시 재제출
```

### io_uring
```c
io_uring ring;
io_uring_queue_init(256, &ring, 0);
struct io_uring_sqe *sqe = io_uring_get_sqe(&ring);
io_uring_prep_poll_add(sqe, fd, POLLIN);
sqe->user_data = (uint64_t)context;
io_uring_submit(&ring);
struct io_uring_cqe *cqe;
io_uring_wait_cqe_timeout(&ring, &cqe, &ts);
// cqe->user_data → SocketEventContext*
// cqe->res → poll revents 비트
io_uring_cqe_seen(&ring, cqe);
// poll_add 재제출 필수 (one-shot)
```
