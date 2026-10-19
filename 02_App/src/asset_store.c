#include "asset_store.h"
uint32_t asset_crc32(const volatile uint8_t *p,uint32_t n){
 static const uint32_t table[16]={0,0x1db71064,0x3b6e20c8,0x26d930ac,0x76dc4190,0x6b6b51f4,0x4db26158,0x5005713c,0xedb88320,0xf00f9344,0xd6d6a3e8,0xcb61b38c,0x9b64c2b0,0x86d3d2d4,0xa00ae278,0xbdbdf21c};
 uint32_t crc=~0u;while(n--){crc^=*p++;crc=(crc>>4)^table[crc&15];crc=(crc>>4)^table[crc&15];}return ~crc;
}
