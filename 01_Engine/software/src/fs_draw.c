#include "fs_draw.h"

static int valid_image(const fs_image *i){
 if(!i || !i->width || !i->height || i->width>4096 || i->height>4096 ||
    ((i->data|i->stride)&1) || i->stride<i->width*2 || i->stride>65536 ||
    i->data<FS_DMA_LO)return 0;
 return (uint64_t)i->data+(uint64_t)(i->height-1)*i->stride+i->width*2<=FS_DMA_HI;
}
int fs_draw_region(const fs_surface *t,const fs_image *i,
 uint32_t sx,uint32_t sy,uint32_t w,uint32_t h,int32_t x,int32_t y,
 uint32_t op,uint32_t key,uint32_t alpha,uint32_t seq){
 if(!valid_image(t) || !valid_image(i) || op<1 || op>4 || key>65535 || alpha>255 ||
    sx>i->width || sy>i->height || w>i->width-sx || h>i->height-sy)return FS_BAD_PARAM;
 if(!w || !h)return FS_OK;
 /* Widen before negation/addition, including INT32_MIN/MAX coordinates. */
 int64_t dx=x,dy=y;
 if(dx>=t->width || dy>=t->height || dx+(int64_t)w<=0 || dy+(int64_t)h<=0)return FS_OK;
 if(dx<0){uint32_t cut=(uint32_t)(-dx);sx+=cut;w-=cut;dx=0;}
 if(dy<0){uint32_t cut=(uint32_t)(-dy);sy+=cut;h-=cut;dy=0;}
 if(w>t->width-(uint32_t)dx)w=t->width-(uint32_t)dx;
 if(h>t->height-(uint32_t)dy)h=t->height-(uint32_t)dy;
 uint32_t src=i->data+sy*i->stride+sx*2;
 uint32_t dst=t->data+(uint32_t)dy*t->stride+(uint32_t)dx*2;
 const fs_blit_config config={op,w,h,i->stride,t->stride,key,alpha};
 int rc=fs_blit_prepare(&config);if(rc)return rc;
 rc=fs_blit_submit(src,dst,seq);if(rc)return rc;
 return fs_wait(seq,4000000);
}
int fs_draw_image(const fs_surface *t,const fs_image *i,int32_t x,int32_t y,
 uint32_t op,uint32_t key,uint32_t alpha,uint32_t seq){
 if(!i)return FS_BAD_PARAM;
 return fs_draw_region(t,i,0,0,i->width,i->height,x,y,op,key,alpha,seq);
}
int fs_image_preload(const fs_image *i,uint32_t slot,uint32_t seq){
 if(!valid_image(i) || slot>1)return FS_BAD_PARAM;
 uint32_t start=i->data&~3u;
 uint64_t end=((uint64_t)i->data+(uint64_t)(i->height-1)*i->stride+i->width*2+3)&~(uint64_t)3;
 if(end>FS_DMA_HI || end-start>4096)return FS_BAD_PARAM;
 return fs_texture_load(start,(uint32_t)(end-start)/4,slot,seq);
}
