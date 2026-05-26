# Milestone: HTTP / WebSocket / User Protocol 통합 지원

> **상태:** 1차 리뷰 반영 (2026-05-26)
> **참조:** [mileston_request.md](./mileston_request.md)
> **버전:** v3 첫 메이저 릴리스 (renumbering 예정. 이 문서에서는 Phase 번호로만 추적)

---

## 0. 이 문서의 목적

`mileston_request.md`에 적힌 9개 항목을 실제 구현 계획으로 옮긴다.
**확정된 결정**과 **리뷰가 필요한 결정 (⚠️ DECISION)** 을 분리해 표시하니, 검토하면서 ⚠️ 표시된 부분에 답을 달아주면 다음 phase로 진행한다.

---

## 1. 전체 범위

### 1-1. In Scope

- 경량 HTTP/1.1 파서 빌드 시스템 편입
- 서버: HTTP 라우팅 (`get/post/put/del/patch/head/options`)
- 서버: WebSocket handshake + frame 인/디코드
- 서버: User Protocol over WebSocket tunneling
- 서버: 단일 포트에서 HTTP / User Protocol 자동 분기 (sniffing)
- 핸들러: `SocketHttpRequestHandler`, `SocketCustomProtocolRequestHandler` 분리 + 조합 가능
- 클라이언트: `SocketHttpClient`, `SocketWebSocketClient`

### 1-2. Out of Scope (이번 마일스톤에서 하지 않음)

- HTTP/2, HTTP/3, QUIC
- Chunked transfer encoding (Content-Length 기반만 지원)
- HTTP 압축 (gzip / deflate)
- WebSocket permessage-deflate 압축 확장
- HTTPS proxy / CONNECT 메서드
- 비동기 콜백 기반 API (기존 동기 모델 유지)

---

## 2. 핵심 설계 결정

### 2-1. ✅ HTTP Parser 선택: **picohttpparser**

| 후보 | 라이선스 | 크기 | 비고 |
|---|---|---|---|
| **picohttpparser** | MIT | 1 헤더 + 1 .c (~700 LOC) | 헤더만 파싱. body는 우리가 처리 |
| llhttp | MIT | ~10K LOC, 생성 코드 | Node.js 사용. 더 무거움 |
| 자체 구현 | - | - | RFC 7230 충실 구현 부담 큼 |

**선택:** picohttpparser

- 단일 파일이라 빌드 통합 단순
- body 처리는 우리 코드가 책임 → Content-Length 기반 read loop만 추가하면 됨
- WebSocket handshake도 같은 파서로 처리

✅ **DECISION-1 (확정):** vendoring 방식.

- 경로: `src/implementation/thirdparty/picohttpparser/`
- `readme.md`의 새 "Third-Party Dependencies" 섹션에 다음을 기록:
  - 라이브러리 이름, 버전 (커밋 해시), 라이선스 (MIT), 원본 repo URL, 용도
  - SECURITYSOCKET_USING_TLS=ON일 때만 빌드되는지 / 항상 빌드되는지 명시 (HTTP는 TLS와 독립 → 항상 빌드)
- 의존성 갱신 시 절차: 새 커밋 받아서 vendoring + readme의 커밋 해시 갱신 + CHANGELOG 기록

---

### 2-2. ✅ `SocketClientConnection` 설계 — Owner/View 분리

✅ **DECISION-2 (확정):** Connection은 **버퍼와 상태의 owner**, Request/Response는 그 버퍼에 대한 **typed view**.

기존 `src/implementation/SocketConnection.hpp`의 구조를 그대로 확장:

- 한 연결당 `input_header_buffer`, `input_payload_buffer`, `output_buffer` 보유 (이미 vector로 관리 중)
- 사용자에게는 두 가지만 노출:
  - **연결 메타데이터** (ip, port, 기타 부수 정보)
  - **현재 요청/응답에 대한 view** (HttpRequest/HttpResponse 또는 SocketUserProtocolRequest/Response)

**API 구조:**

```cpp
// 사용자가 보는 인터페이스 — PIMPL
class SECURITYSOCKET_API SocketClientConnection {
public:
    // 연결 메타데이터
    const char* ip() const;
    uint32_t    port() const;
    bool        isSecure() const;       // TLS 여부
    bool        isWebSocket() const;    // 현재 모드가 WS tunneling인지

    // 사용자 데이터 포인터 (per-connection 상태 저장용; 옵션)
    void  setUserData(void* p);
    void* getUserData() const;

private:
    void* _impl;   // SocketClientConnectionImpl* (서버가 소유)
};

// 내부 구현 — 서버가 fd 단위로 소유
class SocketClientConnectionImpl {
public:
    // 소켓 + 처리 상태
    ServerActiveSocket* socket;
    ConnMode            mode;
    ProcessState        state;

    // 버퍼 — 기존 SocketConnection 구조 계승
    std::vector<char> input_header_buffer;
    std::vector<char> input_payload_buffer;
    std::vector<char> output_buffer;

    // 누적 카운터
    size_t header_received = 0;
    size_t payload_received = 0;
    size_t response_written = 0;

    // 메타데이터
    char    peer_ip[64];
    uint32_t peer_port;
    bool    is_secure;
    void*   user_data;
};
```

**Request/Response는 view:**

```cpp
// HTTP 요청 — 위 input_header_buffer / input_payload_buffer를 가리킴
class HttpRequest {
public:
    const char* method() const;
    const char* path() const;
    const char* header(const char* name) const;
    const char* pathParam(const char* name) const;
    const void* body() const;        // input_payload_buffer.data()
    size_t      bodySize() const;
private:
    SocketClientConnectionImpl* _conn;   // view back to owner
    // 파싱 결과 캐시 (path/method 포인터, 헤더 인덱스, path param 매핑)
};

// HTTP 응답 — output_buffer에 직접 쓰는 builder
class HttpResponse {
public:
    HttpResponse& status(int code);
    HttpResponse& header(const char* name, const char* value);
    HttpResponse& body(const void* data, size_t size);
    HttpResponse& json(const char* json_str);
private:
    SocketClientConnectionImpl* _conn;   // output_buffer를 직접 채움
};

// Custom 프로토콜도 동일 패턴 — 다른 view 타입
class SocketUserProtocolRequest {
public:
    const void* header() const;       // input_header_buffer.data()
    size_t      headerLength() const;
    const void* payload() const;      // input_payload_buffer.data()
    size_t      payloadLength() const;
private:
    SocketClientConnectionImpl* _conn;
};

class SocketUserProtocolResponse {
public:
    void*  data();                    // output_buffer.data()
    size_t capacity() const;
    void   setLength(size_t n);
private:
    SocketClientConnectionImpl* _conn;
};
```

**핵심 정리:**

- 한 fd당 `SocketClientConnectionImpl` 하나 (기존 `SocketConnection`이 이 역할 수행)
- `SocketClientConnection`은 사용자가 들고 다닐 가벼운 핸들 (PIMPL)
- `HttpRequest`/`HttpResponse`/`SocketUserProtocolRequest`/`SocketUserProtocolResponse`는 핸들러 호출 시점에 stack에 생성되는 view (생성 비용 거의 0)
- `clientId()`는 제외 — 사용자 요구 없음
- 라우터 콜백 시그니처: `[](SocketClientConnection& conn, HttpRequest& req, HttpResponse& res)` (mileston_request.md 항목 4와 일치)

---

### 2-3 & 2-4. ✅ Diamond 해결 + `supportXxx()` 유지 — 둘 다 채택

✅ **DECISION-3 & 4 (확정):** **virtual inheritance + supportXxx() 플래그를 함께 사용.** 둘은 대안이 아니라 **다른 문제를 해결하는 도구**임.

#### 두 메커니즘이 해결하는 문제

| 메커니즘 | 해결하는 것 |
|---|---|
| `virtual` inheritance | C++ 컴파일 차원의 다중 상속 ambiguity (한 base의 멤버가 두 벌 존재) |
| `supportXxx()` 플래그 | 런타임에 dynamic_cast/RTTI 없이 능력 탐지 |

→ virtual 상속 안 쓰면 멀티 상속 자체가 컴파일 에러. 플래그를 쓰든 안 쓰든 무관.

#### 최종 구조

```cpp
class SECURITYSOCKET_API SocketRequestHandler {
public:
    virtual ~SocketRequestHandler() = default;
    virtual void onConnected   (const SocketClientConnection& conn) {}
    virtual void onDisconnected(const SocketClientConnection& conn) {}

    // 사용자 부담 없이 derived 클래스가 자동으로 설정
    virtual bool supportHttp()         const { return false; }
    virtual bool supportUserProtocol() const { return false; }
    virtual bool supportWebSocket()    const { return false; }
};

class SECURITYSOCKET_API SocketHttpRequestHandler
    : public virtual SocketRequestHandler {
public:
    bool supportHttp() const override final { return true; }   // 자동
    virtual void registerRoutes(SocketHttpRouter& router) = 0;
};

class SECURITYSOCKET_API SocketCustomProtocolRequestHandler
    : public virtual SocketRequestHandler {
public:
    SocketCustomProtocolRequestHandler() = default;
    explicit SocketCustomProtocolRequestHandler(const WebSocketConfiguration& ws)
        : _ws_config(ws) {}

    bool supportUserProtocol() const override final { return true; }       // 자동
    bool supportWebSocket()    const override final { return _ws_config.valid(); }  // 자동

    const WebSocketConfiguration& webSocketConfig() const { return _ws_config; }
    // ... custom protocol virtual methods ...
private:
    WebSocketConfiguration _ws_config;
};
```

사용자가 직접 플래그를 override할 필요 없음. 상속만 하면 자동 결정.

#### Virtual inheritance 성능 분석 — Phase 5 dispatch 루틴 기준

**오버헤드 종류 3가지:**

| 항목 | 비용 | 빈도 |
|---|---|---|
| Object 크기 증가 | +8 bytes (vptr to virtual base offset) | 한 번 (handler 생성 시) |
| Virtual base 멤버 호출 (`handler.onConnected(...)`) | 추가 1회 indirection (~1-2 ns) | per connection lifecycle |
| Virtual base로 upcast (`(SocketRequestHandler*)handler`) | vtable lookup (~1-2 ns) | per dispatch |

**Request handler routine 1회 처리 비용 비교 (대략):**

| 단계 | 비용 |
|---|---|
| `recv()` syscall | 1-10 μs |
| TLS read (TLS_ON일 때) | 10-50 μs |
| Header 파싱 | 0.1-1 μs |
| `handler.process(...)` 호출 | 사용자 코드 (가변) |
| **virtual base 추가 indirection** | **1-2 ns** |
| Response `send()` | 1-10 μs |

→ virtual inheritance 오버헤드는 **전체의 0.001~0.01%**. 네트워크 I/O가 압도적이라 측정 불가능한 수준.

**결론:** 성능 문제 없음. virtual inheritance를 안전하게 사용.

#### 왜 dynamic_cast를 안 쓰는가

- 사용자 의도와 일치 (`supportXxx()` 플래그가 명시적 표시)
- RTTI 비활성화 빌드에서도 동작
- dynamic_cast는 inheritance tree walk (~10-50 ns) — 플래그 호출(1-2 ns)보다 비쌈
- 정적으로 알 수 있는 정보를 런타임에 풀 필요 없음

#### Server에서의 사용

```cpp
SocketResult SocketRequestServer::open(SocketRequestHandler* handler, size_t n) {
    // ① 능력 탐지 (RTTI 없이)
    bool has_http   = handler->supportHttp();
    bool has_custom = handler->supportUserProtocol();
    bool has_ws     = handler->supportWebSocket();

    // ② 캐스팅은 static_cast로 (플래그 확인 후이므로 안전)
    SocketHttpRequestHandler*           http   = nullptr;
    SocketCustomProtocolRequestHandler* custom = nullptr;
    if (has_http)
        http = static_cast<SocketHttpRequestHandler*>(handler);
    if (has_custom)
        custom = static_cast<SocketCustomProtocolRequestHandler*>(handler);

    // ③ has_http / has_custom / has_ws 조합으로 dispatch 루틴 결정
    // ...
}
```

> ⚠️ **주의:** static_cast로 cross-cast (sibling 간 변환)는 불가. 항상 `SocketRequestHandler*`에서 → 자식으로만 다운캐스트. 위 예시처럼 항상 base pointer에서 시작하면 안전.

---

### 2-5. ✅ WebSocket 활성화 방법

원안 (항목 5):
```cpp
SocketCustomProtocolRequestHandler();                                  // WS OFF
SocketCustomProtocolRequestHandler(const WebSocketConfiguration& cfg); // WS ON
struct WebSocketConfiguration { const char* pattern; };
```

→ **그대로 채택.** 깔끔하고 의도 명확함.

추가 명세:
- pattern은 path glob ("/ws", "/api/stream/*" 등)
- 미설정 시 WS 핸드셰이크 요청은 **400 Bad Request** 응답 후 close
- pattern 매칭 성공 시 → handshake → `WS_TUNNELED_CUSTOM` 모드로 전환
- 이후 WS binary frame의 payload가 그대로 `headerSize/payloadSize/process`로 흐름

---

### 2-6. ✅ Path Parameter 문법: **`:id`** (Express 스타일)

✅ **DECISION-5 (확정):** `:id` 스타일 채택. 사용자 요구: "parsing 비용이 적은 걸로".

**파싱 비용 비교:**

| 문법 | 한 segment 식별 비용 |
|---|---|
| `:id` | 첫 char가 `:`인지 1회 비교 |
| `{id}` | 첫 char가 `{`인지 + 끝 char가 `}`인지 + 잘못된 형태 검증 (3회 비교) |

→ 미세하지만 `:id`가 더 단순함. 라우트 등록 시 한 번만 수행되는 비용이지만, 코드도 더 단순함.

**매칭 방식: Trie 기반** (O(path 깊이), 라우트 수 무관)

```cpp
router.get("/user/:id",            handler1);
router.get("/user/:id/posts",      handler2);
router.get("/user/:id/posts/:pid", handler3);
```

**핸들러 안에서:**

```cpp
const char* id = req.pathParam("id");      // ":id"의 값
const char* pid = req.pathParam("pid");    // ":pid"의 값
```

**충돌 규칙:** 정적 경로 우선. `/user/me`와 `/user/:id`가 같이 등록되면 `/user/me`가 우선 매칭.

---

### 2-7. ✅ `HttpRequest` / `HttpResponse` 메모리 모델

✅ **DECISION-6 (확정):** 기존 `SocketConnection`의 vector buffer 위에 view로 올리는 방식 채택. 단, body 크기에 따라 **vector를 동적으로 resize** 함.

#### 사용자 질문 답변

**Q1: "vector에 대한 view를 제공하는데 무리가 없는지?"**
→ **무리 없음.** 기존 [SocketConnection.hpp:65](src/implementation/SocketConnection.hpp#L65)의 `input_header_buffer`, `input_payload_buffer`, `output_buffer` 패턴을 그대로 활용. HttpRequest는 이 vector를 가리키는 가벼운 view (const char* 몇 개 + path param map). 추가 메모리 할당 없음.

**Q2: "http request나 response가 너무 클 수도 있는지?"**
→ **있을 수 있음.** 일반적인 크기 분포:

| 사용 케이스 | 일반적 크기 |
|---|---|
| REST API JSON 요청 | 100 B ~ 10 KB |
| Form submit / 로그인 | 100 B ~ 1 KB |
| 이미지 / 파일 업로드 | 10 KB ~ 100 MB |
| Webhook payload | 1 KB ~ 1 MB |
| 비디오 / 대용량 업로드 | 100 MB ~ GB |

→ 일반 REST는 KB 단위지만, 업로드 시나리오는 MB~GB로 폭증.

**Q3: "header를 통해 response의 크기를 통해 잠시 body에 해당하는 vector를 교체할 수도 있는거지"**
→ **정확함. 그 방식을 채택.** 흐름:

```
1. picohttpparser로 HTTP 헤더 파싱 (input_header_buffer에 누적)
2. Content-Length 헤더 추출 → body 크기 N 결정
3. N > max_request_body_size 설정값 ? → 413 응답 후 close
   N ≤ pdu_size 기본값         ? → 기존 input_payload_buffer 그대로 사용
   N >  pdu_size 기본값        ? → input_payload_buffer.resize(N) 으로 확장
4. N byte 누적 수신
5. HttpRequest view 생성하여 handler 호출
6. 요청 완료 후 keep-alive면 vector 크기를 pdu_size로 다시 축소 (shrink_to_fit은 호출하지 않음 — capacity는 유지)
```

#### Config 추가

```cpp
class SocketConfiguration {
    // 기존 필드들...
    uint32_t max_http_request_body_size = 1 * 1024 * 1024;  // 1 MB default
    uint32_t max_http_response_body_size = 8 * 1024 * 1024; // 8 MB default
    // ...
};
```

초과 시: 413 Payload Too Large 응답 후 연결 종료.

#### 응답 직렬화 — output_buffer 패턴

```cpp
class HttpResponse {
public:
    HttpResponse& status(int code);
    HttpResponse& header(const char* name, const char* value);
    HttpResponse& body(const void* data, size_t size);
    HttpResponse& bodyText(const char* str);
    HttpResponse& json(const char* json_str);

    // 응답 완료 신호 — 라이브러리가 status line + headers + body를
    // output_buffer에 직렬화. 필요시 output_buffer.resize() 발생.
    // 핸들러가 return하면 라이브러리가 호출 (사용자 명시 호출 불필요)
private:
    SocketClientConnectionImpl* _conn;
    int _status_code = 200;
    std::vector<std::pair<std::string, std::string>> _pending_headers;
    const void* _body_data = nullptr;
    size_t _body_size = 0;
};
```

**큰 응답 처리:**

- `body()`의 인자 size가 `output_buffer.capacity()` 초과 시 자동 resize
- 단, `max_http_response_body_size` 초과면 internal error (500) 응답
- 사용자가 직접 큰 데이터를 한 번에 보낼 때만 발생. 일반 JSON 응답은 영향 없음

#### 향후 확장 (이번 마일스톤 out of scope)

- Chunked transfer encoding (스트리밍 응답)
- Body를 파일로 spill (메모리 압력 회피)
- 사용자 콜백 기반 stream read API

→ 이번 마일스톤은 **(A) view-over-vector + resize 전략**으로 충분. 99% 사용 케이스 커버.

---

### 2-9. ✅ 누적 버퍼 경계 케이스

✅ **DECISION-a1 (확정 — 추가):** sniffing 단계에서 받은 byte가 다음 단계 버퍼에 이미 들어있을 수 있음. 모든 단계의 receive 루틴이 이를 감안해야 함.

#### 시나리오

```
SNIFFING 단계에서 16 byte 누적 → 첫 byte 보고 CUSTOM 판정
  ↓
mode = CUSTOM_HEADER
  ↓
사용자 정의 headerSize = 8
  ↓
"이미 16 byte 들어와 있음" → header 8 byte는 충족됨 + payload 시작 8 byte도 이미 있음
  ↓
payloadSize = 100
  ↓
payload 누적 수신 시작점: 이미 받은 8 byte를 감안하여 92 byte만 더 받기
```

#### 구현 지침

`SocketClientConnectionImpl`에 누적 카운터를 통일 관리:

```cpp
// SNIFFING이 끝나면 받은 byte는 모두 input_header_buffer에 들어 있음
size_t sniffed_bytes;   // SNIFFING에서 받은 총 byte

// 모드 전환 후 header receive 루틴:
//   target_header_size = handler.headerSize(conn)
//   already_have = min(sniffed_bytes, target_header_size)
//   remaining = target_header_size - already_have
//   → remaining byte만 추가 수신

// 만약 sniffed_bytes > target_header_size 이면:
//   초과분 (sniffed_bytes - target_header_size) byte는 payload의 시작
//   → input_payload_buffer 앞부분에 memcpy 해두고
//     payload_received = sniffed_bytes - target_header_size 로 초기화
```

#### 모든 모드 전이에서 동일 원칙 적용

| 전이 | 누적 버퍼 처리 |
| --- | --- |
| SNIFFING → CUSTOM_HEADER | 위 시나리오 |
| SNIFFING → HTTP_RECV | 받은 byte는 HTTP 헤더의 일부. picohttpparser가 incremental 파싱 지원 — 그대로 넘기면 됨 |
| HTTP_RECV → WS_HANDSHAKE | handshake 응답 전송 후 mode 전환. 다음 byte부터는 WS frame |
| HTTP_RECV → WS_TUNNELED_CUSTOM | 위와 동일 |
| keep-alive HTTP 종료 → HTTP_RECV (재진입) | 한 요청 끝난 후 input buffer 잔여 byte가 다음 요청의 시작일 수 있음 (HTTP pipelining 또는 빠른 후속 요청). 잔여 byte 보존 필요 |

#### 테스트 케이스 (Phase 5에 추가)

- [ ] sniffed_bytes < headerSize: 일반 케이스 (추가 수신)
- [ ] sniffed_bytes == headerSize: 정확히 일치
- [ ] sniffed_bytes > headerSize: 초과분이 payload로 이월
- [ ] sniffed_bytes > headerSize + payloadSize: 한 번의 recv()로 여러 메시지 수신 (다음 메시지 시작 byte까지 들어옴)

---

### 2-8. ⚠️ 항목 7의 sniffing 흐름 — 재정리 필요

원안의 의사코드는 가독성이 떨어지고 일부 조건이 중복됨. 다시 정리:

```
[연결 accept]
   │
   ▼
mode = SNIFFING
   │
   ▼
첫 N byte 누적 (최소 4 byte, 최대 16 byte까지)
   │
   ▼
ProtocolSniffer::detect(buf, len)
   ├── HTTP                     : mode = HTTP_RECV
   ├── CUSTOM (magic 또는 그 외) : mode = CUSTOM_HEADER
   ├── UNKNOWN                  : close (400 or just drop)
   └── NEED_MORE                : 더 받기

[mode = HTTP_RECV]
   │
   ▼
picohttpparser로 헤더 파싱 (\r\n\r\n까지)
   │
   ▼
HttpRequest 구성
   │
   ├── handler에 SocketHttpRequestHandler 있음?
   │      ├── path 매칭 라우트 있음?
   │      │      ├── Yes → Content-Length만큼 body 읽기 → handler 실행 → response 전송
   │      │      │         └─ keep-alive면 mode = HTTP_RECV로 복귀, 아니면 close
   │      │      └─ No → 404 + close (or fallback 라우트)
   │      └─ Upgrade: websocket 헤더 있음?
   │           ├── handler에 SocketCustomProtocolRequestHandler + WS config 있음?
   │           │      ├── path가 ws config의 pattern 매칭?
   │           │      │      └── Yes → handshake (101) → mode = WS_TUNNELED_CUSTOM
   │           │      │           No → 404 + close
   │           │      └── No → 400 + close
   │           └─ 그 외: 일반 HTTP 라우트 매칭 시도
   └─ HTTP handler 없음:
        └─ Upgrade 확인 (위와 동일), 그 외엔 400 + close

[mode = CUSTOM_HEADER]
   │
   ▼
SocketCustomProtocolRequestHandler::headerSize() 만큼 누적 수신
   ├── SNIFFING에서 받은 byte가 헤더 일부였음 → 누적 버퍼에 이미 들어있음
   │   누적된 양 ≥ headerSize면 즉시 다음 단계
   ▼
classifyMode(connection) → FAST / SLOW / STREAM
   │
   ▼
payloadSize() 만큼 누적 수신
   │
   ▼
process() 또는 processWithoutResponse() 호출
   │
   ▼
응답 전송 → mode = CUSTOM_HEADER로 복귀 (다음 메시지 대기)

[mode = WS_TUNNELED_CUSTOM]
   │
   ▼
WS frame 디코드 (FIN/opcode/mask/length 파싱, unmask)
   │
   ├── opcode = PING  → PONG 자동 응답
   ├── opcode = PONG  → 무시 (또는 통계)
   ├── opcode = CLOSE → CLOSE echo → close
   ├── opcode = BINARY 또는 CONTINUATION → payload 누적
   │
   ▼
한 메시지 완성 (FIN=1)되면
   │
   ▼
누적된 payload를 CUSTOM_HEADER 처리 루틴과 동일하게 처리:
   - SocketUserProtocolRequest.data() = payload
   - SocketUserProtocolRequest.length = payload size
   - headerSize/payloadSize/process 호출
   │
   ▼
응답은 WS binary frame으로 포장해서 전송
```

원안의 "user_defined_header_size > http header" 같은 케이스가 **자연스럽게 사라짐** — sniffing 단계에서 받은 byte는 항상 누적 버퍼에 들어가 있고, 모드 전환 시 그 버퍼를 그대로 활용하기 때문.

---

## 3. 구현 Phase

### Phase 1 — Parser 편입

**범위:** picohttpparser를 빌드에 추가 + 기본 HttpRequest 구조체

**파일:**
- `src/implementation/thirdparty/picohttpparser/picohttpparser.{h,c}` (vendoring)
- `src/implementation/http/HttpRequest.{hpp,cpp}` (파싱 결과 view)
- `CMakeLists.txt`: picohttpparser를 securitysocket에 포함

**검증:**
- 단위 테스트: 다양한 HTTP 요청 문자열 → HttpRequest 정상 파싱
- 빌드: SECURITYSOCKET_USING_TLS=ON/OFF 모두 통과
- 플랫폼: Windows / Android / Linux

**완료 기준:**
- [ ] `HttpRequest` 객체 생성 + method/path/header/body 추출 가능
- [ ] picohttpparser 빌드 통합 (정적/공유 라이브러리 모두)
- [ ] 기존 SocketClient/Server 기능에 영향 없음

---

### Phase 2 — 공통 타입 + Handler Base

**범위:** 항목 2, 3 — `SocketClientConnection`, `SocketRequestHandler` base

**파일:**

- `src/implementation/SocketClientConnection.{hpp,cpp}`
- `src/SecuritySocket.hpp`: `SocketRequestHandler` base 추가

**API 스케치:** (§2-2, §2-3/4 확정안 반영)

```cpp
class SECURITYSOCKET_API SocketClientConnection {
public:
    const char* ip() const;
    uint32_t    port() const;
    bool        isSecure() const;
    bool        isWebSocket() const;
    void        setUserData(void* p);
    void*       getUserData() const;
private:
    void* _impl;
};

class SECURITYSOCKET_API SocketRequestHandler {
public:
    virtual ~SocketRequestHandler() = default;
    virtual void onConnected   (const SocketClientConnection& conn) {}
    virtual void onDisconnected(const SocketClientConnection& conn) {}
    virtual bool supportHttp()         const { return false; }
    virtual bool supportUserProtocol() const { return false; }
    virtual bool supportWebSocket()    const { return false; }
};
```

**완료 기준:**

- [ ] `SocketClientConnectionImpl`이 기존 `SocketConnection`의 buffer/state 책임 인수
- [ ] virtual 상속 + supportXxx 플래그 조합 동작 (Phase 3/4에서 실제 검증)
- [ ] 기존 `SocketRequestHandler` 사용자 마이그레이션 경로 (DECISION-8)

---

### Phase 3 — HTTP 핸들러 + 라우터

**범위:** 항목 4

**파일:**
- `src/implementation/http/HttpRouter.{hpp,cpp}` (Trie 기반)
- `src/implementation/http/HttpResponse.{hpp,cpp}` (builder)
- `src/SecuritySocket.hpp`: `SocketHttpRequestHandler` 공개

**API:**
```cpp
class SocketHttpRouter {
public:
    using HandlerFn = std::function<void(SocketClientConnection&, HttpRequest&, HttpResponse&)>;
    void get    (const char* pattern, HandlerFn fn);
    void post   (const char* pattern, HandlerFn fn);
    void put    (const char* pattern, HandlerFn fn);
    void del    (const char* pattern, HandlerFn fn);
    void patch  (const char* pattern, HandlerFn fn);
    void head   (const char* pattern, HandlerFn fn);
    void options(const char* pattern, HandlerFn fn);
    void fallback(HandlerFn fn);
};

class SocketHttpRequestHandler : public virtual SocketRequestHandler {
public:
    virtual void registerRoutes(SocketHttpRouter& router) = 0;
};
```

**완료 기준:**

- [ ] Trie 라우터: 정적 path + `:id` 매칭 동작
- [ ] HttpResponse: status/header/body/json 모두 동작
- [ ] keep-alive 지원 (HTTP/1.1 default)
- [ ] body 크기 제한 검증 (413 응답)
- [ ] body가 pdu_size 초과 시 input_payload_buffer 동적 resize

---

### Phase 4 — User Protocol 핸들러 refactoring

**범위:** 항목 5 — 기존 `SocketRequestHandler` → `SocketCustomProtocolRequestHandler`로 이전

**파일:**
- `src/SecuritySocket.hpp`: `SocketCustomProtocolRequestHandler`, `SocketUserProtocolRequest/Response` 추가

**API:**
```cpp
struct WebSocketConfiguration {
    const char* pattern = nullptr;
    bool valid() const { return pattern != nullptr; }
};

class SocketUserProtocolRequest {
public:
    const void* data() const;   // header + payload 연속 버퍼
    size_t length() const;
    const void* header() const;
    size_t headerLength() const;
    const void* payload() const;
    size_t payloadLength() const;
};

class SocketUserProtocolResponse {
public:
    void* data();
    size_t capacity() const;
    void setLength(size_t n);
};

class SocketCustomProtocolRequestHandler : public virtual SocketRequestHandler {
public:
    SocketCustomProtocolRequestHandler();
    explicit SocketCustomProtocolRequestHandler(const WebSocketConfiguration& ws);

    const WebSocketConfiguration& webSocketConfig() const;

    virtual size_t headerSize(const SocketClientConnection& conn) = 0;
    virtual size_t payloadSize(const SocketClientConnection& conn,
                               const void* header) = 0;
    virtual SocketRequestMode classifyMode(const SocketClientConnection& conn,
                                           const void* header) = 0;
    virtual void process(const SocketClientConnection& conn,
                         const SocketUserProtocolRequest& req,
                         SocketUserProtocolResponse& res) = 0;
    virtual void processWithoutResponse(const SocketClientConnection& conn,
                                        const SocketUserProtocolRequest& req) = 0;
};
```

⚠️ **DECISION-7:** `headerSize`/`payloadSize` 시그니처 검토
- 기존: 매개변수 없음 (`size_t getHeaderSize()`)
- 신규: connection 받음 (per-connection 동작 차별화 가능)
- payloadSize: header 포인터를 받아야 길이 계산 가능
- 정합성 검토 필요

⚠️ **DECISION-8:** 기존 `SocketRequestHandler` 사용자 마이그레이션
- (a) 한 메이저 버전 동안 alias로 유지 (`using SocketRequestHandler = SocketCustomProtocolRequestHandler`)
- (b) v2.5에서 즉시 rename, 빌드 에러로 알림
- 추천: (a) — 사용자 부담 최소

**완료 기준:**
- [ ] 기존 user protocol 동작 그대로 (회귀 테스트 통과)
- [ ] WS config 설정 시 handshake 자동 수행
- [ ] WS 모드에서 frame unmask + payload 추출 후 동일 핸들러 호출

---

### Phase 5 — Server 흐름 통합

**범위:** 항목 7 — sniffing + dispatch + 모드 전이

**파일:**
- `src/implementation/SocketConnection.cpp`: state machine 확장
- `src/implementation/protocol/ProtocolSniffer.{hpp,cpp}`
- `src/implementation/protocol/HttpProcessor.{hpp,cpp}`
- `src/implementation/protocol/WsFrameCodec.{hpp,cpp}`

**핵심 클래스:**
```cpp
enum class ConnMode : uint8_t {
    SNIFFING,
    HTTP_RECV, HTTP_RESP,
    WS_HANDSHAKE, WS_TUNNELED_CUSTOM, WS_CLOSING,
    CUSTOM_HEADER, CUSTOM_PAYLOAD,
    CLOSED
};

class WsFrameCodec {
public:
    // 디코드: 누적 버퍼에서 한 frame 추출. NEED_MORE / OK / CONTROL_FRAME / ERROR
    enum class DecodeResult { NEED_MORE, OK, CONTROL_PING, CONTROL_PONG, CONTROL_CLOSE, ERROR };
    DecodeResult decode(const char* in, size_t in_len, size_t* consumed,
                        char* out, size_t out_capacity, size_t* out_len,
                        uint8_t* opcode, bool* fin);
    // 인코드: payload를 frame으로 감쌈
    size_t encode(const char* payload, size_t len, bool binary, bool mask,
                  char* out, size_t out_capacity);
};
```

**완료 기준:**
- [ ] sniff → HTTP / CUSTOM 분기 동작
- [ ] HTTP → WS upgrade 흐름 동작
- [ ] WS_TUNNELED_CUSTOM에서 raw TCP custom과 동일한 핸들러 호출
- [ ] keep-alive HTTP 연결에서 여러 요청 처리
- [ ] 누적 버퍼 경계 케이스 (sniffing에서 받은 byte가 header 일부) 테스트
- [ ] 단편화된 WS 메시지 (FIN=0 연속 frame) 재조립
- [ ] PING/CLOSE 제어 frame 자동 처리

---

### Phase 6 — HTTP Client

**범위:** 항목 8

**API:**
```cpp
class SocketHttpClient {
public:
    explicit SocketHttpClient(const SocketConfiguration& cfg);
    explicit SocketHttpClient(const SocketConfiguration& cfg,
                              const SocketTLSClientConfiguration& tls);

    SocketResult open();
    void close();

    // 요청 builder 패턴
    HttpRequestBuilder request(const char* method, const char* path);
    
    // 또는 헬퍼 메서드
    HttpResponse get   (const char* path);
    HttpResponse post  (const char* path, const void* body, size_t len);
    HttpResponse put   (const char* path, const void* body, size_t len);
    HttpResponse del   (const char* path);
    HttpResponse patch (const char* path, const void* body, size_t len);
    HttpResponse head  (const char* path);
    HttpResponse options(const char* path);
};
```

⚠️ **DECISION-9:** HttpResponse 반환 메모리 모델
- (a) 값 반환 (HttpResponse가 내부 버퍼 소유) — 단순
- (b) `HttpResponse&` out-param — 메모리 재사용
- 추천: (a) for 사용 편의

⚠️ **DECISION-10:** `SocketHttpClient`가 `SocketClient`를 상속? 합성?
- 원안: 상속
- 합성이 더 안전 (LSP 위반 가능성 회피)

**완료 기준:**
- [ ] 7개 메서드 모두 동작
- [ ] keep-alive 연결 재사용
- [ ] TLS 클라 (HTTPS) 동작
- [ ] timeout / 재시도 정책 (기존 SocketClient 설정 그대로)

---

### Phase 7 — WebSocket Client

**범위:** 항목 9 — 사용자가 아이디어 부족이라고 명시

**제안 API:**

```cpp
class SocketWebSocketClient {
public:
    explicit SocketWebSocketClient(const SocketConfiguration& cfg);
    explicit SocketWebSocketClient(const SocketConfiguration& cfg,
                                   const SocketTLSClientConfiguration& tls);

    SocketResult open();
    void close();

    // path와 sub-protocol 지정해서 handshake 수행
    SocketResult connect(const char* path,
                         const char* sub_protocol = nullptr);

    // 메시지 송신 (자동으로 frame으로 감쌈, mask 처리)
    SocketResult sendText  (const char* str);
    SocketResult sendBinary(const void* data, size_t len);
    SocketResult sendPing  (const void* data = nullptr, size_t len = 0);

    // 메시지 수신 (한 메시지 단위로 받음)
    struct Message {
        const void* data;
        size_t      length;
        bool        binary;
    };
    SocketResult receive(Message& out, uint64_t timeout_ms);

    // 정상 종료
    SocketResult sendClose(uint16_t code = 1000, const char* reason = nullptr);
};
```

**사용 예시:**
```cpp
SocketWebSocketClient client{cfg};
client.open();
client.connect("/chat");

client.sendText("hello");

SocketWebSocketClient::Message msg;
while (client.receive(msg, /*timeout_ms=*/5000).code() == SocketCode::SUCCESS) {
    if (msg.binary) handle_binary(msg.data, msg.length);
    else            handle_text(static_cast<const char*>(msg.data), msg.length);
}

client.sendClose();
client.close();
```

⚠️ **DECISION-11:** 동기 receive vs 콜백
- (a) 동기 `receive(timeout)` — 기존 라이브러리 톤과 일치
- (b) `onMessage` 콜백 + 내부 스레드
- 추천: (a) 우선, (b)는 향후

⚠️ **DECISION-12:** Custom protocol over WS 클라이언트 API
- 서버는 자동으로 WS frame을 벗기고 Custom handler에 넘김
- 클라이언트도 같은 방식? 즉 `SocketCustomProtocolClient(ws_config)`?
- 또는 `SocketWebSocketClient::sendBinary(custom_protocol_bytes)`로 사용자가 직접 wrap?
- 추천: 후자 (단순) + 헬퍼 클래스는 v2.8 이후

**완료 기준:**
- [ ] handshake + Sec-WebSocket-Accept 검증
- [ ] frame 인/디코드 (masking 포함)
- [ ] PING 자동 PONG 응답
- [ ] 정상/비정상 종료 처리
- [ ] TLS (wss://) 동작

---

## 4. 마이그레이션 / 호환성

### 4-1. Breaking changes

v3은 메이저 버전이므로 breaking change 허용. 호환 alias 제공 여부는 DECISION-8에서 결정.

| 항목 | 영향 | 대응 |
|---|---|---|
| `SocketRequestHandler` rename → `SocketCustomProtocolRequestHandler` | 기존 사용자 코드 수정 필요 | v3.0에서 즉시 rename (DECISION-8 결정 따름) |
| `SocketRequestServer::open()` 시그니처 | 기존 sig 유지 + 새 오버로드 추가 | 호환 |
| `SocketRequestHandler::onClientConnected(ip, port)` → `onConnected(connection)` | 시그니처 변경 | v2.5: 둘 다 제공 (기본 구현이 새 시그니처로 위임) / v3.0: 옛 시그니처 제거 |

### 4-2. 기존 코드 영향 없는 것

- `SocketClient`, `SocketBroadcastServer` (이번 마일스톤 무관)
- TLS 설정 API
- `SocketConfiguration`

---

## 5. 결정 현황

### 5-1. 1차 리뷰에서 확정된 결정 (2026-05-26)

| ID | 결정 |
|---|---|
| DECISION-1 | vendoring (`src/implementation/thirdparty/picohttpparser/`) + readme.md에 의존성 기록 |
| DECISION-2 | SocketClientConnectionImpl은 buffer/state owner, Request/Response는 view. clientId() 제외 |
| DECISION-3 & 4 | virtual inheritance + supportXxx() 플래그 둘 다 사용 (서로 다른 문제를 해결). 성능 영향 미미 |
| DECISION-5 | `:id` 문법 (parsing 비용 우선) |
| DECISION-6 | view-over-vector. Content-Length로 사전 크기 판단 후 vector 동적 resize. max 제한 (1MB request / 8MB response) |
| DECISION-a1 | sniffing에서 받은 byte가 헤더/payload로 자연스럽게 이월되도록 모든 receive 루틴이 누적 카운터 감안 |

### 5-2. 아직 미해결인 결정

| ID | 질문 | 추천 |
|---|---|---|
| DECISION-7 | `headerSize`/`payloadSize` 시그니처에 `SocketClientConnection&` 인자 추가? `payloadSize(conn, header)`? | 추가 (per-connection 차별화 가능) |
| DECISION-8 | 기존 `SocketRequestHandler` 사용자 마이그레이션 — v3에서 즉시 rename? alias 한 메이저 유지? | v3 즉시 rename (메이저 버전이므로 breaking change 허용) |
| DECISION-9 | HttpResponse 반환 (값 vs out-param) | 값 |
| DECISION-10 | SocketHttpClient 상속 vs 합성 | 합성 |
| DECISION-11 | WS client receive 동기 vs 콜백 | 동기 |
| DECISION-12 | Custom-over-WS 클라이언트 API | 사용자 직접 wrap |

---

## 6. 일정 견적

| Phase | 예상 작업량 | 비고 |
|---|---|---|
| 1. Parser 편입 | 1주 | 빌드 시스템 작업 + 단위 테스트 |
| 2. 공통 타입 + Handler base | 1주 | Diamond 해결 검증 필요 |
| 3. HTTP 핸들러 + 라우터 | 2주 | Trie 라우터 구현 비중 |
| 4. User Protocol refactoring | 1주 | 회귀 테스트 중요 |
| 5. Server 흐름 통합 | 3주 | **가장 위험.** 상태 머신 버그 잡기 |
| 6. HTTP Client | 1주 | |
| 7. WebSocket Client | 2주 | |
| **합계** | **약 11주** | (1인 풀타임 기준) |

---

## 7. 리뷰 체크리스트

### 7-1. 1차 리뷰 결과 (2026-05-26)

- [x] DECISION-1 (parser vendoring)
- [x] DECISION-2 (Connection 설계)
- [x] DECISION-3 & 4 (Diamond + 플래그)
- [x] DECISION-5 (path param 문법)
- [x] DECISION-6 (body 메모리 모델)
- [x] DECISION-a1 (누적 버퍼 경계 — 새로 추가)

### 7-2. 2차 리뷰에서 확인할 것

- [ ] DECISION-7 ~ 12 (§5-2)
- [ ] §1-2 Out of Scope 항목 적절성
- [ ] §3 Phase 순서 — 우선순위 조정 필요?
- [ ] §6 일정 — 현실적인지

2차 리뷰 후 모든 DECISION이 해소되면 **Phase 1부터 순차 진행** 시작.
