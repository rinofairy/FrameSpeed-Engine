/* Adapted from Taisei f4d3342226e915f369db8680688723adb18e4efa.
 * Copyright 2011-2026 Lukas Weber; 2012-2026 Andrei Alexeyev. MIT.
 * timeline.c, midboss_nonspell_1.c, perfect_freeze.c, move.h.
 * Cooperative tasks become fixed-tick enemy/state machines; Q16 movement.
 * Logical viewport remains the upstream 480x560. Rendering alone is scaled.
 */
#include "stage1.h"
#include "stage_events.h"
#include "taisei_render.h"
#include "cirno_boss.h"
#include "snowflake_tables.h"
#include <string.h>
#define Q 65536
static uint32_t rnd(sh_game *g){return g->rng=g->rng*1664525u+1013904223u;}
static int mul(int a,int b){return (int)((int64_t)a*b/Q);}
static int aim(const sh_game *g,int x,int y){return cb_angle(g->px/256+16-x,g->py/256+16-y);}
unsigned stage1_enemy_hp(unsigned k){return k==ST_SINE||k==ST_DROP?100:k==ST_CIRCLETOSS||k==ST_INSTANT||k==ST_WAVE?2000:k==ST_EXPLOSION?8000:k==ST_TRI?28000:400;}
static unsigned projectile(sh_game *g,int x,int y,int angle,int speed,int boost,unsigned kind,unsigned retention){
 unsigned i=cb_alloc_bullet(g);if(i==SH_BULLETS){g->dropped_spawns++;return i;}
 sh_bullet *b=g->bullets+i;sh_rain_motion *m=g->rain+i;memset(b,0,sizeof(*b));memset(m,0,sizeof(*m));
 b->active=1;b->kind=kind;b->cooldown=retention;
 int half=kind==ST_BIG?12:6;m->x=(x-half)*Q;m->y=(y-half)*Q;
 m->ax=mul(speed,cb_cs(angle));m->ay=mul(speed,cb_sn(angle));
 m->vx=mul(m->ax,Q+boost);m->vy=mul(m->ay,Q+boost);return i;
}
static void ring(sh_game *g,int x,int y,unsigned n,int speed,int boost,int angle,unsigned kind){
 for(unsigned i=0;i<n;i++)projectile(g,x,y,angle+i*65536/n,speed,boost,kind,52429);
}
void stage1_bullet_move(sh_game *g,unsigned i){
 sh_bullet *b=g->bullets+i;sh_rain_motion *m=g->rain+i;m->x+=m->vx;m->y+=m->vy;b->age++;
 if(b->kind==ST_FLAKE){
  m->vx=mul(m->vx,62259);m->vy=mul(m->vy,62259);
  if(b->cooldown&&!--b->cooldown){m->vx=-10*m->ax;m->vy=-10*m->ay;b->kind=ST_SPLIT;}
 }else if(b->kind==ST_SPLIT){m->vx+=m->ax;m->vy+=m->ay;}
 else if(b->kind==ST_FROG){/* linear until its freeze event */}
 else if(b->kind==ST_FROZEN){m->vx=mul(m->vx,52429);m->vy=mul(m->vy,52429);}
 else{
  unsigned r=b->cooldown;if(r==65535)return;
  m->vx=mul(m->vx,r)+mul(m->ax,Q-(int)r);m->vy=mul(m->vy,r)+mul(m->ay,Q-(int)r);
 }
}
static void spawn_event(sh_game *g,const sh_stage_event *ev){
 unsigned i;for(i=1;i<SH_ENEMIES&&g->enemies[i].active;i++);if(i==SH_ENEMIES){g->dropped_spawns++;return;}
 sh_actor *e=g->enemies+i;sh_enemy_motion *m=g->em+i;memset(e,0,sizeof(*e));memset(m,0,sizeof(*m));
 int x=ev->x*16,y=ev->y*16,tx=ev->tx*16,ty=ev->ty*16;
 if(ev->flags&2){
  if(ev->kind==ST_WAVE){x=(240+(int)(rnd(g)%139)-69)*256;y=(200+rnd(g)%41)*256;}
  else x=(rnd(g)%480)*256;
 }
 if(ev->flags&4){x=(240-mul(200,cb_sn((int)((uint64_t)g->ticks*12203))))*256;tx=x;}
 e->x=x-12*256;e->y=y-12*256;e->vx=ev->vx;e->vy=ev->vy;e->active=1;e->hp=stage1_enemy_hp(ev->kind);
 m->kind=ev->kind;m->red=ev->flags&1;m->power=ev->power;m->points=ev->points;m->tx=tx-12*256;m->ty=ty-12*256;
 m->ax=ev->ax;m->ay=ev->ay;m->retention=Q;m->exit_time=ev->exit_time;m->origin_y=e->y;
 switch(m->kind){
 case ST_BURST:m->retention=0;m->attraction=1966;break;case ST_CIRCLE:m->attraction=328;m->retention=52429;break;
 case ST_MULTI:m->retention=0;m->attraction=3277;break;case ST_INSTANT:m->retention=0;m->attraction=2621;break;
 case ST_WAVE:case ST_EXPLOSION:m->summon=120;break;case ST_TRI:e->vx=e->vy=0;m->summon=180;break;
 }
 if(m->attraction){e->vx=mul(m->tx-e->x,m->attraction);e->vy=mul(m->ty-e->y,m->attraction);}
 g->stage_spawned++;
}
static void events(sh_game *g,unsigned section,unsigned tick){
 const sh_stage_event *table=(const sh_stage_event *)(uintptr_t)sh_memory(STAGE_EVENTS_ADDR);
 unsigned cursor=g->stage_cursor[section];
 while(cursor<stage_event_count[section]){
  const sh_stage_event *e=table+stage_event_offset[section]+cursor;if(e->tick>tick)break;
  if(e->tick==tick && (section!=1||e->group>=g->mid_end))spawn_event(g,e);cursor++;
 }
 g->stage_cursor[section]=cursor;
}
static void fire_enemy(sh_game *g,unsigned index){
 sh_actor *e=g->enemies+index;sh_enemy_motion *m=g->em+index;unsigned t=e->age;int x=e->x/256+12,y=e->y/256+12;
 int a=aim(g,x,y);
 switch(m->kind){
 case ST_BURST:
  if(t==60)for(int j=-5;j<=5;j++)projectile(g,x,y,a+j*2086,4*Q,5*Q,ST_CRYSTAL,52429);
  if(t==61){m->attraction=0;m->retention=Q;}break;
 case ST_CIRCLETOSS:
  if(t>=60&&t<100&&!(t&1)){
   unsigned k=(t-60)/2;e->vx=e->vx*4/5;e->vy=e->vy*4/5;
   projectile(g,x,y,k*65536/20,2*Q,k*Q/2,ST_RICE,52429);
  }
  if(t>=90&&t<90+4*150&&(t-90)%150<33){
   int jitter=(int)(rnd(g)%2087)-1043;projectile(g,x,y,a+jitter,Q+(rnd(g)%(2*Q)),3*Q,ST_THICK,52429);
  }break;
 case ST_SINE:if(t>=10&&(t-10)%20==0)projectile(g,x,y,a,3*Q,5*Q,ST_BALL,52429);break;
 case ST_CIRCLE:
  if(t>=60&&t<300){unsigned c=(t-60)%120;if(!c)m->angle=rnd(g)&65535;
   if(c<50&&!(c&1)){unsigned k=c/2,round=(t-60)/120;int angle=m->angle+((round&1)?k:25-k)*65536/25;
    projectile(g,x,y,angle,5*Q/2,k*Q/2,ST_RICE,52429);}
   if(c==50){int aa=rnd(g)&65535;m->tx+=mul(30*256,cb_cs(aa));m->ty+=mul(30*256,cb_sn(aa));}
  }
  if(t==310){m->attraction=0;m->retention=Q;int aa=-16384+(int)(rnd(g)%5462);m->ax=mul(10,cb_cs(aa));m->ay=mul(10,cb_sn(aa));}break;
 case ST_DROP:if(t>=20&&(t-20)%20==0)projectile(g,x,y,a,14*Q/5+rnd(g)%Q,0,ST_BALL,65535);break;
 case ST_MULTI:
  if(t>=60&&t<124&&(t-60)%16==0)for(int j=-3;j<=3;j++)projectile(g,x,y,a+j*2086,5*Q/2,0,ST_CRYSTAL,65535);
  if(t==134){m->attraction=0;m->retention=Q;}break;
 case ST_INSTANT:if(t==75)ring(g,x,y,28,3*Q/2,2*Q,0,ST_RICE);if(t==95)ring(g,x,y,32,3*Q,3*Q,0,ST_RICE);
  if(t==200){m->attraction=0;m->retention=Q;}break;
 case ST_WAVE:
  if(t==120)m->angle=a;
  if(t>=180&&t<=240){int k=t-180,phase=(k%12),tri=phase<=6?phase*Q/3-Q:3*Q-phase*Q/3;
   int aa=m->angle+mul(2341,tri);unsigned i=projectile(g,x-mul(24,cb_cs(m->angle)),y-mul(24,cb_sn(m->angle)),aa,4*Q,-5*Q,ST_THICK,58982);(void)i;}
  if(t==300)m->retention=52429;break;
 case ST_EXPLOSION:
  if(t==200){int ofs=rnd(g)&65535;for(unsigned j=0;j<60;j++){
    int aa=ofs+j*65536/60,phase=j%10,tri=phase<=5?phase*Q/5:(10-phase)*Q/5;
    int s=tri,speed=4*Q,boost=Q+8*s;
    projectile(g,x,y,aa,speed,boost,s==Q?ST_BIG:ST_BALL,52429);
    for(unsigned k=0;k<4;k++){speed=speed*4/5;projectile(g,x,y,aa,speed,boost,ST_RICE,52429);}
  }}
  if(t==210)m->retention=52429;break;
 case ST_TRI:
  if(t>=180&&t<858&&(t-180)%3==0){unsigned k=(t-180)/3;int aa=((k/7)%30)*1092+rnd(g)%1043;
   ring(g,x,y,5,2*Q,3*Q,aa,ST_THICK);
   if(k==113||k==225){ring(g,x,y,30,3*Q/2,2*Q,0,ST_RICE);ring(g,x,y,30,3*Q,3*Q,0,ST_RICE);}}
  if(t==918){e->vx=0;e->vy=0;m->ax=0;m->ay=-133;m->retention=52429;}
  break;
 }
}
static void enemies(sh_game *g){
 for(unsigned i=1;i<SH_ENEMIES;i++){
  sh_actor *e=g->enemies+i;sh_enemy_motion *m=g->em+i;if(!e->active)continue;
  if(m->kind==ST_SINE){e->vy-=((e->y-m->origin_y)*3)/100;e->x+=e->vx;e->y+=e->vy*2/5;}
  else{
   e->x+=e->vx;e->y+=e->vy;
   int exit=(m->kind==ST_BURST&&e->age>=61)||(m->kind==ST_CIRCLETOSS&&e->age>=m->exit_time)||
    (m->kind==ST_MULTI&&e->age>=134)||(m->kind==ST_INSTANT&&e->age>=200)||
    (m->kind==ST_WAVE&&e->age>=300)||(m->kind==ST_EXPLOSION&&e->age>=210)||(m->kind==ST_TRI&&e->age>=918)||m->kind==ST_DROP||
    (m->kind==ST_CIRCLE&&e->age>=310);
   e->vx=mul(e->vx,m->retention)+(exit?m->ax:0);e->vy=mul(e->vy,m->retention)+(exit?m->ay:0);
   if(m->attraction){e->vx+=mul(m->tx-e->x,m->attraction);e->vy+=mul(m->ty-e->y,m->attraction);}
  }
  fire_enemy(g,i);e->age++;
  if(e->x>=-24*256&&e->x<(SH_W+24)*256&&e->y>=-24*256&&e->y<(SH_H+24)*256)m->entered=1;
  if((m->entered&&(e->x<-80*256||e->x>(SH_W+80)*256||e->y<-80*256||e->y>(SH_H+80)*256))||e->age>2400)e->active=0;
 }
}
/* Midboss Icy Storm: staggered 6-arm snowflakes and the original split. */
static void snowflake(sh_game *g,unsigned phase_t){
 unsigned burst=g->mid_round;int size=(5*Q+3*cb_sn(burst*23051))/Q;
 unsigned redirect=60+72+65-4*(8-size),period=redirect+20;
 unsigned t=phase_t;if(t>=period){g->mid_round++;g->route_tick=0;return;}
 if(t==60)g->mid_burst_angle=aim(g,g->enemies[0].x/256+16,g->enemies[0].y/256+20);
 for(unsigned flake=0;flake<9;flake++){
  int local=(int)t-60-(int)flake*8;if(local<0)continue;int aa=g->mid_burst_angle+(int)((flake+4)*65536/9);
  int j=local<24?local/6:4+(local-24)/12;
  if(j>=size||local>=80)continue;
  int arm=local<24?local%6:((local-24)%12)/2,which=local<24?0:(local-24)%2;
  int angle=aa+arm*65536/6;
  {
   for(int side=-1;side<=1;side+=2){
    int cx=g->enemies[0].x/256+16,cy=g->enemies[0].y/256+20;
    int xx=cx+mul(12*j*Q,cb_cs(angle))/Q+side*mul(5*Q,cb_cs(angle+16384))/Q;
    int yy=cy+mul(12*j*Q,cb_sn(angle))/Q+side*mul(5*Q,cb_sn(angle+16384))/Q;
    if(which){xx=cx+mul(36*Q,cb_cs(angle))/Q+mul(12*(j-3)*Q,cb_cs(angle+side*8192))/Q;yy=cy+mul(36*Q,cb_sn(angle))/Q+mul(12*(j-3)*Q,cb_sn(angle+side*8192))/Q;}
    unsigned i=projectile(g,xx,yy,aa,35*Q/4,0,ST_FLAKE,redirect-t);if(i==SH_BULLETS)continue;
    sh_rain_motion *m=g->rain+i;int vx=m->vx,vy=m->vy;
    m->x+=mul(vx,snow_distance[local]);m->y+=mul(vy,snow_distance[local]);
    m->vx=mul(vx,snow_decay[local]);m->vy=mul(vy,snow_decay[local]);
    m->ax=-mul(Q/5,cb_cs(angle+side*8192));m->ay=-mul(Q/5,cb_sn(angle+side*8192));
   }
  }
 }
}
/* Perfect Freeze co-routines; freeze/thaw yields depend on the port RNG. */
static void perfect(sh_game *g){
 int x=g->enemies[0].x/256+16,y=g->enemies[0].y/256+20;
 if(g->mid_thaw_due&&g->route_tick>=g->mid_thaw_due){
  while(g->mid_thaw_index<SH_BULLETS){unsigned i=g->mid_thaw_index++;sh_bullet *b=g->bullets+i;if(!b->active||b->kind!=ST_FROZEN||(b->pad&15)!=(g->mid_thaw_generation&15))continue;
   int a=rnd(g)&65535;g->rain[i].ax=mul(4*Q,cb_cs(a));g->rain[i].ay=mul(4*Q,cb_sn(a));b->kind=ST_THAW;b->cooldown=64784+rnd(g)%188;
   if(rnd(g)%5<2)break;
  }if(g->mid_thaw_index==SH_BULLETS)g->mid_thaw_due=0;
 }
 if(g->mid_wait){g->mid_wait--;return;}
 switch(g->mid_state){
 case 0:g->em[0].attraction=2621;g->em[0].retention=0;g->em[0].tx=224*256;g->em[0].ty=80*256;g->mid_generation++;g->mid_index=0;g->mid_state=1;g->mid_wait=39;break;
 case 1:
  for(unsigned j=0;j<4;j++){unsigned i=projectile(g,x,y,rnd(g)&65535,Q+rnd(g)%(9*Q/2),0,ST_FROG,65535);if(i<SH_BULLETS)g->bullets[i].pad=(g->mid_generation&15)|((rnd(g)%3)<<4);}
  if(++g->mid_index==60){g->mid_state=2;g->mid_index=0;g->mid_wait=20;}break;
 case 2:
  while(g->mid_index<SH_BULLETS){unsigned i=g->mid_index++;sh_bullet *b=g->bullets+i;
   if(!b->active||b->kind!=ST_FROG||(b->pad&15)!=(g->mid_generation&15))continue;b->kind=ST_FROZEN;if(rnd(g)%5==0)break;}
  if(g->mid_index==SH_BULLETS){g->mid_state=3;g->mid_wait=60;}break;
 case 3:{int dir=rnd(g)&1?1:-1;g->enemies[0].vx=dir*691;g->enemies[0].vy=256;g->em[0].retention=64881;g->em[0].attraction=0;g->em[0].ax=-dir*4;g->em[0].ay=0;g->mid_state=4;g->mid_wait=70;break;}
 case 4:g->mid_thaw_due=g->route_tick+120;g->mid_thaw_generation=g->mid_generation;g->mid_thaw_index=0;g->mid_index=0;g->mid_state=5;break;
 case 5:{unsigned k=g->mid_index;int r1=mul(cb_sn(k*17596),cb_cs(k*35192)),r2=mul(cb_cs(k*17596),cb_sn(k*35192));
  for(int side=-1;side<=1;side+=2){int aa=aim(g,x,y)+mul(side>0?r1:r2,5215);unsigned i=projectile(g,x+side*60,y,aa,14*Q/5,0,ST_MID_RICE,52429);
   if(i<SH_BULLETS){sh_rain_motion *m=g->rain+i;int vx=m->ax,vy=m->ay;m->vx=vx*7/2+side*3*vy;m->vy=vy*7/2-side*3*vx;}}
  if(++g->mid_index==50)g->mid_state=0;else g->mid_wait=3;break;}
 }
}
static void mid_phase(sh_game *g,unsigned route){
 g->route=route;g->route_tick=0;g->phase_ticks=0;g->mid_state=g->mid_wait=g->mid_round=0;g->mid_index=0;g->mid_thaw_due=0;
 g->em[0].retention=route==2?52429:0;g->em[0].attraction=route==2?0:2621;g->em[0].tx=224*256;g->em[0].ty=80*256;g->em[0].ax=g->em[0].ay=0;
 g->boss_damage_fraction=0;g->boss_hp=g->boss_max_hp=24000;g->phase_limit=route==2?1200:3000;
 memset(g->bullets,0,sizeof(g->bullets));
}
static void mid_finish_attack(sh_game *g){
 if(g->route==2)mid_phase(g,3);
 else if(g->route==3){sh_drop(g,g->enemies[0].x/256+16,g->enemies[0].y/256+20,0,14);sh_drop(g,g->enemies[0].x/256+16,g->enemies[0].y/256+20,1,12);
  g->route=4;g->route_tick=0;g->boss_hp=g->boss_max_hp=0;memset(g->bullets,0,sizeof(g->bullets));}
}
void stage1_mid_hit(sh_game *g,unsigned damage){
 if(g->route!=2&&g->route!=3)return;if(g->phase_ticks<=60)return;
 unsigned factor=g->phase_ticks>=300?240:g->phase_ticks-60;uint64_t a=(uint64_t)damage*factor+g->boss_damage_fraction;
 unsigned actual=a/240;g->boss_damage_fraction=a%240;if(actual>g->boss_hp)actual=g->boss_hp;g->boss_power_damage+=actual;
 if(g->boss_power_damage>=2500){g->boss_power_damage-=2500;sh_drop(g,g->enemies[0].x/256+16,g->enemies[0].y/256+20,0,1);}
 if(actual>=g->boss_hp)mid_finish_attack(g);else g->boss_hp-=actual;
}
void stage1_step(sh_game *g){
 unsigned t=g->stage_ticks++;
 if(t<2700)events(g,0,t);
 if(t==2700){g->route=1;g->route_tick=0;g->mid_start=t;g->enemies[0]=(sh_actor){.active=1,.x=684*256,.y=10*256};}
 if(g->route>=1&&g->route<=4){
  sh_actor *b=g->enemies;g->route_tick++;
  if(g->route==1||g->route==4){int tx=g->route==4?-266*256:224*256,ty=g->route==4?10*256:180*256;b->x+=(tx-b->x)/(g->route==1?29:50);b->y+=(ty-b->y)/(g->route==1?29:50);}
  else{sh_enemy_motion *m=g->em;b->x+=b->vx;b->y+=b->vy;b->vx=mul(b->vx,m->retention)+m->ax+mul(m->tx-b->x,m->attraction);b->vy=mul(b->vy,m->retention)+m->ay+mul(m->ty-b->y,m->attraction);}
  if(g->route==1&&g->route_tick>=120)mid_phase(g,2);
  else if(g->route==2||g->route==3){
   g->phase_ticks++;
   if(g->phase_ticks>=g->phase_limit)mid_finish_attack(g);
   else{cb_pool_reset();if(g->route==2)snowflake(g,g->route_tick);else perfect(g);}
  }else if(g->route==4&&g->route_tick>=120){b->active=0;g->mid_end=t;g->post_start=t>4880?t:4880;g->route=5;}
 }
 if(g->route>=5)events(g,1,t);
 if(g->route==5&&t>=g->post_start)g->route=6;
 if(g->route==6){events(g,2,t-g->post_start);if(t-g->post_start>=2560){sh_skip_to_boss(g);return;}}
 cb_pool_reset();enemies(g);
}
