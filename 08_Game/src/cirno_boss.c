/* Adapted from Taisei f4d3342226e915f369db8680688723adb18e4efa.
 * Copyright (c) 2011-2026 Lukas Weber; 2012-2026 Andrei Alexeyev.
 * MIT license in ../upstream/taisei/COPYING.txt.
 * Integer state machines replace coroutine tasks. See README for differences. */
#include "cirno_boss.h"
#include "taisei_rain.h"
#include "cirno_tables.h"
#include "sh_fast_math.h"
#include <string.h>
#define Q 65536
static const uint32_t health[CB_PHASES]={26000,33000,30000,40000,40000,40000};
static const unsigned seconds[CB_PHASES]={20,40,20,50,40,60};
static const char *const names[]={"ICEPLOSION 0","CRYSTAL RAIN","ICEPLOSION 1","SNOW HALATION","ICICLE CASCADE","CRYSTAL BLIZZARD"};
const char *cb_name(unsigned p){return p<CB_PHASES?names[p]:"CIRNO DEFEATED";}
static uint32_t rng(sh_game *g){g->rng=g->rng*1664525u+1013904223u;return g->rng;}
int cb_sn(int a){unsigned u=(unsigned)a&65535;unsigned i=u>>8,k=u&255;
 return sine256[i]+(int)((int64_t)(sine256[(i+1)&255]-sine256[i])*k/256);}
int cb_cs(int a){return cb_sn(a+16384);}
int cb_angle(int x,int y){
 unsigned ax=x<0?-(int64_t)x:x,ay=y<0?-(int64_t)y:y;int a;
 if(!ax&&!ay)return 16384;
 if(ax>=ay)a=atan256[(uint64_t)ay*256/ax];else a=16384-atan256[(uint64_t)ax*256/ay];
 if(x<0)a=32768-a;if(y<0)a=-a;return a;
}
static unsigned diff(const sh_game *g){(void)g;return 3;} /* Lunatic only. */
static int boss_x(const sh_game *g){return g->enemies[0].x/SH_Q+16;}
static int boss_y(const sh_game *g){return g->enemies[0].y/SH_Q+20;}
static int aimed(const sh_game *g,int x,int y){return cb_angle(g->px/SH_Q+16-x,g->py/SH_Q+16-y);}
/* Rebuilt once per emission tick, then exact lowest-free allocation.
 * Same slot choice as the reference linear search, no sprite-order change. */
static uint32_t pool_free[SH_BULLETS/32];
static unsigned pool_ready;
static void pool_prepare(const sh_game *g){
 for(unsigned word=0;word<SH_BULLETS/32;word++){
  uint32_t bits=0;for(unsigned b=0;b<32;b++)if(!g->bullets[word*32+b].active)bits|=1u<<b;
  pool_free[word]=bits;
 }
}
void cb_pool_reset(void){pool_ready=0;}
unsigned cb_alloc_bullet(sh_game *g){
 if(!pool_ready){pool_prepare(g);pool_ready=1;}for(unsigned w=0;w<SH_BULLETS/32;w++)if(pool_free[w]){
  unsigned bit=__builtin_ctz(pool_free[w]);pool_free[w]&=~(1u<<bit);return w*32+bit;
 }return SH_BULLETS;
}
static unsigned spawn(sh_game *g,int x,int y,int a,int speed,unsigned kind,int boost){
 unsigned i=cb_alloc_bullet(g);
 if(i==SH_BULLETS){g->dropped_spawns++;return i;}
 sh_bullet *b=g->bullets+i;sh_rain_motion *m=g->rain+i;
 memset(b,0,sizeof(*b));memset(m,0,sizeof(*m));b->active=1;b->kind=kind;
 m->x=(x-6)*Q;m->y=(y-6)*Q;
 m->ax=(int)((int64_t)speed*cb_cs(a)/Q);m->ay=(int)((int64_t)speed*cb_sn(a)/Q);
 m->vx=(int)((int64_t)m->ax*(Q+boost)/Q);m->vy=(int)((int64_t)m->ay*(Q+boost)/Q);
 return i;
}
static void clear_scene(sh_game *g){
 memset(g->bullets,0,sizeof(g->bullets));memset(g->rain,0,sizeof(g->rain));
 memset(g->shots,0,sizeof(g->shots));memset(g->effects,0,sizeof(g->effects));g->cage_count=0;
}
static void begin_phase(sh_game *g,unsigned p){
 clear_scene(g);g->boss_phase=p;g->phase_ticks=0;g->boss_misses=0;g->boss_damage_fraction=0;
 g->boss_hp=g->boss_max_hp=health[p];g->phase_limit=seconds[p]*60;g->transition=90;
 g->enemies[0].active=1;g->enemies[0].x=(SH_W/2-16)*SH_Q;g->enemies[0].y=108*SH_Q;
 g->boss_target_x=g->enemies[0].x;g->boss_target_y=g->enemies[0].y;
}
void cb_reset(sh_game *g){tr_reset(g);g->boss_captures=g->boss_timeouts=g->boss_hits=0;
 g->god=g->test_used=0;begin_phase(g,CB_ICE0);}
static void end_phase(sh_game *g,int killed){
 sh_award_phase(g,killed);
 if(killed){sh_score(g,1000);if(!g->boss_misses&&!g->test_used&&(g->boss_phase&1 || g->boss_phase>=CB_CASCADE))g->boss_captures++;}
 else g->boss_timeouts++;
 unsigned p=g->boss_phase+1;
 if(p>=CB_PHASES){clear_scene(g);g->boss_hp=0;g->boss_phase=CB_PHASES;g->transition=0;g->enemies[0].active=0;g->state=SH_CLEAR;return;}
 begin_phase(g,p);
}
void cb_hit(sh_game *g,unsigned damage){
 if(!damage||g->transition||g->state!=SH_PLAY||g->boss_phase>=CB_PHASES)return;
 unsigned t=g->phase_ticks;if(t<=60)return;
 unsigned factor=t>=300?240:t-60;
 uint64_t scaled=(uint64_t)damage*factor+g->boss_damage_fraction;
 unsigned actual=scaled/240;g->boss_damage_fraction=scaled%240;if(actual>g->boss_hp)actual=g->boss_hp;
 if(!actual)return;g->boss_hits++;g->boss_power_damage+=actual;
 if(g->boss_power_damage>=2500){g->boss_power_damage-=2500;sh_drop(g,boss_x(g),boss_y(g),0,1);}
 if(actual>=g->boss_hp){g->boss_hp=0;end_phase(g,1);}else g->boss_hp-=actual;
}
void cb_toggle_god(sh_game *g){
 g->god=!g->god;if(g->god){g->test_used=1;if(!g->hp)g->hp=3;
  if(g->state==SH_OVER){g->state=SH_PLAY;g->invincible=60;}}
}
static void wander(sh_game *g,unsigned t,unsigned period,int radius){
 sh_actor *e=g->enemies;
 if(t%period==0){g->boss_target_x=(SH_W/2+(int)(rng(g)%(2*radius+1))-radius-16)*SH_Q;
  g->boss_target_y=(128+(int)(rng(g)%65)-16)*SH_Q;}
 e->x+=(g->boss_target_x-e->x)/32;e->y+=(g->boss_target_y-e->y)/32;
}
static void ice0(sh_game *g,unsigned t){
 unsigned d=diff(g),c=t%300;int x=boss_x(g),y=boss_y(g);wander(g,t,300,100);
 if(c>=20&&c<30&&!(c&1)){unsigned round=(c-20)/2,n=9+d;int a=aimed(g,x,y);
  for(unsigned i=0;i<n;i++)spawn(g,x,y,a+i*65536/n,(3*Q+round*Q/3),4,round*45875);}
 unsigned interval=d<2?3:1;
 if(c>=40&&c<100&&(c-40)%interval==0){int jitter=(int)(rng(g)%8193)-4096;
  unsigned i=spawn(g,x+(((c-40)/interval)&1?30:-30),y-42,-16384+jitter,(d?d==1?5:6:4)*Q,5,0);
  if(i<SH_BULLETS){g->rain[i].ax=0;g->rain[i].ay=(d==3?8:d==2?6:d==1?4:3)*Q;}
 }
 unsigned period=d<2?25:d==2?15:10;
 if(c>=150&&c<300&&(c-150)%period==0){unsigned k=(c-150)/period;int a=rng(g)&65535;
  for(unsigned i=0;i<20;i++)spawn(g,x,y,a+i*8192,3*Q+k*Q/4,4,5*Q/2);}
}
static void ice1(sh_game *g,unsigned t){
 unsigned d=diff(g),c=t%330;int x=boss_x(g),y=boss_y(g);wander(g,t,330,80);
 const unsigned rounds[]={3,4,5,6},shots[]={4,4,6,7},spreads[]={1356,1669,1982,2295};
 if(c>=20&&c<20+rounds[d]*2&&!(c&1)){
  unsigned r=(c-20)/2,n=shots[d]-(r&1);int a=aimed(g,x,y);
  for(unsigned k=0;k<8;k++)for(unsigned i=0;i<n;i++){
   int ofs=(int)((int64_t)spreads[d]*(2*(int)i-(int)n+1)/shots[d]);
   spawn(g,x,y,a+k*8192+ofs,3*Q+r*Q/3,4,r*45875);}
 }
 const unsigned counts[]={40,60,100,100},turns[]={8,6,4,5},iv[]={2,1,1,1};
 unsigned n=counts[d],ivl=iv[d];
 if(c>=40&&c<40+n*ivl&&(c-40)%ivl==0){unsigned b=(c-40)/ivl;
  for(unsigned side=0;side<2;side++){
   int a=(side?0:32768)+(side?-1:1)*(int)(b*turns[d]*65536/(n-1));
   int px=x+(side?-100:100)+(int)((int64_t)b*cb_cs(a)/Q),py=y+(int)((int64_t)b*cb_sn(a)/Q);
   unsigned idx=spawn(g,px,py,a+32768,d==3?9*Q/2:(d?3:2)*Q,6,0);
   if(idx<SH_BULLETS){g->bullets[idx].cooldown=(n-b)*ivl+20;g->rain[idx].vx=g->rain[idx].vy=0;}
  }
 }
 const unsigned period[]={24,18,12,6};
 if(c>=180&&c<330&&(c-180)%period[d]==0){unsigned k=(c-180)/period[d];if(k>15)k=15;int a=rng(g)&65535;
  for(unsigned j=0;j<20;j++)spawn(g,x,y,a+j*8192,3*Q+k*Q/3,4,5*Q/2);}
}
static void cascade(sh_game *g,unsigned t){
 const unsigned intervals[]={30,22,16,8};const int turn[]={4172,4172,2086,1043};unsigned d=diff(g),iv=intervals[d];
 wander(g,t,400,30);if(t<20+iv||(t-20)%iv)return;unsigned round=(t-20)/iv-1;
 for(unsigned side=0;side<2;side++)for(unsigned i=0;i<4;i++){
  int a=side?32768+1043+turn[d]*(1-(int)round):-1043+turn[d]*((int)round-1);
  spawn(g,boss_x(g)+(side?-28:28),boss_y(g)-42,a,(8+3*i)*Q,7,0);
 }
}
static void snow(sh_game *g,unsigned t){
 unsigned n=diff(g)==3?14:10,c=t%375;g->cage_clock=c;
 if(c==60){int cx=g->px/SH_Q+16,cy=g->py/SH_Q+16;
  g->cage_count=n;for(unsigned i=0;i<n;i++){int a=(int)(t/375)*16384+(i*65536+32768)/n;
   g->cage_x[i]=cx+(int)((int64_t)200*cb_cs(a)/Q);g->cage_y[i]=cy+(int)((int64_t)200*cb_sn(a)/Q);}
 }
 if(c==300){for(unsigned i=0;i<g->cage_count;i++){int a=rng(g)&65535;
   for(unsigned k=0;k<9;k++)spawn(g,g->cage_x[i],g->cage_y[i],a+k*65536/9,Q,4,3*Q);}
  g->cage_count=0;}
 if(c<60||c>=300)g->cage_count=0;
 /* Original taunting chase is represented by bounded attraction, no dialogue. */
 if(g->cage_count && c==180){int cx=0,cy=0;for(unsigned i=0;i<n;i++){cx+=g->cage_x[i];cy+=g->cage_y[i];}
  int dx=g->px/SH_Q+16-cx/(int)n,dy=g->py/SH_Q+16-cy/(int)n;
  if(dx*dx+dy*dy>40000&&g->cage_escapes<7)g->cage_escapes++;}
 if(g->cage_escapes>=7){g->enemies[0].x+=(g->px-g->enemies[0].x)/32;g->enemies[0].y+=(g->py-g->enemies[0].y)/32;}
}
static void blizzard(sh_game *g,unsigned t){
 unsigned d=diff(g),c=t%700;int x=boss_x(g),y=boss_y(g);
 if(c<330){g->enemies[0].x+=((SH_W/2-16)*SH_Q-g->enemies[0].x)/32;g->enemies[0].y+=(280*SH_Q-g->enemies[0].y)/32;}
 if(c>=60&&c<360&&(c-60)%10==0){unsigned n=18+3*d;int ofs=(int)(rng(g)%20)-20;
  for(unsigned i=0;i<n;i++){
   unsigned slot=spawn(g,ofs+i*SH_W/(n-1),SH_TOP+6,16384,0,8,0);
   if(slot<SH_BULLETS){g->rain[slot].ax=0;g->rain[slot].ay=1311+(int)((int64_t)655*cb_sn((int)((i*3+t)*348))/Q);}
  }
 }
 const unsigned period[]={4,3,2,1};
 if(c>=330){g->enemies[0].x+=(g->px-g->enemies[0].x)/64;g->enemies[0].y+=(g->py-g->enemies[0].y)/64;
  if((c-330)%period[d]==0)spawn(g,x,y,aimed(g,x,y)+(int)(rng(g)%4173)-2086,655+(rng(g)%(4*Q-655)),9,5*Q);
  if((c-330)%7==0)for(unsigned i=0;i<d;i++){
   unsigned slot=spawn(g,x,y,(int)(t*522)+i*65536/(d?d:1),0,8,0);
   if(slot<SH_BULLETS){int a=(int)(t*522)+i*65536/d;g->rain[slot].ax=cb_cs(a)/100;g->rain[slot].ay=cb_sn(a)/100;}
  }
 }
}
void cb_step(sh_game *g){
 if(g->state!=SH_PLAY||g->boss_phase>=CB_PHASES)return;
 if(g->transition){g->transition--;return;}
 if(g->phase_ticks>=g->phase_limit){end_phase(g,0);return;}
 unsigned t=g->phase_ticks++;
 pool_ready=0;
 switch(g->boss_phase){
 case CB_ICE0:ice0(g,t);break;
 case CB_RAIN:{unsigned save=g->ticks;g->ticks=t+1;tr_emit(g);g->ticks=save;break;}
 case CB_ICE1:ice1(g,t);break;case CB_SNOW:snow(g,t);break;
 case CB_CASCADE:cascade(g,t);break;case CB_BLIZZARD:blizzard(g,t);break;
 }
}
void cb_move(sh_game *g,unsigned i){
 sh_rain_motion *m=g->rain+i;sh_bullet *b=g->bullets+i;
 if(b->kind<4){tr_move(g,i);return;}
 m->x+=m->vx;m->y+=m->vy;b->age++;
 if(b->kind==6){if(b->age>=b->cooldown){m->vx=m->ax;m->vy=m->ay;}}
 else if(b->kind==7){
  if(b->age<80){m->vx=(int)((int64_t)m->vx*9/10);m->vy=(int)((int64_t)m->vy*9/10);}
  else{if(b->age==80){int a=cb_angle(m->ax,m->ay)-16384+(m->ax>0?32768:0);
    m->ax=(int)((int64_t)(5*Q/2)*cb_cs(a)/Q);m->ay=(int)((int64_t)(5*Q/2)*cb_sn(a)/Q);m->vx=m->ax*3;m->vy=m->ay*3;}
   m->vx=sh_mix5(m->vx,m->ax);m->vy=sh_mix5(m->vy,m->ay);}
 }else if(b->kind==8){m->vx+=m->ax;m->vy+=m->ay;}
 else if(b->kind==5){m->vx=(int)(((int64_t)m->vx*64039+m->ax*1497LL)/Q);m->vy=(int)(((int64_t)m->vy*64039+m->ay*1497LL)/Q);}
 else if(b->kind==9&&b->age>200+100*diff(g)){m->vx=(int)((int64_t)m->vx*103/100);m->vy=(int)((int64_t)m->vy*103/100);}
 else{m->vx=sh_mix5(m->vx,m->ax);m->vy=sh_mix5(m->vy,m->ay);}
 
}
int cb_beam(const sh_game *g,unsigned i,int *x1,int *y1,int *x2,int *y2,int *hot){
 if(g->boss_phase!=CB_SNOW||g->transition||i>=g->cage_count||g->cage_clock<60||g->cage_clock>=300)return 0;
 unsigned n=g->cage_count,phase=(g->cage_clock-60)/60,j;
 j=(i+n/2-(phase==0||phase==2?1:phase==3?2:0))%n;
 *x1=g->cage_x[i];*y1=g->cage_y[i];*x2=g->cage_x[j];*y2=g->cage_y[j];
 *hot=(g->cage_clock-60)%60>=30;return 1;
}
int cb_beam_hit(const sh_game *g){
 int cx=g->px/SH_Q+16,cy=g->py/SH_Q+16;
 for(unsigned i=0;i<14;i++){int x1,y1,x2,y2,hot;if(!cb_beam(g,i,&x1,&y1,&x2,&y2,&hot)||!hot)continue;
  int64_t dx=x2-x1,dy=y2-y1,px=cx-x1,py=cy-y1,len=dx*dx+dy*dy,dot=px*dx+py*dy;
  if(!len||dot<=0){if(px*px+py*py<=81)return 1;}
  else if(dot>=len){px=cx-x2;py=cy-y2;if(px*px+py*py<=81)return 1;}
  else{int64_t cross=px*dy-py*dx;if(cross*cross<=81*len)return 1;}
 }return 0;
}

/* Stage timeline adapters: fan/ring emission from Taisei stage1 timeline.c.
 * Highest difficulty only; fixed point and platform RNG are deliberate ports. */
void cb_stage_wave(sh_game *g,int x,int y,unsigned pattern){
 pool_ready=0;int a=aimed(g,x,y);unsigned n=pattern==1?25:11;
 for(unsigned i=0;i<n;i++){
  int direction=pattern==1?(int)(i*65536/n+g->ticks*83):a+((int)i-5)*2086;
  spawn(g,x,y,direction,pattern==1?5*Q/2:7*Q/2,4,pattern==1?2*Q:Q);
 }
}
