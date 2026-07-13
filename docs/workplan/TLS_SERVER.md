# 서버측 TLS 구현 계획

## 개요

`TlsServerConfiguration`, `RequestServer(config, tls)`, `BroadcastServer(config, tls)`는
공개 API와 README 문서로만 존재하고 실제 구현은 없다. TLS 경로의 모든 진입점이
`throw std::runtime_error("Not Implemented")` 스텁이다.

| 대상 | 위치 | 현재 상태 |
|---|---|---|
| `TLSPassiveSocket` (ctor/close/bind/listen/accept) | `PassiveSocket.cpp:116-137` | 전부 throw |
| `TlsServerActiveSocket` (ctor/close/read/write) | `ServerActiveSocket.cpp:88-116` | 전부 throw |
| `TlsServerConfiguration` 필드 소비처 | — | `.valid()` 외 **0곳** |
| 서버측 TLS 테스트 | — | **0건** (`securitysockettest_tls.cpp`는 클라이언트만) |

`SSL_accept`가 `ClientActiveSocket.cpp:133`에 한 번 등장하지만 핸드셰이크 진단 로그의
**문자열 라벨**일 뿐이다. `TLS_server_method()`는 소스에도, no-TLS 스텁 헤더에도 없다.

현상: `RequestServer::open()`이 `PassiveSocketContainer(true, ...)`를 만드는 순간
`TLSPassiveSocket` 생성자가 예외를 던진다. `SecuritySocket.cpp:101`의 셤에 try/catch가
없으므로 예외가 사용자 코드로 그대로 전파된다.

---

## 구현 전에 반드시 정리해야 할 선행 결함

스텁 4개를 채우는 것만으로는 동작하지 않는다. 아래 5개는 TLS를 넣기 **전에** 고쳐야 한다.

### P0-1. `SocketContainer`의 memcpy 얕은 복사 — **[완료: move-only로 전환]**

`BaseSocket.hpp`의 `copyFrom()`이 raw 버퍼를 `memcpy`했다. `accept()`가 반환한 컨테이너는
값으로 복사되어 `ClientConnectionImpl::_container`로, `BroadcastServer`에서는
`client->container`로 들어갔다 — 즉 **두 컨테이너가 같은 fd를 가리켰다.**

이게 안전했던 유일한 이유는 모든 소켓 소멸자가 의도적으로 no-op이었기 때문이다
(`ServerActiveSocket.cpp:45`, `ClientActiveSocket.cpp:42-47`에 그 취지의 주석이 있다).
`~TlsServerActiveSocket()`이 `SSL_free(ssl)`을 하는 순간 accept 루프의 임시 객체가
소멸하면서 연결이 아직 들고 있는 `SSL*`을 해제한다 → use-after-free.

**결정: 복사를 삭제하고 이동으로 바꿨다.** 호출부가 5곳뿐이라 비용이 작았고,
얻는 것이 크다 — 어떤 fd/`SSL*`도 단 하나의 컨테이너만 소유한다.

- `SocketContainer`: 복사 생성/대입 `= delete`, `moveFrom()` 도입.
  이동은 `memcpy`가 아니라 저장된 실제 타입으로 **placement-new 이동 생성** 후 원본을
  파괴한다 (`_is_tls` 판별자 추가). 다형 타입을 `memcpy`하던 미정의 동작이 사라진다.
- 각 소켓 타입에 이동 생성자를 추가해 원본의 `_socket` / `_ssl` / `_context`를 무효화한다.
- `buffer`에 `alignas`를 붙였다. 기존 `char buffer[]`는 1-정렬이라 vptr을 가진 객체를
  placement-new 하고 있었다 (잠재 버그).
- 소유권이 유일해졌으므로 **이제 소멸자에서 `close()`를 불러도 안전하다.** 다만 이번
  커밋에서는 해제 지점을 `close()`로 유지했다 — 소멸자 해제는 세 서버의 teardown
  순서를 바꾸므로 별도 변경으로 남긴다.

호출부 변경: `RequestServer`는 `acquire(std::move(container), ...)`,
`ClientConnectionImpl` 생성자는 `ServerActiveSocketContainer&&`를 받는다.
`BroadcastServer`는 이동 후 새 소유자에서 소켓을 다시 읽는다.
`FixedObjectPool::acquire()`는 슬롯 고갈 시 인자를 forward 하기 **전에** 반환하므로,
`nullptr`을 받은 경우 원본 컨테이너는 아직 소켓을 소유하고 있다 (그 자리에서 close).

### P0-2. `PassiveSocket`에 가상 소멸자 없음 — **[완료]**

**Android 경고의 정체**: clang의 `-Wdelete-non-abstract-non-virtual-dtor`.

```text
BaseSocket.hpp:55: warning: destructor called on non-final 'Bn3Monkey::PassiveSocket'
that has virtual functions but non-virtual destructor
```

`~SocketContainer()`의 `sock->~PlainSocket()`이 원인이다. `PassiveSocket`은 non-final이고
가상 함수(`close`/`bind`/`listen`/`accept`)를 가졌는데 소멸자가 비가상이다.
`ServerActiveSocket`은 가상 소멸자가 있어 이 경고가 안 뜬다 — 그래서 `PassiveSocket`을
인스턴스화하는 TU(`RequestServer.cpp`, `BroadcastServer.cpp`)에서만 나타난다.

MSVC에는 대응 경고가 없어 Windows 빌드에서는 보이지 않는다. Android 프로젝트의
`build.ninja` FLAGS에도 `-Wall`이 없지만 **Android Studio의 clangd가 기본으로 `-Wall`을
켜기 때문에** IDE에서만 보인다. (NDK 26.1 clang으로 재현 확인.)

두 방향으로 모두 제거했다:

- `virtual ~PassiveSocket() = default` 추가 (경고의 직접 원인 제거)
- `SocketContainer::destroy()`가 `~PlainSocket()`이 아니라 `_is_tls` 판별자로
  **저장된 실제 타입의 소멸자**를 호출 (기반 클래스의 가상 소멸자 유무와 무관하게 정확)

`SSL_CTX_free`는 `TLSPassiveSocket::close()`에서 처리한다.

> `SSL_new()`는 `SSL_CTX`의 refcount를 올린다. 따라서 살아있는 연결보다 먼저
> `SSL_CTX_free()`를 호출해도 안전하다.

### P0-3. `PassiveSocket::accept()`가 TLS 여부를 하드코딩

`PassiveSocket.cpp:111`이 `{false, sock, &client_addr, nullptr}`로 고정이다.
`TLSPassiveSocket`이 `accept()`를 오버라이드해 `{true, sock, &addr, _context}`를
넘기지 않으면 TLS 액티브 소켓은 절대 생성되지 않는다. `accept()`는 이미 virtual이므로
오버라이드만 하면 된다.

### P0-4. `flushOutput()`이 would-block을 치명적 오류로 처리 (기존 버그) — **[완료]**

`ClientConnection.cpp:104-116`:

```cpp
if (code == NetworkResultCode::SOCKET_TIMEOUT) return true;   // would-block; stay
const int32_t n = r.bytes();
if (n < 0) return false;   // ← EAGAIN이 여기로 떨어진다
```

`createResult()`는 `EWOULDBLOCK`/`WSAEWOULDBLOCK`을 `SOCKET_TIMEOUT`이 아니라
**`SOCKET_CONNECTION_NEED_TO_BE_BLOCKED`** 로 매핑한다 (`NetworkResult.hpp:51-53, 87-88`).
그리고 `bytes()`는 `-1`이다. 따라서 부분 전송 중 송신 버퍼가 차면 `flushOutput()`이
`false`를 반환하고 연결이 끊긴다.

**이건 평문 소켓에도 이미 존재하는 버그다.** 지금 잘 안 터지는 이유는 응답이 작아
커널 송신 버퍼에 한 번에 들어가기 때문이다. TLS는 `SSL_write`가 `WANT_WRITE`를 훨씬
자주 반환하므로 이 버그가 즉시 표면화된다.

`recvChunk()`는 같은 코드가 `n <= 0 → return 0`으로 흡수되어 우연히 정상 동작한다.

**수정 완료**: 익명 네임스페이스에 `isWouldBlock(code)` 헬퍼를 두고
`SOCKET_TIMEOUT` / `SOCKET_CONNECTION_NEED_TO_BE_BLOCKED`를 함께 처리한다.
`flushOutput()`은 `n < 0` 치명적 검사 **이전에** 이 검사를 수행한다.
`recvChunk()`도 우연이 아니라 명시적으로 같은 헬퍼를 쓰도록 바꿨다.
`createTLSResult()`가 `SSL_ERROR_WANT_READ`/`WANT_WRITE`를 같은 코드로 매핑하므로
TLS I/O도 이 헬퍼 하나로 커버된다.

### P0-5. `isSecure()`가 설정값을 그대로 반환 — **[완료]**

`RequestServer.cpp:132`가 `_tls_configuration.valid()`를 `is_secure`로 넘겼다.
이건 "TLS 설정이 존재하는가"이지 "이 소켓이 암호화되었는가"가 아니다. 현재 값은
우연히 동치이지만(TLS passive socket이면 모든 연결이 TLS), 의미를 소켓에서 끌어온다:
`ServerActiveSocket::isTls()` 가상 함수 추가(`TlsServerActiveSocket`이 오버라이드),
`ClientConnectionImpl` 생성자에서 `_is_secure = _socket->isTls()`.
생성자의 `bool is_secure` 매개변수는 제거했다 — 소켓이 이미 답을 알고 있다.

---

## 설계

### 용어

이 문서가 기대는 기존 코드의 어휘 세 개. 새로 만드는 게 아니라 이미 있는 것들이다.

**`Disposition { KEEP, CLOSE }`** (`ClientConnection.hpp:43`)
`handleEvent()`가 서버에게 돌려주는 한 마디 — "나 계속 살려둬" / "나 정리해".
소비처는 `RequestServer.cpp:150-156` 하나뿐이고, 거기서 `CLOSE`면
`onDisconnected` → `removeEvent` → `closeSocket` → 풀 반납을 서버가 수행한다.
**`KEEP`은 "성공"이 아니다.** 바이트를 하나도 못 읽었어도(would-block), 응답을 절반만
보냈어도, SLOW 워커에게 소켓을 넘겼어도 전부 `KEEP`이다. 실제 뜻은
*"연결이 여전히 리스너에 등록된 채 다음 이벤트를 기다린다"* 이다.

**arm (무장/장전)**
epoll에 "이 fd는 읽기 가능해지면 깨워줘"(`EPOLLIN`) 또는 "쓰기 가능해지면
깨워줘"(`EPOLLOUT`)를 등록하는 것. `armListener()`는 현재 상태로부터 방향을 **추론**한다
(`isWriteState(_state) ? WRITE : READ`). 지금은 모든 상태의 방향이 고정이라 이게 항상 옳다.

**phase**
`SniffPhase` / `HttpPhase` / `CustomPhase` / `WebSocketPhase`. 프로토콜별 파싱과
다음 상태 결정을 맡는 전략 객체. `PhaseHost` 인터페이스(`ConnectionPhase.hpp:26-75`)를
통해 호스트를 보는데, **거기에 소켓이 없다** — `input()` / `output()` 스테이징 버퍼,
라우터, 핸들러, `dispatchSlow()` 뿐이다. 소켓을 만지는 것은 호스트의 `recvChunk()` /
`flushOutput()` 두 함수가 전부다.

> 이것이 TLS 작업의 구조적 안전장치다. TLS는 phase 계층 **아래**에 통째로 들어간다.
> phase는 자기 앞에 놓인 것이 복호화된 평문이라는 사실만 알면 되고, 그것이 TLS를
> 거쳤는지는 알 필요도 알 방법도 없다.

### 상태 머신 확장

#### 왜 상태를 추가해야 하는가

평문 서버는 `accept()`가 끝나면 바로 데이터를 읽을 수 있다. TLS는 그렇지 않다.
`accept()` 후에 **핸드셰이크라는 별도의 왕복 대화**가 있고, 그게 끝나야 비로소
애플리케이션 바이트가 흐른다. 그 대화 중인 구간을 표현할 상태가 지금 없다.

#### 왜 그냥 생성자에서 `SSL_accept()`를 돌리면 안 되는가

소켓이 논블로킹이라 `SSL_accept()`는 한 번에 안 끝난다. 상대 바이트를 더 기다려야
하면 "지금은 못 끝냄"을 반환하고 돌아온다. 그러면 블로킹으로 바꿔서 끝날 때까지
기다리면 되지 않나? **안 된다.** accept 루프는 단일 스레드다. 느린 클라이언트 하나가
핸드셰이크를 질질 끌면 서버 전체가 그 자리에 멈춘다 (slow-loris).

그래서 핸드셰이크도 다른 모든 I/O와 똑같이 **이벤트 루프에 얹어서 조금씩 진행**시킨다.
그러려면 "이 연결은 아직 핸드셰이크 중"이라는 상태가 필요하다.

#### 왜 `isReadState`/`isWriteState`에 넣지 않는가

기존 상태들은 방향이 고정이다. `ReceivingHttpRequest`는 언제나 읽기를 기다리고,
`SendingHttpResponse`는 언제나 쓰기를 기다린다. 그래서 `armListener()`가
상태만 보고 epoll에 READ를 걸지 WRITE를 걸지 결정할 수 있었다.

핸드셰이크는 다르다. `SSL_accept()`를 부를 때마다 OpenSSL이 **이번엔 뭐가 필요한지**를
알려준다 — 어떤 때는 "상대 바이트를 더 읽어야 해"(`WANT_READ`), 어떤 때는
"내가 보낼 게 남았는데 송신 버퍼가 찼어"(`WANT_WRITE`). 같은 상태인데 대기 방향이
호출마다 바뀐다. 그러니 `TlsHandshaking`을 두 함수 중 하나에 넣어봤자 답이 안 나온다.
방향은 **상태가 아니라 마지막 `SSL_accept()` 반환값**이 정한다.

`ConnectionState`에는 `TlsHandshaking`을 추가하되 `isReadState()`/`isWriteState()`
어느 쪽에도 넣지 않고, 리스너 방향은 `handshake()`의 반환으로 직접 건다.

```
accept()
   │
   ├── 평문 ──────────────────────────────► onAccept() → Sniffing / Receiving*
   │
   └── TLS
        └─► TlsHandshaking ──[SSL_accept]──┬── WANT_READ  → arm READ,  stay
                                           ├── WANT_WRITE → arm WRITE, stay
                                           ├── 1 (done)   → onConnected() 발화
                                           │                → _post_handshake_state
                                           └── error      → Closed (onConnected 미발화)
```

#### 어디에 끼워 넣는가

`groupOf(TlsHandshaking)`는 `Lifecycle`을 반환하고, `phaseForState()`는 Lifecycle 상태에
대해 `nullptr`을 반환한다. 그리고 `driveRead()`는 phase가 `nullptr`이면 연결을 끊는다.
따라서 핸드셰이크는 **phase 라우팅에 닿기 전에** `handleEvent()` 입구에서 가로채야 한다.
Sniff/Http/Custom/WebSocket 어느 phase도 건드리지 않는다 — 그들은 이미 복호화된
평문만 본다.

```cpp
Disposition ClientConnectionImpl::handleEvent(SocketEventType ev, ...) {
    if (ev == SocketEventType::DISCONNECTED) return Disposition::CLOSE;

    // 핸드셰이크 중이면 READ/WRITE 어느 이벤트로 깨어났든 SSL_accept를 한 번 더 민다.
    if (_state == ConnectionState::TlsHandshaking) return driveHandshake(listener);

    if (ev == SocketEventType::WRITE) return onWriteEvent(listener);
    ...
}
```

`driveHandshake()`가 하는 일은 네 갈래뿐이다.

| `handshake()` 반환 | 동작 |
| --- | --- |
| `WANT_READ` | 리스너에 READ를 걸고 `KEEP` — 다음 이벤트에 다시 시도 |
| `WANT_WRITE` | 리스너에 WRITE를 걸고 `KEEP` — 다음 이벤트에 다시 시도 |
| `DONE` | `_state = _post_handshake_state`, `onConnected()` 발화, READ 걸고 `KEEP` |
| `FAILED` | `CLOSE` (`onConnected()`는 발화된 적 없음) |

`_post_handshake_state`는 `onAccept()`가 핸들러 능력으로 계산해둔 값
(`Sniffing` / `ReceivingHttpRequest` / `ReceivingCustomMessage`)이다. 평문 경로에서는
그 값이 곧바로 `_state`가 되고, TLS 경로에서는 핸드셰이크가 끝난 뒤 옮겨 담긴다.
**즉 평문과 TLS의 차이는 "상태 머신 앞에 방 하나가 더 있다"는 것뿐이고, 그 방을
나가는 순간부터는 기존 흐름과 완전히 동일하다.**

### `ServerActiveSocket` 인터페이스 확장

`NetworkResultCode`(공개 enum, ABI)를 건드리지 않기 위해 핸드셰이크 결과는 내부 enum으로 표현한다.

```cpp
// ServerActiveSocket.hpp
//
// SSL_accept()를 한 번 민 결과. 값 이름은 OpenSSL의 SSL_ERROR_WANT_READ /
// SSL_ERROR_WANT_WRITE와 대응이 눈에 보이도록 맞춘다.
enum class TLSHandshakeState : uint8_t { DONE, WANT_READ, WANT_WRITE, FAILED };

class ServerActiveSocket : public BaseSocket {
public:
    virtual bool isTls() const { return false; }

    // 평문 소켓은 "핸드셰이크가 이미 끝난 것"과 같다.
    virtual TLSHandshakeState handshake() { return TLSHandshakeState::DONE; }

    virtual bool hasBufferedInput() const { return false; }   // SSL_pending
    // read()/write()는 기존 시그니처 유지
};
```

`TlsServerActiveSocket`:

- `isTls()` → `true`
- `handshake()` → `SSL_accept(ssl)` 결과를 `TLSHandshakeState`로 분류
- `hasBufferedInput()` → `SSL_pending(ssl) > 0`

> 명명 주의: 이 코드베이스의 TLS 접두사는 갈려 있다. 공개 헤더는 `Tls*`
> (`TlsClientConfiguration`, `TlsVersion`, `TlsClientAuthMode`), 내부 구현은 `TLS*`
> (`TLSPassiveSocket`, `TLSSocket`), 그리고 `TlsServerActiveSocket`은 또 `Tls*`다.
> 새 enum은 내부 전용이므로 `TLSHandshakeState`로 간다.

### 논블로킹 + 레벨 트리거 전제

`SocketEvent_Linux.cpp:97-101`은 `EPOLLET`을 쓰지 않는다(레벨 트리거). Windows는
`WSAPoll` 스냅샷 모델이라 역시 레벨 트리거다. 이 전제 위에서 두 가지를 처리해야 한다.

**(1) `SSL_pending` — 복호화 잔여 데이터**

`SSL_read()`가 하나의 TLS 레코드를 복호화한 뒤 `_input`이 가득 차면, 나머지 평문은
SSL 내부 버퍼에 남는다. TCP 소켓에는 읽을 바이트가 없으므로 **epoll은 다시 깨우지
않는다.** `driveRead()`의 재구동 루프는 이미 `_input`에 복사된 바이트만 커버한다.

`handleEvent()`의 READ 분기를 다음과 같이 감싼다:

```cpp
for (;;) {
    const int n = recvChunk();
    if (n < 0)  return Disposition::CLOSE;
    if (n == 0) break;                       // would-block

    const auto d = driveRead(listener);
    if (d == Disposition::CLOSE) return d;
    if (_detached) return d;                 // SLOW 워커로 넘어감
    if (isWriteState(_state)) return d;      // 응답 송신 대기로 전환됨
    if (!_socket->hasBufferedInput()) break; // SSL 내부 버퍼 비었음
}
return Disposition::KEEP;
```

**(2) TLS가 반대 방향을 요구하는 경우**

`SSL_read()`가 `WANT_WRITE`를, `SSL_write()`가 `WANT_READ`를 반환할 수 있다
(TLS 1.3 key update, 재협상). 논리적 상태는 그대로 두고 리스너 방향만 뒤집어야 한다.

`armListener()`를 **오버로드**한다. 새 어휘를 만들지 않고, "방향을 추론하는 버전"과
"방향을 명시하는 버전"을 나란히 둔다.

```cpp
// 상태에서 방향을 추론한다 (기존 동작, 기존 호출부 그대로).
// _tls_want가 걸려 있으면 그것이 상태보다 우선한다.
void armListener(SocketMultiEventListener& listener);

// 방향을 직접 지정한다. 상태가 방향을 모르는 구간(TlsHandshaking)에서 쓴다.
void armListener(SocketMultiEventListener& listener, SocketEventType desired);
```

`ClientConnectionImpl`에 `_tls_want`를 둔다:

```cpp
enum class TlsWant : uint8_t { NONE, READ, WRITE };
TlsWant _tls_want{ TlsWant::NONE };

void ClientConnectionImpl::armListener(SocketMultiEventListener& listener) {
    SocketEventType desired;
    if      (_tls_want == TlsWant::READ)  desired = SocketEventType::READ;
    else if (_tls_want == TlsWant::WRITE) desired = SocketEventType::WRITE;
    else desired = isWriteState(_state) ? SocketEventType::WRITE : SocketEventType::READ;
    armListener(listener, desired);
}

void ClientConnectionImpl::armListener(SocketMultiEventListener& listener,
                                       SocketEventType desired) {
    if (desired != _listener_event) {
        listener.modifyEvent(this, desired);
        _listener_event = desired;
    }
}
```

`_tls_want`는 해당 SSL 연산이 진행에 성공한 순간 `NONE`으로 되돌린다.
`handleEvent()`는 들어온 이벤트 종류가 아니라 **현재 논리 상태**를 기준으로 분기해야
한다 (WRITE 이벤트로 깨어났지만 실제로는 `SSL_read` 재시도인 경우가 있다).

### `SSL_write` 재시도 규약

OpenSSL은 `SSL_write()`가 `WANT_WRITE`를 반환한 뒤 재시도할 때 **동일한 버퍼 포인터와
길이**를 요구한다. `StagingBuffer`는 `reserve()`에서 재할당할 수 있으므로 두 모드를 켠다:

```cpp
SSL_CTX_set_mode(ctx, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER   // 포인터 이동 허용
                    | SSL_MODE_ENABLE_PARTIAL_WRITE);       // 부분 쓰기 허용
```

`ENABLE_PARTIAL_WRITE`를 켜면 `SSL_write`가 요청보다 적게 쓸 수 있고, 기존
`_output.drain(n)` 로직이 그대로 맞아떨어진다.

### `SSL_CTX` 구성 (`TLSPassiveSocket` 생성자)

`TlsClientActiveSocket` 생성자(`ClientActiveSocket.cpp:159-242`)를 대칭으로 옮긴다.
**예외를 던지지 않고 `_result`에 실패를 기록한다** — `RequestServerImpl::open()`의
`_socket->valid()` 검사가 이미 그것을 읽는다.

```cpp
TLSPassiveSocket::TLSPassiveSocket(bool is_unix_domain,
                                   const TlsServerConfiguration& cfg)
    : PassiveSocket(is_unix_domain)
{
    if (_result.code() != NetworkResultCode::SUCCESS) return;

    _context = SSL_CTX_new(TLS_server_method());
    if (!_context) { _result = { TLS_CONTEXT_INITIALIZATION_FAIL }; return; }

    // [1] 버전 범위 — 클라이언트와 동일 로직
    // [2] cipher list / ciphersuites — cfg.generateTLS1{2,3}CipherSuites()
    // [3] 서버 인증서 + 개인키
    //     - 비밀번호가 있으면 SSL_CTX_set_default_passwd_cb{,_userdata}
    //     - SSL_CTX_use_certificate_chain_file()  ← 중간 CA 포함. use_certificate_file 아님
    //     - SSL_CTX_use_PrivateKey_file()
    //     - SSL_CTX_check_private_key()           ← cert/key 짝 검증
    // [4] 클라이언트 인증 (mTLS)
    // [5] SSL_CTX_set_mode(...)  — 위 재시도 규약
    // [6] info callback — cfg.getOnTLSEvent()
}
```

`[3]`의 각 단계는 실패 시 명확한 코드를 남긴다. 지금 `open()`이 hang/throw로 끝나는
가장 큰 이유가 이 진단 부재다.

`[4]` 클라이언트 인증 모드 매핑:

| `TlsClientAuthMode` | verify mode |
|---|---|
| `AUTH_MODE_NONE` | `SSL_VERIFY_NONE` |
| `AUTH_MODE_OPTIONAL` | `SSL_VERIFY_PEER` |
| `AUTH_MODE_REQUIRED` | `SSL_VERIFY_PEER \| SSL_VERIFY_FAIL_IF_NO_PEER_CERT` |

`OPTIONAL`/`REQUIRED`일 때:
```cpp
SSL_CTX_load_verify_locations(_context, cfg.clientTrustStorePath(), nullptr);
// CertificateRequest에 수용 가능한 CA 목록을 실어 보낸다. 없으면 클라이언트가
// 어떤 인증서를 보내야 할지 모른다 — 실전 mTLS 상호운용에 필수.
SSL_CTX_set_client_CA_list(_context, SSL_load_client_CA_file(cfg.clientTrustStorePath()));
```

**info callback 주의**: 클라이언트는 `SSL_set_ex_data(_ssl, 0, cb)`로 세션마다 콜백을
꽂는다. 서버는 세션이 연결마다 생기므로 `SSL_CTX_set_info_callback`은 CTX에 한 번,
`SSL_set_ex_data`는 `TlsServerActiveSocket` 생성자에서 SSL마다 설정한다.
`trackTLSInfo`는 `ClientActiveSocket.cpp:123`에 있고 `SSL_ST_ACCEPT` 분기를 이미 갖고
있다 — 공용 헤더(`TlsHelper.hpp` 또는 새 `TlsTrace.hpp`)로 승격해 재사용한다.

### `TlsServerActiveSocket`

```cpp
TlsServerActiveSocket::TlsServerActiveSocket(int32_t sock, void* addr, void* ssl_context)
    : ServerActiveSocket(sock, addr, ssl_context)   // fd 저장 + setNonBlockingMode
{
    if (_result.code() != NetworkResultCode::SUCCESS) return;
    ssl = SSL_new(static_cast<SSL_CTX*>(ssl_context));
    if (!ssl) { _result = { TLS_INITIALIZATION_FAIL }; return; }
    if (SSL_set_fd(ssl, _socket) == 0) { _result = { TLS_SETFD_ERROR }; return; }
    SSL_set_accept_state(ssl);      // 핸드셰이크는 여기서 하지 않는다
}

TLSHandshakeState TlsServerActiveSocket::handshake() {
    ERR_clear_error();
    const int ret = SSL_accept(ssl);
    if (ret == 1) return TLSHandshakeState::DONE;
    switch (SSL_get_error(ssl, ret)) {
        case SSL_ERROR_WANT_READ:  return TLSHandshakeState::WANT_READ;
        case SSL_ERROR_WANT_WRITE: return TLSHandshakeState::WANT_WRITE;
        default:
            _result = createTLSResult(ssl, ret);   // 진단용 보존
            return TLSHandshakeState::FAILED;
    }
}

void TlsServerActiveSocket::close() {
    if (ssl) { SSL_shutdown(ssl); SSL_free(ssl); ssl = nullptr; }
    ServerActiveSocket::close();
}
```

**생성자에서 절대 블로킹 핸드셰이크를 하지 않는다.** accept 루프는 단일 스레드이므로
`SSL_accept`를 블로킹으로 돌리면 느린/악의적 클라이언트 하나가 서버 전체를 멈춘다
(slow-loris). 현재 `open()` hang의 구조적 재발 방지책이다.

`~TlsServerActiveSocket()`는 **비워둔다** (P0-1 불변식).

### `sizeof` 예산

`BaseSocket.hpp:73-74`에 `static_assert(sizeof(...) <= 64)`가 있다.
`TlsServerActiveSocket` = vptr(8) + `NetworkResult` + `_socket`(4) + `_client_ip`[22] +
`_client_port`(4) + `SSL*`(8) ≈ 56B. 여유가 크지 않다. 멤버를 추가할 때마다 확인한다.
초과하면 `size` 상수와 `static_assert`를 함께 올린다.

### `RequestServerImpl` accept 경로

```cpp
auto socket_container = _socket->accept();
auto* client_socket = socket_container.get();
if (client_socket->result().code() != SUCCESS) continue;

auto* connection = _socket_connection_pool.acquire(...);
if (!connection) { client_socket->close(); continue; }

connection->onAccept();                 // TLS면 _state = TlsHandshaking
if (!connection->isSecure())
    handler->onConnected(*connection);  // 평문은 즉시
_listener.addEvent(connection, SocketEventType::READ);
```

TLS일 때 `onConnected()`는 핸드셰이크 완료 시점에 `ClientConnectionImpl`이 직접
발화한다. 그래야 핸드셰이크 실패 연결에 대해 `onConnected` 없이 `onDisconnected`만
불리는 비대칭이 생기지 않는다. `_connected_notified` 플래그를 두고, 서버의 CLOSE
처리에서 이 플래그가 참일 때만 `onDisconnected`를 부른다.

### `TlsHelper.hpp` (no-TLS 빌드) 확장

`SECURITYSOCKET_TLS`가 꺼진 빌드에서도 컴파일되어야 하므로 스텁을 추가한다:

```
TLS_server_method, SSL_set_accept_state, SSL_pending,
SSL_CTX_use_certificate_chain_file, SSL_CTX_check_private_key,
SSL_CTX_set_client_CA_list, SSL_load_client_CA_file,
SSL_CTX_set_mode, SSL_CTX_set_options
상수: SSL_VERIFY_FAIL_IF_NO_PEER_CERT, SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER,
      SSL_MODE_ENABLE_PARTIAL_WRITE
타입: STACK_OF(X509_NAME) 대체 (using X509_NAME_STACK = void)
```

기존 스텁 결함도 같이 고친다:
- `SSL_shutdown`이 `void`를 반환한다 → 실제 OpenSSL은 `int`. 서버 `close()`가 반환값을
  쓰므로 `int`로 바꾼다.
- `SSL_CTX_set_verify`의 콜백 타입이 `int(*)(int, void*)` → 실제는
  `int(*)(int, X509_STORE_CTX*)`. 호출부가 `nullptr`만 넘겨서 지금은 무해하나 정정한다.

### 새 `NetworkResultCode`

`LENGTH` **직전에** 추가한다 (기존 값 시프트 금지 — 공개 enum, ABI).

```
TLS_SERVER_CERT_LOAD_FAIL          "Failed to load server certificate"
TLS_SERVER_KEY_LOAD_FAIL           "Failed to load server private key"
TLS_SERVER_KEY_MISMATCH            "Server certificate and private key do not match"
TLS_CLIENT_TRUST_STORE_LOAD_FAIL   "Failed to load client trust store (CA)"
TLS_HANDSHAKE_FAILED               "TLS handshake with the client failed"
```

`NetworkResult.hpp`의 `getMessage()`에 대응 케이스를 추가한다.

---

## 작업 단계

각 단계는 독립 커밋. 앞 단계가 초록불이 아니면 다음으로 넘어가지 않는다.

### Phase 0 — 선행 결함 (TLS 무관, 단독 검증 가능) — **[완료]**

- **0-1** ✅ `flushOutput()` / `recvChunk()`의 would-block 처리 (P0-4).
- **0-2** ✅ `SocketContainer` move-only 전환 + `virtual ~PassiveSocket()` (P0-1, P0-2).
- **0-3** ✅ `ServerActiveSocket::isTls()`, `_is_secure = _socket->isTls()` (P0-5).

검증: NDK 26.1 clang `-Wall -Wextra`로 7개 TU 전부 무경고
(`-Wdelete-non-abstract-non-virtual-dtor`, `-Wunused-private-field` 소멸).
MSVC 빌드 무경고(신규). 테스트 러너 204개 중 166개 통과 —
실패한 38개는 전부 `TLSConnection` 스위트로, 외부 `openssl s_server` 프로세스에
의존하는 **기존 실패**이며 이번 변경과 무관하다.

남은 회귀 테스트 부채: 응답 본문을 소켓 송신 버퍼보다 크게 만들어 부분 전송을
유도하는 케이스가 없다. P0-4는 그래서 아직 테스트로 고정되지 않았다. Phase 7의
10번 케이스(1MB 응답)가 이를 겸한다.

### Phase 1 — 진단 가능한 실패
`TLSPassiveSocket` 생성자가 throw 대신 `_result`를 세팅. 나머지 메서드는 base 위임.
`TlsServerActiveSocket`도 throw 제거. 이 시점에서 TLS 서버는 여전히 동작하지 않지만
`open()`이 명확한 `NetworkResult`를 반환한다. **게이트웨이의 hang이 여기서 사라진다.**

### Phase 2 — `SSL_CTX` 구성 — **[완료]**

`TLSPassiveSocket` 생성자에 버전/cipher/cert/key/mTLS/mode/info-callback 배선.
새 result code 5개 + `TlsHelper.hpp` 스텁 확장 + `trackTLSInfo`를 `TlsTrace.hpp`로 공용화.

`TLSPassiveSocket::accept()`는 아직 `::accept()` 후 **즉시 close** 한다. 연결을 pending으로
두면 리스닝 fd가 계속 readable이라 레벨 트리거 이벤트 루프가 스핀한다. Phase 3이 대체.

**발견 1 — `SSL_CTX_use_PrivateKey_file()`이 이미 짝을 검사한다.**
"키 파일이 못 쓸 물건"과 "키는 멀쩡한데 인증서와 다른 키쌍"이 **같은 실패로 뭉개진다.**
`SSL_CTX_check_private_key()`까지 도달하지 못하므로 원래 설계대로면 불일치가 영원히
`TLS_SERVER_KEY_LOAD_FAIL`로 보고된다. OpenSSL 버전마다 다른 `ERR_GET_REASON` 대신,
**인증서 없는 임시 컨텍스트에 키만 로드해봐서** 구분한다 (실패 경로에서만 컨텍스트 하나 추가).
`SSL_CTX_check_private_key()`는 미래의 OpenSSL이 교차검사를 미룰 경우를 대비해 남겨둔다.

**발견 2 — `test/msvc` 트리는 `SECURITYSOCKET_USING_TLS=OFF`다.**
러너의 `securitysocket.dll`에 OpenSSL이 아예 없다. `TLSConnection` 스위트 38개 실패의
진짜 원인이 이것이다 (`Open Fail : TLS context initialization failed` = `SSL_CTX_new` 스텁이
`nullptr` 반환). openssl 바이너리 부재도, 원격 커맨드 서버 부재도 아니다.
→ 서버측 TLS 테스트는 이 러너에서 **전부 SKIP** 된다. 자세한 대응은 아래 「검증 환경」 참조.

### Phase 3 — accept + 논블로킹 핸드셰이크 — **[완료]**

`TLSPassiveSocket::accept()` 오버라이드(`{true, sock, &addr, _context}`),
`TlsServerActiveSocket` 생성(`SSL_new`/`SSL_set_fd`/`SSL_set_accept_state` — 핸드셰이크는
생성자에서 하지 않는다) + `handshake()`, `ConnectionState::TlsHandshaking`,
`ClientConnectionImpl::driveHandshake()`, `onConnected` 발화 시점 이동.

Phase 4로 계획했던 TLS I/O(`read`/`write`/`SSL_pending` 드레인/`_tls_want`)도 함께 넣었다 —
핸드셰이크만으로는 아무것도 검증할 수 없어 분리가 무의미했다.

**`_tls_want`의 근거**: `createTLSResult()`가 `SSL_ERROR_WANT_READ`와 `WANT_WRITE`를
`SOCKET_CONNECTION_NEED_TO_BE_BLOCKED` 하나로 뭉갠다. 그런데 TLS에서는 **방향이 곧
정보**다 (`SSL_read()`가 write를 기다릴 수 있다). `NetworkResult`로는 전달할 수 없으므로
`ServerActiveSocket::ioWant()`를 추가해 소켓이 직접 기록한다. `armListener()`가 이 값을
상태보다 우선한다 — 레벨 트리거 epoll에서 방향이 틀리면 **영구 정지**다.

**`onConnected`/`onDisconnected` 짝 맞추기**: TLS는 핸드셰이크 완료 시점에
`driveHandshake()`가 `onConnected`를 발화하고 `_connected_notified`를 세운다.
핸드셰이크 중 죽은 연결은 핸들러가 본 적이 없으므로 `onDisconnected`도 부르지 않는다.
평문은 `onAccept()`에서 `_connected_notified = true`로 시작한다 (서버가 즉시 발화하므로).
이걸 빠뜨리면 **평문 연결에서 `onDisconnected`가 영영 안 불리는 회귀**가 난다.

#### 검증 (`scratchpad/tlsserver.cpp` + `openssl s_client`)

| 케이스 | 결과 |
|---|---|
| TLS 1.2 핸드셰이크 + 커스텀 프로토콜 에코 | ✅ `isSecure()==true` |
| TLS 1.3 핸드셰이크 + 에코 | ✅ |
| 60,000바이트 에코 (다중 TLS 레코드, `SSL_pending` 드레인, `WANT_WRITE`) | ✅ 바이트 단위 일치 |
| 파이프라이닝 (한 세그먼트에 두 메시지) | ✅ 둘 다 응답 |
| 핸드셰이크 중 강제 종료 (slow-loris) | ✅ 서버 생존, `onDisconnected` 미발화 |
| mTLS REQUIRED + 유효 클라이언트 인증서 | ✅ 왕복 성공 |
| mTLS REQUIRED + 인증서 없음 | ✅ alert로 거부 |
| mTLS REQUIRED + 신뢰하지 않는 CA 인증서 | ✅ alert로 거부 |

**Phase 2의 「암묵적 핸드셰이크」(옵션 C) 리스크는 해소됐다** — 명시적 `SSL_accept`
(옵션 A)로 갔으므로 OpenSSL 문서에만 의존하는 동작에 설계를 걸지 않았다.

**주의 — `process()`는 `capacity()`를 지켜야 한다.** FAST 모드 응답 버퍼는 `pdu_size`
(기본 64KB)다. 검증 중 1MB 응답을 쓰려다 힙을 넘겨 연결이 죽었다. 라이브러리 버그가
아니라 핸들러 계약이다 (큰 데이터는 스트림 모드용).

### Phase 4 — TLS I/O — **[완료: Phase 3에 흡수]**

`TlsServerActiveSocket::read/write`, `hasBufferedInput()`, `_tls_want` 기반 방향 전환,
`handleEvent()` READ 루프의 `SSL_pending` 소진. 핸드셰이크만으로는 아무것도 검증할 수
없어 Phase 3과 분리가 무의미했다.

### Phase 5 — mTLS — **[완료: Phase 2·3에 흡수]**

`SSL_CTX` 구성(`AUTH_MODE_OPTIONAL`/`REQUIRED`, `SSL_CTX_set_client_CA_list`)은 Phase 2에서,
실제 핸드셰이크 검증은 Phase 3에서 끝났다. 남은 것은 gtest 러너로의 이식(Phase 7)뿐이다.

### Phase 6 — `BroadcastServer` — **[취소: TLS를 얹지 않는다]**

**결정: `BroadcastServer`는 평문 전용이다.** `TlsServerConfiguration` 오버로드를
공개 API에서 **제거**했다 (`BroadcastServer(config, tls)` 생성자 삭제).

근거: 브로드캐스트는 같은 바이트를 모든 구독자에게 밀어내는 단방향 채널이다.
인증할 요청/응답 교환이 없고 peer마다 협상할 것도 마땅치 않다. 기밀성이 필요하면
**애플리케이션이 페이로드를 암호화해서 `write()`에 넘기면 된다** — 키 관리가 데이터를
소유한 쪽에 남는다. 인증된 암호화 채널이 필요하면 `RequestServer` + TLS를 쓴다.

이 결정이 피해간 복잡도 (실제로 구현하다 되돌렸다):

- **`SSL` 객체는 스레드 안전하지 않다.** `BroadcastServer`는 모니터 스레드가
  핸드셰이크·close를, 브로드캐스트 호출 스레드가 `write()`를 한다. 모니터의
  `close()`(= `SSL_free`)가 브로드캐스트의 `SSL_write` 도중에 실행되면 **use-after-free**다.
  per-client `std::mutex`로 모든 소켓 사용을 직렬화해야 했다.
- 핸드셰이크 미완료 클라이언트를 `_active_clients`와 분리한 `_handshaking_clients`
  목록으로 격리해야 했다 (브로드캐스트가 협상 중인 연결을 때리면 안 되고,
  실패할 수도 있는 peer에 `onClientConnected`를 쏘면 안 된다).
- 모니터의 READ/WRITE 이벤트로 핸드셰이크를 구동하고, 실패 시 조용히 회수해야 했다.

`RequestServer`에는 이 문제가 없다 — 단일 이벤트 루프가 모든 것을 소유한다.

README의 「Using TLS Notification Server」 예제도 제거하고 설계 근거로 대체했다.

### Phase 7 — 테스트 + 문서
아래 테스트 계획. README의 서버 TLS 섹션에서 "미구현" 표기 제거.

---

## 검증 환경 — 반드시 먼저 읽을 것

**gtest 러너로는 서버측 TLS를 검증할 수 없다.** `test/msvc/out/build/x64-Debug`의 CMake
캐시가 `SECURITYSOCKET_USING_TLS:BOOL=OFF`라 러너가 링크하는 `securitysocket.dll`에
OpenSSL이 컴파일되어 있지 않다. 반면 메인 트리 `out/build/x64-Debug`는 `ON`이다.

```bash
grep -i SECURITYSOCKET_USING_TLS test/msvc/out/build/x64-Debug/CMakeCache.txt   # OFF
grep -i SECURITYSOCKET_USING_TLS out/build/x64-Debug/CMakeCache.txt             # ON
```

`securitysockettest_tls_server.cpp`의 테스트들은 이 상황을 감지해
(`open()`이 `TLS_CONTEXT_INITIALIZATION_FAIL`을 반환하면) `GTEST_SKIP()` 한다.
즉 러너에서 초록불이어도 **아무것도 검증하지 않은 것**이다.

### 당분간의 검증 수단: `tlsprobe`

메인 트리의 TLS-enabled DLL에 직접 링크하는 작은 콘솔 프로그램으로 검증한다.
(`scratchpad/tlsprobe.cpp`, 빌드 산출물은 `out/build/x64-Debug/tlsprobe.exe`)

```bash
./script/build.sh                       # 메인 트리 (TLS=ON)
cd out/build/x64-Debug
./tlsprobe.exe <server.crt> <server.key> <other.key> <ca.crt>
```

Phase 2 시점 결과: 8/8 통과 (valid open / cert 없음 / key 없음 / cert·key 불일치 /
mTLS trust store 없음·잘못됨·정상 / mTLS optional).

### 해소 방법 (택일, 아직 안 함)

1. `test/msvc` 트리를 `-DSECURITYSOCKET_USING_TLS=ON`으로 재구성.
   **주의**: `script/build*.sh`는 PATH를 sanitize해서 `git`이 없다. 재구성하면
   OpenSSL FetchContent가 깨진다 (CLAUDE.md 참조). `_deps`를 메인 트리에서 복사하거나
   PATH에 git을 남기는 등 별도 처리가 필요하다.
2. 메인 트리에서 gtest 러너 exe를 만든다 (현재 `securitysockettest.dll`까지만 만든다).
3. `tlsprobe`를 유지하고 Phase 7에서 정식 러너로 승격.

**Phase 3 이후의 스모크 테스트(`openssl s_client` 접속)도 같은 제약을 받는다.**
로컬 `openssl`은 `/mingw64/bin/openssl`에 존재하므로 인증서 생성과 클라이언트 역할은
문제없다 — 막는 것은 오직 러너의 TLS 부재다.

## 테스트 계획

`test/securitysockettest_tls_server.cpp` 신설. 기존 `securitysockettest_tls.cpp`의
`TLSServerProcess`(=`openssl s_server`) 패턴을 뒤집어 `openssl s_client`를 쓴다.

**인증서 생성**: 테스트 픽스처에서 `openssl req -x509 -newkey rsa:2048 -nodes ...`로
자기서명 서버 인증서와 CA/클라이언트 인증서를 임시 디렉토리에 생성. Android 빌드는
`LocalProcess`가 no-op이므로(`1f8f0a4`) 해당 스위트를 `#if` 가드한다.

| # | 케이스 | 기대 |
|---|---|---|
| 1 | cert/key 정상, TLS1.2 + TLS1.3 | `open()` SUCCESS, `s_client` 핸드셰이크 완료 |
| 2 | cert 경로 오타 | `open()` → `TLS_SERVER_CERT_LOAD_FAIL` (**throw 아님**) |
| 3 | cert/key 짝 불일치 | `open()` → `TLS_SERVER_KEY_MISMATCH` |
| 4 | 서버 TLS1.3 전용 × 클라이언트 TLS1.2 전용 | 핸드셰이크 실패, 서버 살아있음 |
| 5 | cipher suite 교집합 없음 | 핸드셰이크 실패, 서버 살아있음 |
| 6 | `AUTH_MODE_REQUIRED` + 클라이언트 인증서 없음 | 서버가 거부, 다른 연결 정상 |
| 7 | `AUTH_MODE_REQUIRED` + 유효 클라이언트 인증서 | 성공 |
| 8 | `AUTH_MODE_OPTIONAL` + 인증서 없음 | 성공 |
| 9 | TLS 위 HTTP GET 왕복 | 라우터 정상 동작, `isSecure()==true` |
| 10 | 응답 본문 1MB (다중 레코드 + `WANT_WRITE`) | 완전 수신 |
| 11 | 요청 파이프라이닝 (`SSL_pending` 경로) | 두 응답 모두 수신 |
| 12 | 핸드셰이크 중 클라이언트 강제 종료 | `onDisconnected` 미발화, 커넥션 풀 반납 |
| 13 | 핸드셰이크 미완료 연결 32개 (풀 고갈) | 서버 살아있음, 신규 평문 연결 거부만 |

12·13번이 slow-loris 회귀 가드다.

**DLL export 주의**: `TLSPassiveSocket` / `TlsServerActiveSocket`을 테스트에서 직접
인스턴스화한다면 `SECURITYSOCKET_API`가 필요하거나, `connection/` 계열처럼 소스를
`securitysockettest` 타겟에 직접 컴파일해 넣어야 한다. 가능하면 공개 API
(`RequestServer`)만으로 테스트해 export를 늘리지 않는다.

**stale object 함정**: `test/msvc` 트리는 `src/` 변경을 놓치는 경우가 있다.
`ServerActiveSocket.cpp.obj` / `PassiveSocket.cpp.obj` / `ClientConnection.cpp.obj`를
지우고 `build_msvctest.sh`를 돌린다 (CLAUDE.md 참조).

---

## 미해결 / 후속

- **소멸자 기반 자원 해제**: `SocketContainer`가 move-only가 되어 소유권이 유일해졌으므로,
  이제 `~ServerActiveSocket()`이 `close()`를 불러도 안전하다. 하지만 여전히 명시적
  `close()`가 유일한 해제 지점이다. 소멸자로 옮기면 `RequestServerImpl::close()`,
  `ClientImpl::close()`, `BroadcastServerImpl`의 teardown 순서가 함께 바뀌므로 별도 작업.
- **`SocketContainer` 대입 시 fd 누수**: `_container = PassiveSocketContainer(...)`가
  기존에 살아있던 소켓을 들고 있었다면 `destroy()`는 (no-op 소멸자를 부르므로) fd를
  닫지 않는다. 서버를 재open 하는 경로가 없어 현재는 도달 불가. 위 항목과 함께 해소된다.
- **`SSL_shutdown` 2단계**: `close()`에서 한 번만 호출하고 peer의 close_notify를 기다리지
  않는다. TLS 규격상 truncation attack 방어를 위해 양방향 종료가 권장되나, 이벤트
  루프에서 종료 핸드셰이크를 돌리려면 `Closing` 상태 확장이 필요하다. 현 범위 밖.
- **세션 재개 / 티켓**: `SSL_CTX_set_session_id_context` 미설정. mTLS + 세션 재개
  조합에서 OpenSSL이 경고할 수 있다. Phase 5에서 확인.
- **재협상 차단**: `SSL_OP_NO_RENEGOTIATION` (TLS 1.2) 설정 여부 결정 필요.
  켜면 `_tls_want` 반대 방향 처리 복잡도가 상당히 줄어든다. **켜는 것을 권장.**
- **ALPN**: HTTP/1.1만 지원하므로 지금은 불필요. HTTP/2를 넣으면 필수.
