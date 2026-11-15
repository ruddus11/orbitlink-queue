/*
 * msg_queue.h - OrbitLink message queue module, public API
 *
 * [What this module does]
 * Puts messages into three per-type queues and, whichever Receiver takes them out,
 * always returns them in Emergency -> System -> Normal order. Also reports queue length
 * and wait-time statistics.
 *
 * [Basic usage order]
 *   1. In main(), before starting the scheduler: mq_init(MQ_MODE_PRIORITY)
 *   2. mq_set_full_callback(your_callback) : connect the Retry owner who takes failed messages
 *   3. Sender   : build a message with mq_msg_init(), then mq_send()
 *   4. Receiver : mq_receive()
 *   5. Monitor  : mq_get_status()
 *
 * Every function except mq_init and mq_deinit is safe to call from several tasks at once.
 * Not usable inside an interrupt (ISR); no FromISR versions exist.
 */
#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "orbitlink_msg.h"

/*
 * Per-type queue length. Normal is long because there are many; Emergency is short because
 * it must never pile up. Single-FIFO mode (comparison experiment) uses the sum (32 slots) as
 * one queue, so the comparison is fair: same storage, only the dequeue order differs.
 */
#define MQ_LEN_EMERGENCY    8
#define MQ_LEN_SYSTEM       8
#define MQ_LEN_NORMAL       16

/* Queue mode */
typedef enum
{
    MQ_MODE_PRIORITY = 0,   /* Three per-type queues. Emergency -> System -> Normal (the real mode) */
    MQ_MODE_SINGLE_FIFO     /* One queue. Arrival order only (comparison experiment) */
} mq_mode_t;

/* API return values */
typedef enum
{
    MQ_OK = 0,          /* Success */
    MQ_ERR_PARAM,       /* Bad argument (NULL, unknown type, payload too long, etc.) */
    MQ_ERR_NOT_INIT,    /* Called before mq_init(), or already initialised */
    MQ_ERR_NO_MEMORY,   /* Not enough memory for the queues (check configTOTAL_HEAP_SIZE) */
    MQ_ERR_FULL,        /* Queue full; could not insert within the timeout */
    MQ_ERR_EMPTY        /* No message to take within the timeout */
} mq_result_t;

/*
 * Shape of the function called when a send fails because the queue is full. The Retry owner implements it.
 *
 * Input: msg - the failed message. It disappears after the call returns, so copy it if needed.
 * Note: runs inside the Sender task that called mq_send(). Waiting here stops that Sender,
 *       so only do things that return immediately, such as xQueueSend(queue, msg, 0).
 */
typedef void ( * mq_full_callback_t )( const orbit_msg_t * msg );

/* State and statistics of one type */
typedef struct
{
    uint32_t sent;              /* Successful inserts (includes successful resends) */
    uint32_t received;          /* Messages taken out */
    uint32_t failed;            /* Failed inserts because the queue was full */
    uint32_t length;            /* Messages currently in the queue (*) */
    uint32_t capacity;          /* Total slots of the queue (*) */
    uint32_t peak_length;       /* Highest number of messages ever queued (*) */
    uint64_t total_wait_ticks;  /* Sum of wait ticks of messages taken out */
    uint32_t max_wait_ticks;    /* Longest wait in ticks */
} mq_type_status_t;
/* (*) In single-FIFO mode there is only one queue, so all three types show the shared queue's values. */

/* Overall state. Filled by mq_get_status(). */
typedef struct
{
    mq_mode_t mode;
    mq_type_status_t type[ ORBIT_MSG_TYPE_COUNT ];
} mq_status_t;

/*
 * Initialise the module. Creates the queues and semaphore and clears statistics and callback.
 * Input:  mode - queue mode
 * Output: MQ_OK, MQ_ERR_NO_MEMORY, MQ_ERR_NOT_INIT (already initialised)
 */
mq_result_t mq_init( mq_mode_t mode );

/*
 * Delete the queues and semaphore. Only needed to restart in another mode, as in the comparison experiment.
 * Note: call it only after every task using the queues has finished.
 */
void mq_deinit( void );

/*
 * Build a message to send. Fills id, seq, timestamp and priority automatically.
 * Input:  msg - struct to fill, type - message type, payload/len - contents (copied; payload may be NULL if len is 0)
 * Output: MQ_OK, MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_msg_init( orbit_msg_t * msg, orbit_msg_type_t type, const void * payload, uint16_t len );

/*
 * Put a message into the queue of its type.
 * Input:  msg     - message to send (copied into the queue; the original is not changed)
 *         timeout - max ticks to wait for a free slot when full. 0 = fail immediately
 * Output: MQ_OK, MQ_ERR_FULL (the callback is called if registered), MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_send( const orbit_msg_t * msg, TickType_t timeout );

/*
 * Take out one message with the highest priority. Within a type, first in is first out.
 * Input:  msg     - where to store the message
 *         timeout - max ticks to wait when empty. portMAX_DELAY = wait until one arrives
 * Output: MQ_OK, MQ_ERR_EMPTY, MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_receive( orbit_msg_t * msg, TickType_t timeout );

/*
 * Copy the current state and statistics. Called periodically by a Monitor task.
 * Output: MQ_OK, MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_get_status( mq_status_t * status );

/*
 * Register the send-failure callback. mq_init() clears it, so call this after mq_init(). NULL unregisters.
 * Output: MQ_OK, MQ_ERR_NOT_INIT
 */
mq_result_t mq_set_full_callback( mq_full_callback_t callback );

/* Average wait in ticks. Returns 0 if no message has been taken out. */
static inline double mq_avg_wait_ticks( const mq_type_status_t * status )
{
    return ( status->received == 0U ) ? 0.0 : ( double ) status->total_wait_ticks / ( double ) status->received;
}

#endif /* MSG_QUEUE_H */
