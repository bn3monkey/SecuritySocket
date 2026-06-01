# Milestone: WRITE_STREAM / READ_STREAM 연속 송수신 (구현 스펙)

> **브랜치:** `v3` · **상태:** 설계 확정, 미구현 ([260531_progress.md](./260531_progress.md) §5 #5 후속)
> **상세 상태머신:** [state-machine.html](./state-machine.html) §6-3(WS) / §6-4(Custom) / §6-7
> **구현 지점:** [SecuritySocket.hpp](../../src/SecuritySocket.hpp) ·
> [ConnectionState.hpp](../../src/implementation/connection/ConnectionState.hpp) ·
> [ConnectionPhase.hpp](../../src/implementation/connection/ConnectionPhase.hpp) ·
> [CustomPhase.cpp](../../src/implementation/connection/CustomPhase.cpp) ·
> [WebSocketPhase.cpp](../../src/implementation/connection/WebSocketPhase.cpp) ·
> [ClientConnection.cpp](../../src/implementation/ClientConnection.cpp)

---

## 1. 무엇을 / 왜

`RequestProcessingMode` 4모드 중 `WRITE_STREAM`/`READ_STREAM` 이 지금은 "한 메시지 = 한 번에
다 버퍼링"으로 동작(WRITE 는 `reserve(total)` 후 무응답 1회, READ 는 FAST 취급). → 대용량에서
메모리 폭발 + 연속 송수신 불가. 이를 **고정 메모리 + 경계 없는 연속 송수신**으로 바꾼다.

**핵심 계약 (3줄):**
1. 스트림 = **"응답 없는 framed 메시지(`[header][payload]`)의 연속"**. 기존 프레이밍
   (`headerSize`/`payloadSize`) 그대로 쓴다 — 새 프레이밍 없음.
2. **모드 진입은 `classifyMode(header)`** (process() 아님). 진입하면 `onXxxStreamBegin` 호출 +
   전용 상태로 전환. 그 뒤 모든 메시지는 `onXxxStreamData` 로만 라우팅.
3. **종료는 `onXxxStreamData` 의 반환값 `StreamProgress::COMPLETE`** — 핸들러가 헤더(EOF/last
   플래그)를 보고 결정. 별도 `*End` 콜백 없음(비정상 종료는 기존 `onDisconnected`).

비목표(차후): SLOW worker 오프로딩(1차 inline) · 헤더 없는 raw 스트림 · zero-copy ·
full-duplex 동시 스트림.

---

## 2. 추가할 핸들러 API + `processWithoutResponse` 제거

[SecuritySocket.hpp](../../src/SecuritySocket.hpp#L496) `CustomProtocolRequestHandler` 에:

```cpp
// 진행 신호 — "더 있음 / 이번이 마지막". 이 반환값이 곧 "종료 시점".
enum class StreamProgress { CONTINUE, COMPLETE };

// WRITE_STREAM (서버가 받음, 무응답). Begin=셋업(파일 open 등), Data=청크마다(헤더로 분기+끝판정).
virtual void           onWriteStreamBegin(const ClientConnection& conn,
                                          const CustomProtocolRequest& req) { (void)conn; (void)req; }
virtual StreamProgress onWriteStreamData (const ClientConnection& conn,
                                          const CustomProtocolRequest& req) {
    (void)conn; (void)req; return StreamProgress::COMPLETE;
}

// READ_STREAM (서버가 보냄, 요청 1개 → N청크). Begin=셋업, Data=매 송신완료마다 res 채움.
virtual void           onReadStreamBegin(const ClientConnection& conn,
                                         const CustomProtocolRequest& req) { (void)conn; (void)req; }
virtual StreamProgress onReadStreamData (const ClientConnection& conn,
                                         CustomProtocolResponse& res) {
    (void)conn; (void)res; return StreamProgress::COMPLETE;
}
```

- 4개 다 **빈 기본구현** → 기존 FAST/SLOW 핸들러 영향 0.
- **`processWithoutResponse()` (순수 가상) 삭제.** 유일한 호출처가 WRITE_STREAM 이었고 위 훅으로
  이전됨. ⚠ **순수 가상 제거라 모든 서브클래스(테스트 6개)가 동시에 깨짐 — 한 번에 가야 함.**
- `CustomProtocolResponse`(data/capacity/setLength)는 그대로 재사용. 변경 없음.

| 모드 | 호출 핸들러 | 응답 | 종료 |
| --- | --- | --- | --- |
| FAST / SLOW | `process()` | 있음 | — |
| WRITE_STREAM | `onWriteStreamBegin → onWriteStreamData*` | 없음 | `onWriteStreamData==COMPLETE` |
| READ_STREAM | `onReadStreamBegin → onReadStreamData*` | 청크 스트림 | `onReadStreamData==COMPLETE` |

---

## 3. 추가할 ConnectionState (모드 = 상태, 플래그 없음)

"지금 스트림 모드냐"를 phase 플래그가 아니라 **상태**로 표현 → 단일 출처. Custom 3→5, WebSocket
4→6 (전체 13→17). phase 는 `host.state()` 로 분기한다.

| 추가 상태 | 그룹 | 방향 | 의미 |
| --- | --- | --- | --- |
| `ReceivingCustomStream`    | Custom    | READ  | WRITE_STREAM 청크 수신 중 |
| `SendingCustomStream`      | Custom    | WRITE | READ_STREAM 청크 송신 중 |
| `ReceivingWebSocketStream` | WebSocket | READ  | WS 터널 WRITE_STREAM 수신 중 |
| `SendingWebSocketStream`   | WebSocket | WRITE | WS 터널 READ_STREAM 송신 중 |

전이(Custom; WS 는 1:1 대칭, `*Custom*`→`*WebSocket*`):

```
WRITE_STREAM:
  Receiving/WaitingForNextCustomMessage --(classifyMode=WRITE_STREAM, onWriteStreamBegin)--> ReceivingCustomStream
  ReceivingCustomStream --(onWriteStreamData==CONTINUE)--> ReceivingCustomStream  (self)
  ReceivingCustomStream --(onWriteStreamData==COMPLETE)--> WaitingForNextCustomMessage

READ_STREAM:
  Receiving/WaitingForNextCustomMessage --(classifyMode=READ_STREAM, onReadStreamBegin+첫 청크)--> SendingCustomStream
  SendingCustomStream --(flush 완료, 다음 청크 CONTINUE)--> SendingCustomStream  (self)
  SendingCustomStream --(flush 완료, 마지막 청크였음)--> WaitingForNextCustomMessage
```

**건드릴 곳:**
- [ConnectionState.hpp](../../src/implementation/connection/ConnectionState.hpp): enum 4개 +
  `groupOf`(Custom 2 / WebSocket 2) + `isReadState`(`Receiving*Stream`) + `isWriteState`(`Sending*Stream`).
- `phaseForState`([ClientConnection.cpp:117](../../src/implementation/ClientConnection.cpp#L117)):
  Custom 스트림 2 → `_custom_phase`, WS 스트림 2 → `_websocket_phase`.
- **`PhaseHost::state()` 신설**([ConnectionPhase.hpp](../../src/implementation/connection/ConnectionPhase.hpp)) —
  `virtual ConnectionState state() const = 0;`. `ClientConnectionImpl::state()` 가
  **이미 존재**([ClientConnection.hpp:73](../../src/implementation/ClientConnection.hpp#L73)) → `override` 만.
  테스트 `FakePhaseHost` 에도 `state()`+상태 주입 추가.

**남는 1비트 `_read_last`** (phase 멤버): READ 는 "마지막 청크 판정(fill 시점)"과 "그 flush 완료
(전이 시점)"가 1틱 어긋난다. 모드 플래그가 아니라 송신 bookkeeping. `reset()` 에서 클리어.

**Action 반환값**(상세는 state-machine.html §6-4/§6-3, §7-3): E_Read 액션에
`WRITE_STREAM_STARTED`/`DOING`/`DONE` + `READ_STREAM_STARTED`, E_Write 액션에
`READ_STREAM_DOING`/`DONE` 추가. WS 의 `DISPATCHED_FAST_NO_RESPONSE` 는 제거.

---

## 4. 흐름 (CustomPhase 의사코드)

`frameOne(in)` = 헤더+payload 한 메시지 완성(기존 누적 헬퍼). 미완이면 현재 read-state 유지.

### WRITE_STREAM 수신 (`onReadable`)

```
onReadable(host):
    loop:                                          # 버퍼에 완성된 메시지가 있는 동안
        if host.state() != ReceivingCustomStream:  # 일반 수신
            msg = frameOne(in)                     # 미완 → return ReceivingCustomMessage
            if classifyMode(msg.header) == WRITE_STREAM:
                onWriteStreamBegin(conn, msg); in.drain(msg.size)
                return ReceivingCustomStream       # ★ 모드 = 상태
            else: ... FAST/SLOW/READ_STREAM ...
        else:                                      # ReceivingCustomStream
            msg = frameOne(in)                     # 미완 → return ReceivingCustomStream
            r = onWriteStreamData(conn, msg)       # 헤더 분기 + fwrite
            in.drain(msg.size)
            if r == COMPLETE: return WaitingForNextCustomMessage
            continue                               # 다음 청크
```
- 고정 메모리: 각 청크가 작은 framed 메시지 → `reserve(hs+ps)` 청크 단위 bounded.
- 무응답: output 안 건드림 → `isWriteState` 전이 없음.
- 재진입: host `driveRead` 가 버퍼 빌 때까지 반복, 비면 `armListener(READ)` 후 복귀.

### READ_STREAM 송신 (`onReadable` 진입 + `onSendComplete` 연속)

```
onReadable(host):                  # 요청 도착 (스트림 모드 아님)
    msg = frameOne(in)
    if classifyMode(msg.header) == READ_STREAM:
        onReadStreamBegin(conn, msg); in.drain(msg.size)
        out.clear(); r = onReadStreamData(conn, res(out)); out.fill(res.length)
        _read_last = (r == COMPLETE)
        return SendingCustomStream  # ★ 모드 = 상태. host 가 flush

onSendComplete(host):              # 청크 flush 완료
    if host.state() == SendingCustomStream:
        if _read_last: return WaitingForNextCustomMessage    # ← 종료(마지막 청크 송신완료)
        out.clear(); r = onReadStreamData(conn, res(out)); out.fill(res.length)
        _read_last = (r == COMPLETE)
        return SendingCustomStream                           # 다음 청크
    return WaitingForNextCustomMessage                       # 기존 단발 응답
```
- host `onWriteEvent → onSendComplete` 루프([ClientConnection.cpp:209](../../src/implementation/ClientConnection.cpp#L209))
  가 "flush 완료 → 다음 상태"를 이미 처리 → host 변경 불필요.
- 클라가 총량/끝을 알게 첫 청크(또는 begin 응답)에 메타(총 길이)를 실어 보낸다.

### WS 터널 변형 ([WebSocketPhase.cpp](../../src/implementation/connection/WebSocketPhase.cpp))

raw 와 동일하되 **청크가 WS 프레임으로 한 겹 더 감싸진다**:
- 수신: WS 메시지(프래그먼트 reassemble) 1개 = Custom 청크 1개 → `onWriteStreamData`.
- 송신: `onReadStreamData` 결과를 `encodeWebSocketFrame(BINARY)` 로 wrap 후 송신.
- 상태: `Receiving/SendingWebSocketStream`.
- ⚠ **제어 프레임 resume**(WS 한정): 스트림 중 PING/CLOSE 가 오면 PONG/echo 를
  `SendingWebSocketResponse` 로 보낸 뒤 **원래 스트림 상태로 복귀**해야 한다. 현재
  `WebSocketPhase::onSendComplete` 는 항상 `WaitingForNextWebSocketMessage` 반환 →
  **resume state 1개를 기억**하도록 고쳐야 함. (raw Custom 엔 제어 프레임 없어 무관.)

---

## 5. 목표 동작 예제 (= 통합 테스트 픽스처가 될 것)

공유 헤더:

```cpp
enum class Op : int32_t {
    WriteBegin,   // WRITE_STREAM 진입: payload = 원격 파일명
    WriteChunk,   // 스트림 청크: payload = 파일 바이트, last=1 이면 마지막
    OpenRead,     // FAST: payload = 파일명, 응답 len = 파일 총 크기
    ReadBegin,    // READ_STREAM 진입: 서버가 raw 청크 연속 송신
};
struct Header { Op op; int32_t req_no; uint32_t len; int32_t last; };  // len = payloadSize
```

### 서버 — `RequestServer` + 스트리밍 핸들러

```cpp
struct FileService : CustomProtocolRequestHandler {
    // ⚠ 예제 단순화: 핸들러 1개 공유라 "동시 1연결" 가정. 실제는 conn 으로 키잉.
    FILE* _wf{ nullptr };   // onWriteStreamBegin 에서 open
    FILE* _rf{ nullptr };   // OpenRead 에서 open

    size_t headerSize() override { return sizeof(Header); }
    size_t payloadSize(const void* h) override { return static_cast<const Header*>(h)->len; }

    RequestProcessingMode classifyMode(const void* h) override {   // ★ 모드 진입 결정
        switch (static_cast<const Header*>(h)->op) {
        case Op::WriteBegin: return RequestProcessingMode::WRITE_STREAM;
        case Op::ReadBegin:  return RequestProcessingMode::READ_STREAM;
        default:             return RequestProcessingMode::FAST;     // OpenRead
        }
        // WriteChunk 는 이미 WRITE_STREAM 모드에서 도착 → classifyMode 안 거치고 onWriteStreamData 로.
    }

    void process(const ClientConnection&, const CustomProtocolRequest& req,   // FAST: OpenRead
                 CustomProtocolResponse& res) override {
        auto* h = static_cast<const Header*>(req.header());
        uint32_t reply_len = 0;
        if (h->op == Op::OpenRead) {
            _rf = fopen(static_cast<const char*>(req.payload()), "rb");
            fseek(_rf, 0, SEEK_END); reply_len = (uint32_t)ftell(_rf); fseek(_rf, 0, SEEK_SET);
        }
        new (res.data()) Header{ h->op, h->req_no, reply_len, 0 };
        res.setLength(sizeof(Header));
    }

    void onWriteStreamBegin(const ClientConnection&, const CustomProtocolRequest& req) override {
        _wf = fopen(static_cast<const char*>(req.payload()), "wb");   // payload = 파일명
    }
    StreamProgress onWriteStreamData(const ClientConnection&,
                                     const CustomProtocolRequest& req) override {
        auto* h = static_cast<const Header*>(req.header());
        fwrite(req.payload(), 1, h->len, _wf);                        // 청크 append
        if (h->last) { fclose(_wf); _wf = nullptr; return StreamProgress::COMPLETE; }
        return StreamProgress::CONTINUE;
    }

    void onReadStreamBegin(const ClientConnection&, const CustomProtocolRequest&) override {
        // _rf 는 앞선 OpenRead(FAST)에서 이미 열림
    }
    StreamProgress onReadStreamData(const ClientConnection&,
                                    CustomProtocolResponse& res) override {
        size_t n = fread(res.data(), 1, res.capacity(), _rf);
        res.setLength(n);
        if (feof(_rf)) { fclose(_rf); _rf = nullptr; return StreamProgress::COMPLETE; }
        return StreamProgress::CONTINUE;
    }
};

int main() {
    initializeSecuritySocket();
    NetworkConfiguration config{ "0.0.0.0", 21345 };
    FileService handler; RequestServer server{ config };
    server.open(&handler, /*num_of_clients=*/16);
    /* ... */ server.close();
    releaseSecuritySocket();
}
```

### 클라이언트 — `Bn3Monkey::Client`

```cpp
// Client::read 는 부분 read(메모: SocketClient read semantics) — 고정 프레이밍은 직접 루프.
static bool readFully(Client& c, void* buf, size_t n) {
    auto* p = static_cast<char*>(buf);
    for (size_t got = 0; got < n; ) {
        auto r = c.read(p + got, n - got);
        if (r.code() != NetworkResultCode::SUCCESS) return false;
        got += r.bytes();
    }
    return true;
}

// 로컬 → 원격: WriteBegin → WriteChunk 연속(마지막 last=1). 전부 무응답.
void uploadFile(Client& c, const char* localPath, const char* remoteName) {
    FILE* fp = fopen(localPath, "rb");
    Header bh{ Op::WriteBegin, 1, (uint32_t)std::strlen(remoteName) + 1, 0 };
    c.write(&bh, sizeof(bh)); c.write(remoteName, bh.len);
    char buf[64 * 1024];
    for (;;) {
        size_t n = fread(buf, 1, sizeof(buf), fp);
        int last = feof(fp) ? 1 : 0;
        Header ch{ Op::WriteChunk, 2, (uint32_t)n, last };
        c.write(&ch, sizeof(ch)); c.write(buf, n);     // 서버는 응답하지 않음
        if (last) break;
    }
    fclose(fp);
}

// 원격 → 로컬: OpenRead(응답=총크기) → ReadBegin → raw 청크 연속 수신.
void downloadFile(Client& c, const char* remoteName, const char* localPath) {
    Header oh{ Op::OpenRead, 1, (uint32_t)std::strlen(remoteName) + 1, 0 };
    c.write(&oh, sizeof(oh)); c.write(remoteName, oh.len);
    Header meta; readFully(c, &meta, sizeof(meta));
    uint64_t total = meta.len;
    Header rb{ Op::ReadBegin, 2, 0, 0 };
    c.write(&rb, sizeof(rb));
    FILE* fp = fopen(localPath, "wb");
    char buf[64 * 1024];
    for (uint64_t got = 0; got < total; ) {
        size_t want = (size_t)std::min<uint64_t>(sizeof(buf), total - got);
        readFully(c, buf, want); fwrite(buf, 1, want, fp); got += want;
    }
    fclose(fp);
}
```

---

## 6. 구현 순서 (raw 먼저 닫고 WS 얹기)

1. **API** — `StreamProgress` + 4 훅(빈 기본구현), `processWithoutResponse` 삭제.
   → 이 시점에 기존 핸들러 6개(테스트)의 `processWithoutResponse` override 동시 제거해야 컴파일.
2. **상태/배선** — `ConnectionState` 4개 + `groupOf`/`isReadState`/`isWriteState`/`phaseForState`,
   `PhaseHost::state()` 신설(+ `ClientConnectionImpl::state()` override, `FakePhaseHost::state()`).
3. **CustomPhase (WRITE+READ)** — §4 로직 + `_read_last` + `reset()`. → **raw 테스트 green 확인.**
4. **WebSocketPhase** — §4 WS 변형 + **제어프레임 resume state**. → WS 테스트 green.
5. **픽스처/통합** — `FileRequestHandler`(§5) 로 이전, 통합 테스트.

규모(대략): src/implementation 5파일(핵심 CustomPhase/WebSocketPhase 2개) + SecuritySocket.hpp 1 +
test 8파일(핵심 file_protocol fixture / tcp_request_file / websocket_server 3개). 순증 **~500–700 LOC, T-shirt M**. 새 파일/디렉토리 없음 → 증분 빌드 가능.

> **빌드 주의(CLAUDE.md):** `CustomPhase.cpp`/`WebSocketPhase.cpp` 는 `test/msvc` 트리에서
> 절대경로로 컴파일돼 **stale .obj** 가 남는다. 수정 후 테스트 전 해당 `*.cpp.obj` 삭제하고
> 빌드 로그에 `Building CXX object ...` 가 보이는지 확인.

---

## 7. 테스트 계획

- **phase 단위** (`FakePhaseHost`,
  [custom_phase](../../test/securitysockettest_custom_phase.cpp) /
  [websocket_phase](../../test/securitysockettest_websocket_phase.cpp)):
  - WRITE: `WriteChunk` N개 feed → `onWriteStreamData` N회 + 누적 일치, `onWriteStreamBegin` 1회,
    `last` 에서 `COMPLETE` → `WaitingForNext*` 전이, **버퍼가 청크 급 초과 안 함**(고정 메모리).
  - WRITE 종료 직후 **파이프라인 다음 메시지** 정상 파싱(desync 가드).
  - READ: `onReadStreamData` K회 → 총 응답 일치, `COMPLETE` 청크 송신 후 전이(`_read_last` 1틱).
  - **WS**: 프래그먼트된 WS 메시지 1개 = 청크 1개 reassemble, **스트림 중 PING → resume**.
- **통합** ([file_protocol](../../test/securitysockettest_file_protocol.hpp) fixture, raw + WS):
  10MB WRITE/READ 왕복 memcmp 일치, 다중 클라 동시 전송 **상호 굶김 없음**.
- **회귀:** 기존 `TCPRequestEcho`/`TCPRequestFile`/phase 24/통합 85 green 유지.

---

## 8. 미해결 (구현 중 확정할 항목)

- **에러 중도 처리:** 스트림 도중 핸들러 실패(디스크 풀 등). 1차 후보: `StreamProgress::ABORT`
  추가 → phase 가 `Closing` 으로. **확정 필요.**
- **WS 제어프레임 resume state:** §4 WS — `SendingWebSocketResponse::SENT_DONE` 가 Waiting 대신
  스트림 상태로 가도록 resume 1비트. **WS 한정, 이 기능 유일한 까다로운 부분.**
- **뷰 수명:** `onWriteStream*` 가 받는 `req` 포인터는 **콜백 호출 동안만** 유효(다음 청크 recv
  전 drain/compact). 콜백 안에서 소비하거나 복사. (API 주석 + 픽스처가 모범.)
- **모드 중 비-스트림 메시지:** WRITE_STREAM 중 모든 framed 메시지가 `onWriteStreamData` 로 감 →
  핸들러가 header 로 판별/거부(프로토콜 규약 명시).
- **READ 백프레셔:** 클라 수신 느리면 `write` would-block → `Sending*Stream` 유지 POLLOUT 대기.
  생산이 송신완료 뒤에만 일어나 자연 동기화 — 설계상 OK, 테스트로 확인.
