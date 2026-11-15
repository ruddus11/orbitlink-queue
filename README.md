# OrbitLink Queue - Message Queue & Priority

OS PBL Task 1, *OrbitLink – Spacecraft Communication System*. This repository is the **message-queue part only**. Messages are stored in Emergency / System / Normal queues and always dequeued in that order. Queue length and wait time are measured. It runs standalone, without other teammates' code.

**Build:** clone FreeRTOS-Kernel at commit `ca75944` (see below), then `make` and `./orbitlink_queue selftest` (about 1 second) or `./orbitlink_queue all` (about 2 minutes). Tested on Apple Silicon macOS with Apple clang and GNU Make. On Linux/WSL use the same POSIX port; Windows needs WSL.

---

# OrbitLink Queue - Message Queue & Priority

OS PBL Task 1 "OrbitLink – Spacecraft Communication System"의 **메시지 큐 파트**입니다.
메시지를 Emergency / System / Normal로 나눠 담고, 항상 Emergency → System → Normal 순서로 꺼내며,
큐 길이와 대기 시간을 측정합니다. 다른 팀원의 코드 없이 단독으로 실행됩니다.

- 확인한 환경: Apple Silicon(arm64), macOS, Apple clang, GNU Make
- 커널: FreeRTOS-Kernel 커밋 `ca75944` (POSIX 포트, 태스크 1개 = pthread 1개)
- Linux와 WSL에서는 아직 실행해 보지 않았습니다. 아래 안내는 같은 POSIX 포트를 쓴다는 전제로 적은 것입니다.

## 폴더 구조

```
OrbitLink_Queue/
├── README.md
├── Makefile
├── FreeRTOSConfig.h          커널 설정 (우선순위 7단계)
├── .gitignore
├── include/
│   ├── orbitlink_msg.h       팀 공용 메시지 구조체와 enum  ← 팀원 모두 사용
│   └── msg_queue.h           큐 모듈 공개 API              ← 팀원 모두 사용
├── src/
│   ├── msg_queue.c           큐 모듈 구현                  ← 팀 프로젝트에 그대로 복사
│   └── main_demo.c           단독 실행 데모 (가짜 Sender/Receiver/Retry/Monitor)
├── 결과/
│   └── 시나리오_결과.txt     실제 실행 출력 전체
├── docs/
│   ├── 큐파트_설명자료.docx / .md   발표 준비 자료 (한/영)
│   ├── diagram1~4 *.png             다이어그램
│   ├── tools/                       다이어그램, docx 생성 스크립트
│   └── 설계/                        11/10 제안용 전체 구조도와 남은 개발 목록
└── .vscode/                  빌드, 실행, 디버그 설정
```

`build/`와 `orbitlink_queue`는 `make`가 만드는 파일이라 공유하지 않습니다.

## 빌드와 실행

```bash
git clone --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git
# then check out commit ca75944 (see '커널 받기' below)
make
./orbitlink_queue selftest   # ~1 second
./orbitlink_queue all        # all scenarios + compare, ~2 minutes
```

| 명령 | 내용 | 걸리는 시간(실측, 약) |
|---|---|---|
| `./orbitlink_queue selftest` | 큐 모듈 자가 검사 (꺼내는 순서를 눈으로 확인) | 1초 |
| `./orbitlink_queue 1` | 시나리오 1: 평상시 | 8초 |
| `./orbitlink_queue 2` | 시나리오 2: Normal 폭주, 큐 가득 → 실패 콜백 → Retry | 8초 |
| `./orbitlink_queue 3` | 시나리오 3: 폭주 중 Emergency 끼어들기 | 8초 |
| `./orbitlink_queue compare` | 비교 실험: FIFO / FIFO+태스크 우선순위 / 우선순위 큐 | 90초 |
| `./orbitlink_queue all` | 위 전부 | 2분 |

| make 명령 | 내용 |
|---|---|
| `make` | 빌드 |
| `make run` | 빌드 후 `all` 실행 |
| `make s1` `make s2` `make s3` | 시나리오 하나 실행 |
| `make compare` `make selftest` | 비교 실험, 자가 검사 실행 |
| `make result` | 전체 실행 결과를 `결과/시나리오_결과.txt`에 저장 (덮어씀) |
| `make clean` | `build/`와 실행 파일 삭제 (소스는 그대로) |

프로그램은 정합성 검사가 하나라도 틀리면 종료 코드 1로 끝납니다.

1 tick은 1 ms로 설정했지만 POSIX 시뮬레이터에서는 실제 시간과 정확히 같지 않습니다. 시나리오 3은 약 6500 tick에 8.1초가 걸렸고, 실행 중 CPU 사용률은 약 99%였습니다. 그래서 결과는 ms가 아니라 tick으로 읽습니다.

**VS Code**: 폴더를 열고 `Cmd+Shift+B`를 누르면 빌드 후 전체 실행됩니다. 시나리오별 실행은
`Cmd+Shift+P` → `Tasks: Run Task`, 디버그는 `F5`입니다 (C/C++ 확장 필요).

결과를 다시 만든 뒤 그래프를 실측값으로 갱신하려면 `python3 docs/tools/make_diagrams.py`를 실행합니다.
`docs/큐파트_설명자료.md`의 수치는 손으로 옮겨 적은 값이므로, 다시 측정해 값이 달라지면 직접 고치고 docx를 다시 만들어야 합니다.

## 커널 받기

경연님 PC에서는 옆 폴더 `../FreeRTOS_시뮬레이터/FreeRTOS-Kernel`을 자동으로 찾습니다.
다른 PC에서는 이 폴더 안에서 아래를 실행하면 같은 커밋을 받습니다. 커널 폴더는 `.gitignore`에 있어 Git에 올라가지 않습니다.

```bash
git clone --depth 1 https://github.com/FreeRTOS/FreeRTOS-Kernel.git
cd FreeRTOS-Kernel
git fetch --depth 1 origin ca759444e9ca59cfa3a9faa6cb8b49e0772ca520
git checkout ca759444e9ca59cfa3a9faa6cb8b49e0772ca520
cd ..
```

커널이 다른 곳에 있으면 `make KERNEL_DIR=/경로/FreeRTOS-Kernel`로 지정합니다.

| 환경 | 준비 |
|---|---|
| macOS | `xcode-select --install` |
| Linux | `sudo apt install build-essential clang make git` (clang 대신 `make CC=gcc` 가능) |
| Windows | POSIX 포트라서 Windows에서 바로는 빌드되지 않습니다. WSL(Ubuntu)을 설치하고 Linux와 같이 진행하세요 |

## 팀원 연동 가이드

### 공통

1. `include/orbitlink_msg.h`, `include/msg_queue.h`, `src/msg_queue.c`를 팀 프로젝트에 복사합니다. `main_demo.c`는 복사하지 않습니다.
2. `FreeRTOSConfig.h`에 `configUSE_COUNTING_SEMAPHORES 1`이 필요합니다. 비교 실험처럼 `mq_deinit()`을 쓰려면 `heap_4.c`를 씁니다.
3. `FreeRTOSConfig.h`가 선언한 `vAssertCalled()`는 `main_demo.c`에 있으므로 팀 `main`에도 구현이 필요합니다.
4. 메시지 1개는 104바이트이고 큐는 총 32칸이라, 메시지 저장에만 약 3.3KB를 씁니다.

### RTOS & Task 담당: 초기화

```c
int main( void )
{
    if( mq_init( MQ_MODE_PRIORITY ) != MQ_OK )
    {
        return 1;
    }

    mq_set_full_callback( retry_on_full );  /* mq_init 다음에 호출 */
    /* xTaskCreate(...) 로 Sender, Receiver, Logger, Retry 생성 */
    vTaskStartScheduler();
    return 0;
}
```

### Sender 담당

```c
orbit_msg_t msg;
const char * text = "O2_LEVEL_LOW";

mq_msg_init( &msg, ORBIT_MSG_EMERGENCY, text, ( uint16_t ) strlen( text ) );
/* 암호화 담당: 여기서 msg.payload 를 XOR 하고 msg.encrypted = 1 */
mq_send( &msg, pdMS_TO_TICKS( 50 ) );   /* 꽉 차면 50 tick까지 기다림. 0이면 바로 실패 */
```

### Receiver 담당

```c
orbit_msg_t msg;

if( mq_receive( &msg, portMAX_DELAY ) == MQ_OK )
{
    /* msg.encrypted 가 1이면 복호화 후 처리 */
}
```

`payload`는 문자열이 아닙니다. 출력할 때는 `printf( "%.*s", ( int ) msg.payload_len, ( const char * ) msg.payload )`처럼 길이를 지정합니다.

### Retry & Backoff 담당

큐가 꽉 차면 큐 모듈이 Sender 태스크 안에서 콜백을 부릅니다. 콜백은 기다리면 안 되므로 복사만 하고 끝냅니다.
실제 동작 예는 `src/main_demo.c`의 `on_queue_full()`, `retry_task()`입니다.

```c
static QueueHandle_t g_retry_inbox;     /* mq_init 다음에 xQueueCreate( 32, sizeof( orbit_msg_t ) ) */

static void retry_on_full( const orbit_msg_t * msg )
{
    ( void ) xQueueSend( g_retry_inbox, msg, 0 );   /* 대기 시간 0 */
}
```

Retry 태스크는 대기함에서 꺼내 backoff만큼 기다린 뒤 `msg.retry_count++` 하고 다시 `mq_send()`합니다.
`mq_msg_init()`을 다시 부르면 id와 timestamp가 바뀌므로 부르지 마세요. 시나리오 2에서 대기함 16칸이 넘쳐 318건 중 267건이 버려졌으므로 대기함 크기와 정책은 Retry 담당이 정해야 합니다.

### Encryption & Logging 담당

- 암호화: `mq_send()` 전에 `payload`를 XOR 하고 `encrypted = 1`로 표시합니다. 큐 모듈은 payload 내용을 보지 않습니다.
- 로그: 태스크 안에서 `printf`를 직접 쓰면 POSIX 포트에서 교착이나 출력 누락이 생길 수 있습니다. `main_demo.c`의 `vLog()`처럼 크리티컬 섹션으로 감싸서 쓰세요.
- 통계 로그: `mq_get_status()`로 유형별 sent, received, failed, 길이, 대기 tick을 얻습니다.

## Sender/Receiver app and integrated version (added 2026-11-15)

| Folder | What it is |
|---|---|
| `OrbitLink_App/` | Sender x3 (Emergency/System/Normal) + Receiver on top of this queue module. 6 automatic PASS/FAIL checks, including Emergency > System > Normal dequeue order and a higher task priority for the Emergency sender (4 vs 3). |
| `OrbitLink_Integrated/` | `OrbitLink_App` + XOR encryption and Logger task (adapted from Shushant's code to `orbit_msg_t`). 8 automatic checks. Retry is not included yet. |

Both use the same `msg_queue.c` and `orbitlink_msg.h` as this repository. Shared message format for every member: `include/orbitlink_msg.h` (`orbit_msg_t`).

```bash
cd OrbitLink_App          # or OrbitLink_Integrated
# put FreeRTOS-Kernel (commit ca75944) inside this folder, see "Getting the kernel" above
make                      # Linux: make CC=gcc
./orbitlink_app           # or ./orbitlink_integrated
echo $?                   # 0 = all checks passed
```
Each folder has a Korean README with details.
