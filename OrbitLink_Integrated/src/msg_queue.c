/*
 * msg_queue.c - OrbitLink 메시지 큐 모듈 구현
 *
 * [구조]
 *
 *   mq_send() ──> [ 0: EMERGENCY 큐 ] ─┐
 *             ──> [ 1: SYSTEM    큐 ] ─┼─> mq_receive() : 항상 0번 큐부터 확인
 *             ──> [ 2: NORMAL    큐 ] ─┘
 *
 *   s_msg_count : 세 큐에 들어 있는 메시지 총 개수를 세는 카운팅 세마포어
 *
 * [왜 카운팅 세마포어가 필요한가]
 * 세마포어가 없으면 Receiver는 메시지가 왔는지 알려고 세 큐를 계속 돌아봐야 하고
 * (폴링), 그동안 CPU를 낭비한다. 큐 하나에서만 기다리면 다른 큐의 메시지를 놓친다.
 * 세마포어로 기다리면 메시지가 없는 동안 Receiver가 Blocked 상태로 잠들어 있다가
 * 어느 큐에든 메시지가 들어오는 순간 깨어난다.
 *
 * [왜 메시지 처리 순서가 보장되는가]
 * mq_receive()는 호출될 때마다 EMERGENCY 큐부터 다시 확인한다. 즉 처리 순서는
 * 태스크 우선순위가 아니라 "이 파일의 꺼내는 순서"가 결정한다.
 */

#include "msg_queue.h"

#include <string.h>

#include "queue.h"
#include "semphr.h"
#include "task.h"

#define PRIORITY_QUEUE_COUNT    ORBIT_MSG_TYPE_COUNT    /* 우선순위 모드의 큐 개수 */
#define TOTAL_CAPACITY          ( MQ_LEN_EMERGENCY + MQ_LEN_SYSTEM + MQ_LEN_NORMAL )

/* 유형별 큐 길이. 인덱스는 orbit_msg_type_t 값과 같다. */
static const uint32_t s_queue_length[ PRIORITY_QUEUE_COUNT ] = { MQ_LEN_EMERGENCY, MQ_LEN_SYSTEM, MQ_LEN_NORMAL };

/* 모듈 내부 상태. 다른 파일에서는 mq_* 함수로만 접근한다. */
static int s_initialized;                               /* mq_init 후 1 */
static mq_mode_t s_mode;
static UBaseType_t s_queue_count;                       /* 실제로 만든 큐 개수 (3 또는 1) */
static QueueHandle_t s_queues[ PRIORITY_QUEUE_COUNT ];
static SemaphoreHandle_t s_msg_count;

/* 아래 값은 여러 태스크가 건드리므로 반드시 크리티컬 섹션 안에서 읽고 쓴다. */
static mq_type_status_t s_stats[ ORBIT_MSG_TYPE_COUNT ];
static uint32_t s_peak[ PRIORITY_QUEUE_COUNT ];         /* 실제 큐별 최대 길이 */
static uint32_t s_next_id;
static uint32_t s_next_seq[ ORBIT_MSG_TYPE_COUNT ];
static mq_full_callback_t s_full_callback;

/* 유형 → 실제 큐 번호. 단일 FIFO 모드에서는 모든 유형이 0번 큐를 함께 쓴다. */
static UBaseType_t queue_index( orbit_msg_type_t type )
{
    return ( s_mode == MQ_MODE_SINGLE_FIFO ) ? 0U : ( UBaseType_t ) type;
}

/* 유형 값이 올바른지 확인. enum은 정수라서 범위 밖 값이 들어올 수 있다. */
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

    /* 최댓값 = 모든 큐가 가득 찼을 때의 메시지 수, 시작값 = 0 */
    s_msg_count = ( i == s_queue_count ) ? xSemaphoreCreateCounting( TOTAL_CAPACITY, 0 ) : NULL;

    if( s_msg_count == NULL )
    {
        /* 일부만 만들어졌다면 메모리가 새지 않도록 되돌린다. */
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

    /* 번호 발급은 Sender끼리 공유하는 값이라 크리티컬 섹션으로 보호한다. */
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
     * 원본은 const이므로 복사본에 값을 채워 넣는다. 대기 시간의 시작점은 mq_send를
     * 부른 시각이다. timeout 동안 빈자리를 기다린 시간도 메시지 입장에서는 대기이기
     * 때문에 그 시간까지 포함되도록 큐에 넣기 전에 기록한다.
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

        /* 메시지를 넣은 뒤에 Give 해야 Receiver가 빈 큐를 보는 일이 없다. */
        ( void ) xSemaphoreGive( s_msg_count );

        return MQ_OK;
    }

    taskENTER_CRITICAL();
    {
        s_stats[ msg->type ].failed++;
        callback = s_full_callback;
    }
    taskEXIT_CRITICAL();

    /* 콜백은 크리티컬 섹션 밖에서 부른다. 안에서 부르면 콜백이 큐 API를 쓸 수 없다. */
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

    /* 메시지가 하나라도 생길 때까지 잠들어 있는다. */
    if( xSemaphoreTake( s_msg_count, timeout ) != pdTRUE )
    {
        return MQ_ERR_EMPTY;
    }

    /*
     * 핵심 부분: 매번 0번(EMERGENCY) 큐부터 다시 확인한다. 대기 시간 0으로 꺼내므로
     * 비어 있으면 바로 다음 큐로 넘어간다. 앞선 큐에 메시지가 있는 한 뒤쪽 큐는
     * 보지 않으므로 긴급 메시지가 일반 메시지를 앞지른다.
     */
    for( index = 0; index < s_queue_count; index++ )
    {
        if( xQueueReceive( s_queues[ index ], msg, 0 ) == pdPASS )
        {
            break;
        }
    }

    /* Give는 큐에 넣은 뒤에만 하므로 Take에 성공했다면 메시지가 반드시 있다. */
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

    /* 한 번에 복사해야 sent와 received가 서로 다른 시점의 값이 되는 일이 없다. */
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
