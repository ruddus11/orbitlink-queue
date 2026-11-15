/*
 * msg_queue.h - OrbitLink 메시지 큐 모듈 공개 API
 *
 * [이 모듈이 하는 일]
 * 메시지를 유형별 큐 3개에 나눠 담고, 어느 Receiver가 꺼내든 항상
 * Emergency → System → Normal 순서로 나오게 한다. 큐 길이와 대기 시간 통계도 낸다.
 *
 * [기본 사용 순서]
 *   1. main()에서 스케줄러 시작 전에 mq_init(MQ_MODE_PRIORITY)
 *   2. mq_set_full_callback(내_콜백) : 실패한 메시지를 받아 갈 Retry 담당 연결
 *   3. Sender : mq_msg_init() 으로 메시지를 만들고 mq_send()
 *   4. Receiver : mq_receive()
 *   5. Monitor : mq_get_status()
 *
 * mq_init, mq_deinit 을 뺀 함수는 여러 태스크에서 동시에 불러도 안전하다.
 * 인터럽트(ISR) 안에서는 쓸 수 없다. (FromISR 버전을 만들지 않았다)
 */
#ifndef MSG_QUEUE_H
#define MSG_QUEUE_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "orbitlink_msg.h"

/*
 * 유형별 큐 길이. Normal은 양이 많아서 길게, 긴급은 적체되면 안 되므로 짧게 잡았다.
 * 비교 실험의 단일 FIFO 모드는 세 값의 합(32칸)을 한 큐로 쓴다.
 * 그래야 "저장 공간은 같고 꺼내는 순서만 다른" 공정한 비교가 된다.
 */
#define MQ_LEN_EMERGENCY    8
#define MQ_LEN_SYSTEM       8
#define MQ_LEN_NORMAL       16

/* 큐 동작 방식 */
typedef enum
{
    MQ_MODE_PRIORITY = 0,   /* 유형별 큐 3개. Emergency → System → Normal 순서 (실제로 쓰는 모드) */
    MQ_MODE_SINGLE_FIFO     /* 큐 1개. 도착 순서대로만 꺼낸다 (비교 실험용) */
} mq_mode_t;

/* API 반환값 */
typedef enum
{
    MQ_OK = 0,          /* 성공 */
    MQ_ERR_PARAM,       /* 인자가 잘못됨 (NULL, 없는 유형, payload 길이 초과 등) */
    MQ_ERR_NOT_INIT,    /* mq_init() 전에 호출했거나 이미 초기화함 */
    MQ_ERR_NO_MEMORY,   /* 큐를 만들 메모리 부족 (configTOTAL_HEAP_SIZE 확인) */
    MQ_ERR_FULL,        /* 큐가 꽉 차서 timeout 안에 넣지 못함 */
    MQ_ERR_EMPTY        /* timeout 안에 꺼낼 메시지가 없음 */
} mq_result_t;

/*
 * 큐가 꽉 차서 송신에 실패했을 때 불리는 함수의 모양. Retry 담당이 구현한다.
 *
 * 입력: msg - 실패한 메시지. 호출이 끝나면 사라지므로 필요하면 복사해 가야 한다.
 * 주의: mq_send()를 부른 Sender 태스크 안에서 실행된다. 여기서 오래 기다리면 Sender가
 *       멈추므로, xQueueSend(큐, msg, 0) 처럼 바로 끝나는 일만 해야 한다.
 */
typedef void ( * mq_full_callback_t )( const orbit_msg_t * msg );

/* 유형 1개의 상태와 통계 */
typedef struct
{
    uint32_t sent;              /* 큐에 넣기 성공한 횟수 (재전송 성공도 포함) */
    uint32_t received;          /* 꺼낸 횟수 */
    uint32_t failed;            /* 큐가 꽉 차서 실패한 횟수 */
    uint32_t length;            /* 지금 큐에 들어 있는 메시지 수 (*) */
    uint32_t capacity;          /* 큐 전체 칸 수 (*) */
    uint32_t peak_length;       /* 지금까지 가장 많이 쌓였던 메시지 수 (*) */
    uint64_t total_wait_ticks;  /* 꺼낸 메시지들의 대기 tick 합계 */
    uint32_t max_wait_ticks;    /* 가장 오래 기다린 대기 tick */
} mq_type_status_t;
/* (*) 단일 FIFO 모드에서는 큐가 1개뿐이라 세 유형 모두 같은 공유 큐의 값을 보여 준다. */

/* 전체 상태. mq_get_status()가 채운다. */
typedef struct
{
    mq_mode_t mode;
    mq_type_status_t type[ ORBIT_MSG_TYPE_COUNT ];
} mq_status_t;

/*
 * 모듈 초기화. 큐와 세마포어를 만들고 통계와 콜백을 비운다.
 * 입력: mode - 큐 동작 방식
 * 출력: MQ_OK, MQ_ERR_NO_MEMORY, MQ_ERR_NOT_INIT(이미 초기화됨)
 */
mq_result_t mq_init( mq_mode_t mode );

/*
 * 큐와 세마포어를 지운다. 비교 실험처럼 모드를 바꿔 다시 시작할 때만 필요하다.
 * 주의: 큐를 쓰는 태스크가 모두 끝난 뒤에 불러야 한다.
 */
void mq_deinit( void );

/*
 * 보낼 메시지를 만든다. id, seq, timestamp, priority를 자동으로 채운다.
 * 입력: msg - 채울 구조체, type - 유형, payload/len - 내용(복사됨, len이 0이면 NULL 가능)
 * 출력: MQ_OK, MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_msg_init( orbit_msg_t * msg, orbit_msg_type_t type, const void * payload, uint16_t len );

/*
 * 메시지를 유형에 맞는 큐에 넣는다.
 * 입력: msg     - 보낼 메시지 (큐 안으로 복사되며 원본은 바뀌지 않는다)
 *       timeout - 큐가 꽉 찼을 때 빈자리를 기다릴 최대 tick. 0이면 바로 실패
 * 출력: MQ_OK, MQ_ERR_FULL(콜백이 등록되어 있으면 호출됨), MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_send( const orbit_msg_t * msg, TickType_t timeout );

/*
 * 가장 우선순위가 높은 메시지를 1개 꺼낸다. 같은 유형 안에서는 먼저 들어온 것부터 나온다.
 * 입력: msg     - 꺼낸 메시지를 저장할 곳
 *       timeout - 메시지가 없을 때 기다릴 최대 tick. portMAX_DELAY면 올 때까지 기다림
 * 출력: MQ_OK, MQ_ERR_EMPTY, MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_receive( orbit_msg_t * msg, TickType_t timeout );

/*
 * 현재 상태와 통계를 복사해 준다. Monitor 태스크가 주기적으로 부른다.
 * 출력: MQ_OK, MQ_ERR_PARAM, MQ_ERR_NOT_INIT
 */
mq_result_t mq_get_status( mq_status_t * status );

/*
 * 송신 실패 콜백을 등록한다. mq_init()이 지우므로 그 뒤에 불러야 한다. NULL이면 해제.
 * 출력: MQ_OK, MQ_ERR_NOT_INIT
 */
mq_result_t mq_set_full_callback( mq_full_callback_t callback );

/* 평균 대기 tick 계산. 꺼낸 메시지가 없으면 0을 돌려준다. */
static inline double mq_avg_wait_ticks( const mq_type_status_t * status )
{
    return ( status->received == 0U ) ? 0.0 : ( double ) status->total_wait_ticks / ( double ) status->received;
}

#endif /* MSG_QUEUE_H */
