/*
 * FreeRTOSConfig.h - macOS(Apple Silicon) POSIX 시뮬레이터용 커널 설정 (OrbitLink 큐 파트)
 *
 * 공식 예제 FreeRTOS/Demo/Posix_GCC 의 설정을 참고해서, 이 프로젝트에 필요한
 * 항목만 남긴 최소 구성이다. 각 값의 의미는 아래 문서 참고.
 * https://www.freertos.org/a00110.html
 */
#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/* ---------- 스케줄러 ---------- */
#define configUSE_PREEMPTION                       1   /* 우선순위 선점형 스케줄링 사용 */
#define configUSE_TIME_SLICING                     1   /* 같은 우선순위 태스크끼리 tick마다 교대 */
#define configUSE_PORT_OPTIMISED_TASK_SELECTION    0   /* POSIX 포트에는 최적화 구현이 없으므로 0 */
#define configMAX_PRIORITIES                       7   /* 사용 가능한 우선순위: 0(Idle) ~ 6. 숫자가 클수록 높다 */
#define configIDLE_SHOULD_YIELD                    1

/*
 * tick 주기. 1 tick = 1ms 로 두면 tick 값을 곧 밀리초로 읽을 수 있다.
 * POSIX 포트의 tick은 별도 pthread가 usleep()으로 만들어 내므로 실제 시간과
 * 정확히 같지는 않다(실측 오차는 README.md 참고).
 */
#define configTICK_RATE_HZ                         1000
#define configTICK_TYPE_WIDTH_IN_BITS              TICK_TYPE_WIDTH_64_BITS  /* 64비트 환경이므로 64비트 tick */

/* ---------- 메모리 ---------- */
#define configSUPPORT_DYNAMIC_ALLOCATION           1   /* xTaskCreate, xQueueCreate 사용 */
#define configSUPPORT_STATIC_ALLOCATION            0   /* 정적 할당 API는 쓰지 않음 */
#define configTOTAL_HEAP_SIZE                      ( ( size_t ) ( 256 * 1024 ) ) /* heap_4.c 가 쓰는 힙 크기 */

/*
 * 태스크 스택 크기(단위: word = 8바이트).
 * POSIX 포트에서 태스크 코드는 pthread 자체 스택 위에서 실행되고, 여기서 잡는
 * FreeRTOS 스택에는 포트 내부 자료(Thread_t)만 들어간다. 그래서 작아도 되지만,
 * 반대로 실제 보드와 달리 스택 오버플로우는 이 값으로 관찰할 수 없다.
 */
#define configMINIMAL_STACK_SIZE                   256
#define configMAX_TASK_NAME_LEN                    16

/* ---------- 사용하는 커널 기능 ---------- */
#define configUSE_MUTEXES                          1
#define configUSE_COUNTING_SEMAPHORES              1   /* Receiver를 깨우는 카운팅 세마포어 */
#define configUSE_TASK_NOTIFICATIONS               1
#define configQUEUE_REGISTRY_SIZE                  0

/* ---------- 사용하지 않는 기능 ---------- */
#define configUSE_IDLE_HOOK                        0
#define configUSE_TICK_HOOK                        0
#define configUSE_MALLOC_FAILED_HOOK               0   /* 실패는 main()에서 반환값으로 확인 */
#define configUSE_TIMERS                           0
#define configUSE_CO_ROUTINES                      0
#define configUSE_TRACE_FACILITY                   0
#define configGENERATE_RUN_TIME_STATS              0
#define configCHECK_FOR_STACK_OVERFLOW             0   /* 위 스택 설명 참고: POSIX 포트에서는 의미 없음 */

/* ---------- 포함할 API ---------- */
#define INCLUDE_vTaskDelay                         1
#define INCLUDE_xTaskDelayUntil                    1
#define INCLUDE_vTaskDelete                        1   /* 시나리오가 끝난 태스크를 정리할 때 사용 */
#define INCLUDE_vTaskSuspend                       1   /* portMAX_DELAY 로 무한 대기하려면 필요 */
#define INCLUDE_xTaskGetSchedulerState             1
#define INCLUDE_uxTaskPriorityGet                  1

/* ---------- 개발 중 오류 검출 ---------- */
void vAssertCalled( const char * pcFile, unsigned long ulLine ); /* src/main_demo.c 에 구현 */
#define configASSERT( x )    if( ( x ) == 0 ) vAssertCalled( __FILE__, __LINE__ )

#endif /* FREERTOS_CONFIG_H */
