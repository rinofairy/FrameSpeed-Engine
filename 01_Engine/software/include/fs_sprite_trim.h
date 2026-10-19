#ifndef FS_SPRITE_TRIM_H
#define FS_SPRITE_TRIM_H
#include <stdint.h>

/* Source-local bounds of pixels NOT equal to the exact RGB565 color key.
 * width=height=0 denotes a completely transparent image. Rebuild after
 * modifying the source or color key. This metadata never changes game hitboxes.
 */
typedef struct { uint32_t left,top,width,height; } fs_trim_bounds;

/* Scan once, after CPU/DMA cache ownership has been synchronized by the caller.
 * stride is in bytes; dimensions/stride follow fs_image's RGB565 limits.
 * Returns 0 on success, -1 for invalid arguments. No bus/FPGA accesses here.
 */
int fs_trim_measure(const volatile uint16_t *pixels,uint32_t width,uint32_t height,
 uint32_t stride,uint32_t key,fs_trim_bounds *bounds);

/* Intersect an existing source crop with bounds and compensate destination
 * x/y by exactly the removed left/top margin. Original stride is unchanged.
 * Call ONLY for a color-key operation with matching immutable metadata.
 * Returns 1 for a remaining region, 0 for empty, -1 for invalid arguments.
 * On an unrepresentable compensated int32 coordinate, leave inputs unchanged;
 * the ordinary destination clipper will reject the offscreen region safely.
 */
int fs_trim_region(const fs_trim_bounds *bounds,uint32_t image_width,uint32_t image_height,
 uint32_t *sx,uint32_t *sy,uint32_t *width,uint32_t *height,int32_t *x,int32_t *y);
#endif
