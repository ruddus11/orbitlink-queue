// Log queue and Logger task API. Only the Logger task prints (Shushant's design, changed to non-blocking)
#ifndef ORBITLINK_LOG_H
#define ORBITLINK_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "orbitlink_msg.h"

/* Call once before starting the scheduler. Creates the log queue. */
bool log_init( void );

/*
 * Put one event into the log queue. Any task may call it.
 * If the queue is full the event is dropped without waiting and only counted, because
 * logging must never stop a Sender. A NULL msg means the event is not tied to a message.
 */
void log_event( const char * event, const orbit_msg_t * msg, const char * format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );

/* Number of dropped log entries */
uint32_t log_dropped( void );

/* Send the final line. It waits instead of being dropped, and the Logger ends the scheduler after printing it. */
void log_finish( const char * text );

/* Logger task body. Create it with the lowest priority. */
void logger_task( void * param );

#endif /* ORBITLINK_LOG_H */
