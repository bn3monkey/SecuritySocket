# 진행상황 — 2026-06-01

> **브랜치:** `v3`
> **이번 세션 범위:** ① 연결 입출력 버퍼를 `StagingBuffer` 로 교체, ② 종료 시
> AV 크래시(이전 진행 문서 §4) 근본 원인 확정 및 수정, ③ 파이프라인 경로
> 데이터 불일치 발견(미해결).

---

## 1. 한 줄 요약

- `ClientConnectionImpl` 의 input/output 버퍼를 `core/memory/buffer.hpp` 의
  **`StagingBuffer`(2-커서: `_received`/`_sent`)** 로 교체. `consumeInput`(memmove
  shift) → `drain()` + 필요 시 `compact()`. **빌드 green.**
- 종료 시 AV 크래시(raw Custom 경로) **근본 원인 확정**: `sizeof(RequestServerImpl)
  = 2240 > IMPLEMENTATION_SIZE(2048)` → pImpl 인라인 컨테이너 오버플로우. **수정 적용**
  (`IMPLEMENTATION_SIZE` 4096 + 컴파일타임 `static_assert`). `TCPRequestEcho` **pass**.
- ~~**미해결:** 크래시가 사라지자 가려져 있던 `TCPRequestFile` 의 파이프라인
  STREAM 경로 데이터 불일치.~~ **→ 해결(아래 §4').** 원인은 둘이 겹친 것:
  (a) `StagingBuffer::compact()` 의 memmove 소스 오프셋 버그(회귀), (b) READ_STREAM
  디스패치가 WRITE_STREAM 과 묶여 무응답이던 계약 오류. 둘 다 수정, `TCPRequestFile`
  **runOneClient/runFourClient 통과**.

---

## 2. StagingBuffer 버퍼 교체

### 동기
기존 `PhaseHost` 는 버퍼를 8개 메서드(`input/inputSize/ensureInputCapacity/
consumeInput/output/outputCapacity/ensureOutputCapacity/setOutputSize`)로 노출했고,
`consumeInput(n)` 은 메시지 소비 후 뒤따르는(파이프라인) 바이트를 **memmove 로 앞으로
shift** 해 항상 offset 0 에서 시작하게 했다. 이를 커서 기반 버퍼로 정리.

### 설계 — `StagingBuffer` (2 커서)
```
_data            버퍼 시작
_received        recv 로 채운 양 (write 커서)   tail() = _data + _received
_sent            소비/전송한 양 (read 커서)     head() = _data + _sent
_capacity        전체 용량
  pending()   = _received - _sent   (파싱/전송 대상 바이트)
  remaining() = _capacity - _received (tail 에 더 받을 수 있는 양)
  empty()     = (_sent == _received)
```
- `fill(n)` recv 후 전진, `drain(n)` 소비/전송 후 전진, `clear()` 0/0 리셋.
- **`compact()`** — 소비된 prefix(`_sent`) 회수: 살아있는 `pending()` 바이트만 앞으로
  memmove(다 비웠으면 복사 0), `_sent=0`. base 포인터 무효화 → recv 직전 등 안전
  지점에서만.
- **`reserve(extra)`** — tail 에 `extra` 더 쓸 공간 보장: ① 충분하면 no-op → ②
  `_sent>0` 이면 `compact()` 로 회수 → ③ 그래도 부족하면 `realloc`(grow 를 쪼갠 것).
- 복사/이동 `= delete`(raw malloc 소유), `clear()` 의 불필요한 zero-fill 제거.

### 적용 (`PhaseHost` 계약 축소: 8 메서드 → 2)
- `ConnectionPhase.hpp` — `virtual StagingBuffer& input()/output()` 둘만 노출.
- `ClientConnectionImpl` — 멤버를 `StagingBuffer _input/_output` 로, `_pdu_size` 초기
  용량. `recvChunk` = `reserve(kRecvChunk)`→`read(tail(), remaining())`→`fill(n)`,
  `if(empty()) clear()` 로 완전 소비 시 offset 0 리셋. `flushOutput` =
  `write(head(), pending())`→`drain(n)`, `empty()` 로 완송 판정.
- `SniffPhase` — `head()/pending()` 로 detect, **drain 안 함**(carry-over 유지).
- `CustomPhase` — `consumeInput→drain`, `ensureInputCapacity→reserve`,
  `setOutputSize→clear()+fill()`. SLOW 람다는 실행 시점에 `head()` 재취득.

### 검증
- 라이브러리/러너 **컴파일·링크 green**.
- `TCPRequestEcho.runFourClient`(엄격 req/resp, keep-alive 다중 메시지) — **40/40
  에코 정확히 왕복, pass**. drain → `empty()→clear()` → reserve 사이클 정상 동작 확인.

---

## 3. 종료 시 AV 크래시 — 근본 원인 확정 및 수정 (이전 §4)

### 확정
이전 문서 §4 의 가설(“`HttpRouterImpl _router` 값 멤버 인라인 추가로
`sizeof(RequestServerImpl)` 이 2048 초과 → `char _container[2048]` placement-new
오버플로우 → 종료 시 손상된 라우터 deque 소멸에서 AV”)을 **측정으로 확정**:

- 런타임 probe: `sizeof(RequestServerImpl) = 2240` (> 2048, 192 바이트 초과).
- VEH 백트레이스: AV 는 `~HttpRouterImpl → ~deque<string> _patterns → free`
  (손상 포인터 `0xFFFF...FFED`) 에서 발생, 호출원은 test body 의 `~RequestServer`.
- `FixedObjectPool<ClientConnectionImpl>{32}` 는 **힙 저장**이라 풋프린트에 거의
  기여 안 함 → 초과분은 config/tls/router/thread 등 다른 인라인 멤버 합.

### 수정 (§4 선택지 2 채택)
- `RequestServer::IMPLEMENTATION_SIZE` **2048 → 4096**(Phase 6 HTTP/WS phase 가
  `ClientConnectionImpl` 에 더해질 여유 포함).
- `RequestServer.cpp` 에 **`static_assert(sizeof(RequestServerImpl) <=
  IMPLEMENTATION_SIZE)`** 추가 → 재발 시 런타임이 아니라 **컴파일 타임**에 잡힘.
- 결과: `TCPRequestEcho` **pass**(이전 100% AV → 해소).

> 참고: `BroadcastServer`/`Client` 도 동일한 `char _container[2048]` pImpl 패턴.
> Broadcast 단위/통합은 통과 중이지만, 같은 가드(static_assert)를 거는 것을 권장.

---

## 4. 미해결 — `TCPRequestFile` 파이프라인 데이터 불일치

크래시 수정 후 `TCPRequestFile` 이 **완주는 하지만** 데이터 단언에서 실패:
- `EXPECT_EQ(response_header.response_no, request_no)` 불일치,
  `EXPECT_TRUE(memcmp(response.data, test_case.data(), 4096) == 0)` false 등
  (file.cpp:327/345/369/373…).

### 정황
- `TCPRequestEcho`(엄격 req/resp, 파이프라인 없음)는 **통과**.
- `TCPRequestFile` 은 **응답 없는 WRITE_FILE(STREAM, `processWithoutResponse`)
  메시지를 응답을 읽지 않고 연속 전송** → 서버 recv 버퍼에 **여러 메시지가 파이프라인으로
  적재** → 이후 CLOSE/READ 응답이 desync/손상.
- 즉 실패는 정확히 **다중 메시지 파이프라인(drain/compact) 경로**에 국한.

### 판정 결과 (2026-06-01 확정) — 원인 2개가 겹침

**(a) `StagingBuffer::compact()` memmove 소스 오프셋 버그 → StagingBuffer 회귀.**
살아있는 바이트는 `head() = _data + _sent` 에서 시작하는데
`memmove(_data, _data + live, live)` 로 **틀린 오프셋(`_data + live`)** 에서 복사 →
WRITE 스트림 도중 `reserve()` 가 `compact()` 를 부르면 부분 메시지가 손상 → 이후
헤더 desync → CLOSE/OPEN 응답 불일치(file.cpp:327/345). vector 기반 `consumeInput`
의 shift 는 올바른 오프셋을 썼으므로 **`compact()` 도입 시 들어온 회귀**.
- 수정: `std::memmove(_data, _data + _sent, live)`.
- 회귀 가드: `test/securitysockettest_staging_buffer.cpp` 3 케이스 추가 — 버그
  버전에서 `corrupted` 실패 재현, 수정본 전부 통과로 확인.

**(b) READ_STREAM 디스패치 계약 오류 → 기존 설계 갭(StagingBuffer 무관).**
모드 의미는 **클라이언트 관점**: `READ_STREAM`=클라가 서버에서 **읽음**(서버가 응답을
보냄), `WRITE_STREAM`=클라가 서버에 **씀**(서버 무응답). 그런데 CustomPhase 가 둘을
`processWithoutResponse`(무응답)로 묶어 READ_STREAM 도 응답을 안 만들어 READ_FILE 이
hang/desync. 수정: **WRITE_STREAM 만 무응답**, **READ_STREAM 은 FAST 와 동일하게
`process()` 로 응답 생성**(전용 스트리밍 형태는 차후). (WRITE_STREAM 의 "종료 시점"
기반 연속 송신은 아직 미구현 — 현 경로는 단발 무응답.)

### 검증
`TCPRequestFile.runOneClient`(1.4s) / `runFourClient`(1.8s) **통과**.
`TCPRequestEcho`, `StagingBuffer.*`, `CustomProtocolHandler.*` 회귀 없음.

---

## 5. 다음 작업

1. ~~**(최우선) §4 회귀/기존버그 판정** 후 해당 경로 수정.~~ **완료(§4' 참조).**
2. `BroadcastServer`/`Client` 에도 `static_assert(sizeof(Impl) <= IMPLEMENTATION_SIZE)`.
3. **HttpPhase / WebSocketPhase** 구현 + host `phaseForState` HTTP/WS 그룹 연결.
4. libcurl 기반 http/websocket 서버 통합 테스트.
5. (차후) `WRITE_STREAM` 의 "종료 시점" 기반 연속 송신 메커니즘 설계/구현 —
   현재는 단발 무응답으로만 동작.

---

## 6. 현재 상태 한눈에

- 빌드: 라이브러리/러너 **green**.
- `TCPRequestEcho`(raw Custom, FAST, keep-alive): **pass** (종료 크래시 해소).
- `TCPRequestFile`(raw Custom, STREAM 파이프라인): **pass** (§4' — compact 회귀 +
  READ_STREAM 디스패치 계약 수정).
- 단위 테스트(프로토콜 헬퍼/HTTP view·router·url, **StagingBuffer compact**): pass.
- HttpPhase/WebSocketPhase/libcurl 서버 테스트: 미착수.
