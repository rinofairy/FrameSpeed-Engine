/* Board-generated start screen; no host drawing or image dependencies. */
#include "suite_ui.h"
#include "fs_font.h"
static void rect(volatile uint16_t *p,int x,int y,int w,int h,uint16_t c){for(int yy=y;yy<y+h;yy++)for(int xx=x;xx<x+w;xx++)if(xx>=0&&xx<640&&yy>=0&&yy<480)p[yy*640+xx]=c;}
static void text(volatile uint16_t *p,int x,int y,const char *s,unsigned z,uint16_t c){
 while(*s){unsigned ch=(unsigned char)*s++;if(ch>=128)ch='?';for(unsigned r=0;r<7;r++)for(unsigned k=0;k<5;k++)if(fs_font[ch*8+r]&(1u<<(4-k)))rect(p,x+k*z,y+r*z,z,z,c);x+=6*z;}
}
void suite_menu(volatile uint16_t *p,unsigned selected,int available){
 (void)available;
 for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)p[y*640+x]=0x0843;
 for(unsigned i=0;i<2;i++){
  int y=145+i*119;uint16_t border=selected==i?0x07ff:0x3187;
  rect(p,44,y,552,100,border);rect(p,48,y+4,544,92,selected==i?0x1949:0x10a5);
  text(p,305,y+29,i?"2":"1",6,0xffff);
 }
}
