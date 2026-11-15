// 로그 큐와 Logger 태스크 API. 출력은 Logger 태스크 한 곳에서만 한다 (Shushant 구조를 비차단으로 변경)
#ifndef ORBITLINK_LOG_H
#define ORBITLINK_LOG_H

#include <stdbool.h>
#include <stdint.h>

#include "orbitlink_msg.h"

/* 스케줄러 시작 전에 한 번 부른다. 로그 큐를 만든다. */
bool log_init( void );

/*
 * 사건 하나를 로그 큐에 넣는다. 어느 태스크에서 불러도 된다.
 * 큐가 가득 차면 기다리지 않고 버린 뒤 개수만 센다. 로그 때문에 Sender가 멈추면 안 되기 때문이다.
 * msg가 NULL이면 메시지와 관계없는 사건이다.
 */
void log_event( const char * event, const orbit_msg_t * msg, const char * format, ... ) __attribute__( ( format( printf, 3, 4 ) ) );

/* 버려진 로그 개수 */
uint32_t log_dropped( void );

/* 마지막 줄을 보낸다. 이 줄은 버리지 않고 기다려서 넣으며, Logger가 출력한 뒤 스케줄러를 끝낸다. */
void log_finish( const char * text );

/* Logger 태스크 본체. 가장 낮은 우선순위로 만든다. */
void logger_task( void * param );

#endif /* ORBITLINK_LOG_H */
