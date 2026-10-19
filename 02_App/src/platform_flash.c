/* Vendor BSP provides read-only Fast Read; never erases/programs Flash. */
#include "bsp.h"
#include "spiFlash.h"
#include "asset_store.h"
volatile uint8_t *asset_memory(uint32_t address){return (volatile uint8_t *)(uintptr_t)address;}
void asset_flash_read(uint32_t address,volatile uint8_t *dst,uint32_t bytes){
 static int ready;
 if(!ready){spiFlash_init(SYSTEM_SPI_0_IO_CTRL,0);spiFlash_wake(SYSTEM_SPI_0_IO_CTRL,0);spiFlash_exit4ByteAddr(SYSTEM_SPI_0_IO_CTRL,0);ready=1;}
 spiFlash_f2m(SYSTEM_SPI_0_IO_CTRL,0,address,(uint32_t)(uintptr_t)dst,bytes);
}
