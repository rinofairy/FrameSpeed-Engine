/* Read-only Flash launcher: both apps use the exact same ID6 FPGA engine. */
#include "bsp.h"
#include "asset_store.h"
#include "suite_ui.h"
#include "suite_switch.h"
#include "fs_blitter.h"
#include <string.h>
#define META 0x01010000u
#define WARM 0x4653574du
extern const unsigned char _binary_chain_bin_start[],_binary_chain_bin_end[];
static void fail(const char *s){bsp_printf("DISPLAY_FAIL MENU %s\r\n",s);while(1){}}
static void read_region(const uint32_t *h,unsigned at,unsigned dst){
 unsigned bytes=h[at+1];if(!bytes||bytes>0x200000u||(bytes&3u))fail("region size");
 asset_flash_read(h[at],(volatile uint8_t *)dst,bytes);fs_cache_sync();
 if(asset_crc32((volatile uint8_t *)dst,bytes)!=h[at+2])fail("region CRC");
}
void main(void){
 bsp_init();if(fs_read(0)!=FS_ID||fs_read(0x80)!=0x46534c06u)fail("engine ID");
 bsp_printf("DISPLAY_APP_BEGIN STAGE=49_SUITE_LAUNCHER\r\n");
 uint32_t h[18],tag;__asm__ volatile("csrr %0, mscratch":"=r"(tag));
 if(tag==WARM&&*(volatile uint32_t *)0x1010100==0x4653524du)memcpy(h,(void *)META,sizeof(h));
 else asset_flash_read(0xe0000,(volatile uint8_t *)h,sizeof(h));
 if(h[0]!=0x46535549u||h[1]!=1||h[2]!=0x26101851u)fail("Flash suite header");
 __asm__ volatile("csrr %0, mscratch":"=r"(tag));
 if(tag!=WARM||memcmp((void *)META,h,sizeof(h))){
  *(volatile uint32_t *)0x1010100=0;
  bsp_printf("SUITE_ASSETS_LOADING\r\n");
  read_region(h,3,0x1100000);read_region(h,6,0x1120000);read_region(h,9,0x1140000);
  read_region(h,12,0x1400000);read_region(h,15,0x1500000);
  memcpy((void *)META,h,sizeof(h));
  bsp_printf("SUITE_STAGING_CRC_OK\r\n");
 }
 unsigned bytes=_binary_chain_bin_end-_binary_chain_bin_start;if(bytes>4096)fail("trampoline size");
 memcpy((void *)0x1000000,_binary_chain_bin_start,bytes);fs_cache_sync();
 __asm__ volatile(".word 0x0000100f":::"memory");
 unsigned selected=0,old=~0u,parse=0;uint32_t seq=fs_read(0x114),fb=0x800000;
 bsp_printf("APP_MODE mode=MENU\r\nKEYS 1=DEMO 2=GAME E=DEMO G=GAME M=MENU\r\nDISPLAY_READY\r\n");
 while(1){
  if(old!=selected){suite_menu((volatile uint16_t *)fb,selected,1);fs_cache_sync();
   *(volatile uint32_t *)(FS_BASE+0x108)=fb;*(volatile uint32_t *)(FS_BASE+0x10c)=++seq;
   __asm__ volatile("fence rw,rw":::"memory");*(volatile uint32_t *)(FS_BASE+0x110)=1;
   while(fs_read(0x114)!=seq){}old=selected;fb^=0x100000;
  }
  while(uart_readOccupancy(BSP_UART_TERMINAL)){
   unsigned char c=uart_read(BSP_UART_TERMINAL);if(c=='@'){parse=2;continue;}if(parse){parse--;continue;}
   if(c>='A'&&c<='Z')c+='a'-'A';
   if(c=='1'||c=='e')suite_switch_app(1);if(c=='2'||c=='g')suite_switch_app(2);
   if(c=='w'||c=='s')selected^=1;if(c=='\r'||c=='\n')suite_switch_app(selected+1);
  }
  bsp_uDelay(1000);
 }
}
