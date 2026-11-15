# OrbitLink - Cho's Part: Sender / Receiver, Message Queue, Priority
# OrbitLink - 조(Cho) 담당 파트: Sender / Receiver, 메시지 큐, 우선순위

Team Quarter Killer · OS PBL Task 1 · 2026-11-15

---

## 1. What I did / 담당한 작업

Based on the revised deadline document, my part covers the four items below. All are implemented and tested.
수정된 일정 문서 기준으로 제 파트는 아래 네 가지이며, 모두 구현하고 테스트했습니다.

| Assigned task / 담당 항목 | What was built / 구현 내용 |
|---|---|
| Sender and Receiver tasks<br>Sender, Receiver 태스크 | 3 Sender tasks (Emergency, System, Normal) and 1 Receiver task, created with `xTaskCreate`<br>Sender 태스크 3개(Emergency, System, Normal)와 Receiver 태스크 1개를 `xTaskCreate`로 생성 |
| Message queues and communication<br>메시지 큐와 태스크 간 통신 | Messages move between tasks through FreeRTOS queues (`xQueueCreate`), one queue per message type<br>메시지 유형마다 FreeRTOS 큐(`xQueueCreate`)를 하나씩 두고, 이를 통해 태스크끼리 메시지를 주고받음 |
| Task priorities (Emergency higher)<br>태스크 우선순위 (Emergency를 더 높게) | Emergency Sender = 4, System/Normal Senders = 3. Emergency messages are also always taken out of the queue first<br>Emergency Sender는 4, System/Normal Sender는 3. 큐에서도 Emergency 메시지를 항상 먼저 꺼냄 |
| Basic send/receive test<br>기본 송수신 테스트 | The program checks 6 items automatically and prints PASS/FAIL. All PASS on macOS and Linux<br>프로그램이 6개 항목을 자동으로 확인하고 PASS/FAIL을 출력. macOS와 Linux에서 모두 PASS |

---

## 2. How to run / 실행 방법

**1) Get the FreeRTOS kernel (once) / FreeRTOS 커널 받기 (처음 한 번)**

Run this inside this folder. / 이 폴더 안에서 실행하세요.
```bash
git clone https://github.com/FreeRTOS/FreeRTOS-Kernel.git
cd FreeRTOS-Kernel
git fetch --depth 1 origin ca759444e9ca59cfa3a9faa6cb8b49e0772ca520
git checkout ca759444e9ca59cfa3a9faa6cb8b49e0772ca520
cd ..
```

**2) Build and run / 빌드와 실행**
```bash
make              # macOS. On Linux / WSL use: make CC=gcc
./orbitlink_app
echo $?           # 0 = all checks passed / 0이면 전부 통과
```
Windows needs WSL (Ubuntu). / Windows에서는 WSL(Ubuntu)이 필요합니다.

---

## 3. What you will see / 실행 결과

The full output of a real run is in `run_output.txt`. / 실제 실행 결과 전체는 `run_output.txt`에 있습니다.

```
[    41] SENDER   TX   EMERGENCY #7   seq=1        <- Emergency is sent AFTER some Normal messages
...
[   301] RECEIVER RX   EMERGENCY #7   seq=1 ...    <- but it is received FIRST
...
[   540] ---------------- CHECKS ----------------
[   540]   All 12 messages received                         PASS
[   540]   Per type: sent = received = planned, 0 failed    PASS
[   540]   Order kept within each type                      PASS
[   540]   Payload matches its type                         PASS
[   540]   Dequeue order Emergency -> System -> Normal      PASS
[   540]   Emergency Sender has the highest task priority   PASS
[   540] RESULT: PASS
```

- Emergency messages are sent later than some Normal messages, but the Receiver takes them out first.
  Emergency 메시지는 일부 Normal 메시지보다 늦게 보내지지만, Receiver는 Emergency를 먼저 꺼냅니다.
- The number in `[ ]` is the FreeRTOS tick (1 tick = 1 ms).
  `[ ]` 안의 숫자는 FreeRTOS tick이며, 1 tick은 1 ms입니다.

---

## 4. How priority works / 우선순위 동작 방식

Emergency is favoured in two ways. / Emergency는 두 가지 방식으로 우대합니다.

| Mechanism / 방식 | What it decides / 결정하는 것 |
|---|---|
| **Task priority**: Emergency Sender 4, others 3<br>**태스크 우선순위**: Emergency Sender 4, 나머지 3 | Which Sender gets the CPU first<br>어떤 Sender가 먼저 CPU를 받는지 |
| **Dequeue order**: the Emergency queue is always checked first<br>**꺼내는 순서**: 항상 Emergency 큐부터 확인 | Which message the Receiver handles first<br>Receiver가 어떤 메시지를 먼저 처리하는지 |

Task priority alone cannot move an Emergency message ahead of Normal messages that are already waiting
in a queue, so both are used.
태스크 우선순위만으로는 이미 큐에서 기다리는 Normal 메시지 앞으로 Emergency를 옮길 수 없어서, 두 방식을 함께 씁니다.

---

## 5. Files / 파일 구성

| File / 파일 | Description / 설명 |
|---|---|
| `src/app_main.c` | Sender, Receiver and the automatic checks<br>Sender, Receiver, 자동 판정 |
| `src/msg_queue.c` | Message queue module (3 priority queues, statistics)<br>메시지 큐 모듈 (우선순위 큐 3개, 통계) |
| `include/orbitlink_msg.h` | **Shared message format `orbit_msg_t` for the whole team**<br>**팀 공용 메시지 형식 `orbit_msg_t`** |
| `include/msg_queue.h` | Queue API (`mq_send`, `mq_receive`, ...)<br>큐 API |
| `FreeRTOSConfig.h` | FreeRTOS kernel settings<br>FreeRTOS 커널 설정 |
| `Makefile` | Build script<br>빌드 스크립트 |
| `run_output.txt` | Output of a real run<br>실제 실행 결과 |

---

## 6. For the next step / 다음 단계 담당자에게

Please use `orbit_msg_t` in `include/orbitlink_msg.h` as the message format, so everyone's code fits together.
모두의 코드가 맞물리도록 메시지 형식은 `include/orbitlink_msg.h`의 `orbit_msg_t`를 사용해 주세요.

| Feature / 기능 | Where to add it / 추가할 위치 |
|---|---|
| XOR encryption / XOR 암호화 | `sender_task()`, right before `mq_send()`. Set `msg.encrypted = 1`<br>`sender_task()`의 `mq_send()` 바로 앞. `msg.encrypted = 1`로 설정 |
| XOR decryption / XOR 복호화 | `receiver_task()`, right after `mq_receive()`<br>`receiver_task()`의 `mq_receive()` 바로 뒤 |
| Retry and backoff / 재전송과 backoff | `main()`, register `mq_set_full_callback()` after `mq_init()`. It is called when a queue is full<br>`main()`에서 `mq_init()` 다음에 `mq_set_full_callback()` 등록. 큐가 가득 차면 호출됨 |
| Logger / 로그 | Replace `vLog()` with calls to a Logger task<br>`vLog()`를 Logger 태스크 호출로 교체 |

Notes / 주의
- XOR output can contain `0x00`, so never treat the payload as a C string. Print it with a length, e.g. `%.*s`.
  XOR 결과에는 `0x00`이 섞일 수 있으니 payload를 문자열로 다루지 말고, `%.*s`처럼 길이를 지정해서 출력하세요.
- Do not call `printf` or `usleep` directly inside a task (can deadlock in the POSIX simulator). Use `vLog()` and `vTaskDelay()`.
  태스크 안에서 `printf`나 `usleep`을 직접 쓰지 마세요(POSIX 시뮬레이터에서 교착 가능). `vLog()`와 `vTaskDelay()`를 쓰세요.
