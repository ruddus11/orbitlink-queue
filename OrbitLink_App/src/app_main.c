// Basic send/receive app: checks automatically that 3 Senders and 1 Receiver exchange messages over the queue module
/*
 * app_main.c - OrbitLink basic send/receive app (Cho's part)
 *
 * [Structure]
 *   Sender(NOR) ─┐
 *   Sender(SYS) ─┼─> mq_send() ──> [ msg_queue module ] ──> mq_receive() ──> Receiver
 *   Sender(EMG) ─┘
 *   Checker : when the Receiver finishes, judges the result and ends the scheduler.
 *
 * [What is checked]
 *   1. Every message sent was received
 *   2. Order (seq) is kept within each type
 *   3. The payload was not corrupted (checked by a per-type prefix)
 *   4. With everything already queued, messages come out Emergency -> System -> Normal
 *   5. The Emergency Sender has a higher task priority than the other Senders
 *
 * [Emergency is favoured in two ways]
 *   - Task priority          : Emergency Sender 4, System/Normal Senders 3. Gets the CPU first when sends overlap.
 *   - Message dequeue order  : the queue module takes from the Emergency queue first. Decides what the Receiver handles first.
 *   Task priority alone cannot move an Emergency ahead of Normals already waiting in a queue, so both are used.
 *
 * [Why the Receiver starts late]
 * If all Senders finish before the Receiver starts, every message is already queued regardless
 * of arrival order. The order they come out in is then exactly the priority result, so the
 * check does not depend on timing.
 *
 * [Where the next owners plug in]
 *   - Encryption : right before mq_send() in sender_task() / right after mq_receive() in receiver_task()
 *   - Retry      : in main(), register mq_set_full_callback() after mq_init()
 *   - Logger     : replace vLog() with calls to a logger task
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

/* ---------- Experiment parameters ---------- */

#define PRIO_CHECKER            5U      /* Checker task. Must be highest to see the Receiver finish immediately */
#define PRIO_SENDER_EMERGENCY   4U      /* The Emergency Sender gets the CPU before the other Senders */
#define PRIO_SENDER             3U      /* System and Normal Senders */
#define PRIO_RECEIVER           2U

#define SEND_GAP_TICKS          10U     /* Pause between messages in a Sender */
#define RECEIVER_START_TICKS    300U    /* The Receiver starts after all Senders have sent */
#define PROCESS_TICKS           20U     /* Time the Receiver spends on one message */
#define RECEIVE_TIMEOUT_TICKS   1000U   /* The Receiver gives up if no message arrives for this long */
#define CHECKER_TIMEOUT_TICKS   5000U   /* Max time to wait for the Receiver to finish */
#define EMERGENCY_TIMEOUT       50U     /* Emergency waits this long for a free slot even if the queue is full */

#define COUNT_EMERGENCY         3U
#define COUNT_SYSTEM            3U
#define COUNT_NORMAL            6U
#define TOTAL_MESSAGES          ( COUNT_EMERGENCY + COUNT_SYSTEM + COUNT_NORMAL )

typedef struct
{
    orbit_msg_type_t type;
    uint32_t count;             /* Number of messages to send */
    TickType_t start_delay;     /* Wait before the first send. Differs per type to mix arrival order */
    const char * prefix;        /* Payload prefix. The receiving side checks it matches the type */
} sender_arg_t;

static sender_arg_t s_sender_args[ ORBIT_MSG_TYPE_COUNT ] =
{
    { ORBIT_MSG_EMERGENCY, COUNT_EMERGENCY, 40U, "EMG:O2_LEVEL_LOW" },
    { ORBIT_MSG_SYSTEM,    COUNT_SYSTEM,    20U, "SYS:HEARTBEAT" },
    { ORBIT_MSG_NORMAL,    COUNT_NORMAL,    0U,  "NOR:TELEMETRY_BLOCK" },
};

/* ---------- Values recorded by the Receiver and judged by the Checker ---------- */

static orbit_msg_type_t s_rx_order[ TOTAL_MESSAGES ];   /* Order of types taken out */
static uint32_t s_rx_count;
static uint32_t s_seq_errors;
static uint32_t s_payload_errors;
static UBaseType_t s_sender_prio[ ORBIT_MSG_TYPE_COUNT ];   /* Task priority each Sender actually got */
static SemaphoreHandle_t s_receiver_done;
static volatile int s_exit_code = EXIT_SUCCESS;

/*
 * Tasks must print only through this function. Plain printf in the POSIX port can deadlock,
 * so it is wrapped in a critical section. Log lines also never get mixed.
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

/* Check a call that must succeed. Putting the call inside assert would remove it when asserts are disabled. */
static void must_ok( mq_result_t result )
{
    configASSERT( result == MQ_OK );
}

static void sender_task( void * param )
{
    const sender_arg_t * arg = ( const sender_arg_t * ) param;
    uint32_t i;

    s_sender_prio[ arg->type ] = uxTaskPriorityGet( NULL );   /* The Checker verifies the setting really applied */

    vTaskDelay( arg->start_delay );

    for( i = 0; i < arg->count; i++ )
    {
        orbit_msg_t msg;
        TickType_t timeout = ( arg->type == ORBIT_MSG_EMERGENCY ) ? EMERGENCY_TIMEOUT : 0U;

        must_ok( mq_msg_init( &msg, arg->type, arg->prefix, ( uint16_t ) strlen( arg->prefix ) ) );

        /* [Encryption owner] XOR msg.payload here and set msg.encrypted = 1. */
        if( mq_send( &msg, timeout ) == MQ_OK )
        {
            vLog( "SENDER   TX   %-9s #%-3lu seq=%lu", orbit_msg_type_name( msg.type ),
                  ( unsigned long ) msg.id, ( unsigned long ) msg.seq );
        }
        else
        {
            vLog( "SENDER   FAIL %-9s #%-3lu (queue full)", orbit_msg_type_name( msg.type ), ( unsigned long ) msg.id );
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

        /* [Encryption owner] If msg.encrypted is 1, decrypt the payload with XOR here. */

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

        vTaskDelay( PROCESS_TICKS );   /* Simulated processing time */
    }

    xSemaphoreGive( s_receiver_done );
    vTaskDelete( NULL );
}

/* Judge one item and print a result line. On failure the exit code becomes 1. */
static void check( const char * name, bool passed )
{
    vLog( "  %-48s %s", name, passed ? "PASS" : "FAIL" );

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
        vLog( "The Receiver did not finish in time." );
        s_exit_code = EXIT_FAILURE;
    }

    must_ok( mq_get_status( &status ) );

    /* Everything was queued before the Receiver started, so type numbers (E=0, S=1, N=2) must never go down. */
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

    vLog( "---------------- CHECKS ----------------" );
    check( "All 12 messages received", s_rx_count == TOTAL_MESSAGES );
    check( "Per type: sent = received = planned, 0 failed", counts_ok );
    check( "Order kept within each type", s_seq_errors == 0U );
    check( "Payload matches its type", s_payload_errors == 0U );
    check( "Dequeue order Emergency -> System -> Normal", order_ok );
    check( "Emergency Sender has the highest task priority",
           ( s_sender_prio[ ORBIT_MSG_EMERGENCY ] > s_sender_prio[ ORBIT_MSG_SYSTEM ] ) &&
           ( s_sender_prio[ ORBIT_MSG_EMERGENCY ] > s_sender_prio[ ORBIT_MSG_NORMAL ] ) );
    vLog( "RESULT: %s", ( s_exit_code == EXIT_SUCCESS ) ? "PASS" : "FAIL" );

    vTaskEndScheduler();
    vTaskDelete( NULL );
}

/* Called when configASSERT fails (FreeRTOSConfig.h). Prints the location and stops. */
void vAssertCalled( const char * file, unsigned long line )
{
    fprintf( stderr, "ASSERT failed: %s:%lu\n", file, line );
    abort();
}

int main( void )
{
    int type;

    if( mq_init( MQ_MODE_PRIORITY ) != MQ_OK )
    {
        fprintf( stderr, "mq_init failed (check configTOTAL_HEAP_SIZE)\n" );
        return EXIT_FAILURE;
    }

    s_receiver_done = xSemaphoreCreateBinary();

    if( s_receiver_done == NULL )
    {
        fprintf( stderr, "Failed to create semaphore\n" );
        return EXIT_FAILURE;
    }

    vLog( "OrbitLink basic send/receive app started (%u messages, Receiver starts at tick %u)",
          ( unsigned int ) TOTAL_MESSAGES, ( unsigned int ) RECEIVER_START_TICKS );

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
        ( xTaskCreate( checker_task, "CHECKER", configMINIMAL_STACK_SIZE, NULL, PRIO_CHECKER, NULL ) != pdPASS ) )
    {
        fprintf( stderr, "Failed to create task (check configTOTAL_HEAP_SIZE)\n" );
        return EXIT_FAILURE;
    }

    /* Normally returns here when the Checker calls vTaskEndScheduler(). */
    vTaskStartScheduler();

    return s_exit_code;
}
