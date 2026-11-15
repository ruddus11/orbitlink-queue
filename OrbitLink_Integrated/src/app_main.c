// Sender 3종, Receiver, Logger가 XOR 암호화된 메시지를 우선순위 큐로 주고받는지 자동 판정하는 통합본
/*
 * app_main.c - OrbitLink 통합본 (경연 송수신 + Shushant XOR/Logger)
 *
 *   Sender(NOR/SYS/EMG) ─ XOR 암호화 ─> mq_send() ─> [ 우선순위 큐 ] ─> mq_receive() ─ XOR 복호화 ─> Receiver
 *          모든 태스크 ── log_event() ──> [ 로그 큐 ] ──> Logger (출력은 여기서만)
 *
 * 판정: 전부 수신, 유형별 통계, 같은 유형 안의 순서, 큐 안에서 암호화 상태,
 *       복호화 결과가 원문과 일치, Emergency → System → Normal 순서,
 *       Emergency Sender 태스크 우선순위가 가장 높음, 로그 손실 0.
 * 태스크 우선순위: Emergency Sender 4, System/Normal Sender 3 (큐의 처리 순서와 함께 Emergency를 우대).
 * Retry는 아직 없다. 붙일 자리는 main()의 mq_init() 다음 (mq_set_full_callback).
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

#include "msg_queue.h"
#include "orbitlink_log.h"
#include "orbitlink_xor.h"

#define PRIO_CHECKER            5U
#define PRIO_SENDER_EMERGENCY   4U      /* Emergency Sender는 다른 Sender보다 먼저 CPU를 받는다 */
#define PRIO_SENDER             3U      /* System, Normal Sender */
#define PRIO_RECEIVER           2U
#define PRIO_LOGGER             1U      /* 가장 낮다. 다른 태스크가 쉴 때 몰아서 출력한다 */

#define SEND_GAP_TICKS          10U
#define RECEIVER_START_TICKS    300U    /* Sender가 모두 보낸 뒤에 Receiver를 시작해 우선순위를 결정적으로 판정한다 */
#define PROCESS_TICKS           20U
#define RECEIVE_TIMEOUT_TICKS   1000U
#define CHECKER_TIMEOUT_TICKS   5000U
#define EMERGENCY_TIMEOUT       50U

#define COUNT_EMERGENCY         3U
#define COUNT_SYSTEM            3U
#define COUNT_NORMAL            6U
#define TOTAL_MESSAGES          ( COUNT_EMERGENCY + COUNT_SYSTEM + COUNT_NORMAL )

typedef struct
{
    orbit_msg_type_t type;
    uint32_t count;
    TickType_t start_delay;     /* 유형마다 달리 해서 도착 순서를 섞는다 */
    const char * text;          /* 보낼 원문. 수신 쪽에서 복호화 결과와 비교한다 */
} sender_arg_t;

/* 문구는 Shushant 코드의 샘플 메시지를 썼다. */
static sender_arg_t s_sender_args[ ORBIT_MSG_TYPE_COUNT ] =
{
    { ORBIT_MSG_EMERGENCY, COUNT_EMERGENCY, 40U, "ALERT: oxygen pressure low" },
    { ORBIT_MSG_SYSTEM,    COUNT_SYSTEM,    20U, "SYSTEM: software heartbeat" },
    { ORBIT_MSG_NORMAL,    COUNT_NORMAL,    0U,  "Telemetry: battery 87%" },
};

static orbit_msg_type_t s_rx_order[ TOTAL_MESSAGES ];
static uint32_t s_rx_count;
static uint32_t s_seq_errors;
static uint32_t s_not_encrypted;    /* 큐에서 꺼냈는데 평문이었던 메시지 수 */
static uint32_t s_decrypt_errors;   /* 복호화 결과가 원문과 다른 메시지 수 */
static UBaseType_t s_sender_prio[ ORBIT_MSG_TYPE_COUNT ];   /* 각 Sender가 실제로 받은 태스크 우선순위 */
static SemaphoreHandle_t s_receiver_done;
static volatile int s_exit_code = EXIT_SUCCESS;

static void must_ok( mq_result_t result )
{
    configASSERT( result == MQ_OK );
}

static void sender_task( void * param )
{
    const sender_arg_t * arg = ( const sender_arg_t * ) param;
    uint16_t len = ( uint16_t ) strlen( arg->text );
    uint32_t i;

    s_sender_prio[ arg->type ] = uxTaskPriorityGet( NULL );   /* 설정이 실제로 적용됐는지 판정에서 확인 */

    vTaskDelay( arg->start_delay );

    for( i = 0; i < arg->count; i++ )
    {
        orbit_msg_t msg;
        TickType_t timeout = ( arg->type == ORBIT_MSG_EMERGENCY ) ? EMERGENCY_TIMEOUT : 0U;

        must_ok( mq_msg_init( &msg, arg->type, arg->text, len ) );
        log_event( "CREATED", &msg, "\"%s\"", arg->text );

        orbit_xor_encrypt( &msg );   /* 큐와 Retry 대기함에 평문이 남지 않도록 넣기 전에 암호화 */

        if( mq_send( &msg, timeout ) == MQ_OK )
        {
            log_event( "SENT", &msg, "seq=%lu encrypted=%u", ( unsigned long ) msg.seq, ( unsigned int ) msg.encrypted );
        }
        else
        {
            log_event( "FAIL", &msg, "queue full" );
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
        const char * text;
        uint16_t len;

        if( mq_receive( &msg, pdMS_TO_TICKS( RECEIVE_TIMEOUT_TICKS ) ) != MQ_OK )
        {
            break;
        }

        text = s_sender_args[ msg.type ].text;
        len = ( uint16_t ) strlen( text );

        /* 큐 안에서는 암호문이어야 한다. 표시와 실제 내용을 둘 다 본다. */
        if( ( msg.encrypted == 0U ) || ( memcmp( msg.payload, text, len ) == 0 ) )
        {
            s_not_encrypted++;
        }

        log_event( "RECEIVED", &msg, "wait=%lu encrypted=%u", ( unsigned long ) ( xTaskGetTickCount() - msg.enqueue_tick ),
                   ( unsigned int ) msg.encrypted );

        orbit_xor_decrypt( &msg );

        if( ( msg.payload_len != len ) || ( memcmp( msg.payload, text, len ) != 0 ) )
        {
            s_decrypt_errors++;
        }

        /* payload는 문자열이 아니므로 길이를 지정해 출력한다. */
        log_event( "DECRYPTED", &msg, "\"%.*s\"", ( int ) msg.payload_len, ( const char * ) msg.payload );

        s_rx_order[ s_rx_count ] = msg.type;
        s_rx_count++;

        if( msg.seq <= last_seq[ msg.type ] )
        {
            s_seq_errors++;
        }

        last_seq[ msg.type ] = msg.seq;

        vTaskDelay( PROCESS_TICKS );
    }

    xSemaphoreGive( s_receiver_done );
    vTaskDelete( NULL );
}

static void check( const char * name, bool passed )
{
    log_event( "CHECK", NULL, "%s : %s", name, passed ? "PASS" : "FAIL" );

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
        log_event( "CHECK", NULL, "Receiver가 제때 끝나지 않음" );
        s_exit_code = EXIT_FAILURE;
    }

    must_ok( mq_get_status( &status ) );

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

    check( "보낸 메시지를 전부 수신 (12개)", s_rx_count == TOTAL_MESSAGES );
    check( "유형별 sent = received = 계획, 실패 0", counts_ok );
    check( "같은 유형 안에서 순서 유지", s_seq_errors == 0U );
    check( "큐 안에서는 암호문 상태", s_not_encrypted == 0U );
    check( "복호화 결과가 원문과 일치", s_decrypt_errors == 0U );
    check( "꺼낸 순서가 Emergency -> System -> Normal", order_ok );
    check( "Emergency Sender 태스크 우선순위가 가장 높음",
           ( s_sender_prio[ ORBIT_MSG_EMERGENCY ] > s_sender_prio[ ORBIT_MSG_SYSTEM ] ) &&
           ( s_sender_prio[ ORBIT_MSG_EMERGENCY ] > s_sender_prio[ ORBIT_MSG_NORMAL ] ) );
    check( "로그 손실 0", log_dropped() == 0U );   /* 마지막 판정이어야 위 줄들까지 포함해 센다 */
    log_finish( ( s_exit_code == EXIT_SUCCESS ) ? "결과: PASS" : "결과: FAIL" );   /* Logger가 출력 후 스케줄러를 끝낸다 */

    vTaskDelete( NULL );
}

void vAssertCalled( const char * file, unsigned long line )
{
    fprintf( stderr, "ASSERT 실패: %s:%lu\n", file, line );
    abort();
}

int main( void )
{
    int type;

    s_receiver_done = xSemaphoreCreateBinary();

    if( ( mq_init( MQ_MODE_PRIORITY ) != MQ_OK ) || !log_init() || ( s_receiver_done == NULL ) )
    {
        fprintf( stderr, "초기화 실패 (configTOTAL_HEAP_SIZE 확인)\n" );
        return EXIT_FAILURE;
    }

    /* [Retry 담당] 여기서 mq_set_full_callback( 실패 콜백 ) 을 등록한다. */

    log_event( "START", NULL, "OrbitLink 통합본 시작 (메시지 %u개, XOR key=0x%02X)",
               ( unsigned int ) TOTAL_MESSAGES, ( unsigned int ) ORBIT_XOR_KEY );

    for( type = 0; type < ORBIT_MSG_TYPE_COUNT; type++ )
    {
        UBaseType_t prio = ( type == ORBIT_MSG_EMERGENCY ) ? PRIO_SENDER_EMERGENCY : PRIO_SENDER;

        if( xTaskCreate( sender_task, "SENDER", configMINIMAL_STACK_SIZE, &s_sender_args[ type ], prio, NULL ) != pdPASS )
        {
            fprintf( stderr, "태스크 생성 실패: SENDER\n" );
            return EXIT_FAILURE;
        }
    }

    if( ( xTaskCreate( receiver_task, "RECEIVER", configMINIMAL_STACK_SIZE, NULL, PRIO_RECEIVER, NULL ) != pdPASS ) ||
        ( xTaskCreate( logger_task, "LOGGER", configMINIMAL_STACK_SIZE, NULL, PRIO_LOGGER, NULL ) != pdPASS ) ||
        ( xTaskCreate( checker_task, "CHECKER", configMINIMAL_STACK_SIZE, NULL, PRIO_CHECKER, NULL ) != pdPASS ) )
    {
        fprintf( stderr, "태스크 생성 실패 (configTOTAL_HEAP_SIZE 확인)\n" );
        return EXIT_FAILURE;
    }

    vTaskStartScheduler();   /* Logger가 마지막 줄을 출력하고 vTaskEndScheduler()를 부르면 돌아온다 */

    return s_exit_code;
}
