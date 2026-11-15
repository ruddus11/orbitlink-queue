/*
 * orbitlink_msg.h - OrbitLink shared message format for the whole team
 *
 * [Why this file exists]
 * Sender, Receiver, Logger, Retry and encryption must all exchange messages of the
 * same shape. If each member defined their own struct, values would break when taken
 * out of the queue. So the message format is defined only here and everyone includes it.
 *
 * [Who fills which field]
 *   mq_msg_init()  : type, priority, id, seq, timestamp, payload_len, payload
 *   mq_send()      : priority (re-derived from type), enqueue_tick
 *   Retry owner    : retry_count (increase by 1 on each resend)
 *   Encryption     : payload contents, encrypted
 *
 * Note: the queue stores a full COPY of the struct, not a pointer. So the sender's local
 * variable can safely go away after mq_send() returns.
 */
#ifndef ORBITLINK_MSG_H
#define ORBITLINK_MSG_H

#include <stdint.h>

#include "FreeRTOS.h"

/* Maximum payload length in bytes. A larger value also makes the queues use more memory. */
#define ORBIT_PAYLOAD_MAX    64

/*
 * Message type. The value is also used as an array index, so it must start at 0 with no gaps.
 * To add a type, put it before ORBIT_MSG_TYPE_COUNT and also update the queue lengths in
 * msg_queue.h and orbit_msg_type_name() below.
 */
typedef enum
{
    ORBIT_MSG_EMERGENCY = 0,    /* Emergency: e.g. hull pressure drop. Processed first */
    ORBIT_MSG_SYSTEM,           /* System: status reports, heartbeat */
    ORBIT_MSG_NORMAL,           /* Normal: telemetry, crew messages */
    ORBIT_MSG_TYPE_COUNT        /* Number of types. Not a real type */
} orbit_msg_type_t;

/*
 * Message processing priority. "Smaller number = processed first".
 *
 * Do not confuse this with task priority!
 *   - Task priority (FreeRTOS)    : higher number = higher. Decides which task gets the CPU.
 *   - Message priority (this value): smaller number = higher. Decides which message the
 *     Receiver takes out first.
 */
typedef enum
{
    ORBIT_PRIO_HIGHEST = 0,     /* Emergency */
    ORBIT_PRIO_MEDIUM  = 1,     /* System */
    ORBIT_PRIO_LOWEST  = 2      /* Normal */
} orbit_msg_prio_t;

/* One shared team message */
typedef struct
{
    orbit_msg_type_t type;              /* Message type. Decides which queue it goes to */
    orbit_msg_prio_t priority;          /* Message priority. Set automatically from type */
    uint32_t id;                        /* Unique number across all messages (for log tracing) */
    uint32_t seq;                       /* Per-type sequence number. Checks order within a type */
    TickType_t timestamp;               /* Tick when the message was first created. Unchanged on resend */
    TickType_t enqueue_tick;            /* Tick of the last mq_send() call. Used for wait time */
    uint8_t retry_count;                /* Number of resends so far. 0 on the first send */
    uint8_t encrypted;                  /* 1 if the payload is encrypted, 0 if plain text */
    uint16_t payload_len;               /* Actual payload length in bytes */
    uint8_t payload[ ORBIT_PAYLOAD_MAX ]; /* Contents. XOR output may contain 0x00, so never treat it as a C string */
} orbit_msg_t;

/* Type -> processing priority. An invalid type is treated as the lowest priority. */
static inline orbit_msg_prio_t orbit_msg_priority_of( orbit_msg_type_t type )
{
    switch( type )
    {
        case ORBIT_MSG_EMERGENCY: return ORBIT_PRIO_HIGHEST;
        case ORBIT_MSG_SYSTEM:    return ORBIT_PRIO_MEDIUM;
        default:                  return ORBIT_PRIO_LOWEST;
    }
}

/* Type -> name printed in logs */
static inline const char * orbit_msg_type_name( orbit_msg_type_t type )
{
    switch( type )
    {
        case ORBIT_MSG_EMERGENCY: return "EMERGENCY";
        case ORBIT_MSG_SYSTEM:    return "SYSTEM";
        case ORBIT_MSG_NORMAL:    return "NORMAL";
        default:                  return "UNKNOWN";
    }
}

#endif /* ORBITLINK_MSG_H */
