// Integrated version: checks automatically that 3 Senders, a Receiver and a Logger exchange XOR-encrypted messages over the priority queue
/*
 * app_main.c - OrbitLink integrated version (Cho's send/receive + Shushant's XOR/Logger)
 *
 *   Sender(NOR/SYS/EMG) ─ XOR encrypt ─> mq_send() ─> [ priority queues ] ─> mq_receive() ─ XOR decrypt ─> Receiver
 *          every task ── log_event() ──> [ log queue ] ──> Logger (the only place that prints)
 *
 * Checks: all received, per-type statistics, order within each type, encrypted while queued,
 *         decrypted text matches the original, Emergency -> System -> Normal order,
 *         Emergency Sender has the highest task priority, no lost log lines.
 * Task priority: Emergency Sender 4, System/Normal Senders 3 (favours Emergency together with the dequeue order).
 * Retry is not included yet. It plugs in after mq_init() in main() (mq_set_full_callback).
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
#define PRIO_SENDER_EMERGENCY   4U      /* The Emergency Sender gets the CPU before the other Senders */
#define PRIO_SENDER             3U      /* System, Normal Sender */
#define PRIO_RECEIVER           2U
#define PRIO_LOGGER             1U      /* Lowest. Prints in batches while the other tasks are idle */

#define SEND_GAP_TICKS          10U
#define RECEIVER_START_TICKS    300U    /* Start the Receiver after all Senders finish so priority can be judged deterministically */
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
    TickType_t start_delay;     /* Differs per type to mix arrival order */
    const char * text;          /* Original text. The receiving side compares the decrypted result with it */
} sender_arg_t;

/* Sample texts taken from Shushant's code. */
static sender_arg_t s_sender_args[ ORBIT_MSG_TYPE_COUNT ] =
{
    { ORBIT_MSG_EMERGENCY, COUNT_EMERGENCY, 40U, "ALERT: oxygen pressure low" },
    { ORBIT_MSG_SYSTEM,    COUNT_SYSTEM,    20U, "SYSTEM: software heartbeat" },
    { ORBIT_MSG_NORMAL,    COUNT_NORMAL,    0U,  "Telemetry: battery 87%" },
};

static orbit_msg_type_t s_rx_order[ TOTAL_MESSAGES ];
static uint32_t s_rx_count;
static uint32_t s_seq_errors;
static uint32_t s_not_encrypted;    /* Messages that were plain text when taken from the queue */
static uint32_t s_decrypt_errors;   /* Messages whose decrypted text differs from the original */
static UBaseType_t s_sender_prio[ ORBIT_MSG_TYPE_COUNT ];   /* Task priority each Sender actually got */
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

    s_sender_prio[ arg->type ] = uxTaskPriorityGet( NULL );   /* The Checker verifies the setting really applied */

    vTaskDelay( arg->start_delay );

    for( i = 0; i < arg->count; i++ )
    {
        orbit_msg_t msg;
        TickType_t timeout = ( arg->type == ORBIT_MSG_EMERGENCY ) ? EMERGENCY_TIMEOUT : 0U;

        must_ok( mq_msg_init( &msg, arg->type, arg->text, len ) );
        log_event( "CREATED", &msg, "\"%s\"", arg->text );

        orbit_xor_encrypt( &msg );   /* Encrypt before queueing so no plain text sits in the queue or retry inbox */

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

        /* Must be ciphertext while queued. Check both the flag and the actual bytes. */
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

        /* The payload is not a C string, so print it with an explicit length. */
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
        log_event( "CHECK", NULL, "The Receiver did not finish in time" );
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

    check( "All 12 messages received", s_rx_count == TOTAL_MESSAGES );
    check( "Per type: sent = received = planned, 0 failed", counts_ok );
    check( "Order kept within each type", s_seq_errors == 0U );
    check( "Messages are encrypted while queued", s_not_encrypted == 0U );
    check( "Decrypted text matches the original", s_decrypt_errors == 0U );
    check( "Dequeue order Emergency -> System -> Normal", order_ok );
    check( "Emergency Sender has the highest task priority",
           ( s_sender_prio[ ORBIT_MSG_EMERGENCY ] > s_sender_prio[ ORBIT_MSG_SYSTEM ] ) &&
           ( s_sender_prio[ ORBIT_MSG_EMERGENCY ] > s_sender_prio[ ORBIT_MSG_NORMAL ] ) );
    check( "No log lines lost", log_dropped() == 0U );   /* Must be the last check so the lines above are counted too */
    log_finish( ( s_exit_code == EXIT_SUCCESS ) ? "RESULT: PASS" : "RESULT: FAIL" );   /* The Logger ends the scheduler after printing it */

    vTaskDelete( NULL );
}

void vAssertCalled( const char * file, unsigned long line )
{
    fprintf( stderr, "ASSERT failed: %s:%lu\n", file, line );
    abort();
}

int main( void )
{
    int type;

    s_receiver_done = xSemaphoreCreateBinary();

    if( ( mq_init( MQ_MODE_PRIORITY ) != MQ_OK ) || !log_init() || ( s_receiver_done == NULL ) )
    {
        fprintf( stderr, "Initialisation failed (check configTOTAL_HEAP_SIZE)\n" );
        return EXIT_FAILURE;
    }

    /* [Retry owner] Register mq_set_full_callback( failure_callback ) here. */

    log_event( "START", NULL, "OrbitLink integrated version started (%u messages, XOR key=0x%02X)",
               ( unsigned int ) TOTAL_MESSAGES, ( unsigned int ) ORBIT_XOR_KEY );

    for( type = 0; type < ORBIT_MSG_TYPE_COUNT; type++ )
    {
        UBaseType_t prio = ( type == ORBIT_MSG_EMERGENCY ) ? PRIO_SENDER_EMERGENCY : PRIO_SENDER;

        if( xTaskCreate( sender_task, "SENDER", configMINIMAL_STACK_SIZE, &s_sender_args[ type ], prio, NULL ) != pdPASS )
        {
            fprintf( stderr, "Failed to create task: SENDER\n" );
            return EXIT_FAILURE;
        }
    }

    if( ( xTaskCreate( receiver_task, "RECEIVER", configMINIMAL_STACK_SIZE, NULL, PRIO_RECEIVER, NULL ) != pdPASS ) ||
        ( xTaskCreate( logger_task, "LOGGER", configMINIMAL_STACK_SIZE, NULL, PRIO_LOGGER, NULL ) != pdPASS ) ||
        ( xTaskCreate( checker_task, "CHECKER", configMINIMAL_STACK_SIZE, NULL, PRIO_CHECKER, NULL ) != pdPASS ) )
    {
        fprintf( stderr, "Failed to create task (check configTOTAL_HEAP_SIZE)\n" );
        return EXIT_FAILURE;
    }

    vTaskStartScheduler();   /* Returns when the Logger prints the last line and calls vTaskEndScheduler() */

    return s_exit_code;
}
