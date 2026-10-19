/* Execute the actual game, renderer, clipping and register driver on RV32.
 * DMA is an immediate software model. This is NOT an FPGA/HDMI simulation. */
#include "taisei_render.h"
#include "taisei_rain.h"
#include "cirno_boss.h"
#include "sh_background.h"


#include "fs_scanline.h"
#include <string.h>
#include <stddef.h>
static uint16_t memory[0x800000/2];
static uint32_t regs[64],checks,commands,op_count[6];
static sh_game game;
static uint32_t row_spans[256],span_upload;
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int c,size_t n){unsigned char *a=d;while(n--)*a++=(unsigned char)c;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
static void put(const char *s){while(*s)*(volatile unsigned char *)0x10000000=*s++;}
static void num(uint32_t n){char b[10];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);while(i){char s[2]={b[--i],0};put(s);}}
static void check(int good,unsigned line){checks++;if(good)return;put("SHOOTER_FAIL line=");num(line);put("\n");*(volatile uint32_t *)0x100000=0x13333;while(1){}}
#define CHECK(x) check((x),__LINE__)
volatile uint16_t *sh_memory(uint32_t address){CHECK(address>=FS_DMA_LO && address<FS_DMA_HI && !(address&1));return memory+(address-FS_DMA_LO)/2;}
static uint32_t line_regs[64],upload,largest_band,model_sprite_pixels;
static sl_descriptor desc[512];static uint16_t texture[8192];
static unsigned model_delay,pending_start,pending_polls,busy_preparations;
static void model_write(uint32_t off,uint32_t v);
uint32_t fs_test_read(uint32_t off){
 if(off==0x84 && pending_start && !--pending_polls){unsigned v=pending_start;pending_start=0;model_write(0x88,v);}
 if(off>=0x80&&off<0x100)return line_regs[(off-0x80)/4];CHECK(off<sizeof(regs));return regs[off/4];
}
static uint16_t reference_mix(uint16_t f,uint16_t b,unsigned a){
 unsigned out=0;const unsigned shifts[]={0,5,11},masks[]={31,63,31};
 for(unsigned k=0;k<3;k++)out|=((((f>>shifts[k])&masks[k])*a+((b>>shifts[k])&masks[k])*(255-a)+127)/255)<<shifts[k];return out;
}

void fs_test_sync(void){}
void fs_test_write(uint32_t off,uint32_t v){
 CHECK(!pending_start); /* No APB/legacy-engine mutation while rendering. */
 if(off==0x88 && (v==1 || v==3) && model_delay){
  pending_start=v;pending_polls=model_delay;line_regs[1]=1;return;
 }
 model_write(off,v);
}
static void model_write(uint32_t off,uint32_t v){
 if(off>=0x80&&off<0x100){
  if(off==0xf0){line_regs[0x70/4]=0x00040100u|(v&1u);return;}
  if(off==0xb0){upload=0;return;}
  if(off==0xe0){span_upload=0;line_regs[0x68/4]=0;return;}
  if(off==0xe4){CHECK(span_upload<256);row_spans[span_upload++]=v;line_regs[0x68/4]=span_upload;return;}
  if(off==0xb4){CHECK(upload<2048);((uint32_t *)desc)[upload++]=v;return;}
  line_regs[(off-0x80)/4]=v;if(off!=0x88)return;
  if(v==2){span_upload=0;line_regs[0x68/4]=0;unsigned words=line_regs[0x24/4];CHECK(words<=4096);for(unsigned i=0;i<words*2;i++)texture[i]=*sh_memory(line_regs[0x20/4]+i*2);line_regs[0x50/4]=words;}
  else if(v==1 || v==3){
   unsigned ncmd=line_regs[0x18/4];line_regs[0x78/4]=line_regs[0x7c/4]=0;
   if(v==3){
    unsigned address=line_regs[0x28/4];CHECK(ncmd<=512);
    for(unsigned k=0;k<ncmd*4;k++){
     volatile uint16_t *p=sh_memory(address+k*4);((uint32_t *)desc)[k]=p[0]|((uint32_t)p[1]<<16);
    }
    upload=ncmd*4;line_regs[0x78/4]=ncmd*16;
   }
   line_regs[0x54/4]=line_regs[0x74/4]=0;unsigned n=line_regs[0x18/4],w=line_regs[0x14/4]&65535,h=line_regs[0x14/4]>>16,dst=line_regs[0xc/4],stride=line_regs[0x10/4];
   CHECK(n<=512&&upload==n*4);if(n>largest_band)largest_band=n;
   for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
    uint32_t bg=line_regs[0x1c/4];
    *sh_memory(dst+y*stride+x*2)=(bg&0x80000000u)?texture[(bg&65535)+(((bg>>16)&511)+y)/8*64+x/10]:(bg&SL_DDR_BACKGROUND)?*sh_memory(line_regs[0x5c/4]+y*stride+x*2):(uint16_t)bg;
   }
   for(unsigned i=0;i<n;i++){
    sl_descriptor *d=desc+i;unsigned x0=d->xy&65535,y0=d->xy>>16,sw=d->wh&65535,sh=d->wh>>16,src=d->source_stride&8191,ss=(d->source_stride>>16)&16383;
    CHECK(x0+sw<=w&&y0+sh<=h&&src+(sh-1)*ss+sw<=line_regs[0x50/4]*2);
    int spans=(d->source_stride&SL_SPAN_ENABLE) && (d->effect&(1u<<24)) && (d->effect&65535)==line_regs[0x6c/4];
    unsigned base=sl_span_row(d);if(spans)CHECK(base+sh<=span_upload);
    for(unsigned y=0;y<sh;y++){
     unsigned start=0,end=sw;
     if(spans){unsigned e=row_spans[base+y],left=e&8191,right=e>>13,row_src=src+y*ss;
      if(left>=row_src+sw || right<=row_src){start=end=0;}
      else{if(left>row_src)start=left-row_src;if(right<row_src+sw)end=right-row_src;}
     }
     line_regs[0x74/4]+=sw-(end-start);line_regs[0x54/4]+=end-start;model_sprite_pixels+=end-start;
     for(unsigned x=start;x<end;x++){
      uint16_t fg=texture[src+y*ss+x];if((d->effect&(1u<<24))&&fg==(uint16_t)d->effect)continue;
      volatile uint16_t *p=sh_memory(dst+(y0+y)*stride+(x0+x)*2);
      *p=d->effect&(1u<<25)?reference_mix(fg,*p,(d->effect>>16)&255):fg;
     }
    }
   }
  }
  line_regs[1]=2;line_regs[0x4c/4]=line_regs[0x2c/4];return;
 }
 CHECK(off<sizeof(regs));if(off==4){regs[1]=0;return;}regs[off/4]=v;
 if(off!=8 || v!=1)return;
 unsigned op=regs[3],src=regs[4],dst=regs[5],ss=regs[6],ds=regs[7],w=regs[8],h=regs[9];
 CHECK(op<5);commands++;op_count[op]++;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  uint16_t fg=op?*sh_memory(src+y*ss+x*2):(uint16_t)regs[10];
  if((op==2||op==4) && fg==(uint16_t)regs[11])continue;
  volatile uint16_t *p=sh_memory(dst+y*ds+x*2);
  if(op>=3){
   uint16_t bg=*p;unsigned a=regs[12],out=0;
   const unsigned shifts[]={0,5,11},masks[]={31,63,31};
   for(unsigned k=0;k<3;k++){
    unsigned f=(fg>>shifts[k])&masks[k],b=(bg>>shifts[k])&masks[k];
    out|=((f*a+b*(255-a)+127)/255)<<shifts[k];
   }
   *p=(uint16_t)out;
  }else *p=fg;
 }
 regs[1]=2;regs[0x38/4]=regs[0x34/4];
}

#include "trim_cases.h"
#include "demo_cases.h"
