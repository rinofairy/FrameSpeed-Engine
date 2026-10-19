#ifndef ASSET_STORE_H
#define ASSET_STORE_H
#include <stdint.h>
uint32_t asset_crc32(const volatile uint8_t *,uint32_t);
int asset_store_init(void);
/* Read-only Flash transport; tests replace it with an in-memory model. */
void asset_flash_read(uint32_t address,volatile uint8_t *destination,uint32_t bytes);
volatile uint8_t *asset_memory(uint32_t address);
#endif
