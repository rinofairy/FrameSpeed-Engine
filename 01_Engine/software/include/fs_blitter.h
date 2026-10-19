/* FrameSpeed v0.1 register driver, authored with AI assistance. */
#ifndef FS_BLITTER_H
#define FS_BLITTER_H
#include <stdint.h>
#define FS_BASE 0xf8100000u
#define FS_ID 0x46530107u
#define FS_DMA_LO 0x00400000u
#define FS_DMA_HI 0x00c00000u
enum { FS_OK=0, FS_BUSY=-1, FS_BAD_PARAM=-2, FS_TIMEOUT=-3, FS_HW_ERROR=-4, FS_BAD_ID=-5 };
typedef struct {
 uint32_t op,src,dst,src_stride,dst_stride,width,height,color,seq;
 uint32_t key;
 uint32_t alpha; // 0..255; used only for op 3/4
 /* Fields appended so existing Fill/Copy initializers default to zero. */
} fs_command;
#ifdef FS_DRIVER_TEST
/* Offline test transport only; never defined in the board build. */
uint32_t fs_test_read(uint32_t off);
void fs_test_write(uint32_t off,uint32_t value);
void fs_test_sync(void);
static inline uint32_t fs_read(uint32_t off) { return fs_test_read(off); }
#else
static inline uint32_t fs_read(uint32_t off) { return *(volatile uint32_t *)(FS_BASE+off); }
#endif
/* CPU must not access DMA-owned buffers between these calls; all commands
 * must complete before end. This is ownership batching, not a hardware queue. */
void fs_batch_begin(void);
void fs_batch_end(void);
/* Sapphire RV32 write-through cache only. Sources/HUD are CPU-owned and
 * immutable while DMA reads them; CPU must never read DMA destinations in
 * this batch. Fence orders stores before APB launch. Before CPU reads any
 * DMA output, explicitly fs_cache_sync(). Generic batches above stay safe.
 * Do not use with a write-back CPU or external writers to source buffers. */
void fs_batch_begin_write_through(void);
void fs_batch_end_write_through(void);
int fs_submit(const fs_command *c);
int fs_wait(uint32_t seq,uint32_t poll_budget);
/* Before submit: caller transfers ownership of source/destination to DMA.
 * On completion: invalidate CPU cache before reading hardware-written pixels.
 * These helpers match the CURRENT Sapphire core only. No generic CSR 0x810.
 */
static inline void fs_cache_sync(void) {
#ifdef FS_DRIVER_TEST
 fs_test_sync();
#else
 __asm__ volatile("fence rw,rw\n .word 0x0000500f\n fence rw,rw" ::: "memory");
#endif
}
/* Explicit texture cache; CPU writes require invalidate then preload. */
int fs_texture_load(uint32_t src,uint32_t words,uint32_t slot,uint32_t seq);
void fs_texture_control(uint32_t enable,uint32_t invalidate);
int fs_sprite_prepare(uint32_t op,uint32_t key,uint32_t alpha);
int fs_sprite_submit(uint32_t src,uint32_t dst,uint32_t seq);
/* Reusable geometry, not tied to any screen or sprite size. RGB565 only.
 * prepare/submit require an exclusive batch; wait before changing parameters.
 * Zero width/height submits a no-op. Strides are bytes, not pixels.
 */
typedef struct {
 uint32_t op,width,height,src_stride,dst_stride,key,alpha;
} fs_blit_config;
int fs_blit_prepare(const fs_blit_config *config);
int fs_blit_submit(uint32_t src,uint32_t dst,uint32_t seq);
#endif
