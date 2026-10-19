/* RGB565 Taisei art, ordered alpha/key composition, ping-pong DDR command banks.
 * Hardware is the verified ID6 engine; this file adds no FPGA resources. */
#include "taisei_render.h"
#include "taisei_rain.h"
#include "cirno_boss.h"
#include "stage1.h"
#include "cirno_tables.h"
#include "fs_font.h"
#include <string.h>
#ifndef SH_OFFLINE_TEST
#include "bsp.h"
static uint64_t stamp(void){return clint_getTime(BSP_CLINT);}
static uint32_t elapsed(uint64_t t){return (stamp()-t)*1000000u/BSP_CLINT_HZ;}
#else
static uint64_t stamp(void){return 0;}
static uint32_t elapsed(uint64_t t){(void)t;return 0;}
#endif
sh_render_stats sh_stats;
static unsigned owned;
static uint16_t bounds[TEX_COUNT][4];
static int16_t ring_x[64],ring_y[64];
static uint32_t cmd_addr(unsigned bank,unsigned band){return 0x700000u+bank*0x20000u+band*8192u;}
static sl_descriptor *bucket(unsigned bank,unsigned band){return (sl_descriptor *)(uintptr_t)sh_memory(cmd_addr(bank,band));}
int sh_assets_valid(void){
 const volatile uint32_t *h=(const volatile uint32_t *)(uintptr_t)sh_memory(GA_BASE);
 if(h[0]!=0x47415346u||h[1]!=1||h[6]!=GA_BYTES||h[7]!=GA_CRC)return 0;
 uint32_t crc=~0u;const volatile uint8_t *p=(const volatile uint8_t *)(uintptr_t)sh_memory(GA_BG);
 for(unsigned i=0;i<GA_BYTES;i++){crc^=p[i];for(unsigned k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
 return ~crc==GA_CRC;
}
int sh_assets_init(void){
 for(unsigned id=0;id<TEX_COUNT;id++){
  const ga_texture *t=ga_textures+id;unsigned x0=t->width,y0=t->height,x1=0,y1=0;
  for(unsigned y=0;y<t->height;y++)for(unsigned x=0;x<t->width;x++)if(sh_memory(GA_ATLAS)[t->offset+y*t->width+x]!=SH_KEY){
   if(x<x0)x0=x;if(y<y0)y0=y;if(x+1>x1)x1=x+1;if(y+1>y1)y1=y+1;
  }
  bounds[id][0]=x0;bounds[id][1]=y0;bounds[id][2]=x1;bounds[id][3]=y1;
 }
 for(unsigned i=0;i<64;i++){ring_x[i]=sine256[(i*4+64)&255]*29/65536;ring_y[i]=sine256[i*4]*34/65536;}
 for(unsigned i=0;i<192*480;i++)sh_memory(SH_HUD)[i]=sh_memory(GA_PANEL)[i];
 fs_cache_sync();return sl_load(GA_ATLAS,(GA_PIXELS+1)/2,16,0x11000001u);
}
void sh_snapshot(sh_ui_state *u,const sh_game *g){
 *u=(sh_ui_state){g->score,g->high_score,g->graze,g->point_count,g->power,g->hp,sh_count(g),g->route,g->stage?g->boss_phase:(g->route==3?1:0),
  (g->stage||(g->route>=2&&g->route<=3))?(g->phase_limit>g->phase_ticks?(g->phase_limit-g->phase_ticks+59)/60:0):(g->stage_ticks<2700?(2700-g->stage_ticks+59)/60:g->post_start>g->stage_ticks?(g->post_start-g->stage_ticks+59)/60:g->route==6?(2560-(g->stage_ticks-g->post_start)+59)/60:0),
  g->state,g->god,g->background,g->ticks};
}
int sh_commands_begin(sh_frame_commands *f,unsigned bank,const sh_game *g){
 if(!f||!g||bank>1)return FS_BAD_PARAM;if(owned&(1u<<bank))return FS_BUSY;
 *f=(sh_frame_commands){.bank=bank,.scroll=g->background?(g->ticks/4)%480:0};sh_snapshot(&f->ui,g);return FS_OK;
}
static int queue(sh_frame_commands *f,unsigned id,int x,int y,unsigned alpha){
 if(id>=TEX_COUNT)return FS_BAD_PARAM;const ga_texture *t=ga_textures+id;
 unsigned x0=bounds[id][0],y0=bounds[id][1];if(bounds[id][2]<=x0||bounds[id][3]<=y0)return FS_OK;
 unsigned w=bounds[id][2]-x0,h=bounds[id][3]-y0;
 if(!w||!h)return FS_OK;sl_descriptor d;
 int r=sl_sprite(&d,t->offset+y0*t->width+x0,t->width,w,h,x+(int)x0,y+(int)y0,SH_VIEW_W,SH_VIEW_H,alpha==255?2:4,SH_KEY,alpha);
 if(r<=0)return r<0?r:FS_OK;
 unsigned yy=d.xy>>16,hh=d.wh>>16,first=yy/SH_BAND_H,last=(yy+hh-1)/SH_BAND_H;
 for(unsigned band=first;band<=last;band++){
  if(band>=SH_BANDS||f->count[band]>=512)return FS_BAD_PARAM;
  sl_descriptor c=d;unsigned top=band*SH_BAND_H,start=yy<top?top:yy,end=yy+hh;
  if(end>top+SH_BAND_H)end=top+SH_BAND_H;
  c.source_stride+=(start-yy)*((d.source_stride>>16)&16383u);
  c.xy=((start-top)<<16)|(d.xy&65535u);c.wh=((end-start)<<16)|(d.wh&65535u);
  bucket(f->bank,band)[f->count[band]++]=c;
 }
 f->descriptors++;return FS_OK;
}
int sh_commands_append(sh_frame_commands *f,const sh_game *g,unsigned first,unsigned end){
 if(!f||!g||f->ready||end>SH_DRAW_ITEMS||first>end||(owned&(1u<<f->bank)))return FS_BAD_PARAM;
 uint64_t begin=stamp();
 unsigned ring_lit=first<64&&g->boss_max_hp?(uint64_t)g->boss_hp*64/g->boss_max_hp:0;
 for(unsigned i=first;i<end;i++){
  int x=0,y=0;unsigned tex=0,alpha=255;int draw=0;unsigned n=i;
  if(n<64){if(g->enemies[0].active&&g->boss_max_hp){x=g->enemies[0].x/SH_Q+16+ring_x[n];y=g->enemies[0].y/SH_Q+20+ring_y[n];tex=TEX_RING;
    alpha=ring_lit>n?255:48;draw=1;}}
  else if((n-=64)<SH_SHOTS*3){const sh_actor *a=g->shots+n/3;unsigned tail=2-n%3;
   if(a->active){x=(a->x-(int)tail*a->vx)/SH_Q+4;y=(a->y-(int)tail*a->vy)/SH_Q+8;
    tex=a->cooldown==2?TEX_NEEDLE:a->cooldown==3?TEX_SEAL:TEX_SHOT;alpha=tail==0?128:tail==1?80:32;draw=1;}}
  else if((n-=SH_SHOTS*3)<SH_BULLETS){const sh_bullet *a=g->bullets+n;
   if(a->active){unsigned kind=a->kind,size=kind==2||kind==ST_BIG?24:12;x=sh_bx(g,n)/SH_Q+size/2;y=sh_by(g,n)/SH_Q+size/2;
    tex=kind==2||kind==ST_BIG?TEX_ORB:kind==ST_RICE||kind==ST_MID_RICE?TEX_RICE:kind==ST_THICK?TEX_THICK:
      kind==3||kind==4||kind==ST_BALL||(kind>=ST_FROG&&kind<=ST_THAW)?TEX_PLAIN:TEX_CRYSTAL;
    if(kind==ST_FROZEN||kind==ST_THAW)tex=TEX_ICE;else if(kind==ST_FROG)tex=(a->pad>>4)==1?TEX_RED:(a->pad>>4)==2?TEX_GREEN:TEX_PLAIN;
    alpha=208;draw=1;}}
  else if((n-=SH_BULLETS)<SH_ENEMIES){const sh_actor *a=g->enemies+n;
   if(a->active){x=a->x/SH_Q+(n?12:16);y=a->y/SH_Q+(n?12:20);unsigned kind=g->em[n].kind;
    tex=!n?TEX_BOSS:kind==ST_SINE||kind==ST_DROP?TEX_SWIRL:kind==ST_TRI?TEX_SUPER_FAIRY:kind==ST_EXPLOSION?TEX_HUGE_FAIRY:
     kind==ST_CIRCLETOSS||kind==ST_INSTANT||kind==ST_WAVE?TEX_BIG_FAIRY:g->em[n].red?TEX_FAIRY_RED:TEX_FAIRY;
    alpha=n&&a->age<g->em[n].summon?96:255;draw=1;}}
  else if((n-=SH_ENEMIES)<1){if(g->hp){x=g->px/SH_Q+16;y=g->py/SH_Q+16;tex=g->ticks/8&1?TEX_PLAYER_ALT:TEX_PLAYER;alpha=g->invincible&&(g->ticks&4)?96:255;draw=1;}}
  else if((n-=1)<SH_ITEMS){const sh_actor *a=g->items+n;if(a->active){x=a->x/SH_Q;y=a->y/SH_Q;tex=a->hp?TEX_POINT:TEX_POWER;alpha=128;draw=1;}}
  else if((n-=SH_ITEMS)<SH_FX){const sh_actor *a=g->effects+n;if(a->active){x=a->x/SH_Q;y=a->y/SH_Q;tex=TEX_SNOW;alpha=255-a->age*7;draw=1;}}
  else if((n-=SH_FX)<16){
   unsigned rank=g->power/100;if(rank>4)rank=4;
   if(n==0&&g->hp){x=g->px/SH_Q+16;y=g->py/SH_Q+16;tex=TEX_HIT;draw=1;}
   else if(n<5&&g->hp&&n<=rank){int32_t xx,yy;sh_option_position(g,n-1,&xx,&yy);x=xx/SH_Q;y=yy/SH_Q;tex=TEX_OPTION;alpha=144;draw=1;}
   else if(n>=5&&g->background){x=(n*97+g->ticks/8)%SH_W;y=(n*127+g->ticks*(n%3+1)/4)%SH_H;tex=TEX_SNOW;alpha=64;draw=1;}
  }else {if(!g->stage||g->boss_phase!=CB_SNOW||g->transition||!g->cage_count||g->cage_clock<60||g->cage_clock>=300)break;n-=16;unsigned beam=n/46,k=n%46;int x1,y1,x2,y2,hot;
   if(cb_beam(g,beam,&x1,&y1,&x2,&y2,&hot)){
    int dx=x2-x1,dy=y2-y1,ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;unsigned steps=(ax>ay?ax:ay)/10+1;
    if(k<=steps){x=x1+(int)((int64_t)dx*k/steps);y=y1+(int)((int64_t)dy*k/steps);tex=TEX_CRYSTAL;alpha=hot?224:72;draw=1;}
   }
  }
  if(draw){x=x*SH_VIEW_W/SH_W-ga_textures[tex].width/2;y=y*SH_VIEW_H/SH_H-ga_textures[tex].height/2;int r=queue(f,tex,x,y,alpha);if(r)return r;}
 }
 f->list_us+=elapsed(begin);return FS_OK;
}
int sh_commands_finish(sh_frame_commands *f){if(!f||f->ready||(owned&(1u<<f->bank)))return FS_BAD_PARAM;
 __asm__ volatile("fence rw,rw":::"memory");f->ready=1;return FS_OK;}
static void label(unsigned x,unsigned y,const char *s,uint16_t color){
 volatile uint16_t *p=sh_memory(SH_HUD);
 while(*s&&x+10<=192){unsigned c=(unsigned char)*s++;if(c>=128)c='?';
  for(unsigned r=0;r<7;r++)for(unsigned k=0;k<5;k++)if(fs_font[c*8+r]&(1u<<(4-k)))
   for(unsigned yy=0;yy<2;yy++)for(unsigned xx=0;xx<2;xx++)p[(y+r*2+yy)*192+x+k*2+xx]=color;x+=12;
 }
}
static void num(char *out,unsigned n){char rev[12];unsigned j=0,k=0;do{rev[k++]='0'+n%10;n/=10;}while(n);while(k)out[j++]=rev[--k];out[j]=0;}
static uint32_t prev[14];static unsigned panel_ready[2],dirty[2],initialized,panel_tick;
int sh_render_panel(const sh_ui_state *u,uint32_t fb,unsigned fps,uint32_t *seq){
 uint64_t t=stamp();sh_stats=(sh_render_stats){0};unsigned bank=fb==SH_FB1;
 uint32_t values[14]={7,fps,u->count,u->high,u->score,u->hp,u->power,u->points,u->graze,u->stage,u->phase,u->seconds,u->state,u->god};
 const char *names[14]={"LUNATIC","FPS ","OBJECT ","HI ","SCORE ","LIFE ","POWER ","POINT ","GRAZE ","","PHASE ","TIME ","",""};
 unsigned refresh=!initialized||u->tick-panel_tick>=6||u->tick<panel_tick;
 if(refresh)panel_tick=u->tick;
 for(unsigned row=0;row<14;row++)if((!initialized||values[row]!=prev[row])&&(refresh||row==5||row>=12)){
  unsigned y=14+row*27;for(unsigned yy=y;yy<y+14;yy++)for(unsigned x=8;x<192;x++)sh_memory(SH_HUD)[yy*192+x]=sh_memory(GA_PANEL)[yy*192+x];
  label(8,y,names[row],row==0?0xfcbf:0x9fff);
  if(row==12)label(8,y,u->state==SH_OVER?"GAME OVER":u->state==SH_CLEAR?"STAGE CLEAR":u->state==SH_PAUSED?"PAUSED":"MODE FPGA",0xffff);
  else if(row==9)label(8,y,u->stage>=7?"BOSS CIRNO":u->stage>=1&&u->stage<=4?"MID BOSS":"STAGE 1",0xffff);
  else if(row==13)label(8,y,u->god?"INVINCIBLE":"R RESTART",u->god?0xffe0:0xffff);
  else if(row){char text[16];num(text,row==10?values[row]+1:values[row]);unsigned x=8+strlen(names[row])*12;
   if(row==1){num(text,fps/10);unsigned len=strlen(text);text[len]='.';text[len+1]='0'+fps%10;text[len+2]=0;}
   if(row==6){num(text,u->power/100);unsigned len=strlen(text);text[len]='.';text[len+1]='0'+u->power%100/10;text[len+2]='0'+u->power%10;text[len+3]=0;}
   label(x,y,text,0xffff);
  }
  dirty[0]|=1u<<row;dirty[1]|=1u<<row;prev[row]=values[row];
 }
 initialized=1;fs_batch_begin_write_through();fs_surface dst={fb,640,480,1280};fs_image src={SH_HUD,192,480,384};int r=0;
 if(!panel_ready[bank]){r=fs_draw_region(&dst,&src,0,0,192,480,448,0,1,0,255,++*seq);panel_ready[bank]=1;}
 else for(unsigned row=0;row<14&&!r;row++)if(dirty[bank]&(1u<<row))r=fs_draw_region(&dst,&src,8,14+row*27,184,14,456,14+row*27,1,0,255,++*seq);
 dirty[bank]=0;fs_batch_end_write_through();sh_stats.hud_us=elapsed(t);return r;
}
static int start_band(sh_frame_job *j,uint32_t *seq){
 const sh_frame_commands *c=j->frame;unsigned b=j->band;
 sl_frame f={j->fb+b*SH_BAND_H*1280,1280,SH_VIEW_W,SH_BAND_H,c->ui.background?SL_DDR_BACKGROUND:0,32,++*seq,GA_BG+(c->scroll+b*SH_BAND_H)*1280};
 uint64_t t=stamp();int r=sl_render(&f,bucket(c->bank,b),c->count[b]);sh_stats.submit_us+=elapsed(t);
 sh_stats.descriptors+=c->count[b];j->seq=f.seq;return r;
}
int sh_frame_start(sh_frame_job *j,const sh_frame_commands *c,uint32_t fb,uint32_t *seq){
 if(!j||!c||!seq||!c->ready||c->bank>1||(fb!=SH_FB0&&fb!=SH_FB1))return FS_BAD_PARAM;
 if(owned||(fs_read(0x84)&1))return FS_BUSY;*j=(sh_frame_job){c,fb,0,0,1};owned=1u<<c->bank;
 int r=start_band(j,seq);if(r){owned=0;j->active=0;}return r;
}
int sh_frame_poll(sh_frame_job *j,uint32_t *seq){
 if(!j||!seq||!j->active)return FS_BAD_PARAM;uint32_t s=fs_read(0x84);if(s&1)return 0;
 if(!(s&2)||(s&4)||fs_read(0xcc)!=j->seq)return FS_HW_ERROR;
 sh_stats.core_cycles+=fs_read(0xc0);sh_stats.read_bytes+=fs_read(0xc4);sh_stats.write_bytes+=fs_read(0xc8);
 sh_stats.pixels+=fs_read(0xd4);sh_stats.visits+=fs_read(0xd8);
 if(++j->band==SH_BANDS){j->active=0;owned=0;return 1;}int r=start_band(j,seq);return r?r:0;
}
static uint16_t blend(uint16_t a,uint16_t b,unsigned alpha){
 unsigned r=(((a>>11)&31)*alpha+((b>>11)&31)*(255-alpha)+127)/255;
 unsigned g=(((a>>5)&63)*alpha+((b>>5)&63)*(255-alpha)+127)/255;
 unsigned v=((a&31)*alpha+(b&31)*(255-alpha)+127)/255;return (r<<11)|(g<<5)|v;
}
int sh_reference(const sh_frame_commands *f,uint32_t fb){
 fs_cache_sync();
 for(unsigned b=0;b<SH_BANDS;b++){
  for(unsigned y=0;y<SH_BAND_H;y++)for(unsigned x=0;x<SH_VIEW_W;x++)sh_memory(fb)[(b*SH_BAND_H+y)*640+x]=
   f->ui.background?sh_memory(GA_BG)[(f->scroll+b*SH_BAND_H+y)*640+x]:0;
  for(unsigned i=0;i<f->count[b];i++){
   const sl_descriptor *d=bucket(f->bank,b)+i;unsigned src=d->source_stride&8191u,stride=d->source_stride>>16;
   unsigned x0=d->xy&65535u,y0=d->xy>>16,w=d->wh&65535u,h=d->wh>>16,alpha=(d->effect>>16)&255u,op=(d->effect&(1u<<25))?((d->effect&(1u<<24))?4:3):((d->effect&(1u<<24))?2:1);
   for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
    uint16_t fg=sh_memory(GA_ATLAS)[src+y*stride+x];if((op==2||op==4)&&fg==SH_KEY)continue;
    volatile uint16_t *p=sh_memory(fb)+((b*SH_BAND_H+y0+y)*640+x0+x);*p=op>=3?blend(fg,*p,alpha):fg;
   }
  }
 }
 fs_cache_sync();return FS_OK;
}
