# Phase 6 진행상황 — 2026-05-31

> **대상:** v3 Phase 6 (Server 흐름 통합 — sniffing + dispatch + WS frame + SLOW)
> **참조:** [mileston_http.md](./mileston_http.md) §Phase 6, [state-machine.html](./state-machine.html), [client_connection.html](./client_connection.html)
> **브랜치:** `v3`

---

## 1. 한 줄 요약

프로토콜 헬퍼(Ws/Sniff/Http) + ClientConnection 분해 골격(SniffPhase/CustomPhase + Host) 까지 구현·커밋됨.
**단, 현재 HEAD 는 raw Custom 경로에서 종료 시 크래시(AV)가 있다 — 원인 진단 완료, 수정은 미적용 (아래 §4).**
HttpPhase / WebSocketPhase 는 아직 미구현.

---

## 2. 완료된 것 (커밋됨, 브랜치 v3)

순서대로 커밋:

| 커밋 | 내용 | 테스트 |
| --- | --- | --- |
| `652c7c5` | `protocol/WsFrameCodec` — RFC 6455 frame decode(in-place unmask)/encode/close/pong | 15 gtest pass |
| `31adb67` | `protocol/ProtocolSniffer` — HTTP/CUSTOM/NEED_MORE 단일포트 판별 | 7 gtest pass |
| `e9ca1d0` | `protocol/HttpProcessor`(→ 이후 HttpParser 로 개명) — picohttpparser 래퍼 + Sec-WebSocket-Accept(SHA1+base64) + 101 직렬화 | 15 gtest pass |
| `997889f` | **D2/D3**: HttpProcessor→`HttpParser`, `ObjectPool`→`core/memory/fixed_pool.hpp`(클래스 `FixedObjectPool`) | 빌드 green |
| `c7755c2` | **D6**: `RequestHandler` self-accessor(`asHttpRequestHandler`/`asCustomProtocolRequestHandler`) + `RequestServer::open(RequestHandler*)` | — |
| `42d71fe` | `connection/ConnectionState`(13 state + 4 event + groupOf/isRead/isWrite) + `ConnectionPhase`/`PhaseHost` 계약 + `SniffPhase` | 빌드 green |
| `d784121` | `connection/CustomPhase` — raw Custom 그룹(header→payload→FAST/SLOW/STREAM dispatch) | — |
| `d33c0a3` | Host `ClientConnectionImpl`(PhaseHost 구현) + `RequestServer` run loop 통합 | ⚠ 크래시 |
| `cd5196a` | Host `.cpp` 실제 본문 보강(d33c0a3 의 .cpp 누락분) | ⚠ 크래시 |
| `e7cf5a4` | docs: `client_connection.html` 설계서 + CLAUDE.md stale-obj 경고 | — |

**Step A(listener cross-thread wakeup)** 는 별도 구현 불필요 — 이미 `SocketEvent_*.cpp` 에 eventfd/loopback pair + `wake()` 로 구현되어 있었음(검증만).

### 합의된 설계 결정 (client_connection.html §8, D1~D6)
- **D1** `ConnectionPhase` 기반 분해 + `SniffPhase/HttpPhase/CustomPhase/WebSocketPhase`
- **D2** 무상태 파서 `HttpProcessor`→`HttpParser` (상태 보유 `HttpPhase` 와 구분)
- **D3** `ObjectPool`→`core/memory/fixed_pool.hpp` / `FixedObjectPool` (index 기반 `core/memory/pool.hpp` 의 `ObjectPool` 과 한 TU 공존)
- **D4** `Closing` 은 Host 가 직접 처리(별도 Phase 없음)
- **D5** recv 는 Host 가 일괄 — Phase 는 buffer 만 봄(socket 비의존)
- **D6** `open(RequestHandler*)` + RTTI-free virtual self-accessor (virtual base + RTTI-off 라 cast 불가)

### 분해 구조 (구현된 골격)
```
ClientConnectionImpl (Host = PhaseHost)
  - 소유: socket, 단일 _input_buffer/_output_buffer, lazy SLOW worker(single slot), listener 조작
  - phaseForState(state) 로 현재 ConnectionPhase 선택/교체
  - driveRead / onWriteEvent / armListener / dispatchSlow
  - phases: SniffPhase, CustomPhase   ← HttpPhase/WebSocketPhase 자리 비어있음
RequestServerImpl
  - open(): self-accessor 로 http/custom 판별, 라우터 빌드, run loop 시작
  - run(): accept → onAccept/onConnected → handleEvent 위임 → CLOSE 시 teardown
```

---

## 3. 미완료 (다음 작업)

1. **(최우선) §4 의 크래시 수정** — 이게 막혀서 raw Custom 회귀가 안 돈다.
2. **HttpPhase** — parse/route 매칭/keep-alive/Upgrade 검출 + handshake. host `phaseForState` 의 HTTP 그룹 분기 연결.
3. **WebSocketPhase** — frame decode/재조립/PING·PONG·CLOSE/Text거부 + Custom dispatch(binary wrap). HTTP→WS 그룹 전환.
4. **서버 회귀 테스트** — `SECURITYSOCKET_TEST_USE_CURL=ON` 으로 libcurl 기반 http_server / websocket_server 테스트.

---

## 4. ⚠ 미해결 크래시 — 진단 완료, 수정 미적용

### 증상
- `TCPRequestEcho.runFourClient`(raw Custom, FAST) 가 **100% 재현**으로 종료 시 AV (SEH `0xc0000005`, rc=139).
- 흐름 자체는 정상: 모든 echo 응답이 올바르게 오가고, 4 클라이언트 정상 disconnect, run loop 정상 종료, `close()` 정상 완료까지 trace 로 확인. 그 **직후** test body 에서 AV.

### 원인 (강한 가설 — 타이밍/구조로 특정)
`d33c0a3` 에서 `RequestServerImpl` 에 **`HttpRouterImpl _router;` 를 값 멤버로 인라인 추가**한 것이 원인으로 보인다.
- `RequestServer` 는 `RequestServerImpl` 을 고정 크기 `char _container[2048]` 에 **placement-new** 한다(pImpl).
- `HttpRouterImpl` 은 `SegmentTrie _tries[7]` + `std::vector<Route> _actions[7]` + `std::deque<std::string> _patterns` 등으로 덩치가 커서, 인라인 멤버로 넣으면 `sizeof(RequestServerImpl)` 이 **2048 을 초과 → 컨테이너 오버플로우 → 인접 메모리 손상 → 종료 시 AV**.
- Phase 5(custom-only, `_router` 없음)에서는 정상이었고, `_router` 인라인 추가 시점부터 크래시 → 정황 일치.
- (정확한 `sizeof` 측정은 probe 컴파일이 막혀 아직 숫자로 확정 못 함. 다음 세션에서 `static_assert(sizeof(RequestServerImpl) <= 2048)` 로 즉시 확인 가능.)

### 제안 수정 (이번에 "왜 갑자기 unique_ptr?" 의 맥락)
컨테이너 오버플로우를 피하려고 `_router` 를 **`std::unique_ptr<HttpRouterImpl>`** 로 바꿔 힙에 두고 open() 에서 `reset(new ...)` 하려 했음.
→ 작긴 하지만 합의 없이 들어간 변경이라 **되돌렸고**, working tree 는 HEAD 와 일치(수정 미적용).

**다음 세션 선택지 (택1, 사용자 결정 필요):**
1. `_router` 를 `unique_ptr<HttpRouterImpl>` 로 (힙). RequestServerImpl 풋프린트에서 라우터 제외. — 가장 국소적.
2. `RequestServer::IMPLEMENTATION_SIZE` 를 2048 → 충분히 키움. — pImpl 관례 유지, 인라인 멤버 그대로.
3. 라우터를 RequestServerImpl 밖(별도 소유)으로.

먼저 `static_assert` 로 실제 초과 여부/초과량부터 확정할 것.

---

## 5. 프로세스 메모 (이번에 깨진 것 / 교훈)

- **병렬 편집 race**: 같은 파일에 Edit/Write 를 한 배치에서 여러 개 + 빌드까지 섞어 돌렸더니, linter 재읽기와 충돌해 일부 Write 가 "modified since read" 로 드롭 → **빌드 안 되는 상태로 커밋**되는 사고(d33c0a3/cd5196a). 이후 순차 적용으로 수습. → 소스 파일은 **한 번에 하나씩, 순차로** 편집.
- **test/msvc stale object 함정**: `test/msvc` 트리는 `src/` 를 절대경로로 컴파일하는데 외부경로 변경을 Ninja 가 자주 못 잡아 **relink 만 하고 옛 .obj 재사용** → 러너가 옛 코드 실행. "67/67 pass" 로 보였던 게 실은 stale 바이너리였음(여러 번 오판). 대응: 바뀐 TU 의 `.obj` 강제 삭제 후 빌드, `Building CXX object ...` 로그 확인, obj/src mtime 비교. (CLAUDE.md 에 경고 추가함.)
- **무단 변경 자제**: 합의 안 된 리네임/리팩터(예: ObjectPool 무단 개명, 통합 Outcome enum, unique_ptr)는 사용자에게 먼저 확인.

---

## 6. 현재 상태 한눈에

- 빌드: 두 라이브러리 **컴파일/링크는 green** (HEAD 기준).
- 단위 테스트: 프로토콜 헬퍼(Ws/Sniff/HttpParser) + HTTP view/router/url 등 순수 단위 테스트 **pass**.
- 통합(raw Custom 서버) 회귀: **§4 크래시로 실패** — HEAD 는 Phase 5 대비 이 경로가 regress 상태.
- HttpPhase/WebSocketPhase/libcurl 서버 테스트: **미착수**.
