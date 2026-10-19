/* AI-assisted experimental frame batch driver. Source pixels RGB565. */
#ifndef FS_SCANLINE_H
#define FS_SCANLINE_H
#include "fs_blitter.h"
#define SL_ID 0x46534c06u
#define SL_SPAN_ROWS 256u
#define SL_SPAN_ENABLE 0x8000u
#define SL_CACHED_BACKGROUND(pixel_offset,output_y) (0x80000000u|((uint32_t)(output_y)<<16)|(pixel_offset))
#define SL_DDR_BACKGROUND 0x40000000u
#define SL_MAX_SPRITES 512u
#define SL_TEXTURE_PIXELS 8192u
typedef struct {uint32_t source_stride,xy,wh,effect;} sl_descriptor;
typedef struct {uint32_t dst,stride,width,height,color,burst,seq,bg_source;} sl_frame;
int sl_load(uint32_t source,uint32_t words,uint32_t burst,uint32_t seq);
/* Shared atlas-absolute [first,end) entries. Once per immutable atlas/key.
 * Loading a new atlas invalidates the table. Entries are first|(end<<13).
 */
int sl_load_spans(const uint32_t *rows,uint32_t count,uint32_t key);
uint32_t sl_span_row(const sl_descriptor *d);
int sl_set_span_row(sl_descriptor *d,uint32_t row);
/* Attach a table row AFTER clipping. source is the original requested crop
 * offset and row is its first row's metadata. No command count increase. */
int sl_attach_spans(sl_descriptor *d,uint32_t source,uint32_t row);
/* Clips in CPU once, then submits complete frame with one start/completion.
 * Texture is immutable for the whole batch; no framebuffer feedback textures.
 * Width/height of a source image need not be 32 or powers of two.
 */
int sl_sprite(sl_descriptor *out,uint32_t source_pixel,uint32_t source_stride,
              uint32_t width,uint32_t height,int x,int y,
              uint32_t canvas_width,uint32_t canvas_height,
              uint32_t op,uint32_t key,uint32_t alpha);
int sl_render(const sl_frame *frame,const sl_descriptor *commands,uint32_t count);
/* Identical validation/composition to sl_render, but reads the immutable list
 * through AXI DMA. dma_address maps to commands, count*16 bytes. On this
 * Sapphire write-through cache a fence publishes stores; on a write-back
 * system the caller must clean the source before calling. Keep it unchanged
 * until sl_wait completes. This first version does not overlap CPU work.
 */
int sl_render_dma(const sl_frame *frame,const sl_descriptor *commands,uint32_t count,uint32_t dma_address);
int sl_wait(uint32_t seq,uint32_t poll_budget);
/* Idle-only compositor/writeback overlap control; both modes use two rows.
 * 0 serializes row composition/writeback; 1 overlaps distinct row banks. */
int sl_line_overlap(int enabled);
#endif
