# Milestone: HTTP / WebSocket / Custom Protocol 통합 지원

> **버전:** v3 첫 메이저 릴리스
>
> **참조:**
>
> - [mileston_request.md](./mileston_request.md) — 원안
> - [state-machine.html](./state-machine.html) — 연결 상태 머신 (States / Events / Actions / Transitions)

이 문서는 v3에서 추가될 HTTP / WebSocket / Custom Protocol 통합의 **최종 아키텍처**와 **구현 Phase**를 정의한다.

---

## 1. 범위

### 1-1. In Scope

- 경량 HTTP/1.1 파서 빌드 시스템 편입 (picohttpparser)
- 서버: HTTP 라우팅 (`get/post/put/del/patch/head/options`)
- 서버: WebSocket handshake + frame 인/디코드
- 서버: Custom Protocol over WebSocket tunneling
- 서버: 단일 포트에서 HTTP / Custom Protocol 자동 분기 (sniffing)
- 핸들러: `HttpRequestHandler`, `CustomProtocolRequestHandler` 분리 + 조합 가능
- 클라이언트: `HttpClient`, `RequestClient` (raw + WS tunneling)
- 유틸: `Url` (RFC 3986 percent-encoding)

### 1-2. Out of Scope

- HTTP/2, HTTP/3, QUIC
- Chunked transfer encoding (Content-Length 기반만 지원)
- HTTP 압축 (gzip / deflate)
- WebSocket permessage-deflate 압축 확장
- 비동기 콜백 기반 API (기존 동기 모델 유지)

---

## 2. 아키텍처

### 2-1. HTTP Parser

**picohttpparser** 채택 (MIT, 단일 파일 ~700 LOC). 헤더만 파싱하고 body는 라이브러리가 처리.

- **Vendoring**: `src/implementation/thirdparty/picohttpparser/`
- `readme.md`에 "Third-Party Dependencies" 섹션 신설 — 라이브러리명/커밋 해시/라이선스/용도 기록
- TLS 옵션과 독립 (HTTP는 TLS 여부 무관 → 항상 빌드)

---

### 2-2. 이름 정책 — flat namespace, `Socket` prefix 제거

`Bn3Monkey::` namespace로 컨텍스트를 보장하고, 클래스명은 짧고 명확하게.

| 카테고리 | 이름 |
| --- | --- |
| 결과/에러 | `NetworkResultCode`, `NetworkResult` |
| 설정 | `NetworkConfiguration`, `TlsClientConfiguration`, `TlsServerConfiguration`, `WebSocketConfiguration` |
| TLS enum | `TlsVersion`, `TlsV12CipherSuite`, `TlsV13CipherSuite`, `TlsClientAuthMode` |
| 클라이언트 | `Client`, `HttpClient`, `RequestClient` |
| 서버 | `RequestServer`, `BroadcastServer` |
| 핸들러 | `RequestHandler` (base), `HttpRequestHandler`, `CustomProtocolRequestHandler`, `BroadcastHandler` |
| 연결 | `ClientConnection` |
| HTTP 라우팅 | `HttpRouter` |
| 처리 모드 | `RequestProcessingMode` |
| 서버측 view/builder | `HttpRequest`, `HttpResponse`, `CustomProtocolRequest`, `CustomProtocolResponse` |
| 클라이언트측 | `HttpClientRequest`, `HttpClientResponse` |
| 유틸 | `Url` |

> v3는 메이저 버전 → breaking change 허용. 호환 alias 제공 안 함. "User"는 모두 "Custom"으로 통일.

---

### 2-3. `ClientConnection` — Pure Interface + Impl + 단일 buffer

사용자 측 타입은 **pure abstract interface**, 구현은 `*Impl`. DLL boundary 안전 + 변경 자유도.

#### 사용자 측 (pure abstract)

```cpp
class SECURITYSOCKET_API ClientConnection {
public:
    virtual ~ClientConnection() = default;
    virtual const char* ip() const = 0;
    virtual uint32_t    port() const = 0;
    virtual bool        isSecure() const = 0;
    virtual bool        isWebSocket() const = 0;
};
```

#### Impl 측 (라이브러리 내부) — 단일 input/output buffer + offset + state machine

```cpp
class ClientConnectionImpl : public ClientConnection {
public:
    const char* ip() const override          { return _peer_ip; }
    uint32_t    port() const override        { return _peer_port; }
    bool        isSecure() const override    { return _is_secure; }
    bool        isWebSocket() const override;   // state 그룹 기반 (state-machine.html §3 protocol grouping)

    // Impl factory — wiring 캡슐화. Action 함수에서 사용
    HttpRequestImpl            makeHttpRequest();
    HttpResponseImpl           makeHttpResponse();
    CustomProtocolRequestImpl  makeCustomProtocolRequest();
    CustomProtocolResponseImpl makeCustomProtocolResponse();

    // ── Action 함수 (state-machine.html §5-1, §7-4) ──
    // SLOW 진입 시 listener.removeEvent 호출 위해 listener 인자 받음
    HttpRequestResult    handleHttpRequest   (SocketMultiEventListener& listener);
    WebSocketFrameResult handleWebSocketFrame(SocketMultiEventListener& listener);
    CustomMessageResult  handleCustomMessage (SocketMultiEventListener& listener);
    // 나머지 handle* 함수도 동일 시그니처

private:
    ServerActiveSocket* _socket;
    ConnectionState     _state;            // state-machine.html §7-1, 13개 state

    // 단일 누적 버퍼 — 분리하지 않음
    std::vector<char> _input_buffer;
    size_t            _input_received;     // 누적 byte 수
    size_t            _input_consumed;      // 처리 완료 위치 (다음 메시지 시작)
    size_t            _header_end;          // header 끝 offset
    size_t            _payload_end;         // payload 끝 offset

    // HTTP 파싱 결과 (HTTP 그룹 state 일 때만 유효)
    const char* _method;
    const char* _path;
    HeaderIndex _header_index;
    size_t      _body_start, _body_end;

    std::vector<char> _output_buffer;
    size_t            _output_written;

    char     _peer_ip[64];
    uint32_t _peer_port;
    bool     _is_secure;

    // ── Worker (lazy 생성, state-machine.html §7-4) ──
    // 첫 SLOW dispatch 시점에 시작. FAST 만 쓰는 connection 은 생성 안 됨.
    std::thread             _worker_thread;
    bool                    _worker_should_stop = false;

    // Task slot — 단일 (queue 아님, C++14 호환 위해 std::optional 미사용).
    // listener == nullptr ↔ empty.
    struct WorkerTask {
        SocketMultiEventListener* listener = nullptr;
        std::function<void()>     handler_call;
    };
    WorkerTask              _pending_task;
    std::mutex              _task_mtx;
    std::condition_variable _task_cv;
};
```

**단일 buffer 선택 이유:** sniffing/keep-alive 경계에서 byte를 별도 vector로 옮기는 복사 코드가 사라짐. offset만으로 view 구성.

#### Impl 라이프사이클 — Stack-only

| 객체 | 생명주기 | 위치 |
| --- | --- | --- |
| `ClientConnectionImpl` | 연결 lifetime | heap (server pool) |
| `_input_buffer`, `_output_buffer` | ClientConnectionImpl과 동일 | vector 내부 heap |
| `HttpRequestImpl` / `HttpResponseImpl` | **한 dispatch 호출 동안** | **stack** |
| `CustomProtocolRequestImpl` / `CustomProtocolResponseImpl` | **한 dispatch 호출 동안** | **stack** |

→ 매 요청마다 fresh Impl. 요청 간 상태 누수 위험 없음. heap 할당 0.

#### Factory 패턴

Impl 생성에 필요한 ClientConnectionImpl 멤버를 factory가 wiring. Impl 생성자 시그니처 변경 시 한 곳만 수정.

```cpp
inline HttpRequestImpl ClientConnectionImpl::makeHttpRequest() {
    return HttpRequestImpl(_method, _path, _header_index,
                           _input_buffer, _body_start, _body_end);
}
inline HttpResponseImpl ClientConnectionImpl::makeHttpResponse() {
    return HttpResponseImpl(_output_buffer, _output_written);
}
inline CustomProtocolRequestImpl ClientConnectionImpl::makeCustomProtocolRequest() {
    return CustomProtocolRequestImpl(_input_buffer,
                                     _input_consumed, _header_end, _payload_end);
}
inline CustomProtocolResponseImpl ClientConnectionImpl::makeCustomProtocolResponse() {
    return CustomProtocolResponseImpl(_output_buffer, _output_written);
}
```

---

### 2-4. 서버 핸들러 — virtual inheritance + `supportXxx()` 플래그

다중 상속의 diamond 문제는 `virtual inheritance`로, 능력 탐지는 `supportXxx()` 플래그로. 두 메커니즘은 서로 다른 문제를 해결.

```cpp
class SECURITYSOCKET_API RequestHandler {
public:
    virtual ~RequestHandler() = default;
    virtual void onConnected   (const ClientConnection& conn) {}
    virtual void onDisconnected(const ClientConnection& conn) {}

    // derived가 자동으로 override (사용자 부담 없음)
    virtual bool supportHttp()         const { return false; }
    virtual bool supportUserProtocol() const { return false; }
    virtual bool supportWebSocket()    const { return false; }
};

class SECURITYSOCKET_API HttpRequestHandler
    : public virtual RequestHandler {
public:
    bool supportHttp() const override final { return true; }
    virtual void registerRoutes(HttpRouter& router) = 0;
};

class SECURITYSOCKET_API CustomProtocolRequestHandler
    : public virtual RequestHandler {
public:
    CustomProtocolRequestHandler() = default;
    explicit CustomProtocolRequestHandler(const WebSocketConfiguration& ws)
        : _ws_config(ws) {}

    bool supportUserProtocol() const override final { return true; }
    bool supportWebSocket()    const override final { return _ws_config.valid(); }

    const WebSocketConfiguration& webSocketConfig() const { return _ws_config; }

    // header/payload 크기는 프로토콜의 정적 속성 → connection 인자 없음
    virtual size_t headerSize() = 0;
    virtual size_t payloadSize(const void* header) = 0;
    virtual RequestProcessingMode classifyMode(const void* header) = 0;

    // process에는 connection 유지 — 응답 대상 식별에 필요
    virtual void process(const ClientConnection& conn,
                         const CustomProtocolRequest& req,
                         CustomProtocolResponse& res) = 0;
    virtual void processWithoutResponse(const ClientConnection& conn,
                                        const CustomProtocolRequest& req) = 0;
};

struct SECURITYSOCKET_API WebSocketConfiguration {
    const char* pattern = nullptr;
    bool valid() const { return pattern != nullptr; }
};
```

#### 사용자 조합 패턴

```cpp
// HTTP만
class MyHttpHandler : public HttpRequestHandler { ... };

// Custom만
class MyCustomHandler : public CustomProtocolRequestHandler { ... };

// 둘 다
class MyCombinedHandler : public HttpRequestHandler,
                          public CustomProtocolRequestHandler { ... };
```

#### Server의 능력 탐지

```cpp
NetworkResult RequestServer::open(RequestHandler* handler, size_t n) {
    bool has_http   = handler->supportHttp();
    bool has_custom = handler->supportUserProtocol();
    bool has_ws     = handler->supportWebSocket();

    HttpRequestHandler*           http   = nullptr;
    CustomProtocolRequestHandler* custom = nullptr;
    if (has_http)   http   = static_cast<HttpRequestHandler*>(handler);
    if (has_custom) custom = static_cast<CustomProtocolRequestHandler*>(handler);

    // has_http / has_custom / has_ws 조합으로 dispatch 루틴 결정
}
```

> `dynamic_cast` 사용 안 함 (RTTI 비활성화 빌드 호환 + `supportXxx()`가 더 빠름). cross-cast 금지, 항상 base에서 down-cast.

---

### 2-5. HTTP 라우터 — Trie 기반, `:id` path parameter, FAST/SLOW dispatch

```cpp
class SECURITYSOCKET_API HttpRouter {
public:
    using HandlerFn = std::function<void(ClientConnection&, HttpRequest&, HttpResponse&)>;

    // mode 인자로 FAST/SLOW 지정. 기본은 FAST (event loop thread에서 sync 실행)
    // SLOW는 worker thread로 dispatch — event loop blocking 방지 (DB 조회, 파일 I/O 등)
    void get    (const char* pattern, HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
    void post   (const char* pattern, HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
    void put    (const char* pattern, HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
    void del    (const char* pattern, HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
    void patch  (const char* pattern, HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
    void head   (const char* pattern, HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
    void options(const char* pattern, HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
    void fallback(HandlerFn fn,
                 RequestProcessingMode mode = RequestProcessingMode::FAST);
};
```

- **문법**: Express 스타일 `:id` (parsing 비용 최소)
- **매칭**: Trie — O(path 깊이), 라우트 수 무관
- **충돌 규칙**: 정적 경로 우선 (`/user/me` > `/user/:id`)
- **FAST/SLOW 정책**: 라우트 등록 시 결정. 한 라우트는 한 mode 고정 (메시지별로 바뀌지 않음)
  - `FAST`: handler가 event loop thread에서 sync 실행. context switch 비용 0. CPU 짧은 작업에 적합 (메모리 lookup, 가벼운 파싱 등)
  - `SLOW`: handler가 worker thread로 dispatch. event loop blocking 방지. I/O 작업에 적합 (DB 쿼리, 파일 read/write, 외부 API 호출 등)
  - `READ_STREAM/WRITE_STREAM`은 HTTP에서 미지원 (Custom 프로토콜 전용 — `state-machine.html` §2-5 참조)

사용 예시:

```cpp
// FAST (기본) — 메모리에서 바로 응답
router.get("/user/:id", [](auto& conn, auto& req, auto& res) {
    res.status(200).json("{...}");
});

// SLOW — DB 조회 같은 I/O
router.get("/user/:id/orders",
    [this](auto& conn, auto& req, auto& res) {
        auto orders = _db.queryOrders(req.pathParam("id"));  // blocking I/O
        res.status(200).json(orders.toJson());
    },
    RequestProcessingMode::SLOW);

// 파일 업로드 — body가 크고 처리도 무거움
router.post("/upload",
    [this](auto& conn, auto& req, auto& res) {
        saveToFile(req.body(), req.bodySize());  // disk write
        res.status(201);
    },
    RequestProcessingMode::SLOW);
```

> Custom 프로토콜의 `classifyMode(header)`는 **메시지마다 다르게 분류** (헤더 내용 보고 동적 결정).
> HTTP 라우트는 **등록 시점에 고정** (path가 곧 작업 종류이므로 정적 분류가 자연스러움).

---

### 2-6. 서버측 view/builder

사용자 측은 모두 pure abstract. Impl이 ClientConnectionImpl의 buffer를 view.

```cpp
// HTTP 요청 view
class SECURITYSOCKET_API HttpRequest {
public:
    virtual ~HttpRequest() = default;
    virtual const char* method() const = 0;
    virtual const char* path() const = 0;
    virtual const char* header(const char* name) const = 0;
    virtual const char* pathParam(const char* name) const = 0;  // 자동 decode
    virtual const char* query(const char* name) const = 0;       // 자동 decode
    virtual const void* body() const = 0;
    virtual size_t      bodySize() const = 0;
};

// HTTP 응답 builder
class SECURITYSOCKET_API HttpResponse {
public:
    virtual ~HttpResponse() = default;
    virtual HttpResponse& status(int code) = 0;
    virtual HttpResponse& header(const char* name, const char* value) = 0;
    virtual HttpResponse& body(const void* data, size_t size) = 0;
    virtual HttpResponse& json(const char* json_str) = 0;
};

// Custom protocol request view
class SECURITYSOCKET_API CustomProtocolRequest {
public:
    virtual ~CustomProtocolRequest() = default;
    virtual const void* header() const = 0;
    virtual size_t      headerLength() const = 0;
    virtual const void* payload() const = 0;
    virtual size_t      payloadLength() const = 0;
};

// Custom protocol response builder
class SECURITYSOCKET_API CustomProtocolResponse {
public:
    virtual ~CustomProtocolResponse() = default;
    virtual void*  data() = 0;
    virtual size_t capacity() const = 0;
    virtual void   setLength(size_t n) = 0;
};
```

#### HttpRequestImpl (내부)

ClientConnectionImpl 전체가 아닌 **필요한 buffer/필드만** 받음 (의존성 최소화).

```cpp
class HttpRequestImpl : public HttpRequest {
public:
    HttpRequestImpl(const char* method, const char* path,
                    const HeaderIndex& headers,
                    const std::vector<char>& input_buffer,
                    size_t body_start, size_t body_end);

    const char* method() const override   { return _method; }
    const char* path() const override     { return _path; }
    const char* header(const char* name) const override;
    const char* pathParam(const char* name) const override;
    const char* query(const char* name) const override;
    const void* body() const override     { return _input_buffer.data() + _body_start; }
    size_t      bodySize() const override { return _body_end - _body_start; }

    void setPathParam(const char* name, const char* value);  // route 매칭 시 사용

private:
    const char* _method;
    const char* _path;
    const HeaderIndex& _headers;
    const std::vector<char>& _input_buffer;
    size_t _body_start, _body_end;

    // path param inline 저장 — null-terminated 복사본 (heap 0)
    struct Param { char name[32]; char value[128]; };
    Param  _path_params[8];
    size_t _path_param_count = 0;
};
```

#### Handler dispatch — Action 함수 안에서 진행

정식 흐름은 state-machine.html §6-2 `handleHttpRequest()` 참조 (parse → Upgrade 검출 → route 매칭 → FAST/SLOW 분기 → response 직렬화).
이 mileston 의 view/builder 관점에서 핵심은: Action 함수가 `makeHttpRequest() / makeHttpResponse()` factory 로 Impl 을 stack 생성 → route handler 에 전달 → handler 가 buffer view/builder 통해 작업 → Action 함수가 response 직렬화 및 listener event 전이.

```cpp
// state-machine.html §6-2 handleHttpRequest 내부의 FAST 분기 발췌
if (route->mode == RequestProcessingMode::FAST) {
    auto req  = makeHttpRequest();    // stack, _input_buffer view
    auto resp = makeHttpResponse();   // stack, _output_buffer builder
    fillPathParams(route, req);
    route->handler(*this, req, resp);
    serializeHttpResponse();          // _output_buffer 채움
    return HttpRequestResult::ROUTE_DISPATCHED_FAST;
}
```

---

### 2-7. 클라이언트측 — container 패턴

사용자가 직접 생성하는 클라이언트측 타입은 concrete class. 기존 `Client` (`SocketClient`)와 동일하게 **`_container[]` + placement-new** 패턴.

```cpp
class SECURITYSOCKET_API HttpClientRequest {
public:
    static constexpr size_t IMPLEMENTATION_SIZE = 1024;

    HttpClientRequest();
    ~HttpClientRequest();
    HttpClientRequest(const HttpClientRequest&) = delete;
    HttpClientRequest& operator=(const HttpClientRequest&) = delete;

    HttpClientRequest& method(const char* m);

    // 단일 path 메서드 — printf-style. compiler가 fmt literal 검증
    HttpClientRequest& path(const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    HttpClientRequest& query (const char* name, const char* value);  // auto-encode
    HttpClientRequest& header(const char* name, const char* value);
    HttpClientRequest& body  (const void* data, size_t len);
    HttpClientRequest& json  (const char* json_str);

private:
    alignas(std::max_align_t) char _container[IMPLEMENTATION_SIZE]{0};
};

class SECURITYSOCKET_API HttpClientResponse {
public:
    static constexpr size_t IMPLEMENTATION_SIZE = 1024;

    HttpClientResponse();
    ~HttpClientResponse();
    HttpClientResponse(const HttpClientResponse&) = delete;
    HttpClientResponse& operator=(const HttpClientResponse&) = delete;

    // 값 반환을 위한 move (container 안의 Impl을 placement-new로 옮김)
    HttpClientResponse(HttpClientResponse&&) noexcept;
    HttpClientResponse& operator=(HttpClientResponse&&) noexcept;

    NetworkResultCode resultCode() const;    // 통신 자체의 성공/실패
    int               status()     const;    // HTTP status code
    const char*       header(const char* name) const;
    const void*       body() const;
    size_t            bodySize() const;

private:
    alignas(std::max_align_t) char _container[IMPLEMENTATION_SIZE]{0};
};
```

#### Container 구현 예시

```cpp
HttpClientResponse::HttpClientResponse() {
    static_assert(sizeof(HttpClientResponseImpl) <= IMPLEMENTATION_SIZE,
                  "container too small — increase IMPLEMENTATION_SIZE");
    new (_container) HttpClientResponseImpl();
}
HttpClientResponse::~HttpClientResponse() {
    reinterpret_cast<HttpClientResponseImpl*>(_container)->~HttpClientResponseImpl();
}
HttpClientResponse::HttpClientResponse(HttpClientResponse&& other) noexcept {
    auto* src = reinterpret_cast<HttpClientResponseImpl*>(other._container);
    new (_container) HttpClientResponseImpl(std::move(*src));
}
```

#### HttpClient

```cpp
class SECURITYSOCKET_API HttpClient : public Client {
public:
    explicit HttpClient(const NetworkConfiguration& cfg);
    explicit HttpClient(const NetworkConfiguration& cfg,
                        const TlsClientConfiguration& tls);

    // 간단 헬퍼 — body 없음
    HttpClientResponse get    (const char* path);
    HttpClientResponse head   (const char* path);
    HttpClientResponse del    (const char* path);
    HttpClientResponse options(const char* path);

    // body 있음
    HttpClientResponse post (const char* path, const void* body, size_t len);
    HttpClientResponse put  (const char* path, const void* body, size_t len);
    HttpClientResponse patch(const char* path, const void* body, size_t len);

    // 풀 컨트롤 — 커스텀 헤더/query 등
    HttpClientResponse request(const HttpClientRequest& req);
};
```

응답은 **값 반환 + ResultCode 내장** (out-param 사용 안 함, read/write buffer는 예외):

```cpp
auto resp = client.get("/api/health");
if (resp.resultCode() == NetworkResultCode::SUCCESS && resp.status() == 200) {
    printf("body: %s\n", (const char*)resp.body());
}
```

#### RequestClient — Custom Protocol + WS toggle

WebSocket을 별도 클라이언트로 만들지 않고, `RequestClient` 생성자의 `WebSocketConfiguration` 유무로 raw / WS tunneling 전환. 서버측 `CustomProtocolRequestHandler`와 정확히 대칭.

```cpp
class SECURITYSOCKET_API RequestClient : public Client {
public:
    // raw TCP 모드
    explicit RequestClient(const NetworkConfiguration& cfg);
    explicit RequestClient(const NetworkConfiguration& cfg,
                           const TlsClientConfiguration& tls);

    // WS tunneled 모드
    RequestClient(const NetworkConfiguration& cfg,
                  const WebSocketConfiguration& ws);
    RequestClient(const NetworkConfiguration& cfg,
                  const TlsClientConfiguration& tls,
                  const WebSocketConfiguration& ws);

    NetworkResult connect();   // raw: TCP / WS: TCP + Upgrade handshake
    NetworkResult send   (const void* data, size_t len);
    NetworkResult receive(void* buf, size_t buf_size, size_t* received,
                          uint64_t timeout_ms = 0);

    bool isWebSocket() const;
};
```

→ **같은 테스트 코드로 raw/WS 양 모드 회귀 테스트 가능.**

#### Client 계층

```text
Client (raw TCP/TLS, 기존)
    ├── HttpClient        (HTTP/HTTPS)
    └── RequestClient     (Custom Protocol; ctor의 ws_config로 raw/WS 결정)
```

---

### 2-8. WebSocket — Custom Protocol의 transport

#### Handshake

HTTP/1.1 `Upgrade: websocket` 표준. `WebSocketConfiguration::pattern`이 path와 매칭되면 framework가 자동 handshake (`SendingHandshakeResponse`) 후 WebSocket 그룹 state (`WaitingForNextWebSocketMessage` 등) 로 전환.

#### Frame opcode 처리 정책

Custom 프로토콜은 **Binary frame (`0x2`) 에만 실린다.** Custom 헤더는 UTF-8 검증을 통과할 수 없어 Text frame과 본질적으로 호환 안 됨.

| Opcode | 처리 |
| --- | --- |
| `0x0` Continuation | framework가 자동 재조립 (FIN=1까지) |
| `0x1` Text | **거부.** Close (status `1003` "Unsupported Data") 후 종료 |
| `0x2` Binary | payload 추출 → Custom handler dispatch |
| `0x8` Close | Close echo 를 `_output_buffer` 에 채우고 `Closing` 으로 전이 → 송신 완료 후 socket close + `onDisconnected` |
| `0x9` Ping | PONG frame 을 `_output_buffer` 에 채우고 `SendingWebSocketResponse` 로 전이 → 일반 응답 경로로 송신 (즉시 send 안 함) |
| `0xA` Pong | 통계 카운터만, state 유지 |
| 그 외 reserved | Close (status `1002` "Protocol Error") |

#### Close status code

| code | 의미 | 발생 |
| --- | --- | --- |
| 1000 | Normal closure | 정상 종료 |
| 1002 | Protocol error | reserved opcode / 잘못된 frame |
| 1003 | Unsupported data | Text frame 수신 |
| 1009 | Message too big | payload > `max_http_request_body_size` |
| 1011 | Server error | 내부 처리 실패 |

#### WS frame을 통한 Custom 흐름

WS handshake 후 들어온 binary frame의 payload가 그대로 Custom handler의 `headerSize/payloadSize/process` 흐름으로 흘러감. raw TCP와 동일한 핸들러 한 벌이 양쪽 전송 방식을 처리.

---

### 2-9. `Url` 클래스 — RFC 3986 percent-encoding

```cpp
class SECURITYSOCKET_API Url {
public:
    // [A-Za-z0-9-_.~]는 그대로, 나머지는 %XX
    static std::string encode(const char* input);
    static std::string encode(const char* input, size_t len);
    static std::string encode(const std::string& input);

    // %XX → 원본 byte. 잘못된 인코딩은 빈 문자열 반환
    static std::string decode(const char* input);
    static std::string decode(const char* input, size_t len);
    static std::string decode(const std::string& input);

    static bool tryDecode(const char* input, size_t len, std::string& out);
};
```

#### 자동 적용 정책

| 위치 | 자동 처리 |
| --- | --- |
| 클라 `req.path(fmt, ...)` | **수동** — printf 포맷이라 자동화 불가. 사용자가 `Url::encode()` 호출 |
| 클라 `req.query(name, value)` | **자동 encode** (name + value) |
| 클라 `req.header(name, value)` | 안 함 (HTTP header는 별도 규칙) |
| 클라 `req.body(...)` | 안 함 |
| 서버 `req.pathParam("name")` | **자동 decode** |
| 서버 `req.query("name")` | **자동 decode** |
| 서버 `req.header("X")` | 안 함 |

#### 사용 예시

```cpp
HttpClientRequest req;
req.method("GET")
   .path("/user/%s", Url::encode(user_name).c_str())   // 사용자가 호출
   .query("filter", "name=alice & age>20")              // 자동 encode
   .header("Authorization", "Bearer xyz");              // 그대로

router.get("/user/:name", [](auto& conn, auto& req, auto& res) {
    const char* name   = req.pathParam("name");   // 자동 decode
    const char* filter = req.query("filter");     // 자동 decode
});
```

---

### 2-10. Server 흐름 — 단일 포트 sniffing + dispatch

> ConnectionState 명칭은 state-machine.html §7-1 기준. 13개 state 의 전이/Action 은 state-machine.html §6 참조. 아래는 흐름 요약.

```text
[연결 accept]
   ↓
state = Sniffing, 첫 N byte 누적
   ↓
ProtocolSniffer::detect(buf, len)
   ├── HTTP      → state = ReceivingHttpRequest         (HTTP 그룹)
   ├── CUSTOM    → state = ReceivingCustomMessage       (Custom 그룹)
   ├── UNKNOWN   → close
   └── NEED_MORE → Sniffing 유지, 더 받기

[ReceivingHttpRequest] — HTTP 그룹
   ↓ picohttpparser 로 헤더 파싱 (\r\n\r\n 까지)
   ↓
   ├── Upgrade: websocket?
   │     ├── handler 가 CustomProtocolRequestHandler + WS config 보유?
   │     │     ├── path 가 ws_config.pattern 매칭?
   │     │     │     → SendingHandshakeResponse (101) → WaitingForNextWebSocketMessage (WebSocket 그룹)
   │     │     │     → No: 404 + Closing
   │     │     └── No: 400 + Closing
   │     └── 그 외: HTTP 라우트 매칭 시도
   ├── HTTP 라우트 매칭?
   │     → Content-Length 만큼 body 누적 → handler 실행 → SendingHttpResponse
   │     → keep-alive 면 WaitingForNextHttpRequest, 아니면 Closing
   └── 404 → Closing

[ReceivingCustomMessage] — Custom 그룹
   ↓ handler.headerSize() 만큼 누적
   ↓ handler.classifyMode(header) → FAST / SLOW / READ_STREAM / WRITE_STREAM
   ↓ handler.payloadSize(header) 만큼 누적
   ↓ handler.process() 또는 processWithoutResponse()
   ↓ SendingCustomResponse → 송신 완료 → WaitingForNextCustomMessage

[ReceivingWebSocketFrame] — WebSocket 그룹
   ↓ WS frame 디코드 (FIN/opcode/mask/length, in-place unmask)
   ↓ opcode 별 처리 (§2-8 표)
   ↓ BINARY/CONTINUATION → payload 누적 → FIN=1 시 Custom 처리 루틴 동일
   ↓ 응답은 binary frame 으로 wrap → SendingWebSocketResponse → WaitingForNextWebSocketMessage
```

**단일 버퍼 + offset 구조 덕에 sniffing 경계의 byte 복사 없음.** `Sniffing` 에서 받은 byte 는 그대로 누적 버퍼에 남고, state 전환 시 offset 만 갱신.

**Protocol identity 는 연결 lifetime 동안 불변** (state-machine.html §3, §8). Sniffing 은 연결 시작 직후 *최대 1회* 만 진입.
HTTP / Custom 으로 한 번 정해지면 같은 그룹 state 끼리만 순환. WebSocket 은 HTTP Upgrade 후 진입하면 마찬가지로 WS state 끼리만 순환.
Mid-connection protocol switch (예: Custom 메시지 후 HTTP, WebSocket → HTTP) 지원 안 함 — 잘못된 byte 는 parse error → `Closing`.

---

### 2-11. Body 메모리 모델

기존 vector buffer 위에 view 올림. Content-Length로 크기 사전 판단 후 vector 동적 resize.

```cpp
class NetworkConfiguration {
    // 기존 필드들...
    uint32_t max_http_request_body_size  = 1 * 1024 * 1024;  // 1 MB
    uint32_t max_http_response_body_size = 8 * 1024 * 1024;  // 8 MB
};
```

흐름:

1. 헤더 파싱 → Content-Length 추출
2. 크기 > max ? → 413 Payload Too Large + close
3. 크기 ≤ pdu_size ? → 기본 buffer 그대로 사용
4. 크기 > pdu_size ? → `_input_buffer.resize(N)` 확장
5. handler 호출
6. keep-alive면 다음 요청 전에 capacity 유지 (shrink 안 함)

---

## 3. 구현 Phase

### Phase 1 — 테스트 클라이언트 라이브러리 통합 (libcurl)

#### 범위

- libcurl ≥ 8.11.0 FetchContent 편입 — HTTP + WebSocket stable API 한 라이브러리에서 커버 (`curl_ws_send` / `curl_ws_recv`)
- CMake 옵션 `SECURITYSOCKET_TEST_USE_CURL` (기본 OFF) — HTTP/WebSocket 테스트 코드 컴파일을 통째로 토글하는 단일 스위치
- ON: libcurl 자동 다운로드/빌드 + HTTP·WS 서버 회귀 테스트 타깃 활성화
- OFF: libcurl 의존 0, HTTP·WS 테스트 타깃 미생성 (기존 Custom Protocol 회귀 테스트는 영향 없음)
- 산출물은 Phase 4 이후의 HTTP/WS 서버 구현을 검증할 회귀 테스트 인프라 — 후속 Phase 들이 이 토글을 사용해 자신의 테스트 코드를 조건부 컴파일

#### 파일

- `CMakeLists.txt`: `option(SECURITYSOCKET_TEST_USE_CURL ...)` + `FetchContent_Declare(curl URL ...)` 블록, `check_symbol_exists(curl_ws_send curl/websockets.h ...)` 가용성 가드
- `test/http/CMakeLists.txt`: HTTP 서버 회귀 테스트 타깃 자리 (libcurl easy interface)
- `test/websocket/CMakeLists.txt`: WebSocket 서버 회귀 테스트 타깃 자리 (`curl_ws_send` / `curl_ws_recv`)
- `readme.md` Third-Party Dependencies: libcurl 항목 추가 (test 전용, MIT-like, 버전 핀 명시)

#### 완료 기준

- [ ] `cmake -DSECURITYSOCKET_TEST_USE_CURL=ON` 빌드 시 libcurl 8.11+ 자동 fetch + 정적 링크 성공
- [ ] OFF 빌드 시 libcurl 다운로드/빌드/링크 0회 — 의존 완전 격리
- [ ] OFF 빌드 시 HTTP/WebSocket 테스트 타깃 미생성 (CTest 목록에도 미노출)
- [ ] `curl/websockets.h` 의 `curl_ws_send` / `curl_ws_recv` symbol 컴파일 타임 검출 — 미존재 시 친절한 에러로 빌드 중단
- [ ] Windows / Linux / Android × TLS ON/OFF × USE_CURL ON/OFF 매트릭스 빌드 통과
- [ ] libcurl 기반 smoke test 1건 (loopback HTTP GET + WS echo) — 클라이언트 동작과 빌드 인프라만 검증, 실서버 테스트는 후속 Phase

---

### Phase 2 — Parser 편입 + 이름 일괄 rename

#### 범위

- picohttpparser vendoring
- 전체 코드베이스 `Socket` prefix 제거 (`NetworkResultCode`, `Client`, `RequestServer` 등)

#### 파일

- `src/implementation/thirdparty/picohttpparser/picohttpparser.{h,c}`
- `CMakeLists.txt`: picohttpparser 추가
- `readme.md`: Third-Party Dependencies 섹션 신설
- 모든 기존 cpp/hpp/test: rename

#### 완료 기준

- [ ] picohttpparser로 HTTP 요청 문자열 파싱 단위 테스트 통과
- [ ] 빌드: SECURITYSOCKET_USING_TLS=ON/OFF × Windows/Android/Linux 모두 통과
- [ ] 기존 기능 회귀 테스트 (rename만 적용, 동작 동일) 통과

---

### Phase 3 — 공통 타입 + Handler base

#### 범위

- `ClientConnection` (pure abstract), `ClientConnectionImpl` (단일 buffer + offset)
- `RequestHandler` base + `supportXxx()` 플래그
- `NetworkResultCode`, `NetworkResult` 등 결과 타입

#### 파일

- `src/implementation/ClientConnection.{hpp,cpp}`
- `src/implementation/ClientConnectionImpl.{hpp,cpp}` (기존 SocketConnection 대체)
- `src/SecuritySocket.hpp`: 공개 타입 선언

#### 완료 기준

- [ ] `ClientConnectionImpl`이 기존 buffer/state 책임 인수 (단일 buffer 구조)
- [ ] virtual 상속 + supportXxx 플래그 컴파일 + 동작
- [ ] 기존 Custom Protocol 회귀 테스트 통과

---

### Phase 4 — HTTP 핸들러 + 라우터 + Url

#### 범위

- `HttpRouter` (Trie + `:id` 매칭)
- `HttpRequest`/`HttpResponse` 인터페이스 + Impl
- `HttpRequestHandler`
- `Url` 클래스 (자동 decode 위해 먼저 구현)
- 서버측 `pathParam` / `query` auto-decode

#### 파일

- `src/implementation/http/HttpRouter.{hpp,cpp}`
- `src/implementation/http/HttpRequest.{hpp,cpp}` (server-side Impl)
- `src/implementation/http/HttpResponse.{hpp,cpp}` (server-side Impl)
- `src/implementation/http/Url.{hpp,cpp}`
- `src/SecuritySocket.hpp`: 공개 선언

#### 완료 기준

- [ ] Trie 라우터: 정적 path + `:id` 매칭
- [ ] HttpResponse: status / header / body / json 동작
- [ ] keep-alive (HTTP/1.1 default)
- [ ] body 크기 제한 (413)
- [ ] body > pdu_size 시 buffer 동적 resize
- [ ] `pathParam` / `query` auto-decode 확인 (encoded URL → 원본 byte)
- [ ] `Url::encode` / `decode` / `tryDecode` 단위 테스트
- [ ] FAST 라우트: event loop thread에서 sync 실행 (기본 동작)
- [ ] SLOW 라우트: worker thread로 dispatch, event loop blocking 없음
- [ ] mode 미지정 시 FAST default 적용 확인

---

### Phase 5 — Custom Protocol 핸들러 refactoring

#### 범위

- 기존 `SocketRequestHandler` → `CustomProtocolRequestHandler`
- `CustomProtocolRequest` / `CustomProtocolResponse` (pure abstract + Impl)
- `RequestProcessingMode` (FAST / SLOW / READ_STREAM / WRITE_STREAM)

#### 파일

- `src/SecuritySocket.hpp`: `CustomProtocolRequestHandler` 외 공개
- `src/implementation/custom/CustomProtocolRequest.{hpp,cpp}`
- `src/implementation/custom/CustomProtocolResponse.{hpp,cpp}`

#### 완료 기준

- [ ] 기존 Custom Protocol 회귀 테스트 통과 (인터페이스 변경 반영)
- [ ] `WebSocketConfiguration` 설정 시 `supportWebSocket()` 자동 true

---

### Phase 6 — Server 흐름 통합 (sniffing + dispatch + WS frame + SLOW)

> **참조:** [state-machine.html](./state-machine.html) — State / Event / Action / Transition 전체 명세.
> Phase 6는 이 명세대로 구현.

#### 범위

- `ProtocolSniffer` (HTTP / CUSTOM / NEED_MORE)
- 13개 `ConnectionState` state machine (state-machine.html §7-1)
- 4개 `Event` 만 (state-machine.html §7-2): `E_Accept` / `E_Read` / `E_Write` / `E_Disconnected`. SLOW worker 완료는 별도 event 아님 — worker 가 직접 `listener.addEvent(socket, WRITE)` 호출.
- `WsFrameCodec` (encode/decode, masking, fragmentation 재조립)
- **`SocketMultiEventListener` cross-thread 안전 + 내부 wakeup 추가** — 별도 클래스 (SocketUserEvent 류) 없음. listener 자체가 wakeup fd 보유 (Linux eventfd / Windows loopback socket pair, listener 당 1개).
- Custom 라우팅 (raw TCP, WS tunneled 모두 동일 handler)
- **Action 함수에 listener 인자 전달** — SLOW dispatch race 방지
- **Worker thread**: connection 당 1개, lazy 생성 (첫 SLOW 시점), single task slot (queue 아님, C++14 호환 위해 `std::optional` 미사용)

#### 파일

- `src/implementation/protocol/ProtocolSniffer.{hpp,cpp}`
- `src/implementation/protocol/HttpProcessor.{hpp,cpp}`
- `src/implementation/protocol/WsFrameCodec.{hpp,cpp}`
- `src/implementation/SocketEvent.hpp` / `SocketEvent_Linux.cpp` / `SocketEvent_Windows.cpp`: **wakeup 메커니즘 추가** (cross-thread `addEvent/modifyEvent/removeEvent` + 내부 wakeup fd)
- `src/implementation/ClientConnectionImpl.cpp`: state machine + worker thread 통합 (state-machine.html §7-4 명세)

#### 핵심 클래스

```cpp
// ConnectionState — state-machine.html §7-1, 13개 state
enum class ConnectionState : uint8_t { /* ... */ };

// Event — state-machine.html §7-2, 4개 event 만
enum class Event : uint8_t {
    E_Accept, E_Read, E_Write, E_Disconnected
};

// SocketMultiEventListener — cross-thread 안전 + 내부 wakeup
// (state-machine.html §5-3, §8 참조)
//   - addEvent / modifyEvent / removeEvent / wait 모두 lock 보호
//   - 내부 wakeup fd 보유: 함수 호출 시 wait() 중인 thread 즉시 깨움
//   - per-listener 1개 (open() 에서 생성, close() 에서 정리)
//   - per-connection wakeup fd 또는 SocketUserEvent 류의 별도 클래스 없음
class SocketMultiEventListener { /* 기존 + wakeup 멤버 + 내부 lock */ };

// WsFrameCodec
class WsFrameCodec {
public:
    enum class DecodeResult { NEED_MORE, OK,
                              CONTROL_PING, CONTROL_PONG, CONTROL_CLOSE,
                              ERROR };
    DecodeResult decode(const char* in, size_t in_len, size_t* consumed,
                        char* out, size_t out_capacity, size_t* out_len,
                        uint8_t* opcode, bool* fin);
    size_t encode(const char* payload, size_t len, bool binary, bool mask,
                  char* out, size_t out_capacity);
};

// ClientConnectionImpl — Action 함수가 listener 인자 받음. Worker lazy 생성.
// (state-machine.html §7-4 참조)
class ClientConnectionImpl {
public:
    // Action 함수 (state-machine.html §5-1 참조)
    HttpRequestResult    handleHttpRequest   (SocketMultiEventListener& listener);
    WebSocketFrameResult handleWebSocketFrame(SocketMultiEventListener& listener);
    CustomMessageResult  handleCustomMessage (SocketMultiEventListener& listener);
    // 나머지 handle* 함수도 동일 시그니처 (일관성)

private:
    // Worker (lazy 생성 — 첫 SLOW 시점에 시작)
    std::thread             _worker_thread;
    bool                    _worker_should_stop = false;

    // Task slot — 단일 (queue 아님). listener == nullptr ↔ empty.
    struct WorkerTask {
        SocketMultiEventListener* listener = nullptr;
        std::function<void()>     handler_call;
    };
    WorkerTask              _pending_task;
    std::mutex              _task_mtx;
    std::condition_variable _task_cv;
};
```

#### SLOW dispatch race 방지 패턴

> **불변 조건** (state-machine.html §8): Action 함수 내부에서 `listener.removeEvent(socket)` 이 `queueToWorker` 보다 반드시 먼저 호출.
> 그렇지 않으면 worker 가 즉시 실행되는 동안 peer 가 byte 를 보내면 socket POLLIN → handleRead → buffer race.

```cpp
// 잘못된 순서 (race 가능)
queueToWorker(route);                        // worker 즉시 실행 가능
// → peer 가 더 보내면 socket POLLIN → 또 handleRead → buffer race

// 올바른 순서
listener.removeEvent(_socket);               // socket 제거 → 더 이상 POLLIN 안 받음
queueToWorker(route, listener);              // 이제 worker 실행해도 안전

// Worker 완료 시
//   handler 실행 → _output_buffer 채움 → listener.addEvent(socket, WRITE)
//   → listener 내부 wakeup → main thread wait() 깨어남 → 다음 wait() 결과에 WRITE 포함
```

#### 완료 기준

- [ ] sniff → HTTP / CUSTOM 분기 동작
- [ ] HTTP → WS upgrade 흐름 동작 (Sec-WebSocket-Accept 검증)
- [ ] `ReceivingWebSocketFrame` (state-machine.html state) 에서 raw TCP와 동일한 Custom handler 호출
- [ ] keep-alive HTTP에서 여러 요청 연속 처리
- [ ] 누적 버퍼 경계 케이스 테스트
  - [ ] `sniffed_bytes < headerSize`
  - [ ] `sniffed_bytes == headerSize`
  - [ ] `sniffed_bytes > headerSize` (초과분 자동 이월)
  - [ ] 한 recv()로 여러 메시지 수신
  - [ ] keep-alive 두 번째 요청이 같은 recv()에 들어옴
- [ ] 단편화된 WS 메시지 (FIN=0 연속 frame) 재조립
- [ ] PING 처리: PONG frame 을 `_output_buffer` 에 채우고 `SendingWebSocketResponse` 로 전이
- [ ] CLOSE echo + 종료
- [ ] Text frame 수신 시 Close (1003)
- [ ] **`SocketMultiEventListener` 내부 wakeup Linux/Windows 모두 동작** (eventfd / loopback pair)
- [ ] **Listener cross-thread API 안전성 테스트**: worker thread 에서 `addEvent` 호출 시 main thread `wait()` 즉시 깨어남
- [ ] **SLOW dispatch race 테스트**: handler가 즉시 완료되는 케이스에서도 listener 정합성 유지
- [ ] **SLOW dispatch 순서 invariant 검증**: Action 안에서 `listener.removeEvent(socket)` → `queueToWorker` 순서
- [ ] **Worker lazy 생성 검증**: FAST 만 쓰는 connection 은 worker thread 생성 안 됨
- [ ] **Single task slot 검증**: 동시 task 2개 enqueue 시도가 invariant 로 절대 일어나지 않음 확인 (state 가 `Sending*` → `Waiting*` 도달해야 다음 SLOW 가능)

---

### Phase 7 — HTTP Client

#### 범위

- `HttpClient` (container 패턴, `Client` 상속)
- `HttpClientRequest` / `HttpClientResponse` (container 패턴)
- 클라측 `query()` auto-encode

#### 파일

- `src/implementation/http/HttpClient.{hpp,cpp}`
- `src/implementation/http/HttpClientRequest.{hpp,cpp}`
- `src/implementation/http/HttpClientResponse.{hpp,cpp}`

#### 완료 기준

- [ ] 간단 헬퍼 7개 (get/head/del/options/post/put/patch) 동작
- [ ] `request(HttpClientRequest&)` 풀 컨트롤 동작
- [ ] `path(fmt, ...)` printf-style + compiler format 검증
- [ ] `query()` auto-encode 확인
- [ ] HttpClientResponse 값 반환 + move 동작
- [ ] keep-alive 연결 재사용
- [ ] HTTPS 동작 (TLS config)
- [ ] container `static_assert` (size/align) 통과

---

### Phase 8 — Request Client (raw + WS tunneling)

#### 범위

- `RequestClient` (`Client` 상속, ctor의 ws_config로 raw/WS 결정)
- raw 모드: `send`/`receive`가 `Client::write`/`read` 직접 호출
- WS 모드: TCP connect + HTTP Upgrade handshake → frame encode/decode

#### 파일

- `src/implementation/custom/RequestClient.{hpp,cpp}`

#### 완료 기준

- [ ] raw 모드: send/receive가 `Client::write`/`read`와 동일 동작
- [ ] WS 모드: handshake + Sec-WebSocket-Accept 검증
- [ ] WS 모드: 클라→서버 frame mask, 서버→클라 unmask + binary 추출
- [ ] PING 자동 PONG, CLOSE 정상 처리
- [ ] `wss://` (TLS) 동작
- [ ] **동일 테스트 코드가 raw/WS 양 모드에서 통과** (회귀 테스트 핵심)

---

## 4. 일정 견적

| Phase | 작업량 | 비고 |
| --- | --- | --- |
| 1. 테스트 클라이언트 라이브러리 통합 (libcurl) | 1주 | FetchContent + `SECURITYSOCKET_TEST_USE_CURL` 토글, smoke test |
| 2. Parser 편입 + 일괄 rename | 2주 | rename으로 인한 회귀 검증 포함 |
| 3. 공통 타입 + Handler base | 1주 | Diamond / supportXxx 검증 |
| 4. HTTP 핸들러 + 라우터 + Url | 2주 | Trie 라우터 + Url 인코딩 비중 |
| 5. Custom Protocol 핸들러 refactoring | 1주 | 회귀 테스트 중요 |
| 6. Server 흐름 통합 (sniffing + WS frame) | 3주 | **가장 위험.** state machine 버그 잡기 |
| 7. HTTP Client | 1.5주 | container 패턴 + 클라 auto-encode |
| 8. Request Client (raw + WS) | 2주 | WsFrameCodec 재사용 |
| **합계** | **약 13.5주** | (1인 풀타임 기준) |
