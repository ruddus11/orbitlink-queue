/*
 * orbitlink_msg.h - OrbitLink 팀 공용 메시지 형식
 *
 * [이 파일이 왜 필요한가]
 * Sender, Receiver, Logger, Retry, 암호화 담당이 모두 "같은 모양의 메시지"를
 * 주고받아야 한다. 팀원마다 구조체를 따로 만들면 큐에서 꺼낼 때 값이 깨지므로,
 * 메시지 형식은 이 파일 하나에서만 정의하고 모두 이 파일을 include 한다.
 *
 * [필드를 누가 채우는가]
 *   mq_msg_init() : type, priority, id, seq, timestamp, payload_len, payload
 *   mq_send()     : priority(type 기준으로 다시 맞춤), enqueue_tick
 *   Retry 담당    : retry_count (다시 보낼 때 1 증가)
 *   암호화 담당   : payload 내용, encrypted
 *
 * 주의: 큐는 포인터가 아니라 구조체 전체를 "복사"해서 저장한다. 그래서 mq_send()가
 * 끝나면 보낸 쪽 지역 변수는 사라져도 안전하다.
 */
#ifndef ORBITLINK_MSG_H
#define ORBITLINK_MSG_H

#include <stdint.h>

#include "FreeRTOS.h"

/* payload 최대 길이(바이트). 크게 잡을수록 큐가 쓰는 메모리도 늘어난다. */
#define ORBIT_PAYLOAD_MAX    64

/*
 * 메시지 유형. 값이 곧 배열 인덱스로도 쓰이므로 0부터 빈틈없이 이어져야 한다.
 * 새 유형을 넣는다면 ORBIT_MSG_TYPE_COUNT 앞에 추가하고
 * msg_queue.h 의 큐 길이, 아래 orbit_msg_type_name() 도 함께 고친다.
 */
typedef enum
{
    ORBIT_MSG_EMERGENCY = 0,    /* 긴급: 선체 압력 저하 등. 가장 먼저 처리 */
    ORBIT_MSG_SYSTEM,           /* 시스템: 상태 보고, heartbeat */
    ORBIT_MSG_NORMAL,           /* 일반: 텔레메트리, 승무원 메시지 */
    ORBIT_MSG_TYPE_COUNT        /* 유형 개수. 실제 유형이 아니다 */
} orbit_msg_type_t;

/*
 * 메시지 처리 우선순위. "숫자가 작을수록 먼저" 처리한다.
 *
 * 태스크 우선순위와 헷갈리면 안 된다!
 *   - 태스크 우선순위(FreeRTOS) : 숫자가 클수록 높다. "어느 태스크가 CPU를 쓸까"를 정한다.
 *   - 메시지 처리 우선순위(이 값): 숫자가 작을수록 높다. "Receiver가 어떤 메시지를
 *     먼저 꺼낼까"를 정한다.
 */
typedef enum
{
    ORBIT_PRIO_HIGHEST = 0,     /* Emergency */
    ORBIT_PRIO_MEDIUM  = 1,     /* System */
    ORBIT_PRIO_LOWEST  = 2      /* Normal */
} orbit_msg_prio_t;

/* 팀 공용 메시지 1개 */
typedef struct
{
    orbit_msg_type_t type;              /* 메시지 유형. 어느 큐로 갈지를 결정한다 */
    orbit_msg_prio_t priority;          /* 메시지 처리 우선순위. type에 따라 자동으로 정해진다 */
    uint32_t id;                        /* 전체 메시지에서 유일한 번호 (로그 추적용) */
    uint32_t seq;                       /* 유형별 일련번호. 같은 유형 안의 순서 확인용 */
    TickType_t timestamp;               /* 메시지를 처음 만든 시각(tick). 재전송해도 그대로 */
    TickType_t enqueue_tick;            /* 마지막으로 mq_send()를 호출한 시각. 대기 시간 계산용 */
    uint8_t retry_count;                /* 지금까지 재전송한 횟수. 처음 보낼 때는 0 */
    uint8_t encrypted;                  /* payload가 암호화되어 있으면 1, 평문이면 0 */
    uint16_t payload_len;               /* payload 실제 길이(바이트) */
    uint8_t payload[ ORBIT_PAYLOAD_MAX ]; /* 내용. XOR 암호화하면 0이 섞일 수 있어 문자열로 다루면 안 된다 */
} orbit_msg_t;

/* 유형 → 처리 우선순위 변환. 잘못된 유형은 가장 낮은 우선순위로 취급한다. */
static inline orbit_msg_prio_t orbit_msg_priority_of( orbit_msg_type_t type )
{
    switch( type )
    {
        case ORBIT_MSG_EMERGENCY: return ORBIT_PRIO_HIGHEST;
        case ORBIT_MSG_SYSTEM:    return ORBIT_PRIO_MEDIUM;
        default:                  return ORBIT_PRIO_LOWEST;
    }
}

/* 유형 → 로그에 찍을 이름 변환 */
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
