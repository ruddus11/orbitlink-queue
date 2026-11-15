// 메시지 payload를 XOR로 암호화하고 복호화하는 함수 선언 (Shushant의 xor_crypt를 orbit_msg_t에 맞춤)
#ifndef ORBITLINK_XOR_H
#define ORBITLINK_XOR_H

#include "orbitlink_msg.h"

/* 과제용 시뮬레이션 키. 실제 보안이 아니며 무결성 검사 기능도 없다. */
#define ORBIT_XOR_KEY    0x5AU

/* payload_len 바이트만 변환한다. type, id, 길이는 그대로 두어야 큐가 분류할 수 있다. */
void orbit_xor_encrypt( orbit_msg_t * msg );   /* 이미 암호화된 메시지는 건드리지 않는다 */
void orbit_xor_decrypt( orbit_msg_t * msg );   /* 평문 메시지는 건드리지 않는다 */

#endif /* ORBITLINK_XOR_H */
