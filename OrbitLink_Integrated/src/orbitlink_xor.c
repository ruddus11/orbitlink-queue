// XOR encryption and decryption. XOR-ing twice with the same key gives back the original
#include "orbitlink_xor.h"

static void xor_bytes( uint8_t * data, uint16_t len )
{
    uint16_t i;

    for( i = 0; i < len; i++ )
    {
        data[ i ] ^= ( uint8_t ) ORBIT_XOR_KEY;
    }
}

void orbit_xor_encrypt( orbit_msg_t * msg )
{
    if( msg->encrypted == 0U )
    {
        xor_bytes( msg->payload, msg->payload_len );
        msg->encrypted = 1U;
    }
}

void orbit_xor_decrypt( orbit_msg_t * msg )
{
    if( msg->encrypted != 0U )
    {
        xor_bytes( msg->payload, msg->payload_len );
        msg->encrypted = 0U;
    }
}
