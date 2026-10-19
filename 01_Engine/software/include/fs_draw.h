#ifndef FS_DRAW_H
#define FS_DRAW_H
#include "fs_blitter.h"
/* Pixel data already resident in DMA DDR, not a PNG/JPEG file.
 * Nonempty dimensions <=4096; even stride >= width*2 and <=65536.
 * data is a 2-byte-aligned byte address; stride is in bytes.
 */
typedef struct { uint32_t data,width,height,stride; } fs_image;
typedef fs_image fs_surface;

/* Submit and wait synchronously, within fs_batch_begin/end.
 * x/y may be negative. Clip destination edges and shift the source crop.
 * Source crop must be inside image. Fully offscreen/empty crops succeed
 * without a command: seq then has no hardware completion to wait on.
 * op=1 copy, 2 color key, 3 alpha, 4 color key+alpha. No scaling/rotation.
 */
int fs_draw_region(const fs_surface *target,const fs_image *image,
 uint32_t sx,uint32_t sy,uint32_t width,uint32_t height,
 int32_t x,int32_t y,uint32_t op,uint32_t key,uint32_t alpha,uint32_t seq);
int fs_draw_image(const fs_surface *target,const fs_image *image,
 int32_t x,int32_t y,uint32_t op,uint32_t key,uint32_t alpha,uint32_t seq);
/* Optional whole-image preload into one 4KiB slot (0/1).
 * Includes stride padding and 4-byte alignment padding. If too large,
 * return FS_BAD_PARAM; draw still works directly from DDR without preload.
 * Call before the drawing batch, after CPU finishes writing the pixels.
 */
int fs_image_preload(const fs_image *image,uint32_t slot,uint32_t seq);
#endif
