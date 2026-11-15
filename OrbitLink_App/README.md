# OrbitLink_App - 기본 송수신 앱 (경연 파트)

Sender 3종(Emergency, System, Normal)과 Receiver가 `OrbitLink_Queue`의 큐 모듈 위에서 메시지를
주고받고, 우선 처리가 지켜지는지 자동으로 판정합니다. 큐 모듈(`src/msg_queue.c`, `include/*.h`)은
`OrbitLink_Queue`와 같은 파일이며 수정하지 않았습니다.

## 실행
```bash
cd OrbitLink_App
make          # 맥은 clang, 리눅스는 make CC=gcc
./orbitlink_app
```
커널은 이 폴더에 `FreeRTOS-Kernel/`이 있으면 그것을, 없으면 `../FreeRTOS_시뮬레이터/FreeRTOS-Kernel`을 씁니다.
판정이 모두 통과하면 종료 코드 0, 하나라도 실패하면 1입니다.

## 동작
1. Sender 3개가 12개 메시지(E 3, S 3, N 6)를 도착 순서를 섞어서 보냅니다.
2. Receiver는 300 tick에 시작해서 모두 큐에 쌓인 뒤 꺼냅니다.
3. Checker가 6가지를 판정합니다. 전부 수신, 유형별 통계, 같은 유형 안의 순서, payload 무결, Emergency → System → Normal, Emergency Sender의 태스크 우선순위가 가장 높음(4, 나머지 3).

실제 출력 예는 `실행결과.txt`를 보세요.

## 이어받는 사람이 붙일 자리 (src/app_main.c 주석 참고)
| 기능 | 위치 |
|---|---|
| XOR 암호화 | `sender_task()`의 `mq_send()` 직전, 암호화하면 `msg.encrypted = 1` |
| XOR 복호화 | `receiver_task()`의 `mq_receive()` 직후 |
| Retry | `main()`에서 `mq_init()` 다음에 `mq_set_full_callback()` 등록 (예시는 `OrbitLink_Queue/src/main_demo.c`의 `on_queue_full`, `retry_task`) |
| Logger | `vLog()`를 로그 태스크 호출로 교체 |

주의. XOR 결과에는 0x00이 섞일 수 있어서 payload를 문자열로 다루면 안 됩니다(출력할 때 `%.*s`처럼 길이 지정).
태스크 안에서 `printf`나 `usleep`을 직접 쓰지 마세요.

## 알려진 한계
- 지금은 큐가 가득 차지 않는 작은 시나리오입니다. 큐가 가득 찼을 때의 동작은 `OrbitLink_Queue`의 시나리오 2, 3에서 확인합니다.
- 우선 처리 판정은 "Receiver를 늦게 시작해 전부 쌓인 뒤 꺼내는" 방식이라, 실시간으로 섞여 들어올 때의 지연은 측정하지 않습니다.
- 맥(Apple Silicon, clang)과 리눅스(gcc)에서 빌드와 실행을 확인했습니다 (2026-11-15).
