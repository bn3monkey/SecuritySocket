# candidate/ — 선행 작성(staging) 소스

이 디렉토리는 **빌드에 포함되지 않는** 선행 작성 코드다. 무거운 작업을 승인 없이
미리 만들어 두고, 검증 단계에서 사람이 **수동으로 목적지에 복사한 뒤** 하나씩
컴파일·테스트한다. (요청: "내가 수동으로 복사한 다음에 하나하나 컴파일하고 테스트")

> **주의:** 여기 파일들은 컴파일/테스트되지 않은 상태다. include 경로는 *복사 목적지*
> 기준으로 작성되어 있어 candidate 폴더 안에서는 컴파일되지 않는다(의도된 동작).

마일스톤: [docs/workplan/mileston_http.md](../../../docs/workplan/mileston_http.md)

---

## Phase6/ — `HttpPhase`, `WebSocketPhase` (ConnectionPhase 전략)

복사 목적지: `src/implementation/connection/`

`SniffPhase`/`CustomPhase`와 동일한 `ConnectionPhase` 전략 패턴. 호스트
(`ClientConnectionImpl`)는 recv/send·단일 버퍼·SLOW 워커를 소유하고, phase는
파싱·디스패치·다음 state 결정만 담당한다.

- `HttpPhase` — `ReceivingHttpRequest → SendingHttpResponse → WaitingForNextHttpRequest`
  (keep-alive). `Upgrade: websocket`가 `wsPattern()`과 일치하면
  `SendingHandshakeResponse`로 WebSocket 그룹에 진입.
- `WebSocketPhase` — `SendingHandshakeResponse / ReceivingWebSocketFrame /
  SendingWebSocketResponse / WaitingForNextWebSocketMessage`. BINARY 프레임 =
  Custom 메시지(header+payload) → raw TCP와 동일한 `CustomProtocolRequestHandler`
  경로. TEXT 거부(1003), PING→PONG, CLOSE echo, 단편화 재조립.

### 통합 시 `ClientConnection.{hpp,cpp}` 수정 사항
1. 멤버 추가: `HttpPhase _http_phase;`, `WebSocketPhase _websocket_phase;`
2. `phaseForState()` 매핑:
   - `ReceivingHttpRequest / SendingHttpResponse / WaitingForNextHttpRequest` → `&_http_phase`
   - `SendingHandshakeResponse / ReceivingWebSocketFrame / SendingWebSocketResponse /
     WaitingForNextWebSocketMessage` → `&_websocket_phase`
3. **버퍼 모델은 `StagingBuffer` 기반 `PhaseHost`** (`input()`/`output()`이
   `StagingBuffer&` 반환, `core/memory/buffer.hpp`). phase는 `head()`에서
   `pending()`만큼 파싱하고 `drain(n)`으로 소비, 응답은 `clear()`→`data()`에
   기록→`fill(n)`. recv 시 버퍼 reserve/compact는 호스트가 담당.
   - HttpPhase는 헤더 블록을 in-place NUL 토큰화한다. `head()`가 가변 `void*`라
     `const_cast`로 충분(별도 `inputMutable()` 접근자 불필요). parse와 dispatch
     사이에 input을 `reserve()`하지 않으므로 파싱 포인터 rebase가 필요 없다.
   - WebSocketPhase의 `WsFrameCodec::decode`도 `head()`(가변 void*)에 in-place
     unmask하므로 const_cast 불필요.
4. **`WsFrameCodec`는 서버 전용**: `decode`는 마스킹된 프레임만 받고 `encode`는
   비마스킹. 서버 측에서는 그대로 맞다(client→server 마스킹, server→client 비마스킹).
5. (선택) 그룹 전환 시 `phase->reset()` 호출로 phase-local 상태 초기화.

### 알려진 TODO
- `HttpPhase`의 `kMaxRequestBody`(1 MB)는 하드코딩. 통합 시
  `NetworkConfiguration::max_http_request_body_size`를 `PhaseHost`로 전달.

---

## Phase7/ — `HttpClient`, `HttpClientRequest`, `HttpClientResponse`

복사 목적지: `src/implementation/http/`

- 공개 클래스 3종은 container 패턴(`mileston_http.md` §2-7). `// PUBLIC` 섹션은
  통합 시 `src/SecuritySocket.hpp`로 이동, `// INTERNAL` *Impl은 `http/`에 유지.
- `HttpClient : public Client` — 별도 *Impl 없이 상속한 `Client`의 소켓
  lifecycle(open/connect/read/write) 위에 직렬화/파싱만 얹음.
- `HttpClient`는 `HttpClientRequest`/`HttpClientResponse`의 **friend**로 선언되어
  컨테이너 안의 Impl에 접근(요청 직렬화/응답 채움). friend 선언 유지할 것.
- Host 헤더 권위(authority)는 생성 시 `NetworkConfiguration`에서 캡처.

---

## Phase8/ — `RequestClient` (raw + WS tunneling)

복사 목적지: `src/implementation/custom/`

- `RequestClient : public Client` — 생성자의 `WebSocketConfiguration` 유무로
  raw TCP / WS 터널링 결정(서버 `CustomProtocolRequestHandler`와 대칭).
- 공개 클래스는 통합 시 `src/SecuritySocket.hpp`로 이동.
- **클라이언트 프레이밍은 자체 구현**: `WsFrameCodec`는 서버 방향이라 재사용 불가
  (client→server는 **마스킹 필수**, server→client는 **비마스킹**). 이 파일은
  마스킹 인코더 + 비마스킹 디코더를 자체 보유. 단, `Sec-WebSocket-Accept` 검증은
  `HttpParser::computeAccept`를 재사용.
- 마스킹 키는 `std::random_device`로 생성.

### 검증 핵심 (마일스톤 §3 Phase 8 완료 기준)
- **동일 테스트 코드가 raw/WS 양 모드에서 통과**해야 함.
