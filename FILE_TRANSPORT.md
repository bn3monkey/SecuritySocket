# 대용량 파일 전송 최적화 전략

## 현재 구조와 병목

### 현재 전송 패턴

```
클라이언트:
  [ChunkHeader][payload] [ChunkHeader][payload] [ChunkHeader][payload] ...

서버 (SocketRequestServer):
  poll → read header → poll → read payload → poll → read header → poll → read payload ...
```

### 병목 원인

**1. 불필요한 poll wait**

파일 전송 중에는 chunk N의 payload를 읽고 나서 chunk N+1의 header가
이미 소켓 버퍼에 들어와 있을 가능성이 높다.
그런데도 매번 poll로 복귀해서 기다리는 것이 낭비다.

```
chunk 1:  [poll wait] → read header → [poll wait] → read payload
chunk 2:  [poll wait] → read header → [poll wait] → read payload
           ↑                           ↑
           데이터 이미 있을 수도 있는데 poll에서 기다림
```

**2. per-chunk ACK (이미 제거됨)**

매 청크마다 ACK response를 보내면 RTT만큼 대기가 발생한다.
ACK를 제거했을 때 속도가 크게 향상된 것이 이를 증명한다.

---

## 최적화 방법

### 방법 1: Read Until EAGAIN (권장, 코드 변경 최소)

poll로 돌아가지 않고 **소켓 버퍼가 비었을 때만 poll로 복귀**한다.

```
변경 전:
  poll → read header → poll → read payload → poll → ...
                       ↑                     ↑
                   불필요한 poll           불필요한 poll

변경 후:
  poll → read header → read payload → read header → read payload → ...
                                ↑
                           EAGAIN 발생 시에만 poll로 복귀
```

pseudo logic:

```
loop:
    events = poll()        // 소켓 버퍼가 빌 때만 여기서 대기

    while true:            // 버퍼에 데이터 있는 한 계속 처리
        if state == READING_HEADER:
            ret = recv(header)
            if ret == EAGAIN: break    // 데이터 없음 → poll로 복귀
            state = READING_PAYLOAD

        if state == READING_PAYLOAD:
            ret = recv(payload)
            if ret == EAGAIN: break    // 데이터 없음 → poll로 복귀
            process(payload)
            state = READING_HEADER
            // break 없이 계속 → 다음 header 바로 시도
```

[SocketRequestServer.cpp](src/implementation/SocketRequestServer.cpp)의 상태머신 구조를 크게 바꾸지 않아도 된다.
`recv()` 결과가 `EAGAIN` / `EWOULDBLOCK` 이면 poll로 복귀, 아니면 내부 루프를 계속 도는 방식이다.

---

### 방법 2: 청크 크기 키우기 (즉시 적용 가능)

poll wait 횟수 자체를 줄인다.

| 청크 크기 | 100MB 파일 기준 최대 poll wait 횟수 |
|---|---|
| 4KB | 25,600회 |
| 64KB | 1,600회 |
| 256KB | 400회 |
| 1MB | 100회 |

코드 변경 없이 청크 크기 설정만 바꾸면 된다. 방법 1과 병행하면 효과가 배가된다.

---

### 방법 3: FileStart / FileEnd 프로토콜 (per-chunk header 제거)

현재는 매 청크마다 header를 붙이지만, 파일 전송 시작 시 한 번만 보내고
이후는 raw 데이터 스트림으로 처리한다.

```
현재:
  [ChunkHeader][payload] [ChunkHeader][payload] [ChunkHeader][payload]
       ↑ 매번                  ↑ 매번                  ↑ 매번

변경 후:
  [FileStartHeader: filename, total_size] [raw data .....................] [FileEnd]
        ↑ 1번                              ↑ recv 루프만 돌면 됨            ↑ 1번
```

서버는 `FileStart`를 받으면 `total_size`만큼 raw recv 루프만 돌면 된다.
상태머신 전환이 파일 전체에서 2번(Start / End)으로 줄어든다.

---

### 방법 4: Zero-copy 전송

**파일 디스크립터가 있는 경우 (디스크에서 바로 전송)**

```
일반 send():
  디스크 → [커널 페이지 캐시] → [유저 버퍼] → [커널 소켓 버퍼] → 네트워크
                                    ↑                ↑
                                 copy 1            copy 2

sendfile() / TransmitFile():
  디스크 → [커널 페이지 캐시] → 네트워크
                                (커널이 직접, 유저 공간 경유 없음)
```

```c
// Linux
sendfile(socket_fd, file_fd, &offset, file_size);

// Windows
TransmitFile(socket_handle, file_handle, file_size, 0, NULL, NULL, 0);
```

**버퍼에 이미 올라온 경우**

파일 디스크립터가 없으므로 sendfile은 사용 불가.
유저 버퍼 → 소켓 버퍼 복사 1회가 남는다.

이를 없애려면:

```c
// Linux 4.14+: MSG_ZEROCOPY
setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &one, sizeof(one));
send(fd, buf, size, MSG_ZEROCOPY);
recvmsg(fd, &msg, MSG_ERRQUEUE);  // "버퍼 다 썼으니 이제 써도 됨" 신호

// Linux 6.0+: io_uring SEND_ZC (비동기 + zero-copy)
io_uring_prep_send_zc(sqe, fd, buf, size, 0, 0);
```

단, MSG_ZEROCOPY는 **~10KB 이상**부터 이득이 생긴다.
그 이하 크기에서는 오히려 오버헤드가 더 크다.

**zero-copy가 실제로 의미 있는 조건**

```
복사 1회 비용:  ~1GB/s 메모리 대역폭 기준 1MB ≈ 1µs
네트워크 전송:  1Gbps LAN 기준 1MB ≈ 8ms
```

대부분의 경우 복사보다 네트워크 전송이 훨씬 느리다.
zero-copy가 의미 있는 시점은 10Gbps 이상 고속 네트워크이거나
수백 MB 단위 전송이 초당 수십 번 반복되는 경우다.

---

### 방법 5: 전용 스트리밍 소켓 (채택 불가)

파일 전송만을 위한 별도 연결을 여는 방식이다.

```
제어 채널 (기존 Request/Response 소켓):
  Client → "파일 전송 요청, 포트 9001 열어줘" → Server
  Server → "OK" → Client

데이터 채널 (새 소켓):
  Client → 9001번 포트로 연결
  Client → raw 데이터 스트림 (header 없음, ACK 없음)
  Server → recv 루프만 돌면서 수신 (상태머신 불필요)
```

Request/Response 오버헤드가 파일 전송 경로에서 완전히 사라지는 장점이 있으나
**방화벽 문제로 현실적으로 채택 불가**하다.

#### 방화벽 문제

새 포트를 열면 방화벽에서 해당 포트를 별도로 허용해야 한다.

```
기존:  방화벽에서 포트 8080 허용 → 끝
전용 소켓: 방화벽에서 포트 8080 허용 + 포트 9001 허용
           → 배포 환경마다 방화벽 규칙 추가 필요
           → 기업 환경, 클라우드 보안 그룹, 컨테이너 등에서 별도 설정 필요
```

특히 배포 환경을 통제할 수 없는 경우(고객사 서버, 클라우드 환경 등)
추가 포트 허용을 요청하는 것 자체가 운영 부담이 된다.

#### 결론

기존 소켓 하나로 모든 통신을 처리하는 구조를 유지한다.
방법 1~3의 조합으로 동일한 소켓에서 성능을 끌어올리는 방향을 선택한다.

---

## 권장 적용 순서

| 단계 | 방법 | 코드 변경 범위 | 기대 효과 |
|---|---|---|---|
| **1** | 청크 크기 키우기 | 설정값만 변경 | poll wait 횟수 감소 |
| **2** | Read Until EAGAIN | SocketRequestServer 상태머신 소량 수정 | 불필요한 poll 제거 |
| **3** | FileStart/FileEnd 프로토콜 | 프로토콜 + 상태머신 변경 | per-chunk header 오버헤드 제거 |
| **4** | Zero-copy | 플랫폼별 API 추가 | 메모리 복사 제거 (고속 네트워크 환경에서 의미) |
| ~~5~~ | ~~전용 스트리밍 소켓~~ | — | **방화벽 포트 문제로 채택 불가** |

1, 2단계만 적용해도 ACK 제거 때와 유사한 수준의 체감 향상을 기대할 수 있다.
