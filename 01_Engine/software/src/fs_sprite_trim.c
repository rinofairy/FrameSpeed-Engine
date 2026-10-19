#include "fs_sprite_trim.h"
#include <limits.h>

int fs_trim_measure(const volatile uint16_t *pixels,uint32_t w,uint32_t h,
 uint32_t stride,uint32_t key,fs_trim_bounds *b){
 if(!pixels || !b || ((uintptr_t)pixels&1u) || !w || !h || w>4096 || h>4096 ||
    (stride&1u) || stride<w*2 || stride>65536 || key>65535)return -1;
 uint32_t left=w,top=h,right=0,bottom=0;
 for(uint32_t y=0;y<h;y++)for(uint32_t x=0;x<w;x++){
  if(pixels[y*(stride/2)+x]==key)continue;
  if(x<left)left=x;if(x>right)right=x;
  if(y<top)top=y;if(y>bottom)bottom=y;
 }
 if(left==w){*b=(fs_trim_bounds){0,0,0,0};return 0;}
 *b=(fs_trim_bounds){left,top,right-left+1,bottom-top+1};return 0;
}

int fs_trim_region(const fs_trim_bounds *b,uint32_t iw,uint32_t ih,
 uint32_t *sx,uint32_t *sy,uint32_t *w,uint32_t *h,int32_t *x,int32_t *y){
 if(!b || !sx || !sy || !w || !h || !x || !y || !iw || !ih || iw>4096 || ih>4096 ||
    b->left>iw || b->top>ih || b->width>iw-b->left || b->height>ih-b->top ||
    *sx>iw || *sy>ih || *w>iw-*sx || *h>ih-*sy)return -1;
 if(!*w || !*h || !b->width || !b->height){*w=*h=0;return 0;}
 uint32_t left=*sx>b->left?*sx:b->left,top=*sy>b->top?*sy:b->top;
 uint32_t right=*sx+*w,bottom=*sy+*h;
 if(right>b->left+b->width)right=b->left+b->width;
 if(bottom>b->top+b->height)bottom=b->top+b->height;
 if(left>=right || top>=bottom){*w=*h=0;return 0;}
 int64_t dx=(int64_t)*x+left-*sx,dy=(int64_t)*y+top-*sy;
 if(dx>INT32_MAX || dy>INT32_MAX)return 1;
 *sx=left;*sy=top;*w=right-left;*h=bottom-top;*x=(int32_t)dx;*y=(int32_t)dy;
 return 1;
}
