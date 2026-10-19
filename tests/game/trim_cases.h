#include "fs_sprite_trim.h"
#include <limits.h>
static uint16_t trim_source[32*20];
static uint32_t trim_seed=0x17001026u,trim_cases_count;
static uint32_t trim_random(void){trim_seed=trim_seed*1664525u+1013904223u;return trim_seed;}
static uint16_t crop_sample(unsigned stride,unsigned sx,unsigned sy,unsigned w,unsigned h,
 int32_t x,int32_t y,int dx,int dy,unsigned op,unsigned a){
 uint16_t bg=(uint16_t)(0x0241u+dx*73u+dy*211u);
 int64_t px=(int64_t)dx-x,py=(int64_t)dy-y;
 if(px<0 || py<0 || px>=w || py>=h)return bg;
 uint16_t fg=trim_source[(sy+(unsigned)py)*stride+sx+(unsigned)px];
 if((op==2 || op==4) && fg==SH_KEY)return bg;
 return op>=3?reference_mix(fg,bg,a):fg;
}
static void trim_unit_cases(void){
 fs_trim_bounds b;
 CHECK(fs_trim_measure(0,1,1,2,SH_KEY,&b)<0);
 CHECK(fs_trim_measure(trim_source,0,1,2,SH_KEY,&b)<0);
 CHECK(fs_trim_measure(trim_source,2,1,2,SH_KEY,&b)<0);
 CHECK(fs_trim_measure(trim_source,1,1,3,SH_KEY,&b)<0);
 CHECK(fs_trim_measure(trim_source,1,1,2,65536,&b)<0);
 for(unsigned trial=0;trial<320;trial++){
  unsigned iw=1+trim_random()%19,ih=1+trim_random()%17,ss=iw+3;
  for(unsigned y=0;y<ih;y++)for(unsigned x=0;x<ss;x++){
   uint16_t c=(uint16_t)trim_random();
   trim_source[y*ss+x]=x>=iw?0xffff:(trial%4==0 || (trial%4==2 && (x<2 || y<2)) || (c&3)?SH_KEY:(uint16_t)(c&0x7fff));
  }
  unsigned left=iw,top=ih,right=0,bottom=0,nonkey=0;
  for(unsigned y=0;y<ih;y++)for(unsigned x=0;x<iw;x++)if(trim_source[y*ss+x]!=SH_KEY){
   nonkey++;if(x<left)left=x;if(x>right)right=x;if(y<top)top=y;if(y>bottom)bottom=y;
  }
  CHECK(!fs_trim_measure(trim_source,iw,ih,ss*2,SH_KEY,&b));
  if(nonkey)CHECK(b.left==left&&b.top==top&&b.width==right-left+1&&b.height==bottom-top+1);
  else CHECK(!b.width&&!b.height);
  for(unsigned crop=0;crop<12;crop++){
   unsigned sx=trim_random()%(iw+1),sy=trim_random()%(ih+1);
   unsigned w=trim_random()%(iw-sx+1),h=trim_random()%(ih-sy+1);
   if(!crop){sx=sy=0;w=iw;h=ih;}
   int32_t x=(int32_t)(trim_random()%35)-16,y=(int32_t)(trim_random()%31)-14;
   if(crop==9)x=INT32_MAX;if(crop==10)x=INT32_MIN;if(crop==11)y=INT32_MAX;
   for(unsigned op=1;op<=4;op++){
    uint32_t tx=sx,ty=sy,tw=w,th=h;unsigned a=(crop*23)%256;int32_t dx=x,dy=y;
    int r=1;if(op==2||op==4)r=fs_trim_region(&b,iw,ih,&tx,&ty,&tw,&th,&dx,&dy);
    CHECK(r>=0);
    for(int py=0;py<18;py++)for(int px=0;px<24;px++)
     CHECK(crop_sample(ss,sx,sy,w,h,x,y,px,py,op,a)==crop_sample(ss,tx,ty,tw,th,dx,dy,px,py,op,a));
    trim_cases_count++;
   }
  }
 }
 b=(fs_trim_bounds){3,2,5,8};uint32_t sx=0,sy=0,w=12,h=12;int32_t x=100,y=200;
 CHECK(fs_trim_region(&b,12,12,&sx,&sy,&w,&h,&x,&y)==1);
 CHECK(sx==3&&sy==2&&w==5&&h==8&&x==103&&y==202);
 sx=12;w=1;CHECK(fs_trim_region(&b,12,12,&sx,&sy,&w,&h,&x,&y)<0);
}
static void trim_asset_cases(void){
 static const unsigned expected[][4]={{3,2,5,8},{2,2,20,20},{1,1,10,10}};
 for(unsigned k=0;k<3;k++){
  const fs_image *im=sh_textures+SH_RAIN_TEX+k;fs_trim_bounds b;
  CHECK(!fs_trim_measure(sh_memory(im->data),im->width,im->height,im->stride,SH_KEY,&b));
  CHECK(b.left==expected[k][0]&&b.top==expected[k][1]&&b.width==expected[k][2]&&b.height==expected[k][3]);
 }
}
