// XOR encryption/decryption of a message payload (Shushant's xor_crypt adapted to orbit_msg_t)
#ifndef ORBITLINK_XOR_H
#define ORBITLINK_XOR_H

#include "orbitlink_msg.h"

/* Simulation key for the assignment. Not real security and no integrity check. */
#define ORBIT_XOR_KEY    0x5AU

/* Only payload_len bytes are changed. type, id and length stay plain so the queue can classify the message. */
void orbit_xor_encrypt( orbit_msg_t * msg );   /* Does nothing if already encrypted */
void orbit_xor_decrypt( orbit_msg_t * msg );   /* Does nothing if already plain text */

#endif /* ORBITLINK_XOR_H */
