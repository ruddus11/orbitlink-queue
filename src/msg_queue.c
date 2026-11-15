/*
 * msg_queue.c - OrbitLink message queue module, implementation
 *
 * [Structure]
 *
 *   mq_send() ──> [ 0: EMERGENCY queue ] ─┐
 *             ──> [ 1: SYSTEM    queue ] ─┼─> mq_receive() : always checks queue 0 first
 *             ──> [ 2: NORMAL    queue ] ─┘
 *
 *   s_msg_count : counting semaphore holding the total number of messages in the three queues
 *
 * [Why a counting semaphore]
 * Without it the Receiver would have to keep checking all three queues to see whether a
 * message arrived (polling), wasting CPU. Waiting on only one queue would miss the others.
 * Waiting on the semaphore keeps the Receiver Blocked while there is nothing to do and wakes
 * it the moment a message enters any queue.
 *
 * [Why the processing order is guaranteed]
 * Every call to mq_receive() starts again from the EMERGENCY queue. So the processing order
 * is decided by "the dequeue order in this file", not by task priority.
 */

#include "msg_queue.h"

#include <string.h>

#include "queue.h"
#include "semphr.h"
#include "task.h"

#define PRIORITY_QUEUE_COUNT    ORBIT_MSG_TYPE_COUNT    /* Number of queues in priority mode */
#define TOTAL_CAPACITY          ( MQ_LEN_EMERGENCY + MQ_LEN_SYSTEM + MQ_LEN_NORMAL )

/* Per-type queue length. The index equals the orbit_msg_type_t value. */
static const uint32_t s_queue_length[ PRIORITY_QUEUE_COUNT ] = { MQ_LEN_EMERGENCY, MQ_LEN_SYSTEM, MQ_LEN_NORMAL };

/* Internal module state. Other files access it only through mq_* functions. */
static int s_initialized;                               /* 1 after mq_init */
static mq_mode_t s_mode;
static UBaseType_t s_queue_count;                       /* Number of queues actually created (3 or 1) */
static QueueHandle_t s_queues[ PRIORITY_QUEUE_COUNT ];
static SemaphoreHandle_t s_msg_count;

/* Several tasks touch the values below, so always read and write them inside a critical section. */
static mq_type_status_t s_stats[ ORBIT_MSG_TYPE_COUNT ];
static uint32_t s_peak[ PRIORITY_QUEUE_COUNT ];         /* Peak length per real queue */
static uint32_t s_next_id;
static uint32_t s_next_seq[ ORBIT_MSG_TYPE_COUNT ];
static mq_full_callback_t s_full_callback;

/* Type -> real queue index. In single-FIFO mode every type shares queue 0. */
static UBaseType_t queue_index( orbit_msg_type_t type )
{
    return ( s_mode == MQ_MODE_SINGLE_FIFO ) ? 0U : ( UBaseType_t ) type;
}

/* Check that the type is valid. An enum is an int, so out-of-range values can come in. */
static int is_valid_type( orbit_msg_type_t type )
{
    return ( ( int ) type >= 0 ) && ( ( int ) type < ( int ) ORBIT_MSG_TYPE_COUNT );
}

mq_result_t mq_init( mq_mode_t mode )
{
    UBaseType_t i;

    if( s_initialized )
    {
        return MQ_ERR_NOT_INIT;
    }

    s_mode = mode;
    s_queue_count = ( mode == MQ_MODE_SINGLE_FIFO ) ? 1U : PRIORITY_QUEUE_COUNT;

    memset( s_queues, 0, sizeof( s_queues ) );
    memset( s_stats, 0, sizeof( s_stats ) );
    memset( s_peak, 0, sizeof( s_peak ) );
    memset( s_next_seq, 0, sizeof( s_next_seq ) );
    s_next_id = 1U;
    s_full_callback = NULL;

    for( i = 0; i < s_queue_count; i++ )
    {
        uint32_t length = ( mode == MQ_MODE_SINGLE_FIFO ) ? TOTAL_CAPACITY : s_queue_length[ i ];

        s_queues[ i ] = xQueueCreate( length, sizeof( orbit_msg_t ) );

        if( s_queues[ i ] == NULL )
        {
            break;
        }
    }

    /* Max = number of messages when every queue is full, initial = 0 */
    s_msg_count = ( i == s_queue_count ) ? xSemaphoreCreateCounting( TOTAL_CAPACITY, 0 ) : NULL;

    if( s_msg_count == NULL )
    {
        /* If only some were created, undo them so no memory leaks. */
        for( i = 0; i < s_queue_count; i++ )
        {
            if( s_queues[ i ] != NULL )
            {
                vQueueDelete( s_queues[ i ] );
                s_queues[ i ] = NULL;
            }
        }

        return MQ_ERR_NO_MEMORY;
    }

    s_initialized = 1;

    return MQ_OK;
}

void mq_deinit( void )
{
    UBaseType_t i;

    if( !s_initialized )
    {
        return;
    }

    s_initialized = 0;

    for( i = 0; i < s_queue_count; i++ )
    {
        vQueueDelete( s_queues[ i ] );
        s_queues[ i ] = NULL;
    }

    vSemaphoreDelete( s_msg_count );
    s_msg_count = NULL;
}

mq_result_t mq_msg_init( orbit_msg_t * msg, orbit_msg_type_t type, const void * payload, uint16_t len )
{
    if( !s_initialized )
    {
        return MQ_ERR_NOT_INIT;
    }

    if( ( msg == NULL ) || !is_valid_type( type ) || ( len > ORBIT_PAYLOAD_MAX ) || ( ( payload == NULL ) && ( len > 0U ) ) )
    {
        return MQ_ERR_PARAM;
    }

    memset( msg, 0, sizeof( *msg ) );
    msg->type = type;
    msg->priority = orbit_msg_priority_of( type );
    msg->timestamp = xTaskGetTickCount();
    msg->payload_len = len;

    if( len > 0U )
    {
        memcpy( msg->payload, payload, len );
    }

    /* Numbering is shared between Senders, so protect it with a critical section. */
    taskENTER_CRITICAL();
    {
        msg->id = s_next_id++;
        msg->seq = ++s_next_seq[ type ];
    }
    taskEXIT_CRITICAL();

    return MQ_OK;
}

mq_result_t mq_send( const orbit_msg_t * msg, TickType_t timeout )
{
    orbit_msg_t copy;
    UBaseType_t index;
    mq_full_callback_t callback;

    if( !s_initialized )
    {
        return MQ_ERR_NOT_INIT;
    }

    if( ( msg == NULL ) || !is_valid_type( msg->type ) || ( msg->payload_len > ORBIT_PAYLOAD_MAX ) )
    {
        return MQ_ERR_PARAM;
    }

    /*
     * The original is const, so fill in values on a copy. Wait time starts when mq_send
     * is called. Time spent waiting for a free slot during timeout is also waiting from the
     * message's point of view, so record it before inserting.
     */
    copy = *msg;
    copy.priority = orbit_msg_priority_of( msg->type );
    copy.enqueue_tick = xTaskGetTickCount();
    index = queue_index( msg->type );

    if( xQueueSend( s_queues[ index ], &copy, timeout ) == pdPASS )
    {
        taskENTER_CRITICAL();
        {
            uint32_t length = ( uint32_t ) uxQueueMessagesWaiting( s_queues[ index ] );

            s_stats[ msg->type ].sent++;

            if( length > s_peak[ index ] )
            {
                s_peak[ index ] = length;
            }
        }
        taskEXIT_CRITICAL();

        /* Give only after inserting, so the Receiver never sees an empty queue. */
        ( void ) xSemaphoreGive( s_msg_count );

        return MQ_OK;
    }

    taskENTER_CRITICAL();
    {
        s_stats[ msg->type ].failed++;
        callback = s_full_callback;
    }
    taskEXIT_CRITICAL();

    /* Call the callback outside the critical section; inside it the callback could not use queue APIs. */
    if( callback != NULL )
    {
        callback( &copy );
    }

    return MQ_ERR_FULL;
}

mq_result_t mq_receive( orbit_msg_t * msg, TickType_t timeout )
{
    UBaseType_t index;
    uint32_t waited;

    if( !s_initialized )
    {
        return MQ_ERR_NOT_INIT;
    }

    if( msg == NULL )
    {
        return MQ_ERR_PARAM;
    }

    /* Sleep until at least one message exists. */
    if( xSemaphoreTake( s_msg_count, timeout ) != pdTRUE )
    {
        return MQ_ERR_EMPTY;
    }

    /*
     * Core part: always start again from queue 0 (EMERGENCY). Receiving with zero wait
     * moves on to the next queue immediately if one is empty. Later queues are never looked
     * at while an earlier queue has a message, so emergency messages overtake normal ones.
     */
    for( index = 0; index < s_queue_count; index++ )
    {
        if( xQueueReceive( s_queues[ index ], msg, 0 ) == pdPASS )
        {
            break;
        }
    }

    /* Give happens only after an insert, so a successful Take guarantees a message exists. */
    configASSERT( index < s_queue_count );

    waited = ( uint32_t ) ( xTaskGetTickCount() - msg->enqueue_tick );

    taskENTER_CRITICAL();
    {
        mq_type_status_t * stat = &s_stats[ msg->type ];

        stat->received++;
        stat->total_wait_ticks += waited;

        if( waited > stat->max_wait_ticks )
        {
            stat->max_wait_ticks = waited;
        }
    }
    taskEXIT_CRITICAL();

    return MQ_OK;
}

mq_result_t mq_get_status( mq_status_t * status )
{
    int type;

    if( !s_initialized )
    {
        return MQ_ERR_NOT_INIT;
    }

    if( status == NULL )
    {
        return MQ_ERR_PARAM;
    }

    status->mode = s_mode;

    /* Copy everything at once so sent and received are never from different moments. */
    taskENTER_CRITICAL();
    {
        for( type = 0; type < ( int ) ORBIT_MSG_TYPE_COUNT; type++ )
        {
            UBaseType_t index = queue_index( ( orbit_msg_type_t ) type );

            status->type[ type ] = s_stats[ type ];
            status->type[ type ].length = ( uint32_t ) uxQueueMessagesWaiting( s_queues[ index ] );
            status->type[ type ].capacity = ( s_mode == MQ_MODE_SINGLE_FIFO ) ? TOTAL_CAPACITY : s_queue_length[ type ];
            status->type[ type ].peak_length = s_peak[ index ];
        }
    }
    taskEXIT_CRITICAL();

    return MQ_OK;
}

mq_result_t mq_set_full_callback( mq_full_callback_t callback )
{
    if( !s_initialized )
    {
        return MQ_ERR_NOT_INIT;
    }

    taskENTER_CRITICAL();
    {
        s_full_callback = callback;
    }
    taskEXIT_CRITICAL();

    return MQ_OK;
}
