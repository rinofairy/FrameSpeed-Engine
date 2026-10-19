#include "fs_blitter.h"
static int batch_owned,sprite_prepared;
static fs_blit_config prepared;
static uint32_t source_span,destination_span;
void fs_batch_begin(void){fs_cache_sync();batch_owned=1;sprite_prepared=0;}
void fs_batch_end(void){fs_cache_sync();batch_owned=0;sprite_prepared=0;}
static void memory_fence(void){
#ifndef FS_DRIVER_TEST
 __asm__ volatile("fence rw,rw" ::: "memory");
#endif
}
void fs_batch_begin_write_through(void){memory_fence();batch_owned=1;sprite_prepared=0;}
void fs_batch_end_write_through(void){memory_fence();batch_owned=0;sprite_prepared=0;}
static void wr(uint32_t off,uint32_t x) {
#ifdef FS_DRIVER_TEST
 fs_test_write(off,x);
#else
 *(volatile uint32_t *)(FS_BASE+off)=x;
#endif
}
static int extent(uint32_t a,uint32_t s,uint32_t w,uint32_t h,uint64_t *end) {
 if((a|s)&1u || s<w*2u || s>65536u || a<FS_DMA_LO) return 0;
 *end=(uint64_t)a+(uint64_t)(h-1)*s+2u*w;
 return *end<=FS_DMA_HI;
}
int fs_submit(const fs_command *c) {
 uint64_t se=0,de=0;
 if(!c || c->op>4 || (c->op>=3 && c->alpha>255) || c->width>4096 || c->height>4096) return FS_BAD_PARAM;
 if(fs_read(0)!=FS_ID) return FS_BAD_ID;
 if(fs_read(4)&1) return FS_BUSY;
 if(c->width && c->height) {
  if(!extent(c->dst,c->dst_stride,c->width,c->height,&de)) return FS_BAD_PARAM;
  if(c->op!=0 && (!extent(c->src,c->src_stride,c->width,c->height,&se) ||
     ((uint64_t)c->src<de && (uint64_t)c->dst<se))) return FS_BAD_PARAM;
 }
 if(!batch_owned)fs_cache_sync();
 sprite_prepared=0;
 wr(4,6);wr(0x0c,c->op);wr(0x10,c->src);wr(0x14,c->dst);
 wr(0x18,c->src_stride);wr(0x1c,c->dst_stride);wr(0x20,c->width);wr(0x24,c->height);
 wr(0x30,c->alpha);wr(0x2c,c->key);wr(0x28,c->color);wr(0x34,c->seq);
 __asm__ volatile("fence rw,rw" ::: "memory");wr(8,1);
 return FS_OK;
}
int fs_wait(uint32_t seq,uint32_t poll_budget) {
 while(poll_budget--) {
  uint32_t s=fs_read(4);
  if(!(s&1) && (s&2) && fs_read(0x38)==seq) {
   if(!batch_owned)fs_cache_sync(); return s&4 ? FS_HW_ERROR : FS_OK;
  }
 }
 /* No unsafe abort: timeout does not cancel an in-flight AXI transaction. */
 return FS_TIMEOUT;
}

void fs_texture_control(uint32_t enable,uint32_t invalidate){
 wr(0x54,(enable?1u:0u)|(invalidate?2u:0u));
}
int fs_texture_load(uint32_t src,uint32_t words,uint32_t slot,uint32_t seq){
 if(fs_read(0)!=FS_ID)return FS_BAD_ID;
 if(fs_read(4)&1)return FS_BUSY;
 if(slot>1 || !words || words>1024 || (src&3) || src<FS_DMA_LO ||
    (uint64_t)src+words*4u>FS_DMA_HI)return FS_BAD_PARAM;
 sprite_prepared=0;fs_cache_sync();wr(4,6);wr(0x0c,5);wr(0x10,src);wr(0x20,words);wr(0x24,1);
 wr(0x28,slot);wr(0x34,seq);__asm__ volatile("fence rw,rw" ::: "memory");wr(8,1);
 return fs_wait(seq,4000000);
}

/* Store extents once per geometry; only addresses change per submission.
 * Hardware remains authoritative and validates every command too.
 */
int fs_blit_prepare(const fs_blit_config *c){
 uint64_t ss=0,ds=0;
 if(!batch_owned || !c || c->op<1 || c->op>4 || c->width>4096 ||
    c->height>4096 || c->key>65535 || c->alpha>255){sprite_prepared=0;return FS_BAD_PARAM;}
 if(c->width && c->height){
  if(((c->src_stride|c->dst_stride)&1) || c->src_stride<c->width*2 ||
     c->dst_stride<c->width*2 || c->src_stride>65536 || c->dst_stride>65536){
   sprite_prepared=0;return FS_BAD_PARAM;
  }
  ss=(uint64_t)(c->height-1)*c->src_stride+c->width*2;
  ds=(uint64_t)(c->height-1)*c->dst_stride+c->width*2;
  if(ss>FS_DMA_HI-FS_DMA_LO || ds>FS_DMA_HI-FS_DMA_LO){sprite_prepared=0;return FS_BAD_PARAM;}
 }
 if(fs_read(0)!=FS_ID){sprite_prepared=0;return FS_BAD_ID;}
 if(fs_read(4)&1)return FS_BUSY;
 /* Successive high-level draws with equal geometry avoid redundant APB writes. */
 if(sprite_prepared && prepared.op==c->op && prepared.width==c->width &&
    prepared.height==c->height && prepared.src_stride==c->src_stride &&
    prepared.dst_stride==c->dst_stride && prepared.key==c->key && prepared.alpha==c->alpha)return FS_OK;
 sprite_prepared=0;
 wr(0x0c,c->op);wr(0x18,c->src_stride);wr(0x1c,c->dst_stride);
 wr(0x20,c->width);wr(0x24,c->height);wr(0x28,0);wr(0x2c,c->key);wr(0x30,c->alpha);
 prepared=*c;source_span=(uint32_t)ss;destination_span=(uint32_t)ds;
 sprite_prepared=1;return FS_OK;
}
int fs_blit_submit(uint32_t src,uint32_t dst,uint32_t seq){
 if(!batch_owned || !sprite_prepared)return FS_BAD_PARAM;
 if(source_span && (((src|dst)&1) || src<FS_DMA_LO || dst<FS_DMA_LO ||
    src>FS_DMA_HI-source_span || dst>FS_DMA_HI-destination_span ||
    (src<dst+destination_span && dst<src+source_span)))return FS_BAD_PARAM;
 if(fs_read(4)&1)return FS_BUSY;
 wr(4,6);wr(0x10,src);wr(0x14,dst);wr(0x34,seq);
 __asm__ volatile("fence rw,rw" ::: "memory");wr(8,1);return FS_OK;
}
/* Compatibility only. New applications should supply their own geometry. */
int fs_sprite_prepare(uint32_t op,uint32_t key,uint32_t alpha){
 const fs_blit_config c={op,32,32,64,1280,key,alpha};return fs_blit_prepare(&c);
}
int fs_sprite_submit(uint32_t src,uint32_t dst,uint32_t seq){return fs_blit_submit(src,dst,seq);}
