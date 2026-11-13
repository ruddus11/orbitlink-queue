/*
 * main_demo.c - 큐 모듈 단독 실행 데모
 *
 * 다른 팀원의 코드 없이 큐 파트만 실행해서 동작을 증명한다. 여기의 Sender, Receiver,
 * Retry는 전부 "가짜"다. 실제 코드가 생기면 이 파일만 빼고 msg_queue.c 를 그대로 쓴다.
 *
 * [실행]  ./orbitlink_queue 1|2|3|compare|selftest|all
 *
 * [구조]
 *
 *   Sender(EMG) ─┐                                       ┌─> Receiver (처리 20 tick)
 *   Sender(SYS) ─┼─> mq_send() ──> [ msg_queue 모듈 ] ──> mq_receive()
 *   Sender(NOR) ─┘                        │    ▲
 *                             꽉 참 → 콜백 │    │ 재전송
 *                                         ▼    │
 *                                     Retry(가짜)      Monitor : 주기적으로 mq_get_status()
 *
 *   Director : 시나리오마다 위 태스크를 만들고, 끝나면 정리하고, 표를 출력한다.
 *
 * [왜 Director가 있는가]
 * POSIX 포트에서는 스케줄러를 끝낸 뒤 다시 시작할 수 없다. 그래서 스케줄러는 한 번만
 * 시작하고, 가장 높은 우선순위의 Director 태스크가 시나리오를 하나씩 진행한다.
 *
 * [Receiver가 왜 일부러 느린가]
 * 메시지 1개에 20 tick이 걸리므로 처리량은 초당 약 50개다. Sender가 이보다 빨리 보내면
 * 큐에 적체가 생기고, 그때 비로소 "어떤 순서로 꺼내느냐"의 차이가 드러난다.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "msg_queue.h"

/* ---------- 실험 파라미터: 실험할 때는 여기 값을 바꾸면 된다 ---------- */

#define PROCESS_TICKS           20U     /* Receiver가 메시지 1개를 처리하는 시간 */
#define MONITOR_PERIOD_TICKS    500U    /* Monitor 출력 주기 */
#define CONGESTION_PERCENT      80U     /* 큐가 이 비율 이상 차면 적체로 표시 */
#define EMERGENCY_TIMEOUT       50U     /* Emergency는 큐가 꽉 차면 이만큼 기다려서라도 넣는다 */
#define COMPARE_ROUNDS          5       /* 비교 실험을 방식마다 몇 번 반복할지 */
#define COMPARE_TICKS           4000U   /* 비교 실험 1회 길이 */

/* 가짜 Retry 설정 */
#define RETRY_INBOX_LENGTH      16U     /* 재전송을 기다릴 수 있는 메시지 수 */
#define RETRY_MAX_COUNT         3U      /* 재전송 최대 횟수 */
#define RETRY_BACKOFF_TICKS     50U     /* 첫 재전송 대기. 재전송할 때마다 2배로 늘어난다 */

/* 실패 로그는 폭주하면 수백 줄이 되므로 앞부분과 일정 간격으로만 출력한다. */
#define FAIL_LOG_FIRST          3U
#define FAIL_LOG_EVERY          100U

/*
 * 태스크 우선순위 (숫자가 클수록 높음)
 *
 * Director : 시나리오 진행자. 평소에는 잠들어 있다.
 * Monitor  : 다른 태스크가 밀려도 제때 관찰해야 하므로 Sender보다 높다.
 * Sender   : 메시지가 발생하는 쪽.
 * Receiver : 남는 시간에 큐를 비운다.
 *
 * 메시지 처리 순서는 이 숫자들이 아니라 msg_queue.c 의 꺼내는 순서가 결정한다.
 * PRIO_SENDER_BOOST 는 비교 실험에서 "Emergency Sender 태스크 우선순위만 높이면
 * 해결될까?"를 시험할 때만 쓴다.
 */
#define PRIO_RECEIVER           ( tskIDLE_PRIORITY + 2 )
#define PRIO_RETRY              ( tskIDLE_PRIORITY + 2 )
#define PRIO_SENDER             ( tskIDLE_PRIORITY + 3 )
#define PRIO_SENDER_BOOST       ( tskIDLE_PRIORITY + 4 )
#define PRIO_MONITOR            ( tskIDLE_PRIORITY + 5 )
#define PRIO_DIRECTOR           ( tskIDLE_PRIORITY + 6 )

/* ---------- 시나리오 정의 ---------- */

/* 로그를 얼마나 자세히 찍을지 */
typedef enum
{
    LOG_QUIET = 0,      /* 표만 출력 (비교 실험: 로그가 측정에 영향을 주지 않도록) */
    LOG_KEY,            /* Emergency와 일부 실패, Monitor만 출력 (폭주 시나리오) */
    LOG_ALL             /* 모든 송수신 출력 (평상시 시나리오) */
} log_level_t;

/* Sender 태스크 1개의 설정. 같은 함수를 이 설정만 바꿔서 여러 번 생성한다. */
typedef struct
{
    uint32_t min_gap;           /* 평상시 전송 간격의 최솟값(tick). 0이면 이 Sender는 만들지 않는다 */
    uint32_t max_gap;           /* 평상시 전송 간격의 최댓값(tick) */
    uint32_t flood_gap;         /* 폭주 구간의 전송 간격(tick). 0이면 폭주 없음 */
    uint32_t flood_until;       /* 시작 후 이 tick까지가 폭주 구간 */
    uint32_t start_delay;       /* 시작 후 첫 전송까지 기다릴 tick */
    TickType_t send_timeout;    /* mq_send의 timeout */
} sender_plan_t;

/* 시나리오 1개의 설정 */
typedef struct
{
    const char * tag;           /* 결과 파일의 CSV 줄에 쓰는 이름 */
    const char * title;
    uint32_t duration;          /* Sender가 동작하는 시간(tick) */
    log_level_t log_level;
    bool use_retry;             /* 가짜 Retry 태스크를 쓸지 */
    sender_plan_t plan[ ORBIT_MSG_TYPE_COUNT ];
} scenario_t;

/* 시나리오 1: 평상시. 초당 약 15개 < 처리량 50개라서 적체가 없어야 한다. */
static const scenario_t s_scenario1 =
{
    "S1", "시나리오 1: 평상시 (부하 낮음)", 5000U, LOG_ALL, false,
    {
        [ ORBIT_MSG_EMERGENCY ] = { 700U, 900U, 0U, 0U, 300U, EMERGENCY_TIMEOUT },
        [ ORBIT_MSG_SYSTEM ]    = { 200U, 300U, 0U, 0U, 100U, 0U },
        [ ORBIT_MSG_NORMAL ]    = { 80U,  120U, 0U, 0U, 50U,  0U },
    }
};

/* 시나리오 2: 처음 2초 동안 Normal이 초당 200개로 폭주. 이후에는 잦아들어 큐가 회복된다. */
static const scenario_t s_scenario2 =
{
    "S2", "시나리오 2: 일반 메시지 폭주 (Normal 큐 가득 -> 실패 콜백 -> Retry)", 5000U, LOG_KEY, true,
    {
        [ ORBIT_MSG_EMERGENCY ] = { 900U, 1100U, 0U, 0U,    600U, EMERGENCY_TIMEOUT },
        [ ORBIT_MSG_SYSTEM ]    = { 250U, 350U,  0U, 0U,    100U, 0U },
        [ ORBIT_MSG_NORMAL ]    = { 80U,  120U,  5U, 2000U, 50U,  0U },
    }
};

/* 시나리오 3: Normal 폭주가 계속되는 중에 Emergency가 끼어든다. */
static const scenario_t s_scenario3 =
{
    "S3", "시나리오 3: 폭주 중 긴급 메시지 끼어들기", 6000U, LOG_KEY, false,
    {
        [ ORBIT_MSG_EMERGENCY ] = { 300U, 500U, 0U, 0U,     500U, EMERGENCY_TIMEOUT },
        [ ORBIT_MSG_SYSTEM ]    = { 80U,  120U, 0U, 0U,     100U, 0U },
        [ ORBIT_MSG_NORMAL ]    = { 5U,   5U,   0U, 0U,     0U,   0U },
    }
};

/* 비교 실험: 시나리오 3과 같은 부하를 조용히 반복한다. 방식만 바꾸고 부하는 같다. */
static const scenario_t s_compare =
{
    "CMP", "비교 실험", COMPARE_TICKS, LOG_QUIET, false,
    {
        [ ORBIT_MSG_EMERGENCY ] = { 300U, 500U, 0U, 0U, 500U, EMERGENCY_TIMEOUT },
        [ ORBIT_MSG_SYSTEM ]    = { 80U,  120U, 0U, 0U, 100U, 0U },
        [ ORBIT_MSG_NORMAL ]    = { 5U,   5U,   0U, 0U, 0U,   0U },
    }
};

/* 비교 실험에서 시험하는 방식 3가지 */
typedef struct
{
    const char * tag;
    const char * label;
    mq_mode_t mode;
    bool boost_emergency_sender;    /* Emergency Sender 태스크의 우선순위만 높일지 */
} compare_case_t;

static const compare_case_t s_compare_cases[] =
{
    { "FIFO",       "A: single FIFO",                 MQ_MODE_SINGLE_FIFO, false },
    { "FIFO_BOOST", "B: FIFO + EMG sender task prio", MQ_MODE_SINGLE_FIFO, true  },
    { "PRIORITY",   "C: priority queues",             MQ_MODE_PRIORITY,    false },
};

#define COMPARE_CASE_COUNT    ( ( int ) ( sizeof( s_compare_cases ) / sizeof( s_compare_cases[ 0 ] ) ) )

/* 가짜 내용. 기존 데모의 메시지를 재사용한다. */
static const char * const s_payloads[ ORBIT_MSG_TYPE_COUNT ][ 3 ] =
{
    [ ORBIT_MSG_EMERGENCY ] = { "HULL_PRESSURE_DROP", "O2_LEVEL_LOW", "ENGINE_OVERHEAT" },
    [ ORBIT_MSG_SYSTEM ]    = { "HEARTBEAT", "POWER_STATUS", "ATTITUDE_REPORT" },
    [ ORBIT_MSG_NORMAL ]    = { "TELEMETRY_BLOCK", "CREW_MESSAGE", "CAMERA_FRAME" },
};

/* ---------- 태스크들이 공유하는 값 ---------- */

/* 태스크에 넘기는 Sender 인자 */
typedef struct
{
    orbit_msg_type_t type;
    const sender_plan_t * plan;
    unsigned int seed;          /* 태스크별 난수 상태(rand_r 용) */
} sender_arg_t;

static sender_arg_t s_sender_args[ ORBIT_MSG_TYPE_COUNT ];
static log_level_t s_log_level;

/* Director가 태스크에게 멈추라고 알리는 표시. 한 태스크만 쓰고 나머지는 읽기만 한다. */
static volatile bool s_stop_senders;
static volatile bool s_stop_receiver;
static volatile bool s_stop_monitor;

static SemaphoreHandle_t s_task_done;       /* 태스크가 끝날 때마다 Give. Director가 기다린다 */
static QueueHandle_t s_retry_inbox;         /* 실패한 메시지를 Retry 태스크로 넘기는 큐 */

/* 측정값. 크리티컬 섹션 안에서 읽고 쓴다. */
static uint32_t s_callback_count;           /* 실패 콜백이 불린 횟수 */
static uint32_t s_retry_recovered;          /* 재전송으로 큐에 다시 들어간 횟수 */
static uint32_t s_retry_gave_up;            /* 재전송 한도를 넘겨 포기한 메시지 수 */
static uint32_t s_retry_dropped;            /* Retry 대기함이 꽉 차서 버려진 메시지 수 */
static uint32_t s_order_errors;             /* 같은 유형 안에서 순서가 뒤바뀐 횟수. 0이어야 한다 */
static UBaseType_t s_emergency_task_prio;   /* Emergency Sender가 실제로 받은 태스크 우선순위 */

static volatile int s_exit_code = EXIT_SUCCESS;

/* ---------- 로그 ---------- */

/*
 * 태스크에서 출력할 때는 반드시 아래 두 함수만 쓴다.
 *
 * POSIX 포트에서는 태스크가 곧 pthread이고 tick은 시그널로 전달된다. 그래서
 *  - printf가 내부 잠금을 잡은 채로 태스크가 전환되면 교착 상태가 될 수 있고
 *  - write()가 시그널에 끊겨서 출력이 사라질 수도 있다.
 * 크리티컬 섹션은 시그널을 막아 태스크 전환이 일어나지 않게 하므로 두 문제를
 * 모두 피할 수 있다. 로그 줄이 서로 섞이지도 않는다.
 */
static void vPrint( const char * format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );
static void vLog( const char * format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );

/* 표처럼 시각 없이 그대로 출력할 때 쓴다. */
static void vPrint( const char * format, ... )
{
    va_list args;

    va_start( args, format );

    taskENTER_CRITICAL();
    {
        vprintf( format, args );
        fflush( stdout );   /* 파이프나 파일로 받을 때도 바로 나오도록 */
    }
    taskEXIT_CRITICAL();

    va_end( args );
}

/* "[tick] 내용" 한 줄 로그 */
static void vLog( const char * format, ... )
{
    va_list args;

    va_start( args, format );

    taskENTER_CRITICAL();
    {
        printf( "[%6lu] ", ( unsigned long ) xTaskGetTickCount() );
        vprintf( format, args );
        putchar( '\n' );
        fflush( stdout );
    }
    taskEXIT_CRITICAL();

    va_end( args );
}

/* ---------- 실패 콜백 (가짜 Retry 담당의 입구) ---------- */

/*
 * msg_queue 모듈이 "큐가 꽉 찼다"고 알려 주는 연결점이다. 실제 Retry 담당도 이
 * 모양으로 구현하면 된다. mq_send를 부른 태스크 안에서 실행되므로 기다리면 안 된다.
 * 그래서 대기 시간 0으로 Retry 대기함에 복사만 해 두고 바로 끝낸다.
 */
static void on_queue_full( const orbit_msg_t * msg )
{
    uint32_t count;

    taskENTER_CRITICAL();
    count = ++s_callback_count;
    taskEXIT_CRITICAL();

    if( ( s_log_level != LOG_QUIET ) && ( ( count <= FAIL_LOG_FIRST ) || ( ( count % FAIL_LOG_EVERY ) == 0U ) ) )
    {
        vLog( "CALLBACK FULL %-9s #%-4lu retry=%u (%lu번째 실패)", orbit_msg_type_name( msg->type ),
              ( unsigned long ) msg->id, ( unsigned int ) msg->retry_count, ( unsigned long ) count );
    }

    if( s_retry_inbox == NULL )
    {
        return;
    }

    if( msg->retry_count >= RETRY_MAX_COUNT )
    {
        taskENTER_CRITICAL();
        s_retry_gave_up++;
        taskEXIT_CRITICAL();
    }
    else if( xQueueSend( s_retry_inbox, msg, 0 ) != pdPASS )
    {
        taskENTER_CRITICAL();
        s_retry_dropped++;
        taskEXIT_CRITICAL();
    }
}

/* ---------- 태스크 ---------- */

/*
 * 반드시 성공해야 하는 호출을 검사한다. configASSERT( mq_init(...) == MQ_OK ) 처럼 쓰지
 * 않는 이유: assert를 끄는 설정이면 괄호 안의 호출 자체가 사라지기 때문이다.
 */
static void must_ok( mq_result_t result )
{
    configASSERT( result == MQ_OK );
}

/* 태스크가 끝날 때 Director에게 알리고 자신을 삭제한다. 태스크 함수는 return 하면 안 된다. */
static void finish_task( void )
{
    xSemaphoreGive( s_task_done );
    vTaskDelete( NULL );
}

/* 계획대로 메시지를 만들어 mq_send로 보내는 가짜 Sender */
static void sender_task( void * param )
{
    sender_arg_t * arg = ( sender_arg_t * ) param;
    const sender_plan_t * plan = arg->plan;
    TickType_t start = xTaskGetTickCount();
    uint32_t range = plan->max_gap - plan->min_gap + 1U;

    /* 비교 실험 B에서 우선순위 상향이 정말 적용됐는지 결과에 남기기 위해 기록한다. */
    if( arg->type == ORBIT_MSG_EMERGENCY )
    {
        s_emergency_task_prio = uxTaskPriorityGet( NULL );
    }

    vTaskDelay( plan->start_delay );

    while( !s_stop_senders )
    {
        orbit_msg_t msg;
        const char * text = s_payloads[ arg->type ][ ( uint32_t ) rand_r( &arg->seed ) % 3U ];
        uint32_t random_gap = plan->min_gap + ( ( uint32_t ) rand_r( &arg->seed ) % range );
        bool flooding = ( plan->flood_gap != 0U ) && ( ( xTaskGetTickCount() - start ) < plan->flood_until );

        vTaskDelay( flooding ? plan->flood_gap : random_gap );

        if( s_stop_senders )
        {
            break;
        }

        must_ok( mq_msg_init( &msg, arg->type, text, ( uint16_t ) strlen( text ) ) );

        if( mq_send( &msg, plan->send_timeout ) == MQ_OK )
        {
            if( ( s_log_level == LOG_ALL ) || ( ( s_log_level == LOG_KEY ) && ( arg->type == ORBIT_MSG_EMERGENCY ) ) )
            {
                vLog( "SENDER   TX   %-9s #%-4lu \"%s\"", orbit_msg_type_name( msg.type ), ( unsigned long ) msg.id, text );
            }
        }
    }

    finish_task();
}

/* mq_receive로 꺼내 처리하는 가짜 Receiver */
static void receiver_task( void * param )
{
    uint32_t last_seq[ ORBIT_MSG_TYPE_COUNT ] = { 0 };

    ( void ) param;

    /* 큐가 비어 있고 멈추라는 표시가 있을 때만 끝낸다. 남은 메시지를 다 측정하기 위해서다. */
    for( ; ; )
    {
        orbit_msg_t msg;

        if( mq_receive( &msg, pdMS_TO_TICKS( 20 ) ) != MQ_OK )
        {
            if( s_stop_receiver )
            {
                break;
            }

            continue;
        }

        /* 재전송 메시지는 원래 늦게 도착하므로 처음 보낸 메시지끼리만 순서를 검사한다. */
        if( msg.retry_count == 0U )
        {
            if( msg.seq <= last_seq[ msg.type ] )
            {
                taskENTER_CRITICAL();
                s_order_errors++;
                taskEXIT_CRITICAL();
            }

            last_seq[ msg.type ] = msg.seq;
        }

        if( ( s_log_level == LOG_ALL ) || ( ( s_log_level == LOG_KEY ) && ( msg.type == ORBIT_MSG_EMERGENCY ) ) )
        {
            /* payload는 문자열이 아니므로 길이를 지정해서 출력한다. */
            vLog( "RECEIVER RX   %-9s #%-4lu \"%.*s\" wait=%lu", orbit_msg_type_name( msg.type ), ( unsigned long ) msg.id,
                  ( int ) msg.payload_len, ( const char * ) msg.payload,
                  ( unsigned long ) ( xTaskGetTickCount() - msg.enqueue_tick ) );
        }

        /*
         * 메시지 처리 시간 흉내. 처리 중에는 끼어들지 못하므로 Emergency도 지금 처리가
         * 끝날 때까지는 기다린다. 그래서 Emergency 대기 시간은 0이 아니라 최대 약 20 tick이다.
         */
        vTaskDelay( PROCESS_TICKS );
    }

    finish_task();
}

/*
 * 가짜 Retry. 실제 재전송 정책은 Retry 담당의 몫이고, 여기서는 콜백 연결이 끝까지
 * 동작하는지만 보여 준다. 재전송 횟수가 늘수록 대기 시간을 2배씩 늘린다(backoff).
 */
static void retry_task( void * param )
{
    ( void ) param;

    for( ; ; )
    {
        orbit_msg_t msg;

        if( xQueueReceive( s_retry_inbox, &msg, pdMS_TO_TICKS( 20 ) ) != pdPASS )
        {
            if( s_stop_senders )
            {
                break;
            }

            continue;
        }

        vTaskDelay( RETRY_BACKOFF_TICKS << msg.retry_count );

        msg.retry_count++;

        /* 다시 실패하면 on_queue_full이 또 불려서 대기함으로 돌아가거나 포기 처리된다. */
        if( mq_send( &msg, 0 ) == MQ_OK )
        {
            taskENTER_CRITICAL();
            s_retry_recovered++;
            taskEXIT_CRITICAL();
        }
    }

    finish_task();
}

/* 큐 길이를 막대로 그린다. 예: [#####.....] */
static void make_bar( char * out, uint32_t length, uint32_t capacity )
{
    enum { BAR_WIDTH = 10 };
    uint32_t filled = ( length * BAR_WIDTH + capacity / 2U ) / capacity;
    uint32_t i;

    for( i = 0; i < BAR_WIDTH; i++ )
    {
        out[ i ] = ( i < filled ) ? '#' : '.';
    }

    out[ BAR_WIDTH ] = '\0';
}

/* 주기적으로 큐 길이를 관찰하는 Monitor */
static void monitor_task( void * param )
{
    TickType_t last_wake = xTaskGetTickCount();

    ( void ) param;

    while( !s_stop_monitor )
    {
        mq_status_t st;
        char bar[ ORBIT_MSG_TYPE_COUNT ][ 12 ];
        const char * mark = "";
        int type;

        /* vTaskDelay와 달리 실행 시간이 누적되지 않아 주기가 일정하다. */
        xTaskDelayUntil( &last_wake, MONITOR_PERIOD_TICKS );

        if( s_log_level == LOG_QUIET )
        {
            continue;
        }

        must_ok( mq_get_status( &st ) );

        for( type = 0; type < ( int ) ORBIT_MSG_TYPE_COUNT; type++ )
        {
            const mq_type_status_t * t = &st.type[ type ];

            make_bar( bar[ type ], t->length, t->capacity );

            if( ( t->length * 100U ) >= ( t->capacity * CONGESTION_PERCENT ) )
            {
                mark = "  << CONGESTED";
            }
        }

        vLog( "MONITOR  E %2lu/%-2lu [%s]  S %2lu/%-2lu [%s]  N %2lu/%-2lu [%s]%s",
              ( unsigned long ) st.type[ 0 ].length, ( unsigned long ) st.type[ 0 ].capacity, bar[ 0 ],
              ( unsigned long ) st.type[ 1 ].length, ( unsigned long ) st.type[ 1 ].capacity, bar[ 1 ],
              ( unsigned long ) st.type[ 2 ].length, ( unsigned long ) st.type[ 2 ].capacity, bar[ 2 ], mark );
    }

    finish_task();
}

/* ---------- 시나리오 실행 (Director 태스크 안에서만 호출) ---------- */

/* 태스크 생성 실패는 설정 오류이므로 바로 종료한다. */
static void create_task( TaskFunction_t code, const char * name, void * param, UBaseType_t priority )
{
    if( xTaskCreate( code, name, configMINIMAL_STACK_SIZE, param, priority, NULL ) != pdPASS )
    {
        vPrint( "태스크 생성 실패: %s (configTOTAL_HEAP_SIZE 확인)\n", name );
        exit( EXIT_FAILURE );
    }
}

/* 시나리오 또는 비교 실험을 1회 실행하고 결과를 status에 담는다. */
static void run_once( const scenario_t * sc, mq_mode_t mode, bool boost_emergency_sender, unsigned int seed, mq_status_t * status )
{
    int type;
    int sender_count = 0;
    int i;

    must_ok( mq_init( mode ) );
    must_ok( mq_set_full_callback( on_queue_full ) );

    s_log_level = sc->log_level;
    s_stop_senders = false;
    s_stop_receiver = false;
    s_stop_monitor = false;
    s_callback_count = 0U;
    s_retry_recovered = 0U;
    s_retry_gave_up = 0U;
    s_retry_dropped = 0U;
    s_order_errors = 0U;
    s_retry_inbox = NULL;

    if( sc->use_retry )
    {
        s_retry_inbox = xQueueCreate( RETRY_INBOX_LENGTH, sizeof( orbit_msg_t ) );
        configASSERT( s_retry_inbox != NULL );
        create_task( retry_task, "RETRY", NULL, PRIO_RETRY );
    }

    create_task( receiver_task, "RECEIVER", NULL, PRIO_RECEIVER );
    create_task( monitor_task, "MONITOR", NULL, PRIO_MONITOR );

    for( type = 0; type < ( int ) ORBIT_MSG_TYPE_COUNT; type++ )
    {
        if( sc->plan[ type ].min_gap != 0U )
        {
            bool boost = boost_emergency_sender && ( type == ORBIT_MSG_EMERGENCY );

            s_sender_args[ type ].type = ( orbit_msg_type_t ) type;
            s_sender_args[ type ].plan = &sc->plan[ type ];
            /* 씨앗을 고정하므로 방식이 달라도 메시지 간격과 내용은 같은 순서로 나온다. */
            s_sender_args[ type ].seed = seed + ( unsigned int ) type;
            create_task( sender_task, "SENDER", &s_sender_args[ type ], boost ? PRIO_SENDER_BOOST : PRIO_SENDER );
            sender_count++;
        }
    }

    vTaskDelay( sc->duration );

    /* 1) Sender와 Retry를 멈춘다. */
    s_stop_senders = true;

    for( i = 0; i < sender_count + ( sc->use_retry ? 1 : 0 ); i++ )
    {
        xSemaphoreTake( s_task_done, portMAX_DELAY );
    }

    /* 2) 남은 메시지까지 Receiver가 모두 꺼내면 멈춘다. */
    s_stop_receiver = true;
    xSemaphoreTake( s_task_done, portMAX_DELAY );

    s_stop_monitor = true;
    xSemaphoreTake( s_task_done, portMAX_DELAY );

    must_ok( mq_get_status( status ) );

    mq_deinit();

    if( s_retry_inbox != NULL )
    {
        vQueueDelete( s_retry_inbox );
        s_retry_inbox = NULL;
    }

    /* 삭제된 태스크의 메모리는 Idle 태스크가 회수하므로 잠깐 양보한다. */
    vTaskDelay( 5U );
}

/* 결과가 앞뒤가 맞는지 검사한다. 어긋나면 종료 코드를 실패로 바꾼다. */
static void check_result( const mq_status_t * st )
{
    uint32_t failed = 0U;
    bool ok = ( s_order_errors == 0U );
    int type;

    for( type = 0; type < ( int ) ORBIT_MSG_TYPE_COUNT; type++ )
    {
        failed += st->type[ type ].failed;

        if( ( st->type[ type ].sent != st->type[ type ].received ) || ( st->type[ type ].length != 0U ) )
        {
            ok = false;
        }
    }

    if( failed != s_callback_count )
    {
        ok = false;
    }

    vPrint( "정합성 검사: sent==received %s, 실패 수==콜백 수(%lu/%lu), 유형 내 순서 오류 %lu건 -> %s\n",
            ok ? "OK" : "불일치", ( unsigned long ) failed, ( unsigned long ) s_callback_count,
            ( unsigned long ) s_order_errors, ok ? "PASS" : "FAIL" );

    if( !ok )
    {
        s_exit_code = EXIT_FAILURE;
    }
}

/* 시나리오 끝에 출력하는 요약 표. CSV 줄은 다이어그램 스크립트가 읽는다. */
static void print_summary( const char * tag, const mq_status_t * st )
{
    int type;

    vPrint( "+-----------+--------+--------+--------+-----------+----------+----------+\n" );
    vPrint( "| TYPE      |   sent |   recv | failed | peak/cap  | avg wait | max wait |\n" );
    vPrint( "+-----------+--------+--------+--------+-----------+----------+----------+\n" );

    for( type = 0; type < ( int ) ORBIT_MSG_TYPE_COUNT; type++ )
    {
        const mq_type_status_t * t = &st->type[ type ];

        vPrint( "| %-9s | %6lu | %6lu | %6lu | %4lu/%-4lu | %8.1f | %8lu |\n", orbit_msg_type_name( ( orbit_msg_type_t ) type ),
                ( unsigned long ) t->sent, ( unsigned long ) t->received, ( unsigned long ) t->failed,
                ( unsigned long ) t->peak_length, ( unsigned long ) t->capacity, mq_avg_wait_ticks( t ),
                ( unsigned long ) t->max_wait_ticks );
    }

    vPrint( "+-----------+--------+--------+--------+-----------+----------+----------+\n" );

    for( type = 0; type < ( int ) ORBIT_MSG_TYPE_COUNT; type++ )
    {
        const mq_type_status_t * t = &st->type[ type ];

        vPrint( "CSV,%s,%s,%lu,%lu,%lu,%lu,%lu,%.1f,%lu\n", tag, orbit_msg_type_name( ( orbit_msg_type_t ) type ),
                ( unsigned long ) t->sent, ( unsigned long ) t->received, ( unsigned long ) t->failed,
                ( unsigned long ) t->peak_length, ( unsigned long ) t->capacity, mq_avg_wait_ticks( t ),
                ( unsigned long ) t->max_wait_ticks );
    }
}

/* 시나리오 1, 2, 3 공통 실행 */
static void run_scenario( const scenario_t * sc )
{
    mq_status_t st;

    vPrint( "\n==================================================================\n" );
    vPrint( "%s\n", sc->title );
    vPrint( "모드: 우선순위 큐 3개, 처리 시간 %u tick, 1 tick = 1 ms\n", PROCESS_TICKS );
    vPrint( "==================================================================\n" );

    run_once( sc, MQ_MODE_PRIORITY, false, 1000U, &st );

    vPrint( "\n[요약] %s (대기 단위: tick)\n", sc->title );
    print_summary( sc->tag, &st );
    check_result( &st );

    if( sc->use_retry )
    {
        vPrint( "실패 콜백 %lu회, 재전송 성공 %lu회, 한도 초과 포기 %lu건, Retry 대기함 넘침 %lu건\n",
                ( unsigned long ) s_callback_count, ( unsigned long ) s_retry_recovered,
                ( unsigned long ) s_retry_gave_up, ( unsigned long ) s_retry_dropped );
        vPrint( "CSV,%s_RETRY,%lu,%lu,%lu,%lu\n", sc->tag, ( unsigned long ) s_callback_count,
                ( unsigned long ) s_retry_recovered, ( unsigned long ) s_retry_gave_up,
                ( unsigned long ) s_retry_dropped );
    }

    if( st.type[ ORBIT_MSG_EMERGENCY ].total_wait_ticks > 0U )
    {
        vPrint( "Normal 평균 대기 / Emergency 평균 대기 = %.1f배\n",
                mq_avg_wait_ticks( &st.type[ ORBIT_MSG_NORMAL ] ) / mq_avg_wait_ticks( &st.type[ ORBIT_MSG_EMERGENCY ] ) );
    }
}

/* 방식별 누적 결과 */
typedef struct
{
    mq_type_status_t total[ ORBIT_MSG_TYPE_COUNT ];
    double min_round_avg;   /* Emergency 회차별 평균 중 최솟값 */
    double max_round_avg;   /* Emergency 회차별 평균 중 최댓값 */
} compare_total_t;

/* 비교 실험: 같은 부하를 방식 3가지로 COMPARE_ROUNDS번씩 실행한다. */
static void run_compare( void )
{
    static compare_total_t totals[ COMPARE_CASE_COUNT ];
    int c;
    int round;

    vPrint( "\n==================================================================\n" );
    vPrint( "비교 실험: 태스크 우선순위 vs 큐 내부 처리 순서\n" );
    vPrint( "부하는 시나리오 3과 같고, 방식마다 %d회 반복, 1회 %u tick (총 큐 용량은 모두 32칸)\n", COMPARE_ROUNDS, COMPARE_TICKS );
    vPrint( "==================================================================\n" );
    vPrint( "방식          회차 E태스크prio E전송 E수신 E실패 E평균대기 E최대대기 N평균대기\n" );

    for( c = 0; c < COMPARE_CASE_COUNT; c++ )
    {
        const compare_case_t * cc = &s_compare_cases[ c ];

        totals[ c ].min_round_avg = 1e9;
        totals[ c ].max_round_avg = 0.0;

        for( round = 0; round < COMPARE_ROUNDS; round++ )
        {
            mq_status_t st;
            const mq_type_status_t * e = &st.type[ ORBIT_MSG_EMERGENCY ];
            const mq_type_status_t * n = &st.type[ ORBIT_MSG_NORMAL ];
            double avg;
            int type;

            /* 같은 회차는 방식이 달라도 씨앗이 같다. */
            run_once( &s_compare, cc->mode, cc->boost_emergency_sender, 2000U + ( unsigned int ) round * 10U, &st );
            check_result( &st );

            avg = mq_avg_wait_ticks( e );

            vPrint( "%-12s %4d %11lu %5lu %5lu %5lu %9.1f %9lu %9.1f\n", cc->tag, round + 1,
                    ( unsigned long ) s_emergency_task_prio, ( unsigned long ) e->sent,
                    ( unsigned long ) e->received, ( unsigned long ) e->failed, avg, ( unsigned long ) e->max_wait_ticks,
                    mq_avg_wait_ticks( n ) );
            vPrint( "CSV,CMP_ROUND,%s,%d,%lu,%lu,%lu,%lu,%.1f,%lu,%.1f\n", cc->tag, round + 1,
                    ( unsigned long ) s_emergency_task_prio, ( unsigned long ) e->sent,
                    ( unsigned long ) e->received, ( unsigned long ) e->failed, avg, ( unsigned long ) e->max_wait_ticks,
                    mq_avg_wait_ticks( n ) );

            for( type = 0; type < ( int ) ORBIT_MSG_TYPE_COUNT; type++ )
            {
                mq_type_status_t * sum = &totals[ c ].total[ type ];

                sum->sent += st.type[ type ].sent;
                sum->received += st.type[ type ].received;
                sum->failed += st.type[ type ].failed;
                sum->total_wait_ticks += st.type[ type ].total_wait_ticks;

                if( st.type[ type ].max_wait_ticks > sum->max_wait_ticks )
                {
                    sum->max_wait_ticks = st.type[ type ].max_wait_ticks;
                }
            }

            if( avg < totals[ c ].min_round_avg )
            {
                totals[ c ].min_round_avg = avg;
            }

            if( avg > totals[ c ].max_round_avg )
            {
                totals[ c ].max_round_avg = avg;
            }
        }
    }

    vPrint( "\n[요약] 비교 실험 %d회 합산 (대기 단위: tick)\n", COMPARE_ROUNDS );
    vPrint( "+--------------------------------+-------+-------+----------+-------------+----------+----------+\n" );
    vPrint( "| Case                           | E ok  | E fail| E avg    | E avg range | E max    | N avg    |\n" );
    vPrint( "+--------------------------------+-------+-------+----------+-------------+----------+----------+\n" );

    for( c = 0; c < COMPARE_CASE_COUNT; c++ )
    {
        const mq_type_status_t * e = &totals[ c ].total[ ORBIT_MSG_EMERGENCY ];
        const mq_type_status_t * n = &totals[ c ].total[ ORBIT_MSG_NORMAL ];

        vPrint( "| %-30s | %5lu | %5lu | %8.1f | %5.1f~%-5.1f | %8lu | %8.1f |\n", s_compare_cases[ c ].label,
                ( unsigned long ) e->received, ( unsigned long ) e->failed, mq_avg_wait_ticks( e ),
                totals[ c ].min_round_avg, totals[ c ].max_round_avg, ( unsigned long ) e->max_wait_ticks,
                mq_avg_wait_ticks( n ) );
        vPrint( "CSV,CMP_TOTAL,%s,%lu,%lu,%.1f,%.1f,%.1f,%lu,%.1f\n", s_compare_cases[ c ].tag,
                ( unsigned long ) e->received, ( unsigned long ) e->failed, mq_avg_wait_ticks( e ),
                totals[ c ].min_round_avg, totals[ c ].max_round_avg, ( unsigned long ) e->max_wait_ticks,
                mq_avg_wait_ticks( n ) );
    }

    vPrint( "+--------------------------------+-------+-------+----------+-------------+----------+----------+\n" );
    vPrint( "FIFO(A) 대비 우선순위 큐(C): Emergency 평균 대기 %.1f배 단축\n",
            mq_avg_wait_ticks( &totals[ 0 ].total[ ORBIT_MSG_EMERGENCY ] ) /
            mq_avg_wait_ticks( &totals[ 2 ].total[ ORBIT_MSG_EMERGENCY ] ) );
}

/* ---------- 자가 검사 ---------- */

static uint32_t s_check_failures;

/* 검사 결과 한 줄 출력 */
static void check( const char * name, bool passed )
{
    vPrint( "  %-52s %s\n", name, passed ? "PASS" : "FAIL" );

    if( !passed )
    {
        s_check_failures++;
        s_exit_code = EXIT_FAILURE;
    }
}

static uint32_t s_selftest_callback_count;

static void selftest_callback( const orbit_msg_t * msg )
{
    ( void ) msg;
    s_selftest_callback_count++;
}

/* 정해진 순서로 넣고 꺼내서 꺼낸 순서를 문자열로 돌려준다. 예: "E1 E2 S1 N1" */
static void push_and_drain( const orbit_msg_type_t * input, int count, char * out, size_t out_size )
{
    static const char letter[ ORBIT_MSG_TYPE_COUNT ] = { 'E', 'S', 'N' };
    orbit_msg_t msg;
    size_t used = 0U;
    int i;

    for( i = 0; i < count; i++ )
    {
        must_ok( mq_msg_init( &msg, input[ i ], "x", 1U ) );
        must_ok( mq_send( &msg, 0 ) );
    }

    out[ 0 ] = '\0';

    while( mq_receive( &msg, 0 ) == MQ_OK )
    {
        used += ( size_t ) snprintf( out + used, out_size - used, "%c%lu ", letter[ msg.type ], ( unsigned long ) msg.seq );
    }

    if( used > 0U )
    {
        out[ used - 1U ] = '\0';    /* 끝의 공백 제거 */
    }
}

/* 태스크 경쟁 없이 큐 모듈의 약속을 하나씩 확인한다. 꺼내는 순서를 눈으로 볼 수 있다. */
static void run_selftest( void )
{
    static const orbit_msg_type_t input[] =
    {
        ORBIT_MSG_NORMAL, ORBIT_MSG_NORMAL, ORBIT_MSG_SYSTEM, ORBIT_MSG_EMERGENCY,
        ORBIT_MSG_NORMAL, ORBIT_MSG_EMERGENCY, ORBIT_MSG_SYSTEM
    };
    enum { INPUT_COUNT = sizeof( input ) / sizeof( input[ 0 ] ) };
    orbit_msg_t msg;
    mq_status_t st;
    char order[ 64 ];
    uint8_t too_long[ ORBIT_PAYLOAD_MAX + 1 ] = { 0 };
    int i;

    vPrint( "\n==================================================================\n" );
    vPrint( "자가 검사: 큐 모듈 단위 동작\n" );
    vPrint( "==================================================================\n" );
    vPrint( "넣은 순서                 : N1 N2 S1 E1 N3 E2 S2\n" );

    must_ok( mq_init( MQ_MODE_PRIORITY ) );
    push_and_drain( input, INPUT_COUNT, order, sizeof( order ) );
    vPrint( "꺼낸 순서 (우선순위 모드) : %s\n", order );
    check( "우선순위 모드: E -> S -> N, 유형 안에서는 먼저 온 순서", strcmp( order, "E1 E2 S1 S2 N1 N2 N3" ) == 0 );

    must_ok( mq_get_status( &st ) );
    check( "통계: 유형별 sent/received 일치", ( st.type[ 0 ].sent == 2U ) && ( st.type[ 0 ].received == 2U ) &&
           ( st.type[ 1 ].sent == 2U ) && ( st.type[ 1 ].received == 2U ) &&
           ( st.type[ 2 ].sent == 3U ) && ( st.type[ 2 ].received == 3U ) );

    check( "빈 큐 mq_receive: MQ_ERR_EMPTY", mq_receive( &msg, 0 ) == MQ_ERR_EMPTY );
    check( "NULL 인자: MQ_ERR_PARAM", ( mq_send( NULL, 0 ) == MQ_ERR_PARAM ) && ( mq_receive( NULL, 0 ) == MQ_ERR_PARAM ) );
    check( "없는 유형: MQ_ERR_PARAM", mq_msg_init( &msg, ORBIT_MSG_TYPE_COUNT, "x", 1U ) == MQ_ERR_PARAM );
    check( "payload 길이 초과: MQ_ERR_PARAM", mq_msg_init( &msg, ORBIT_MSG_NORMAL, too_long, sizeof( too_long ) ) == MQ_ERR_PARAM );

    /* Normal 큐를 16칸 채운 뒤 17번째가 실패하고 콜백이 불리는지 본다. */
    s_selftest_callback_count = 0U;
    must_ok( mq_set_full_callback( selftest_callback ) );

    for( i = 0; i < MQ_LEN_NORMAL; i++ )
    {
        must_ok( mq_msg_init( &msg, ORBIT_MSG_NORMAL, "x", 1U ) );
        must_ok( mq_send( &msg, 0 ) );
    }

    must_ok( mq_msg_init( &msg, ORBIT_MSG_NORMAL, "x", 1U ) );
    check( "가득 찬 큐: MQ_ERR_FULL 반환", mq_send( &msg, 0 ) == MQ_ERR_FULL );
    check( "가득 찬 큐: 실패 콜백 1회 호출", s_selftest_callback_count == 1U );

    /* Normal 큐가 가득 차도 Emergency 큐는 따로라서 영향이 없어야 한다. */
    must_ok( mq_msg_init( &msg, ORBIT_MSG_EMERGENCY, "x", 1U ) );
    check( "Normal이 가득 차도 Emergency 전송 성공", mq_send( &msg, 0 ) == MQ_OK );

    must_ok( mq_get_status( &st ) );
    check( "peak_length: Normal 16/16 기록", ( st.type[ ORBIT_MSG_NORMAL ].peak_length == 16U ) && ( st.type[ ORBIT_MSG_NORMAL ].capacity == 16U ) );
    check( "failed: Normal 1건 기록", st.type[ ORBIT_MSG_NORMAL ].failed == 1U );

    mq_deinit();

    check( "mq_deinit 후 호출: MQ_ERR_NOT_INIT", mq_send( &msg, 0 ) == MQ_ERR_NOT_INIT );

    must_ok( mq_init( MQ_MODE_SINGLE_FIFO ) );
    push_and_drain( input, INPUT_COUNT, order, sizeof( order ) );
    vPrint( "꺼낸 순서 (단일 FIFO 모드): %s\n", order );
    check( "단일 FIFO 모드: 넣은 순서 그대로", strcmp( order, "N1 N2 S1 E1 N3 E2 S2" ) == 0 );
    mq_deinit();

    vPrint( "자가 검사 결과: 실패 %lu건 -> %s\n", ( unsigned long ) s_check_failures, ( s_check_failures == 0U ) ? "PASS" : "FAIL" );
}

/* ---------- 시작 ---------- */

typedef enum
{
    RUN_SELFTEST = 1 << 0,
    RUN_S1       = 1 << 1,
    RUN_S2       = 1 << 2,
    RUN_S3       = 1 << 3,
    RUN_COMPARE  = 1 << 4,
    RUN_ALL      = ( 1 << 5 ) - 1
} run_flags_t;

/* 선택된 시나리오를 차례대로 실행하고 스케줄러를 끝낸다. */
static void director_task( void * param )
{
    int flags = *( const int * ) param;

    vPrint( "OrbitLink Message Queue & Priority demo (tick = 1 ms, sizeof(orbit_msg_t) = %lu bytes)\n",
            ( unsigned long ) sizeof( orbit_msg_t ) );

    if( ( flags & RUN_SELFTEST ) != 0 )
    {
        run_selftest();
    }

    if( ( flags & RUN_S1 ) != 0 )
    {
        run_scenario( &s_scenario1 );
    }

    if( ( flags & RUN_S2 ) != 0 )
    {
        run_scenario( &s_scenario2 );
    }

    if( ( flags & RUN_S3 ) != 0 )
    {
        run_scenario( &s_scenario3 );
    }

    if( ( flags & RUN_COMPARE ) != 0 )
    {
        run_compare();
    }

    /* 스케줄러를 끝내면 main()의 vTaskStartScheduler() 다음 줄로 돌아간다. */
    vTaskEndScheduler();

    /* 태스크 함수는 return 하면 안 된다. 커널이 삭제할 때까지 여기서 멈춰 있는다. */
    for( ; ; )
    {
        vTaskDelay( portMAX_DELAY );
    }
}

/* configASSERT 실패 시 호출된다(FreeRTOSConfig.h). 위치를 출력하고 바로 멈춘다. */
void vAssertCalled( const char * file, unsigned long line )
{
    fprintf( stderr, "ASSERT 실패: %s:%lu\n", file, line );
    abort();
}

static int parse_args( int argc, char ** argv )
{
    static const struct { const char * name; int flags; } options[] =
    {
        { "1",        RUN_S1 },
        { "2",        RUN_S2 },
        { "3",        RUN_S3 },
        { "compare",  RUN_COMPARE },
        { "selftest", RUN_SELFTEST },
        { "all",      RUN_ALL },
    };
    size_t i;

    if( argc == 2 )
    {
        for( i = 0; i < sizeof( options ) / sizeof( options[ 0 ] ); i++ )
        {
            if( strcmp( argv[ 1 ], options[ i ].name ) == 0 )
            {
                return options[ i ].flags;
            }
        }
    }

    return 0;
}

int main( int argc, char ** argv )
{
    static int flags;

    flags = parse_args( argc, argv );

    if( flags == 0 )
    {
        fprintf( stderr, "사용법: %s 1|2|3|compare|selftest|all\n", argv[ 0 ] );
        return EXIT_FAILURE;
    }

    /* 최댓값은 한 시나리오에서 동시에 살아 있는 태스크 수보다 크면 된다. */
    s_task_done = xSemaphoreCreateCounting( 16, 0 );

    if( s_task_done == NULL )
    {
        fprintf( stderr, "세마포어 생성 실패 (configTOTAL_HEAP_SIZE 확인)\n" );
        return EXIT_FAILURE;
    }

    if( xTaskCreate( director_task, "DIRECTOR", configMINIMAL_STACK_SIZE, &flags, PRIO_DIRECTOR, NULL ) != pdPASS )
    {
        fprintf( stderr, "태스크 생성 실패: DIRECTOR\n" );
        return EXIT_FAILURE;
    }

    /* 정상이라면 Director가 vTaskEndScheduler()를 호출할 때 여기서 돌아온다. */
    vTaskStartScheduler();

    return s_exit_code;
}
