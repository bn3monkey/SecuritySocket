# SecuritySocket v3.0.0 — Architecture

> v3 의 HTTP / WebSocket / Custom-Protocol 통합 아키텍처와 그 **설계 근거**를 한 곳에
> 모은 문서. 코드만 봐서는 알기 어려운 "왜 이렇게 했는가"를 남긴다.

---

## 1. 범위

### In Scope
- 경량 HTTP/1.1 파서(picohttpparser) vendoring
- 서버: HTTP 라우팅(`get/post/put/del/patch/head/options/fallback`)
- 서버: WebSocket handshake + frame 인/디코드
- 서버: Custom Protocol over WebSocket tunneling
- 서버: 단일 포트에서 HTTP / Custom Protocol 자동 분기(sniffing)
- 핸들러: `HttpRequestHandler` + `CustomProtocolRequestHandler` 분리 + 조합 가능
- 클라이언트: `HttpClient`, `RequestClient`(raw + WS tunneling)
- `WRITE_STREAM` / `READ_STREAM` 연속 송수신(고정 메모리)

### Out of Scope
- HTTP/2, HTTP/3, QUIC
- Chunked transfer encoding(Content-Length 기반만 지원)
- HTTP 압축(gzip/deflate), WebSocket permessage-deflate
- 비동기 콜백 API(동기 모델 유지)

---

## 2. 이름 정책 — flat namespace, `Socket` prefix 제거

`Bn3Monkey::` namespace 로 컨텍스트를 보장하고 클래스명은 짧고 명확하게. v3 는 메이저
버전이므로 breaking change 허용 — 호환 alias 없음, "User" 는 모두 "Custom" 으로 통일.

| 카테고리 | 이름 |
| --- | --- |
| 결과/에러 | `NetworkResultCode`, `NetworkResult` |
| 설정 | `NetworkConfiguration`, `TlsClientConfiguration`, `TlsServerConfiguration`, `WebSocketConfiguration` |
| TLS enum | `TlsVersion`, `TlsV12CipherSuite`, `TlsV13CipherSuite`, `TlsClientAuthMode` |
| 클라이언트 | `Client`, `HttpClient`, `RequestClient` |
| 서버 | `RequestServer`, `BroadcastServer` |
| 핸들러 | `RequestHandler`(base), `HttpRequestHandler`, `CustomProtocolRequestHandler`, `BroadcastHandler` |
| 연결 | `ClientConnection` |
| HTTP 라우팅 | `HttpRouter`, 처리 모드 `RequestProcessingMode` |
| 서버측 view/builder | `HttpRequest`, `HttpResponse`, `CustomProtocolRequest`, `CustomProtocolResponse` |
| 클라이언트측 | `HttpClientRequest`, `HttpClientResponse` |

---

## 3. 타입 노출 전략 — pure interface vs. container

DLL boundary 안전성과 변경 자유도를 위해 두 패턴을 쓴다.

- **서버가 소유하는 타입**(`ClientConnection`, `HttpRequest/Response`,
  `CustomProtocolRequest/Response`, `HttpRouter`)은 **pure abstract interface**.
  구현 `*Impl` 은 DLL 안쪽에 있고, 서버가 dispatch 마다 stack 에 생성해 인터페이스만
  넘긴다. 사용자 코드는 Impl 을 절대 보지 않는다.
- **사용자가 직접 생성하는 타입**(`Client`, `HttpClient`, `RequestClient`,
  `HttpClientRequest/Response`, 각종 `*Server`)은 concrete class + **`_container[]` +
  placement-new** (PImpl-by-inline-storage). heap 할당 0, DLL 경계 안전.
  `IMPLEMENTATION_SIZE` 는 `static_assert(sizeof(Impl) <= IMPLEMENTATION_SIZE)` 로
  컴파일 타임에 가드한다(§11 참조).

---

## 4. `ClientConnection` — 단일 누적 버퍼 + offset

사용자 측은 pure abstract(`ip()/port()/isSecure()/isWebSocket()`), 구현
`ClientConnectionImpl` 은 연결 lifetime 동안 서버 풀에서 산다.

**단일 input/output 버퍼 + offset 모델을 택한 이유:** sniffing / keep-alive / 파이프라인
경계에서 byte 를 별도 vector 로 옮기는 복사 코드가 사라진다. state 전환 시 offset 만
갱신하고, view/builder Impl 은 이 버퍼 위에 올라간다.

버퍼는 `core/memory/buffer.hpp` 의 **`StagingBuffer`(2-커서)** 로 구현한다:

```
_received  recv 로 채운 양 (write 커서)   tail() = _data + _received
_sent      소비/전송한 양 (read 커서)     head() = _data + _sent
  pending()   = _received - _sent     (파싱/전송 대상 바이트)
  remaining() = _capacity - _received (tail 에 더 받을 수 있는 양)
```

- `fill(n)`/`drain(n)` 으로 커서 전진, `clear()` 로 0/0 리셋.
- `compact()` — 소비된 prefix 회수: 살아있는 `pending()` 바이트만 `head()` 에서 앞으로
  memmove. base 포인터 무효화 → recv 직전 같은 안전 지점에서만 호출.
- `reserve(extra)` — tail 공간 보장: 충분하면 no-op → `_sent>0` 이면 `compact()` →
  그래도 부족하면 realloc.

> ⚠ `compact()` 의 memmove 소스 오프셋은 반드시 `head()=_data+_sent`(살아있는
> `pending()` 바이트의 시작)여야 한다. 회귀 가드: `test/securitysockettest_staging_buffer.cpp`.

#### Impl 라이프사이클 — view/builder 는 stack-only

| 객체 | 생명주기 | 위치 |
| --- | --- | --- |
| `ClientConnectionImpl`, `_input/_output` | 연결 lifetime | server pool / 버퍼 heap |
| `Http*Impl` / `CustomProtocol*Impl` | **한 dispatch 호출 동안** | **stack** |

매 요청마다 fresh Impl → 요청 간 상태 누수 없음, dispatch 당 heap 할당 0. Impl 생성에
필요한 멤버 wiring 은 `ClientConnectionImpl::makeXxx()` factory 한 곳에 모은다.

---

## 5. 서버 핸들러 — virtual inheritance + `supportXxx()` 플래그

다중 상속의 diamond 는 `virtual inheritance` 로, 능력 탐지는 `supportXxx()` 플래그로.
서로 다른 두 문제를 각자의 메커니즘으로 푼다.

```cpp
class RequestHandler {                       // base
    virtual void onConnected   (const ClientConnection&) {}
    virtual void onDisconnected(const ClientConnection&) {}
    virtual bool supportHttp()         const { return false; }
    virtual bool supportUserProtocol() const { return false; }
    virtual bool supportWebSocket()    const { return false; }
    // 타입 복구용 self-accessor (아래 참조)
    virtual HttpRequestHandler*           asHttpRequestHandler()           { return nullptr; }
    virtual CustomProtocolRequestHandler* asCustomProtocolRequestHandler() { return nullptr; }
};
class HttpRequestHandler           : public virtual RequestHandler { /* supportHttp()=true */ };
class CustomProtocolRequestHandler : public virtual RequestHandler { /* supportUserProtocol()=true,
                                                                        WS config 있으면 supportWebSocket()=true */ };
```

사용자는 필요한 만큼만 조합한다 — `HttpRequestHandler` 만, `CustomProtocolRequestHandler`
만, 또는 둘 다 상속(WS 까지). `RequestServer::open(RequestHandler*, n)` 은 `supportXxx()`
플래그 조합으로 dispatch 루틴을 고른다.

> **RTTI 비활성화 빌드 호환.** 두 concrete 핸들러가 `RequestHandler` 를 *virtual* 상속하므로
> `static_cast<HttpRequestHandler*>(base)` 가 ill-formed 이고 `dynamic_cast` 도 없다. 그래서
> 각 derived 가 자기 self-accessor(`asXxxHandler()`)를 override 해 `this` 를 돌려주고, 서버는
> 그걸로 concrete 포인터를 복구한다. cross-cast 금지, 항상 base 에서 자기 타입으로.

---

## 6. HTTP 라우터 — Trie 기반, `:id` / `*rest`, 라우트-정적 FAST/SLOW

```cpp
using HandlerFn = std::function<void(ClientConnection&, HttpRequest&, HttpResponse&)>;
void get/post/put/del/patch/head/options(const char* pattern, HandlerFn,
        RequestProcessingMode mode = FAST);
void fallback(HandlerFn, RequestProcessingMode mode = FAST);   // 404/405 커스텀
```

- **문법**: Express 스타일 `:id`(path param), `*rest`(wildcard).
- **매칭**: 메서드별 `SegmentTrie` — O(path 깊이), 라우트 수 무관. 정적 경로 우선
  (`/user/me` > `/user/:id`).
- **FAST/SLOW 는 등록 시점에 고정**(메시지별로 안 바뀜):
  - `FAST` — event loop thread 에서 sync 실행. context switch 0. 메모리 lookup 등.
  - `SLOW` — worker thread 로 dispatch. event loop blocking 방지. DB/파일/외부 API 등.
  - HTTP 는 `READ_STREAM`/`WRITE_STREAM` 미지원(Custom 전용).

> Custom 의 `classifyMode(header)` 는 **메시지마다** 동적 분류(헤더 내용 기반). HTTP 는
> path 가 곧 작업 종류라 **등록 시 정적 분류**가 자연스럽다.

공개 표면(`MatchResult`/`PathParamView`/`Method`/`parseMethod`)은
`test/securitysockettest_http_router.cpp` 의 계약이므로 유지한다.

---

## 7. Custom Protocol — 4-콜백 프레이밍

`CustomProtocolRequestHandler`:

- `headerSize()` — 헤더 크기(프로토콜의 정적 속성 → connection 인자 없음).
- `payloadSize(header)` — 방금 받은 헤더에서 payload 길이 도출.
- `classifyMode(header)` — `FAST`/`SLOW`/`READ_STREAM`/`WRITE_STREAM` 중 **메시지별** 선택.
- `process(conn, req, res)` — `FAST`/`SLOW` 응답 작성. `conn` 은 응답 대상 식별용.

`CustomProtocolRequest`(header/payload view) / `CustomProtocolResponse`(data/capacity/
setLength builder) 는 §4 의 단일 버퍼 위 view/builder. raw TCP 와 WS tunnel 이 같은 핸들러
한 벌을 공유한다.

---

## 8. 스트리밍 — `WRITE_STREAM` / `READ_STREAM`

대용량을 "한 메시지 = 한 번에 다 버퍼링"하면 메모리가 폭발한다. 이를 **고정 메모리 +
경계 없는 연속 송수신**으로 바꾼다.

**계약 3줄:**
1. 스트림 = **"응답 없는 framed 메시지(`[header][payload]`)의 연속"**. 기존 프레이밍
   (`headerSize`/`payloadSize`)을 그대로 재사용 — 새 프레이밍 없음.
2. **모드 진입은 `classifyMode(header)`**(process() 아님). 진입하면 `onXxxStreamBegin`
   호출 + 전용 상태로 전환. 이후 모든 메시지는 `onXxxStreamData` 로만 라우팅.
3. **종료는 `onXxxStreamData` 반환값 `StreamProgress::COMPLETE`** — 핸들러가 헤더
   (EOF/last 플래그)를 보고 결정. 별도 `*End` 콜백 없음(비정상 종료는 `onDisconnected`).
   실패 시 `StreamProgress::ABORT` 로 `Closing` 전이.

| 모드 | 호출 핸들러 | 응답 | 종료 |
| --- | --- | --- | --- |
| FAST / SLOW | `process()` | 있음 | — |
| WRITE_STREAM(업로드) | `onWriteStreamBegin → onWriteStreamData*` | 없음 | `onWriteStreamData==COMPLETE` |
| READ_STREAM(다운로드) | `onReadStreamBegin → onReadStreamData*` | 청크 스트림 | `onReadStreamData==COMPLETE` |

- **고정 메모리**: 각 청크가 작은 framed 메시지 → `reserve(hs+ps)` 청크 단위 bounded.
- **READ 의 1-틱 어긋남**: "마지막 청크 판정(fill 시점)"과 "그 flush 완료(전이 시점)"가
  1틱 차이 → phase 멤버 `_read_last` 1비트로 bookkeeping. 모드 플래그가 아니라 송신 상태.
- **뷰 수명**: `onXxxStream*` 가 받는 `req`/`res` 는 **콜백 호출 동안만** 유효(다음 청크
  recv 전 drain/compact). 콜백 안에서 소비하거나 복사.
- **백프레셔**: READ 는 송신완료 뒤에만 다음 청크를 생산 → would-block 시 `Sending*Stream`
  유지하며 POLLOUT 대기. 자연 동기화.

> 모드 의미는 **클라이언트 관점**: `READ_STREAM`=클라가 서버에서 읽음(서버가 응답 송신),
> `WRITE_STREAM`=클라가 서버에 씀(서버 무응답). 둘을 무응답으로 묶지 말 것.

---

## 9. WebSocket — Custom Protocol 의 transport

#### Handshake
HTTP/1.1 `Upgrade: websocket` 표준. `WebSocketConfiguration::pattern` 이 path 와 매칭되면
framework 가 자동으로 101 handshake 후 WebSocket 그룹 state 로 전환. WS 진입은 **항상 HTTP
를 거친다**(RFC 6455). raw bytes 로 직접 WS state 진입 불가.

#### Frame opcode 처리
Custom 프로토콜은 **Binary frame(`0x2`) 에만 실린다.** Custom 헤더는 UTF-8 검증을 통과할 수
없어 Text 와 본질적으로 호환 안 됨.

| Opcode | 처리 |
| --- | --- |
| `0x0` Continuation | 자동 재조립(FIN=1 까지) |
| `0x1` Text | **거부.** Close(1003 "Unsupported Data") |
| `0x2` Binary | payload 추출 → Custom handler dispatch |
| `0x8` Close | Close echo → `Closing` → 송신 후 close + `onDisconnected` |
| `0x9` Ping | PONG 를 `_output_buffer` 에 채우고 `SendingWebSocketResponse` 로(즉시 send 안 함) |
| `0xA` Pong | 통계 카운터만, state 유지 |
| 그 외 reserved | Close(1002 "Protocol Error") |

Close status: 1000 정상 / 1002 protocol error / 1003 unsupported data / 1009 message too
big / 1011 server error.

> ⚠ **WS 한정 제어프레임 resume**: 스트림 도중 PING/CLOSE 가 오면 PONG/echo 를
> `SendingWebSocketResponse` 로 보낸 뒤 **원래 스트림 상태로 복귀**해야 한다 → resume state
> 1비트 필요. raw Custom 엔 제어프레임이 없어 무관.

---

## 10. 서버 흐름 — 단일 포트 sniffing + dispatch

```text
[accept] → Sniffing(첫 N byte 누적) → ProtocolSniffer::detect()
   ├── HTTP    → ReceivingHttpRequest   (HTTP 그룹)
   ├── CUSTOM  → ReceivingCustomMessage (Custom 그룹)
   ├── UNKNOWN → close
   └── NEED_MORE → Sniffing 유지

HTTP:   header 파싱 → Upgrade? → (WS pattern 매칭 시 101 → WebSocket 그룹) / 라우트 매칭
        → Content-Length 만큼 body → handler → keep-alive 면 WaitingForNextHttpRequest
Custom: headerSize 누적 → classifyMode → payloadSize 누적 → process / stream hook
WS:     frame 디코드(unmask) → opcode 처리 → BINARY payload 를 Custom 흐름으로
```

**Protocol identity 는 연결 lifetime 동안 불변.** Sniffing 은 시작 직후 최대 1회. 한 번
HTTP/Custom 으로 정해지면 같은 그룹 state 끼리만 순환. mid-connection switch 미지원(잘못된
byte → parse error → `Closing`). 단일 버퍼 + offset 덕에 sniffing 경계의 byte 복사 없음.

#### Connection State Machine 요약

이벤트는 4개뿐: `E_Accept` / `E_Read` / `E_Write` / `E_Disconnected`. SLOW worker 완료는
별도 event 가 아니라 worker 가 직접 `listener.addEvent(socket, WRITE)` 호출 → 자연스레
`E_Write` 로 들어옴. 모든 state 는 wait point 이고, handler 호출/파싱/unmask 같은 sync 작업은
Action 안에서 처리되어 별도 state 로 모델링하지 않는다.

17개 `ConnectionState`(그룹별):

- **Lifecycle**: `Sniffing`, `Closing`, `Closed`(terminal)
- **HTTP**: `ReceivingHttpRequest`, `SendingHttpResponse`, `WaitingForNextHttpRequest`
- **WebSocket**: `SendingHandshakeResponse`, `ReceivingWebSocketFrame`,
  `ReceivingWebSocketStream`, `SendingWebSocketResponse`, `SendingWebSocketStream`,
  `WaitingForNextWebSocketMessage`
- **Custom**: `ReceivingCustomMessage`, `ReceivingCustomStream`, `SendingCustomResponse`,
  `SendingCustomStream`, `WaitingForNextCustomMessage`

스트림 "모드" 는 플래그가 아니라 `Receiving/SendingXxxStream` **상태 자체**로 표현 → 단일
출처. `E_Disconnected` 는 모든 client state 에서 `Closed` 로 직행(peer 가 끊었으니 송신 불필요).

---

## 11. 동시성 — listener cross-thread + SLOW worker

- **`SocketMultiEventListener`** 하나가 accept fd 와 모든 client fd 를 소유(reactor).
  `addEvent/modifyEvent/removeEvent/wait` 모두 lock + 내부 **wakeup fd**(Linux eventfd /
  Windows loopback pair, listener 당 1개)로 보호 → worker thread 가 직접 호출하면 wait 중인
  main thread 를 즉시 깨운다. wakeup fd 는 `open()` 에서 만들고 `close()` 에서 정리(매
  dispatch 마다 새로 만들지 않음).
- **Worker thread 는 connection 당 1개, lazy 생성**(첫 SLOW dispatch 시점). FAST 만 쓰는
  connection 은 worker 를 안 만든다. **Task slot 은 1개**(queue 아님; `listener==nullptr`
  이 empty 지표, C++14 호환 위해 `std::optional` 미사용) — 한 connection 이 동시에 둘 이상
  처리하지 않으므로(state 가 `Sending*`→`Waiting*`→`Receiving*` 를 거쳐야 다음 SLOW 가능).

> **SLOW dispatch race invariant** (가장 중요): Action 함수 안에서
> `listener.removeEvent(socket)` 이 `queueToWorker` 보다 **반드시 먼저** 호출돼야 한다.
> 순서가 바뀌면 worker 가 즉시 실행되는 동안 peer 가 byte 를 보낼 때 socket POLLIN →
> 또 다른 handleRead → buffer race. SLOW 중에는 socket 이 listener 에서 일시 제거되어
> main thread 가 이 connection 의 read/write 를 안 받고, worker 가 출력을 다 채운 뒤
> `addEvent(socket, WRITE)` 로 다시 넣는다.

#### `IMPLEMENTATION_SIZE` 가드
`RequestServer`/`Client`/`BroadcastServer` 등 PImpl-by-inline-storage 타입은
`static_assert(sizeof(Impl) <= IMPLEMENTATION_SIZE)` 를 cpp 에 둬서, Impl 이 inline 컨테이너를
초과(placement-new 오버플로우 → 메모리 손상)하면 런타임이 아니라 컴파일 타임에 잡는다.

---

## 12. Body 메모리 모델

기존 vector 버퍼 위에 view 를 올린다. Content-Length 로 크기를 사전 판단 후 동적 resize.

1. 헤더 파싱 → Content-Length 추출
2. `> max` → 413 Payload Too Large + close
3. `≤ pdu_size` → 기본 버퍼 그대로
4. `> pdu_size` → `reserve(N)` 확장
5. handler 호출 → keep-alive 면 capacity 유지(shrink 안 함)

---

## 13. 클라이언트 계층

```text
Client (raw TCP/TLS)
    ├── HttpClient     (HTTP/HTTPS; lazy connect + keep-alive 재사용)
    └── RequestClient  (Custom Protocol; ctor 의 WebSocketConfiguration 유무로 raw/WS 결정)
```

- **`HttpClient`**: `get/head/del/options` + `post/put/patch` 헬퍼 + `request(HttpClientRequest&)`
  풀 컨트롤. 응답은 값 반환 + `resultCode()`(통신 성공/실패) / `status()`(HTTP) 내장.
  `HttpClientRequest::path(fmt,...)` 는 printf-style(자동 인코딩 안 함 — 사용자가 인코딩),
  `query(name,value)` 는 자동 percent-encode.
- **`RequestClient`**: `connect`/`send`/`receive`. `WebSocketConfiguration` ctor 면 TCP +
  Upgrade handshake → masked binary frame. **같은 send/receive 호출이 raw/WS 양쪽에서 동작**
  → 동일 테스트 코드로 양 모드 회귀.

#### percent-encoding 자동 적용

| 위치 | 처리 |
| --- | --- |
| 클라 `req.path(fmt,...)` | **수동**(printf 라 자동화 불가) |
| 클라 `req.query(name,value)` | **자동 encode**(name+value) |
| 클라 `req.header`/`req.body` | 안 함 |
| 서버 `req.pathParam`/`req.query` | **자동 decode** |
| 서버 `req.header` | 안 함(HTTP header 별도 규칙) |

---

## 14. 향후 확장

- Server-initiated WebSocket Close (현재는 peer-initiated 만).
- HTTP `100 Continue`, SSE(`StreamingHttpResponse` state 필요), idle timeout.
- SLOW dispatch 중 peer disconnect 시 worker partial cancellation(현재는 끝까지 실행 후 폐기).
- **Linux epoll 전환**(같은 reactor라 state machine 그대로, idle 다수에서 wait O(N)→O(M)).
- **Windows IOCP backend**(v4): 현 WSAPoll reactor 는 ~5000 동시연결이 실용 상한. 더 큰
  규모면 proactor 모델 + 별도 state machine(`E_*Completed` 기반) 필요.
- **driveRead head-of-line blocking**: 한 client 의 >1MB 요청을 받는 동안 다른 client 가
  굶는 문제 → PDU 단위 점유 후 양보(context-switch) 검토. [260601_memo.md](../workplan/260601_memo.md).
