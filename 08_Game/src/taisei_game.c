/* Bare-metal adaptation: Taisei art/spells, project stage timeline and systems.
 * Single Lunatic configuration. Fixed 60Hz simulation; no floating point. */
#include "taisei_game.h"
#include "taisei_rain.h"
#include "cirno_boss.h"
#include "stage1.h"
#include <string.h>
static uint32_t rng(sh_game *g){return g->rng=g->rng*1664525u+1013904223u;}
static int absint(int x){return x<0?-x:x;}
static int hit(const sh_game *g,int x,int y,int w,int h){
 int cx=g->px/SH_Q+16,cy=g->py/SH_Q+16;
 int nx=cx<x?x:cx>=x+w?x+w-1:cx,ny=cy<y?y:cy>=y+h?y+h-1:cy;
 int dx=cx-nx,dy=cy-ny;return dx*dx+dy*dy<=9;
}
void sh_score(sh_game *g,unsigned amount){
 g->score=amount>999999999u-g->score?999999999u:g->score+amount;
 if(g->score>g->high_score)g->high_score=g->score;
}
void sh_drop(sh_game *g,int x,int y,unsigned type,unsigned count){
 for(unsigned n=0;n<count;n++)for(unsigned i=0;i<SH_ITEMS;i++)if(!g->items[i].active){
  sh_actor *a=g->items+i;*a=(sh_actor){.x=(x+(int)(rng(g)%25)-12)*SH_Q,.y=y*SH_Q,
   .vx=((int)(rng(g)%257)-128),.vy=-SH_Q,.active=1,.hp=type};break;
 }
}
static void damage(sh_game *g){
 if(g->god||g->invincible||g->state!=SH_PLAY)return;
 g->boss_misses=1;if(g->hp)g->hp--;g->invincible=210;g->combo=0;
 unsigned drop=g->power*3/100;if(drop<2)drop=2;sh_drop(g,g->px/SH_Q+16,g->py/SH_Q+16,0,drop);g->power=g->power*7/10;
 g->respawn=60;g->px=(SH_W/2-16)*SH_Q;g->py=(SH_H+30-16)*SH_Q;
 memset(g->bullets,0,sizeof(g->bullets));g->cage_count=0;
 if(!g->hp)g->state=SH_OVER;
}
void sh_reset(sh_game *g,uint32_t seed){
 memset(g,0,sizeof(*g));g->rng=seed;g->px=(SH_W/2-16)*SH_Q;g->py=480*SH_Q;
 g->hp=3;g->state=SH_PLAY;g->level=7;g->power=100;g->point_value=10000;g->background=1;g->homing_cd=g->needle_cd=21;
}
void sh_reset_level(sh_game *g,uint32_t seed,unsigned level){(void)level;sh_reset(g,seed);}
void sh_pause(sh_game *g){if(g->state==SH_PLAY)g->state=SH_PAUSED;else if(g->state==SH_PAUSED)g->state=SH_PLAY;}
void sh_skip_to_boss(sh_game *g){
 unsigned god=g->god,test=g->test_used;memset(g->enemies,0,sizeof(g->enemies));
 g->stage=1;g->route=7;cb_reset(g);g->god=god;g->test_used=test;
}
void sh_award_phase(sh_game *g,int killed){
 sh_score(g,killed?50000+g->phase_limit-g->phase_ticks:5000);
 if(g->boss_phase==1||g->boss_phase>=3){sh_drop(g,g->enemies[0].x/SH_Q+16,g->enemies[0].y/SH_Q+20,0,14);sh_drop(g,g->enemies[0].x/SH_Q+16,g->enemies[0].y/SH_Q+20,1,12);}
}
static void effect(sh_game *g,int x,int y){
 for(unsigned n=0;n<8;n++)for(unsigned i=0;i<SH_FX;i++)if(!g->effects[i].active){
  g->effects[i]=(sh_actor){.x=x*SH_Q,.y=y*SH_Q,.vx=(int)(rng(g)%769)-384,
   .vy=(int)(rng(g)%769)-384,.active=1};break;
 }
}
void sh_enemy_damage(sh_game *g,unsigned j,unsigned amount){
 if(j>=SH_ENEMIES||!g->enemies[j].active)return;
 if(!j){if(g->stage)cb_hit(g,amount);else stage1_mid_hit(g,amount);return;}
 sh_actor *e=g->enemies+j;sh_enemy_motion *m=g->em+j;if(e->age<m->summon)return;
 if(amount>=e->hp){int x=e->x/SH_Q+12,y=e->y/SH_Q+12;e->hp=0;e->active=0;g->kills++;g->stage_killed++;g->combo++;
  sh_score(g,100);sh_drop(g,x,y,0,m->power);sh_drop(g,x,y,1,m->points);effect(g,x,y);
 }else e->hp-=amount;
}
static void shot(sh_game *g,int x,int y,int vx,int vy,unsigned kind,unsigned damage){
 for(unsigned i=0;i<SH_SHOTS;i++)if(!g->shots[i].active){g->shots[i]=(sh_actor){.active=1,.x=x-4*SH_Q,.y=y-8*SH_Q,.vx=vx,.vy=vy,.hp=damage,.cooldown=kind};g->shot_spawned[kind]++;return;}
 g->dropped_spawns++;
}
void sh_option_position(const sh_game *g,unsigned i,int32_t *x,int32_t *y){
 unsigned rank=g->power/100;if(rank>4)rank=4;int dx=0,dy=0;
 if(g->slow){int radius=rank==4&&i>=2?78:52;int angle=(rank==4&&i>=2?-(int)g->ticks*626:(int)g->ticks*1252);
  if(rank==2||rank==4)angle+=(i&1)*32768;else if(rank==3)angle+=i*65536/3;
  dx=(int)((int64_t)radius*cb_cs(angle)/65536);dy=(int)((int64_t)radius*cb_sn(angle)/65536);
 }else if(rank==1){dy=50;}else if(rank==2){dx=i? -40:40;}else if(rank==3){if(i==0)dy=50;else dx=i==1?40:-40;}
 else if(rank==4){const int positions[4]={40,-40,80,-80};dx=positions[i&3];}
 *x=g->px+(16+dx)*SH_Q;*y=g->py+(16+dy)*SH_Q;
}
static unsigned root32(uint32_t n){uint32_t r=0,b=1u<<30;while(b>n)b>>=2;while(b){if(n>=r+b){n-=r+b;r=(r>>1)+b;}else r>>=1;b>>=2;}return r;}
static void steer(sh_game *g,sh_actor *a,const uint8_t *targets,unsigned count){
 if(a->age>=60)return;
 int nearest=-1,best=0x7fffffff;
 for(unsigned n=0;n<count;n++){unsigned i=targets[n];if(!g->enemies[i].active)continue;int dx=(g->enemies[i].x-a->x)/SH_Q,dy=(g->enemies[i].y-a->y)/SH_Q;int d=dx*dx+dy*dy;if(d<best){best=d;nearest=i;}}
 if(nearest<0)return;
 int dx=(g->enemies[nearest].x+(nearest?12:16)*SH_Q-a->x)/SH_Q,dy=(g->enemies[nearest].y+(nearest?12:20)*SH_Q-a->y)/SH_Q;
 unsigned len=root32((int64_t)dx*dx+(int64_t)dy*dy);if(!len)return;
 unsigned t=a->age,ratio=(uint64_t)t*t*t*65536u/(60*60*60),factor=(uint64_t)ratio*(65536-ratio)*4/65536;
 unsigned speed=1024+(unsigned)(t+1)*(t+1)*1024/3600;
 int ax=(int)((int64_t)dx*speed*factor/(len*4u*65536u)),ay=(int)((int64_t)dy*speed*factor/(len*4u*65536u));
 int vx=a->vx+ax,vy=a->vy+ay;unsigned norm=root32((int64_t)vx*vx+(int64_t)vy*vy);if(norm){a->vx=(int)((int64_t)vx*speed/norm);a->vy=(int)((int64_t)vy*speed/norm);}
}
static void weapons(sh_game *g,unsigned input){
 if(g->fire_cd)g->fire_cd--;if(g->volley_cd)g->volley_cd--;if(g->homing_cd)g->homing_cd--;if(g->needle_cd)g->needle_cd--;if(g->focus_cd)g->focus_cd--;
 if(g->slow!=g->previous_slow){g->focus_cd=8;g->previous_slow=g->slow;}
 unsigned rank=g->power/100;if(rank>4)rank=4;int x=g->px+16*SH_Q,y=g->py+16*SH_Q;
 if(input&SH_FIRE){
  if(!g->fire_cd){shot(g,x+(g->fire_side?10:-10)*SH_Q,y-15*SH_Q,0,-20*SH_Q,0,50);g->fire_side^=1;g->fire_cd=3;}
  if(!g->volley_cd){for(unsigned p=0;p<=rank;p++)for(unsigned i=0;i<12;i++)if(!g->volleys[i].active){g->volleys[i]=(sh_volley){g->ticks+5*p,p,1,0};break;}g->volley_cd=16;}
  if(!g->focus_cd&&(!g->slow?!g->homing_cd:!g->needle_cd)){
   for(unsigned i=0;i<rank;i++){int32_t xx,yy;sh_option_position(g,i,&xx,&yy);
    if(g->slow)shot(g,xx,yy-25*SH_Q,0,-20*SH_Q,2,64);
    else {int angle=-16384;if(rank>=2){int side=(rank==3?i==1:i%2==0)?1:-1;if(!(rank==3&&i==0))angle+=side*(rank==4&&i>=2?2048:1365);}
     shot(g,xx,yy,(int)((int64_t)8*SH_Q*cb_cs(angle)/65536),(int)((int64_t)8*SH_Q*cb_sn(angle)/65536),1,175);}
   }
   if(g->slow)g->needle_cd=3;else g->homing_cd=21;
  }
 }
 for(unsigned i=0;i<12;i++)if(g->volleys[i].active&&g->volleys[i].tick<=g->ticks){
  int spread=1024+g->volleys[i].rank*358;for(int side=-1;side<=1;side+=2){int a=-16384+side*spread;
   shot(g,x+side*5*SH_Q,y-SH_Q,(int)((int64_t)18*SH_Q*cb_cs(a)/65536),(int)((int64_t)18*SH_Q*cb_sn(a)/65536),3,35);}
  g->volleys[i].active=0;
 }
}
void sh_step_begin(sh_game *g,uint32_t input){
 if(g->state!=SH_PLAY)return;
 g->ticks++;if(g->invincible)g->invincible--;g->slow=!!(input&SH_SLOW);
 if(g->respawn){unsigned t=--g->respawn;g->py=(480+128*(int)t*t*(180-2*(int)t)/(60*60*60))*SH_Q;input=0;}
 int dx=!!(input&SH_RIGHT)-!!(input&SH_LEFT),dy=!!(input&SH_DOWN)-!!(input&SH_UP);
 int speed=g->slow?720:1440;if(dx&&dy)speed=speed*181/256;g->px+=dx*speed;g->py+=dy*speed;
 if(g->px<0)g->px=0;if(g->px>(SH_W-32)*SH_Q)g->px=(SH_W-32)*SH_Q;
 if(g->py<0)g->py=0;if(!g->respawn&&g->py>(SH_H-32)*SH_Q)g->py=(SH_H-32)*SH_Q;
 weapons(g,input);
 if(g->stage){cb_step(g);if(cb_beam_hit(g))damage(g);
  if(!g->transition&&g->enemies[0].active&&hit(g,g->enemies[0].x/SH_Q,g->enemies[0].y/SH_Q,32,40))damage(g);}
 else stage1_step(g);
 for(unsigned i=1;i<SH_ENEMIES;i++)if(g->enemies[i].active&&g->enemies[i].age>=g->em[i].summon){int dx=g->px+16*SH_Q-g->enemies[i].x-12*SH_Q,dy=g->py+16*SH_Q-g->enemies[i].y-12*SH_Q;
  if(absint(dx)<7*SH_Q&&absint(dy)<7*SH_Q&&dx*dx+dy*dy<49*SH_Q*SH_Q)damage(g);}
 uint8_t targets[SH_ENEMIES];unsigned target_count=0;
 for(unsigned i=0;i<SH_ENEMIES;i++)if(g->enemies[i].active&&(!i||g->enemies[i].age>=g->em[i].summon))targets[target_count++]=i;
 unsigned boss_first=target_count&&targets[0]==0;
 for(unsigned i=0;i<SH_SHOTS;i++){
  sh_actor *a=g->shots+i;if(!a->active)continue;a->age++;
  if(a->cooldown==1)steer(g,a,targets,target_count);a->x+=a->vx;a->y+=a->vy;
  if(a->x<-24*SH_Q||a->x>(SH_W+24)*SH_Q||a->y<-24*SH_Q||a->y>SH_H*SH_Q||a->age>180){a->active=0;continue;}
  for(unsigned order=0;order<target_count;order++){
   unsigned index=order+boss_first;if(index==target_count)index=0;unsigned j=targets[index];
   sh_actor *e=g->enemies+j;if(!e->active)continue;
   if(j&&e->age<g->em[j].summon)continue;
   if(!j&&((g->stage&&g->transition)||(!g->stage&&g->route!=2&&g->route!=3)))continue;
   int radius=(j?30:42)*SH_Q;
   int dx=a->x+4*SH_Q-e->x-(j?12:16)*SH_Q,dy=a->y+8*SH_Q-e->y-(j?12:20)*SH_Q;
   if(absint(dx)<radius&&absint(dy)<radius&&dx*dx+dy*dy<radius*radius){a->active=0;sh_enemy_damage(g,j,a->hp);break;}
  }
 }
 for(unsigned i=0;i<SH_ITEMS;i++){
  sh_actor *a=g->items+i;if(!a->active)continue;a->age++;
  int dx=g->px+16*SH_Q-a->x,dy=g->py+16*SH_Q-a->y;
  if(g->py+16*SH_Q<160*SH_Q || (absint(dx)+absint(dy)<(g->slow?60:30)*SH_Q))a->cooldown=1;
  if(a->cooldown){a->vx=dx/8;a->vy=dy/8;}else{a->vx=a->vx*15/16;if(a->vy<SH_Q)a->vy+=SH_Q/32;}
  a->x+=a->vx;a->y+=a->vy;
  if(absint(dx)+absint(dy)<18*SH_Q){a->active=0;
   if(a->hp){g->point_count++;g->point_value=10000+g->graze*5+(448-g->py/SH_Q)*4;sh_score(g,g->point_value);}
   else {g->power+=5;if(g->power>600)g->power=600;sh_score(g,25);}
  }else if(a->y>(SH_H+20)*SH_Q)a->active=0;
 }
 for(unsigned i=0;i<SH_FX;i++){sh_actor *a=g->effects+i;if(a->active){a->x+=a->vx;a->y+=a->vy;if(++a->age>=30)a->active=0;}}
 g->active_bullets=0;
}
void sh_step_bullets(sh_game *g,unsigned first,unsigned end){
 if(g->state!=SH_PLAY)return;if(end>SH_BULLETS)end=SH_BULLETS;
 for(unsigned i=first;i<end;i++){
  sh_bullet *b=g->bullets+i;if(!b->active)continue;if(b->kind>=16)stage1_bullet_move(g,i);else cb_move(g,i);unsigned size=b->kind==20?24:tr_bullet_size(g,i);
  int32_t bx=sh_bx(g,i),by=sh_by(g,i);
  int margin=b->kind>=4?256:0;
  if(bx<-(int)(size+margin)*SH_Q||bx>=(SH_W+margin)*SH_Q||by<-(int)(size+margin)*SH_Q||by>=(SH_H+margin)*SH_Q){b->active=0;continue;}
  g->active_bullets++;int inset=size==24?5:3;
  if(!g->god&&hit(g,bx/SH_Q+inset,by/SH_Q+inset,size-2*inset,size-2*inset)){b->active=0;damage(g);continue;}
  int dx=bx/SH_Q+(int)size/2-g->px/SH_Q-16,dy=by/SH_Q+(int)size/2-g->py/SH_Q-16;
  if(!b->hp&&dx*dx+dy*dy<400){b->hp=1;g->graze++;sh_score(g,10);}
 }
}
void sh_step(sh_game *g,uint32_t input){sh_step_begin(g,input);sh_step_bullets(g,0,SH_BULLETS);}
unsigned sh_count(const sh_game *g){unsigned n=g->hp!=0;for(unsigned i=0;i<SH_ENEMIES;i++)n+=g->enemies[i].active!=0;for(unsigned i=0;i<SH_SHOTS;i++)n+=g->shots[i].active!=0;for(unsigned i=0;i<SH_BULLETS;i++)n+=g->bullets[i].active!=0;for(unsigned i=0;i<SH_ITEMS;i++)n+=g->items[i].active!=0;return n;}
static int hex(unsigned char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;}
void sh_input_byte(sh_input *in,unsigned char c){
 if(c=='@'){in->parse=1;return;}
 if(in->parse){int v=hex(c);if(v<0){in->parse=0;return;}if(in->parse==1){in->nibble=v;in->parse=2;}else{in->mask=((in->nibble<<4)|v)&63;in->lease_ms=250;in->parse=0;}return;}
 uint32_t bit=0;if(c=='w')bit=SH_UP;if(c=='s')bit=SH_DOWN;if(c=='a')bit=SH_LEFT;if(c=='d')bit=SH_RIGHT;if(c=='j')bit=SH_FIRE;
 if(bit){in->mask=bit;in->lease_ms=180;}if(c==' '){in->mask=0;in->lease_ms=0;}
}
void sh_input_elapse(sh_input *in,uint32_t ms){if(ms>=in->lease_ms){in->mask=0;in->lease_ms=0;}else in->lease_ms-=ms;}
unsigned sh_clock_advance(sh_clock *c,uint32_t us){uint64_t phase=(uint64_t)c->phase+(uint64_t)us*60;unsigned ticks=phase/1000000;c->phase=phase%1000000;if(ticks>4){c->dropped_ticks+=ticks-4;ticks=4;}return ticks;}
