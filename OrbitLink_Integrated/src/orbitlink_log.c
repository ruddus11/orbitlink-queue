// 로그 큐에 사건을 모아 Logger 태스크가 tick 타임스탬프와 함께 출력하는 구현
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "orbitlink_log.h"

#define LOG_QUEUE_LEN     64U
#define LOG_EVENT_LEN     12U
#define LOG_INFO_LEN      96U

typedef struct
{
    TickType_t tick;
    char event[ LOG_EVENT_LEN ];
    int32_t type;                   /* -1 = 메시지와 관계없음 */
    uint32_t id;
    char info[ LOG_INFO_LEN ];
    bool is_last;                   /* true면 Logger가 출력 후 스케줄러를 끝낸다 */
} log_entry_t;

static QueueHandle_t s_log_queue;
static volatile uint32_t s_dropped;

bool log_init( void )
{
    s_log_queue = xQueueCreate( LOG_QUEUE_LEN, sizeof( log_entry_t ) );
    return s_log_queue != NULL;
}

/* 서식 처리도 크리티컬 섹션 안에서 한다. POSIX 포트에서 stdio 함수가 태스크 전환에 끊기지 않게 하려는 것이다. */
static void fill_entry( log_entry_t * e, const char * event, const orbit_msg_t * msg, const char * format, va_list args )
{
    memset( e, 0, sizeof( *e ) );
    e->tick = xTaskGetTickCount();
    strncpy( e->event, event, LOG_EVENT_LEN - 1U );
    e->type = ( msg != NULL ) ? ( int32_t ) msg->type : -1;
    e->id = ( msg != NULL ) ? msg->id : 0U;

    taskENTER_CRITICAL();
    vsnprintf( e->info, LOG_INFO_LEN, format, args );
    taskEXIT_CRITICAL();
}

void log_event( const char * event, const orbit_msg_t * msg, const char * format, ... )
{
    log_entry_t e;
    va_list args;

    va_start( args, format );
    fill_entry( &e, event, msg, format, args );
    va_end( args );

    if( xQueueSend( s_log_queue, &e, 0 ) != pdPASS )
    {
        taskENTER_CRITICAL();
        s_dropped++;
        taskEXIT_CRITICAL();
    }
}

uint32_t log_dropped( void )
{
    return s_dropped;
}

static void finish_helper( log_entry_t * e, const char * format, ... )
{
    va_list args;

    va_start( args, format );
    fill_entry( e, "END", NULL, format, args );
    va_end( args );
}

void log_finish( const char * text )
{
    log_entry_t e;

    finish_helper( &e, "%s", text );
    e.is_last = true;
    ( void ) xQueueSend( s_log_queue, &e, portMAX_DELAY );
}

void logger_task( void * param )
{
    log_entry_t e;

    ( void ) param;

    for( ; ; )
    {
        if( xQueueReceive( s_log_queue, &e, portMAX_DELAY ) != pdPASS )
        {
            continue;
        }

        taskENTER_CRITICAL();
        {
            if( e.type >= 0 )
            {
                printf( "[%6lu] %-9s %-9s #%-3lu %s\n", ( unsigned long ) e.tick, e.event,
                        orbit_msg_type_name( ( orbit_msg_type_t ) e.type ), ( unsigned long ) e.id, e.info );
            }
            else
            {
                printf( "[%6lu] %-9s %s\n", ( unsigned long ) e.tick, e.event, e.info );
            }

            fflush( stdout );
        }
        taskEXIT_CRITICAL();

        if( e.is_last )
        {
            vTaskEndScheduler();
        }
    }
}
