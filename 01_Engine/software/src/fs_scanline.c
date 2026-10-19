#include "fs_scanline.h"
static void wr(uint32_t off,uint32_t value){
#ifdef FS_DRIVER_TEST
 fs_test_write(off,value);
#else
 *(volatile uint32_t *)(FS_BASE+off)=value;
#endif
}
static int idle(void){
 if(fs_read(0x80)!=SL_ID)return FS_BAD_ID;
 return ((fs_read(0x84)|fs_read(4))&1)?FS_BUSY:FS_OK;
}
int sl_line_overlap(int enabled){
 int r=idle();if(r)return r;
 if((fs_read(0xf0)&0xffffff00u)!=0x00040100u)return FS_BAD_ID;
 wr(0xf0,enabled!=0);return (fs_read(0xf0)&1u)==(unsigned)(enabled!=0)?FS_OK:FS_HW_ERROR;
}
int sl_wait(uint32_t seq,uint32_t budget){
 while(budget--){uint32_t s=fs_read(0x84);
  if(!(s&1) && (s&2) && fs_read(0xcc)==seq)return (s&4)?FS_HW_ERROR:FS_OK;
 }return FS_TIMEOUT; /* does not abort outstanding DMA */
}
int sl_load(uint32_t source,uint32_t words,uint32_t burst,uint32_t seq){
 int r=idle();if(r)return r;
 if((source&3)||source<FS_DMA_LO||!words||words>4096||!burst||burst>32||
    (uint64_t)source+words*4u>FS_DMA_HI)return FS_BAD_PARAM;
 fs_cache_sync();wr(0xa0,source);wr(0xa4,words);wr(0xb8,burst);wr(0xac,seq);
 __asm__ volatile("fence rw,rw" ::: "memory");wr(0x88,2);
 return sl_wait(seq,4000000);
}
uint32_t sl_span_row(const sl_descriptor *d){return (d->effect>>26)|((d->source_stride>>30)<<6);}
int sl_set_span_row(sl_descriptor *d,uint32_t row){
 if(!d || row>=SL_SPAN_ROWS)return FS_BAD_PARAM;
 d->source_stride=(d->source_stride&0x3fff1fffu)|SL_SPAN_ENABLE|((row>>6)<<30);
 d->effect=(d->effect&0x03ffffffu)|((row&63u)<<26);return FS_OK;
}
int sl_attach_spans(sl_descriptor *d,uint32_t source,uint32_t row){
 if(!d || (d->source_stride&65535u)<source)return FS_BAD_PARAM;
 uint32_t stride=d->source_stride>>16;if(!stride)return FS_BAD_PARAM;
 return sl_set_span_row(d,row+((d->source_stride&65535u)-source)/stride);
}
int sl_load_spans(const uint32_t *rows,uint32_t count,uint32_t key){
 int r=idle();if(r)return r;
 if(!rows || !count || count>SL_SPAN_ROWS || key>65535)return FS_BAD_PARAM;
 uint32_t pixels=fs_read(0xd0)*2;
 for(uint32_t i=0;i<count;i++)if((rows[i]>>27) || (rows[i]&8191u)>(rows[i]>>13) || (rows[i]>>13)>pixels)return FS_BAD_PARAM;
 wr(0xe0,0);wr(0xec,key);for(uint32_t i=0;i<count;i++)wr(0xe4,rows[i]);
 __asm__ volatile("fence rw,rw" ::: "memory");
 return fs_read(0xe8)==count?FS_OK:FS_HW_ERROR;
}
int sl_sprite(sl_descriptor *d,uint32_t src,uint32_t stride,uint32_t w,uint32_t h,
 int x,int y,uint32_t cw,uint32_t ch,uint32_t op,uint32_t key,uint32_t alpha){
 if(!d||!cw||cw>640||!ch||ch>480||w>4096||h>4096||stride<w||stride>8192||
    op<1||op>4||key>65535||alpha>255)return FS_BAD_PARAM;
 if(!w||!h)return 0;
 /* Bounds are checked before signed additions: extreme int inputs are safe. */
 if(x>=(int)cw||y>=(int)ch||(int64_t)x+w<=0||(int64_t)y+h<=0)return 0;
 uint64_t start=src;
 if(x<0){uint32_t n=(uint32_t)(-(int64_t)x);start+=n;w-=n;x=0;}
 if(y<0){uint32_t n=(uint32_t)(-(int64_t)y);start+=(uint64_t)n*stride;h-=n;y=0;}
 if(w>cw-(uint32_t)x)w=cw-(uint32_t)x;
 if(h>ch-(uint32_t)y)h=ch-(uint32_t)y;
 if(start+(uint64_t)(h-1)*stride+w>SL_TEXTURE_PIXELS)return FS_BAD_PARAM;
 d->source_stride=(stride<<16)|(uint32_t)start;
 d->xy=((uint32_t)y<<16)|(uint32_t)x;d->wh=(h<<16)|w;
 d->effect=key|(alpha<<16)|((op==2||op==4)?1u<<24:0)|((op>=3)?1u<<25:0);
 return 1;
}
static int render(const sl_frame *f,const sl_descriptor *d,uint32_t n,int dma,uint32_t dma_address){
 int r=idle();if(r)return r;
 if(!f||(!d&&n)||n>512||!f->width||f->width>640||(f->width&1)||
    !f->height||f->height>480||(f->dst&3)||(f->stride&3)||
    f->stride<f->width*2||f->stride>65532||f->dst<FS_DMA_LO||
    (uint64_t)f->dst+(uint64_t)(f->height-1)*f->stride+f->width*2>FS_DMA_HI||
    !f->burst||f->burst>32)return FS_BAD_PARAM;
 if(dma && n && ((dma_address&3u) || dma_address<FS_DMA_LO ||
    (uint64_t)dma_address+(uint64_t)n*16u>FS_DMA_HI))return FS_BAD_PARAM;
#ifndef FS_DRIVER_TEST
 if(dma && n && (uint32_t)(uintptr_t)d!=dma_address)return FS_BAD_PARAM;
#endif
 uint32_t cached_pixels=fs_read(0xd0)*2,span_rows=fs_read(0xe8),span_key=fs_read(0xec);
 if((f->color&0x80000000u) && ((f->color&0x7e00e001u) ||
    ((f->color>>16)&511)+f->height>480 ||
    (f->color&65535)+(((((f->color>>16)&511)+f->height+7)/8)*64)>cached_pixels))return FS_BAD_PARAM;
 if(f->color&SL_DDR_BACKGROUND){
  uint64_t size=(uint64_t)(f->height-1)*f->stride+f->width*2;
  if(f->color!=SL_DDR_BACKGROUND || (f->bg_source&3) || f->bg_source<FS_DMA_LO ||
     (uint64_t)f->bg_source+size>FS_DMA_HI ||
     ((uint64_t)f->bg_source<(uint64_t)f->dst+size && (uint64_t)f->dst<(uint64_t)f->bg_source+size))return FS_BAD_PARAM;
 }
 for(uint32_t i=0;i<n;i++){
  uint32_t w=d[i].wh&65535,h=d[i].wh>>16,x=d[i].xy&65535,y=d[i].xy>>16;
  uint32_t s=d[i].source_stride&8191u,st=(d[i].source_stride>>16)&16383u;
  int spans=(d[i].source_stride&SL_SPAN_ENABLE)!=0;
  if(!w||!h||w>f->width||h>f->height||x+w>f->width||y+h>f->height||
     st<w||st>8192||(d[i].source_stride&0x6000u)||
     (!spans&&((d[i].effect>>26)||(d[i].source_stride>>30)))||
     (uint64_t)s+(uint64_t)(h-1)*st+w>cached_pixels)return FS_BAD_PARAM;
  if(spans && (d[i].effect&(1u<<24)) && (d[i].effect&65535u)==span_key &&
    sl_span_row(d+i)+h>span_rows)return FS_BAD_PARAM;
 }
 if(!dma)wr(0xb0,0);
 if(f->color&SL_DDR_BACKGROUND){
  uint64_t size=(uint64_t)(f->height-1)*f->stride+f->width*2;
  if(f->color!=SL_DDR_BACKGROUND || (f->bg_source&3) || f->bg_source<FS_DMA_LO ||
     (uint64_t)f->bg_source+size>FS_DMA_HI ||
     ((uint64_t)f->bg_source<(uint64_t)f->dst+size && (uint64_t)f->dst<(uint64_t)f->bg_source+size))return FS_BAD_PARAM;
 }
 if(dma)wr(0xa8,dma_address);
 else for(uint32_t i=0;i<n;i++){
  wr(0xb4,d[i].source_stride);wr(0xb4,d[i].xy);wr(0xb4,d[i].wh);wr(0xb4,d[i].effect);
 }
 wr(0x8c,f->dst);wr(0x90,f->stride);wr(0x94,(f->height<<16)|f->width);
 wr(0x98,n);wr(0x9c,f->color);wr(0xdc,f->bg_source);wr(0xb8,f->burst);wr(0xac,f->seq);
 __asm__ volatile("fence rw,rw" ::: "memory");wr(0x88,dma?3:1);return FS_OK;
}
int sl_render(const sl_frame *f,const sl_descriptor *d,uint32_t n){return render(f,d,n,0,0);}
int sl_render_dma(const sl_frame *f,const sl_descriptor *d,uint32_t n,uint32_t address){return render(f,d,n,1,address);}
