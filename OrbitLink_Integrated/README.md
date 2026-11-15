# OrbitLink_Integrated - 송수신 + XOR + Logger 통합본

`OrbitLink_App`(경연, Sender/Receiver와 우선순위 큐)에 Shushant 코드의 XOR 암호화와 Logger 태스크를
팀 공용 메시지 형식 `orbit_msg_t`에 맞춰 붙인 버전입니다. Wonseok이 이어받지 못할 때를 대비한 보험이며,
`OrbitLink_App`과 `OrbitLink_Queue`는 수정하지 않았습니다.

## 실행
```bash
cd OrbitLink_Integrated
make
./orbitlink_integrated
echo $?      # 0이면 통과
```

## Shushant 코드에서 바뀐 점
| 항목 | Shushant | 통합본 | 이유 |
|---|---|---|---|
| 메시지 구조 | `Message` (NORMAL=0, EMERGENCY=1) | `orbit_msg_t` (EMERGENCY=0, NORMAL=2) | 큐 모듈과 같은 형식 |
| XOR | `xor_crypt(char*, len, key)` | `orbit_xor_encrypt/decrypt(msg)` | `encrypted` 표시를 함께 관리, 두 번 암호화 방지 |
| 로그 전송 | `portMAX_DELAY`로 대기 | 대기 0, 넘치면 버리고 개수 집계 | 로그가 밀려도 Sender가 멈추지 않게 |
| 로그 출력 | 크리티컬 섹션 없음 | 크리티컬 섹션 안에서 출력 | POSIX 포트 printf 교착 방지 |
| 종료 | `exit(0)` | `vTaskEndScheduler()` | 정상 종료 경로 사용 |
| 큐 | FIFO 1개 | 우선순위 큐 3개 | 경연 큐 모듈 사용 |

## 판정 (8개)
전부 수신, 유형별 통계, 같은 유형 안의 순서, 큐 안에서 암호문 상태, 복호화 결과 일치,
Emergency → System → Normal 순서, Emergency Sender 태스크 우선순위가 가장 높음(4, 나머지 3), 로그 손실 0.

## 남은 것
- Retry와 backoff는 없습니다. `main()`의 `mq_init()` 다음에 `mq_set_full_callback()`으로 붙입니다.
  예시는 `OrbitLink_Queue/src/main_demo.c`의 `on_queue_full`, `retry_task`입니다.
- 큐가 가득 차는 상황은 아직 이 통합본에서 다루지 않습니다.
