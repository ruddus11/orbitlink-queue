// Sender 3종과 Receiver가 큐 모듈 위에서 메시지를 주고받는지 자동 판정하는 기본 송수신 앱
/*
 * app_main.c - OrbitLink 기본 송수신 앱 (경연 파트)
 *
 * [구조]
 *   Sender(NOR) ─┐
 *   Sender(SYS) ─┼─> mq_send() ──> [ msg_queue 모듈 ] ──> mq_receive() ──> Receiver
 *   Sender(EMG) ─┘
 *   Checker : Receiver가 끝나면 결과를 판정하고 스케줄러를 끝낸다.
 *
 * [판정하는 것]
 *   1. 보낸 메시지가 전부 수신되었는가
 *   2. 같은 유형 안에서 순서(seq)가 지켜졌는가
 *   3. payload가 깨지지 않았는가 (유형별 접두어로 확인)
 *   4. 큐에 다 쌓인 뒤 꺼낼 때 Emergency → System → Normal 순서인가
 *
 * [왜 Receiver를 늦게 시작하는가]
 * Receiver 시작 전에 Sender가 모두 보내 두면 큐에는 도착 순서와 상관없이 전부 쌓여 있다.
 * 그 상태에서 꺼낸 순서가 곧 우선순위 처리 결과라서, 시간에 흔들리지 않고 판정할 수 있다.
 *
 * [다음 담당이 붙일 자리]
 *   - 암호화 : sender_task()의 mq_send() 직전 / receiver_task()의 mq_receive() 직후
 *   - Retry  : main()에서 mq_init() 다음에 mq_set_full_callback() 등록
 *   - Logger : vLog()를 로그 태스크 호출로 교체
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "msg_queue.h"

/* ---------- 실험 파라미터 ---------- */

#define PRIO_CHECKER            5U      /* 판정 태스크. 가장 높아야 Receiver 종료를 바로 본다 */
#define PRIO_SENDER             3U
#define PRIO_RECEIVER           2U

#define SEND_GAP_TICKS          10U     /* Sender가 메시지 사이에 쉬는 시간 */
#define RECEIVER_START_TICKS    300U    /* Sender가 모두 보낸 뒤에 Receiver를 시작한다 */
#define PROCESS_TICKS           20U     /* Receiver가 메시지 1개를 처리하는 시간 */
#define RECEIVE_TIMEOUT_TICKS   1000U   /* 이 시간 동안 메시지가 없으면 Receiver는 포기한다 */
#define CHECKER_TIMEOUT_TICKS   5000U   /* Receiver 종료를 기다리는 최대 시간 */
#define EMERGENCY_TIMEOUT       50U     /* Emergency는 큐가 차도 이만큼 기다려서 넣는다 */

#define COUNT_EMERGENCY         3U
#define COUNT_SYSTEM            3U
#define COUNT_NORMAL            6U
#define TOTAL_MESSAGES          ( COUNT_EMERGENCY + COUNT_SYSTEM + COUNT_NORMAL )

typedef struct
{
    orbit_msg_type_t type;
    uint32_t count;             /* 보낼 메시지 수 */
    TickType_t start_delay;     /* 첫 전송 전 대기. 유형마다 달리 해서 도착 순서를 섞는다 */
    const char * prefix;        /* payload 접두어. 수신 쪽에서 유형과 맞는지 확인한다 */
} sender_arg_t;

static sender_arg_t s_sender_args[ ORBIT_MSG_TYPE_COUNT ] =
{
    { ORBIT_MSG_EMERGENCY, COUNT_EMERGENCY, 40U, "EMG:O2_LEVEL_LOW" },
    { ORBIT_MSG_SYSTEM,    COUNT_SYSTEM,    20U, "SYS:HEARTBEAT" },
    { ORBIT_MSG_NORMAL,    COUNT_NORMAL,    0U,  "NOR:TELEMETRY_BLOCK" },
};

/* ---------- Receiver가 기록하고 Checker가 판정하는 값 ---------- */

static orbit_msg_type_t s_rx_order[ TOTAL_MESSAGES ];   /* 꺼낸 유형의 순서 */
static uint32_t s_rx_count;
static uint32_t s_seq_errors;
static uint32_t s_payload_errors;
static SemaphoreHandle_t s_receiver_done;
static volatile int s_exit_code = EXIT_SUCCESS;

/*
 * 태스크에서 출력할 때는 반드시 이 함수만 쓴다. POSIX 포트에서 printf를 그냥 쓰면
 * 교착이 생길 수 있어서 크리티컬 섹션으로 감싼다. 로그 줄이 섞이지도 않는다.
 */
static void vLog( const char * format, ... ) __attribute__( ( format( printf, 1, 2 ) ) );

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

/* 반드시 성공해야 하는 호출을 검사한다. assert 안에 호출을 넣으면 assert를 끌 때 호출도 사라진다. */
static void must_ok( mq_result_t result )
{
    configASSERT( result == MQ_OK );
}

static void sender_task( void * param )
{
    const sender_arg_t * arg = ( const sender_arg_t * ) param;
    uint32_t i;

    vTaskDelay( arg->start_delay );

    for( i = 0; i < arg->count; i++ )
    {
        orbit_msg_t msg;
        TickType_t timeout = ( arg->type == ORBIT_MSG_EMERGENCY ) ? EMERGENCY_TIMEOUT : 0U;

        must_ok( mq_msg_init( &msg, arg->type, arg->prefix, ( uint16_t ) strlen( arg->prefix ) ) );

        /* [암호화 담당] 여기서 msg.payload 를 XOR 하고 msg.encrypted = 1 로 바꾼다. */
        if( mq_send( &msg, timeout ) == MQ_OK )
        {
            vLog( "SENDER   TX   %-9s #%-3lu seq=%lu", orbit_msg_type_name( msg.type ),
                  ( unsigned long ) msg.id, ( unsigned long ) msg.seq );
        }
        else
        {
            vLog( "SENDER   FAIL %-9s #%-3lu (큐가 가득 참)", orbit_msg_type_name( msg.type ), ( unsigned long ) msg.id );
        }

        vTaskDelay( SEND_GAP_TICKS );
    }

    vTaskDelete( NULL );
}

static void receiver_task( void * param )
{
    uint32_t last_seq[ ORBIT_MSG_TYPE_COUNT ] = { 0 };
    orbit_msg_t msg;

    ( void ) param;

    vTaskDelay( RECEIVER_START_TICKS );

    while( s_rx_count < TOTAL_MESSAGES )
    {
        if( mq_receive( &msg, pdMS_TO_TICKS( RECEIVE_TIMEOUT_TICKS ) ) != MQ_OK )
        {
            break;
        }

        /* [암호화 담당] 여기서 msg.encrypted 가 1이면 payload 를 XOR 로 복호화한다. */

        s_rx_order[ s_rx_count ] = msg.type;
        s_rx_count++;

        if( msg.seq <= last_seq[ msg.type ] )
        {
            s_seq_errors++;
        }

        last_seq[ msg.type ] = msg.seq;

        if( strncmp( ( const char * ) msg.payload, s_sender_args[ msg.type ].prefix, 3 ) != 0 )
        {
            s_payload_errors++;
        }

        vLog( "RECEIVER RX   %-9s #%-3lu seq=%lu wait=%lu \"%.*s\"", orbit_msg_type_name( msg.type ),
              ( unsigned long ) msg.id, ( unsigned long ) msg.seq,
              ( unsigned long ) ( xTaskGetTickCount() - msg.enqueue_tick ),
              ( int ) msg.payload_len, ( const char * ) msg.payload );

        vTaskDelay( PROCESS_TICKS );   /* 메시지 처리 시간 흉내 */
    }

    xSemaphoreGive( s_receiver_done );
    vTaskDelete( NULL );
}

/* 한 항목을 판정하고 결과 줄을 출력한다. 실패하면 종료 코드를 1로 바꾼다. */
static void check( const char * name, bool passed )
{
    vLog( "  %-46s %s", name, passed ? "PASS" : "FAIL" );

    if( !passed )
    {
        s_exit_code = EXIT_FAILURE;
    }
}

static void checker_task( void * param )
{
    mq_status_t status;
    bool order_ok = true;
    bool counts_ok = true;
    uint32_t i;
    int type;

    ( void ) param;

    if( xSemaphoreTake( s_receiver_done, pdMS_TO_TICKS( CHECKER_TIMEOUT_TICKS ) ) != pdTRUE )
    {
        vLog( "Receiver가 제때 끝나지 않았습니다." );
        s_exit_code = EXIT_FAILURE;
    }

    must_ok( mq_get_status( &status ) );

    /* Receiver 시작 전에 전부 쌓였으므로, 꺼낸 순서는 유형 번호(E=0, S=1, N=2)가 줄지 않아야 한다. */
    for( i = 1; i < s_rx_count; i++ )
    {
        if( s_rx_order[ i ] < s_rx_order[ i - 1U ] )
        {
            order_ok = false;
        }
    }

    for( type = 0; type < ORBIT_MSG_TYPE_COUNT; type++ )
    {
        if( ( status.type[ type ].sent != s_sender_args[ type ].count ) ||
            ( status.type[ type ].received != s_sender_args[ type ].count ) ||
            ( status.type[ type ].failed != 0U ) )
        {
            counts_ok = false;
        }
    }

    vLog( "---------------- 판정 ----------------" );
    check( "보낸 메시지를 전부 수신 (12개)", s_rx_count == TOTAL_MESSAGES );
    check( "유형별 sent = received = 계획, 실패 0", counts_ok );
    check( "같은 유형 안에서 순서 유지", s_seq_errors == 0U );
    check( "payload가 유형과 일치", s_payload_errors == 0U );
    check( "꺼낸 순서가 Emergency → System → Normal", order_ok );
    vLog( "결과: %s", ( s_exit_code == EXIT_SUCCESS ) ? "PASS" : "FAIL" );

    vTaskEndScheduler();
    vTaskDelete( NULL );
}

/* configASSERT 실패 시 호출된다(FreeRTOSConfig.h). 위치를 출력하고 바로 멈춘다. */
void vAssertCalled( const char * file, unsigned long line )
{
    fprintf( stderr, "ASSERT 실패: %s:%lu\n", file, line );
    abort();
}

int main( void )
{
    int type;

    if( mq_init( MQ_MODE_PRIORITY ) != MQ_OK )
    {
        fprintf( stderr, "mq_init 실패 (configTOTAL_HEAP_SIZE 확인)\n" );
        return EXIT_FAILURE;
    }

    s_receiver_done = xSemaphoreCreateBinary();

    if( s_receiver_done == NULL )
    {
        fprintf( stderr, "세마포어 생성 실패\n" );
        return EXIT_FAILURE;
    }

    vLog( "OrbitLink 기본 송수신 앱 시작 (메시지 %u개, Receiver는 %u tick에 시작)",
          ( unsigned int ) TOTAL_MESSAGES, ( unsigned int ) RECEIVER_START_TICKS );

    for( type = 0; type < ORBIT_MSG_TYPE_COUNT; type++ )
    {
        if( xTaskCreate( sender_task, "SENDER", configMINIMAL_STACK_SIZE, &s_sender_args[ type ], PRIO_SENDER, NULL ) != pdPASS )
        {
            fprintf( stderr, "태스크 생성 실패: SENDER\n" );
            return EXIT_FAILURE;
        }
    }

    if( ( xTaskCreate( receiver_task, "RECEIVER", configMINIMAL_STACK_SIZE, NULL, PRIO_RECEIVER, NULL ) != pdPASS ) ||
        ( xTaskCreate( checker_task, "CHECKER", configMINIMAL_STACK_SIZE, NULL, PRIO_CHECKER, NULL ) != pdPASS ) )
    {
        fprintf( stderr, "태스크 생성 실패 (configTOTAL_HEAP_SIZE 확인)\n" );
        return EXIT_FAILURE;
    }

    /* 정상이라면 Checker가 vTaskEndScheduler()를 호출할 때 여기로 돌아온다. */
    vTaskStartScheduler();

    return s_exit_code;
}
