/* Crystal Rain Easy/Normal/Hard/Lunatic, adapted from Taisei Project, commit
 * f4d3342226e915f369db8680688723adb18e4efa.
 * Copyright (c) 2011-2026 Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2026 Andrei Alexeyev <akari@taisei-project.org>.
 * MIT License: see ../upstream/taisei/COPYING.txt.
 * AI-assisted bare-metal adaptation: integer state machine replaces coroutines.
 * Preserved: rain cadence/acceleration, aimed pair angles/speed/retention,
 * charge and attack timing. Adapted: viewport, RNG, boss wander interpolation,
 * art, collision and 60-second survival completion. No claim of full-game port.
 */
#include "taisei_rain.h"
#include "cirno_boss.h"
#include "sh_fast_math.h"
#include <string.h>
#define Q16 65536
static uint32_t random32(sh_game *g){g->rng=g->rng*1664525u+1013904223u;return g->rng;}
static uint32_t root(uint64_t n){
 uint64_t r=0,b=(uint64_t)1<<62;
 while(b>n)b>>=2;
 while(b){if(n>=r+b){n-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}
 return (uint32_t)r;
}
/* Unit vector Q16. Used at spawn only, not for every bullet every tick. */
static void aim(int dx,int dy,int *x,int *y){
 uint32_t d=root((uint64_t)((int64_t)dx*dx+(int64_t)dy*dy));
 if(!d){*x=0;*y=Q16;return;}
 *x=(int)((int64_t)dx*Q16/d);*y=(int)((int64_t)dy*Q16/d);
}
static void spawn(sh_game *g,int x,int y,int ux,int uy,unsigned kind){
 unsigned i=cb_alloc_bullet(g);
 if(i==SH_BULLETS){g->dropped_spawns++;return;}
 sh_bullet *b=g->bullets+i;sh_rain_motion *m=g->rain+i;
 memset(b,0,sizeof(*b));memset(m,0,sizeof(*m));b->active=1;b->kind=(uint8_t)kind;
 unsigned half=kind==2?12:6;
 m->x=(x-(int)half*SH_Q)*256;m->y=(y-(int)half*SH_Q)*256;
 
 if(kind==1){
  m->vy=Q16; /* Original move_accelerated(I, 0.001 * aim). */
  int divisor=g->level>=6?100:1000;
  m->ax=ux/divisor;m->ay=uy/divisor;g->rain_spawned++;
 }else{
  m->ax=2*ux;m->ay=2*uy; /* Asymptotic target velocity. */
  m->vx=m->ax*33/10;m->vy=m->ay*33/10;g->orb_spawned++;
 }
}
void tr_reset(sh_game *g){
 g->enemies[0]=(sh_actor){.x=(SH_W/2-16)*SH_Q,.y=128*SH_Q,.active=1,.hp=255};
 g->boss_target_x=g->enemies[0].x;g->boss_target_y=g->enemies[0].y;
 g->invincible=180;
}
void tr_emit(sh_game *g){
 unsigned t=g->ticks-1,cycle=t%500;
 sh_actor *e=g->enemies;
 /* Original wander event times. Bounded integer interpolation replaces
  * attraction exponent 0.5; its different path is documented explicitly. */
 if(cycle==20 || cycle==200 || cycle==320){
  int dist=cycle==20?80:40;
  g->boss_target_x=e->x+((int)(random32(g)%(2*dist+1))-dist)*SH_Q;
  g->boss_target_y=e->y+((int)(random32(g)%(2*dist+1))-dist)*SH_Q;
  if(g->boss_target_x<48*SH_Q)g->boss_target_x=48*SH_Q;
  if(g->boss_target_x>(SH_W-80)*SH_Q)g->boss_target_x=(SH_W-80)*SH_Q;
  if(g->boss_target_y<112*SH_Q)g->boss_target_y=112*SH_Q;
  if(g->boss_target_y>258*SH_Q)g->boss_target_y=258*SH_Q;
 }
 e->x+=(g->boss_target_x-e->x)/32;e->y+=(g->boss_target_y-e->y)/32;
 int ux,uy;
 unsigned difficulty=3; /* Lunatic only. */
 const unsigned rain_count[]={1,2,4,5},intervals[]={100,80,50,20};
 if(t%10==0)for(unsigned shot=0;shot<rain_count[difficulty];shot++){
  int x=(int)(random32(g)%SH_W)*SH_Q,y=SH_TOP*SH_Q;
  aim(g->px+16*SH_Q-x,g->py+16*SH_Q-y,&ux,&uy);
  spawn(g,x,y,ux,uy,1);
 }
 /* Original 400-tick shooting window. Easy 4x2, Normal 5x3,
  * Hard 8 rounds alternating 7/8; Lunatic 20 rounds alternating 12/13. */
 unsigned interval=intervals[difficulty];
 if(cycle>=110 && cycle<510 && (cycle-110)%interval==0){
  int x=e->x+16*SH_Q,y=e->y+(20-60)*SH_Q;
  aim(g->px+16*SH_Q-x,g->py+16*SH_Q-y,&ux,&uy);
  unsigned round=(cycle-110)/interval,odd=difficulty>=2?(round&1):0;
  int span=difficulty==3?11+(int)odd:difficulty==2?6+(int)odd:difficulty==1?2:1;
  /* cos/sin(k*0.15), Q16, k=0..12; no runtime trigonometry. */
  static const int cosine[]={65536,64800,62609,59012,54089,47951,40738,32610,23747,14353,4636,-5185,-14890};
  static const int sine[]={0,9794,19367,28506,37005,44672,51337,56848,61082,63945,65372,65331,63822};
  for(int k=-span;k<=span;k+=2){
   int a=k<0?-k:k,s=k<0?-sine[a]:sine[a];
   int rx=(int)(((int64_t)ux*cosine[a]-(int64_t)uy*s)/Q16);
   int ry=(int)(((int64_t)uy*cosine[a]+(int64_t)ux*s)/Q16);
   spawn(g,x,y,rx,ry,odd?3:2);
  }
 }
}
void tr_move(sh_game *g,unsigned i){
 sh_rain_motion *m=g->rain+i;sh_bullet *b=g->bullets+i;
 /* Same update order as Taisei move_update: position uses old velocity. */
 m->x+=m->vx;m->y+=m->vy;
 if(b->kind==1){m->vx+=m->ax;m->vy+=m->ay;}
 else{m->vx=sh_mix5(m->vx,m->ax);m->vy=sh_mix5(m->vy,m->ay);}
 b->age++;
}
unsigned tr_bullet_size(const sh_game *g,unsigned i){return g->bullets[i].kind==2?24:12;}
