/* Execute the actual game, renderer, clipping and register driver on RV32.
 * DMA is an immediate software model. This is NOT an FPGA/HDMI simulation. */
#include "taisei_render.h"
#include "taisei_rain.h"
#include "cirno_boss.h"
#include "stage1.h"
#include "stage_events.h"



#include "fs_scanline.h"
#include <string.h>
#include <stddef.h>
static uint16_t memory[0x800000/2];
static uint32_t regs[64],checks,commands,op_count[6];
static sh_game game;
static uint32_t row_spans[256],span_upload;
void *memcpy(void *d,const void *s,size_t n){unsigned char *a=d;const unsigned char *b=s;while(n--)*a++=*b++;return d;}
void *memset(void *d,int c,size_t n){unsigned char *a=d;while(n--)*a++=(unsigned char)c;return d;}
int memcmp(const void *a,const void *b,size_t n){const unsigned char *x=a,*y=b;while(n--){if(*x!=*y)return *x-*y;x++;y++;}return 0;}
static void put(const char *s){while(*s)*(volatile unsigned char *)0x10000000=*s++;}
static void num(uint32_t n){char b[10];unsigned i=0;do{b[i++]=(char)('0'+n%10);n/=10;}while(n);while(i){char s[2]={b[--i],0};put(s);}}
static void check(int good,unsigned line){checks++;if(good)return;put("SHOOTER_FAIL line=");num(line);put("\n");*(volatile uint32_t *)0x100000=0x13333;while(1){}}
#define CHECK(x) check((x),__LINE__)
volatile uint16_t *sh_memory(uint32_t address){CHECK(address>=FS_DMA_LO && address<FS_DMA_HI && !(address&1));return memory+(address-FS_DMA_LO)/2;}
static uint32_t line_regs[64],upload,largest_band,model_sprite_pixels;
size_t strlen(const char *s){size_t n=0;while(s[n])n++;return n;}
extern const unsigned char _binary_assets_bin_start[];
static sl_descriptor desc[512];static uint16_t texture[8192];
static unsigned model_delay,pending_start,pending_polls;
static void model_write(uint32_t off,uint32_t v);
uint32_t fs_test_read(uint32_t off){
 if(off==0x84 && pending_start && !--pending_polls){unsigned v=pending_start;pending_start=0;model_write(0x88,v);}
 if(off>=0x80&&off<0x100)return line_regs[(off-0x80)/4];CHECK(off<sizeof(regs));return regs[off/4];
}
static uint16_t reference_mix(uint16_t f,uint16_t b,unsigned a){
 unsigned out=0;const unsigned shifts[]={0,5,11},masks[]={31,63,31};
 for(unsigned k=0;k<3;k++)out|=((((f>>shifts[k])&masks[k])*a+((b>>shifts[k])&masks[k])*(255-a)+127)/255)<<shifts[k];return out;
}

void fs_test_sync(void){}
void fs_test_write(uint32_t off,uint32_t v){
 CHECK(!pending_start); /* No APB/legacy-engine mutation while rendering. */
 if(off==0x88 && (v==1 || v==3) && model_delay){
  pending_start=v;pending_polls=model_delay;line_regs[1]=1;return;
 }
 model_write(off,v);
}
static void model_write(uint32_t off,uint32_t v){
 if(off>=0x80&&off<0x100){
  if(off==0xf0){line_regs[0x70/4]=0x00040100u|(v&1u);return;}
  if(off==0xb0){upload=0;return;}
  if(off==0xe0){span_upload=0;line_regs[0x68/4]=0;return;}
  if(off==0xe4){CHECK(span_upload<256);row_spans[span_upload++]=v;line_regs[0x68/4]=span_upload;return;}
  if(off==0xb4){CHECK(upload<2048);((uint32_t *)desc)[upload++]=v;return;}
  line_regs[(off-0x80)/4]=v;if(off!=0x88)return;
  if(v==2){span_upload=0;line_regs[0x68/4]=0;unsigned words=line_regs[0x24/4];CHECK(words<=4096);for(unsigned i=0;i<words*2;i++)texture[i]=*sh_memory(line_regs[0x20/4]+i*2);line_regs[0x50/4]=words;}
  else if(v==1 || v==3){
   unsigned ncmd=line_regs[0x18/4];line_regs[0x78/4]=line_regs[0x7c/4]=0;
   if(v==3){
    unsigned address=line_regs[0x28/4];CHECK(ncmd<=512);
    for(unsigned k=0;k<ncmd*4;k++){
     volatile uint16_t *p=sh_memory(address+k*4);((uint32_t *)desc)[k]=p[0]|((uint32_t)p[1]<<16);
    }
    upload=ncmd*4;line_regs[0x78/4]=ncmd*16;
   }
   line_regs[0x54/4]=line_regs[0x74/4]=0;unsigned n=line_regs[0x18/4],w=line_regs[0x14/4]&65535,h=line_regs[0x14/4]>>16,dst=line_regs[0xc/4],stride=line_regs[0x10/4];
   CHECK(n<=512&&upload==n*4);if(n>largest_band)largest_band=n;
   for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
    uint32_t bg=line_regs[0x1c/4];
    *sh_memory(dst+y*stride+x*2)=(bg&0x80000000u)?texture[(bg&65535)+(((bg>>16)&511)+y)/8*64+x/10]:(bg&SL_DDR_BACKGROUND)?*sh_memory(line_regs[0x5c/4]+y*stride+x*2):(uint16_t)bg;
   }
   for(unsigned i=0;i<n;i++){
    sl_descriptor *d=desc+i;unsigned x0=d->xy&65535,y0=d->xy>>16,sw=d->wh&65535,sh=d->wh>>16,src=d->source_stride&8191,ss=(d->source_stride>>16)&16383;
    CHECK(x0+sw<=w&&y0+sh<=h&&src+(sh-1)*ss+sw<=line_regs[0x50/4]*2);
    int spans=(d->source_stride&SL_SPAN_ENABLE) && (d->effect&(1u<<24)) && (d->effect&65535)==line_regs[0x6c/4];
    unsigned base=sl_span_row(d);if(spans)CHECK(base+sh<=span_upload);
    for(unsigned y=0;y<sh;y++){
     unsigned start=0,end=sw;
     if(spans){unsigned e=row_spans[base+y],left=e&8191,right=e>>13,row_src=src+y*ss;
      if(left>=row_src+sw || right<=row_src){start=end=0;}
      else{if(left>row_src)start=left-row_src;if(right<row_src+sw)end=right-row_src;}
     }
     line_regs[0x74/4]+=sw-(end-start);line_regs[0x54/4]+=end-start;model_sprite_pixels+=end-start;
     for(unsigned x=start;x<end;x++){
      uint16_t fg=texture[src+y*ss+x];if((d->effect&(1u<<24))&&fg==(uint16_t)d->effect)continue;
      volatile uint16_t *p=sh_memory(dst+(y0+y)*stride+(x0+x)*2);
      *p=d->effect&(1u<<25)?reference_mix(fg,*p,(d->effect>>16)&255):fg;
     }
    }
   }
  }
  line_regs[1]=2;line_regs[0x4c/4]=line_regs[0x2c/4];return;
 }
 CHECK(off<sizeof(regs));if(off==4){regs[1]=0;return;}regs[off/4]=v;
 if(off!=8 || v!=1)return;
 unsigned op=regs[3],src=regs[4],dst=regs[5],ss=regs[6],ds=regs[7],w=regs[8],h=regs[9];
 CHECK(op<5);commands++;op_count[op]++;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  uint16_t fg=op?*sh_memory(src+y*ss+x*2):(uint16_t)regs[10];
  if((op==2||op==4) && fg==(uint16_t)regs[11])continue;
  volatile uint16_t *p=sh_memory(dst+y*ds+x*2);
  if(op>=3){
   uint16_t bg=*p;unsigned a=regs[12],out=0;
   const unsigned shifts[]={0,5,11},masks[]={31,63,31};
   for(unsigned k=0;k<3;k++){
    unsigned f=(fg>>shifts[k])&masks[k],b=(bg>>shifts[k])&masks[k];
    out|=((f*a+b*(255-a)+127)/255)<<shifts[k];
   }
   *p=(uint16_t)out;
  }else *p=fg;
 }
 regs[1]=2;regs[0x38/4]=regs[0x34/4];
}


static sh_frame_commands frames[2];
static void build(sh_frame_commands *f,unsigned bank){CHECK(sh_commands_begin(f,bank,&game)==FS_OK);for(unsigned i=0;i<SH_DRAW_ITEMS;i+=32){unsigned e=i+32;if(e>SH_DRAW_ITEMS)e=SH_DRAW_ITEMS;int r=sh_commands_append(f,&game,i,e);if(r){put("APPEND_ERROR i=");num(i);put(" rc=");num(-r);put("\n");}CHECK(r==FS_OK);}CHECK(sh_commands_finish(f)==FS_OK);}
static void logic_tests(void){
 CHECK(sizeof(sh_rain_motion)==24&&sizeof(sh_bullet)==8);CHECK(sizeof(sh_game)<70000);
 /* Half-tick initial phase absorbs sub-frame scheduling jitter, not elapsed time. */
 sh_clock clock={.phase=500000};
 for(unsigned i=0;i<600;i++)CHECK(sh_clock_advance(&clock,i%2?18667:14666)==1);
 CHECK(clock.dropped_ticks==0);CHECK(sh_clock_advance(&clock,50000)==3);
 CHECK(sh_clock_advance(&clock,1000000)==4&&clock.dropped_ticks==56);
 /* A target killed earlier in the same tick must not consume the next shot. */
 sh_reset(&game,9);game.god=1;sh_skip_to_boss(&game);
 game.enemies[1]=(sh_actor){.active=1,.hp=400,.x=220*SH_Q,.y=200*SH_Q};
 game.enemies[2]=game.enemies[1];
 game.shots[0]=(sh_actor){.active=1,.hp=400,.x=220*SH_Q,.y=200*SH_Q};
 game.shots[1]=game.shots[0];game.shots[1].hp=50;
 sh_step(&game,0);CHECK(!game.enemies[1].active&&game.enemies[2].hp==350);

 sh_reset_level(&game,1,4);CHECK(game.level==7&&game.power==100&&game.stage==0);
 int x=game.px;sh_step(&game,SH_RIGHT);int speed=game.px-x;x=game.px;sh_step(&game,SH_RIGHT|SH_SLOW);CHECK(game.px-x==speed/2);
 CHECK(stage1_enemy_hp(ST_SINE)==100&&stage1_enemy_hp(ST_BURST)==400&&stage1_enemy_hp(ST_CIRCLETOSS)==2000&&stage1_enemy_hp(ST_EXPLOSION)==8000&&stage1_enemy_hp(ST_TRI)==28000);
 game.enemies[1]=(sh_actor){.active=1,.hp=400};game.em[1]=(sh_enemy_motion){.power=1,.points=1};
 for(unsigned i=0;i<7;i++)sh_enemy_damage(&game,1,50);CHECK(game.enemies[1].hp==50&&game.enemies[1].active);
 sh_enemy_damage(&game,1,50);CHECK(!game.enemies[1].active&&game.stage_killed==1);
 sh_reset(&game,2);game.god=1;for(unsigned i=0;i<60;i++)sh_step(&game,SH_FIRE);
 CHECK(game.shot_spawned[0]==20&&game.shot_spawned[1]==2&&game.shot_spawned[2]==0&&game.shot_spawned[3]==16);
 sh_reset(&game,2);game.god=1;game.power=400;for(unsigned i=0;i<60;i++)sh_step(&game,SH_FIRE);
 CHECK(game.shot_spawned[0]==20&&game.shot_spawned[1]==8&&game.shot_spawned[3]>16);
 sh_reset(&game,2);game.god=1;game.power=400;for(unsigned i=0;i<60;i++)sh_step(&game,SH_FIRE|SH_SLOW);
 CHECK(game.shot_spawned[2]>40&&game.shot_spawned[1]==0);
 sh_reset(&game,3);game.god=1;sh_drop(&game,game.px/SH_Q+16,game.py/SH_Q+16,0,1);sh_drop(&game,game.px/SH_Q+16,game.py/SH_Q+16,1,1);
 for(unsigned i=0;i<8;i++)sh_step(&game,0);CHECK(game.power==105&&game.point_count==1&&game.score>1000);
 sh_reset(&game,4);game.god=1;sh_skip_to_boss(&game);game.transition=0;game.phase_ticks=60;unsigned hp=game.boss_hp;
 cb_hit(&game,50);CHECK(game.boss_hp==hp);game.phase_ticks=180;cb_hit(&game,50);CHECK(game.boss_hp==hp-25);
 game.phase_ticks=300;cb_hit(&game,50);CHECK(game.boss_hp==hp-75);
 CHECK(game.boss_max_hp==26000);for(unsigned i=0;i<6;i++){CHECK(game.boss_phase==i);game.transition=0;game.phase_ticks=game.phase_limit;sh_step(&game,0);}CHECK(game.state==SH_CLEAR);
 /* Natural complete timeline: no shortened repeat-wave replacement. */
 sh_reset(&game,5);game.god=1;unsigned seen=0;
 for(unsigned i=0;i<12000&&!game.stage;i++){sh_step(&game,0);seen|=1u<<game.route;}
 CHECK(game.stage==1);CHECK((seen&((1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<6)))==((1u<<1)|(1u<<2)|(1u<<3)|(1u<<4)|(1u<<6)));
 CHECK(game.stage_ticks>9000&&game.stage_spawned>=200);put("FULL_STAGE_ROUTE_PASS spawn=");num(game.stage_spawned);put(" dropped=");num(game.dropped_spawns);put(" ticks=");num(game.stage_ticks);put("\n");
 CHECK(game.dropped_spawns==0);
 /* Fast midboss defeat: positive filler groups survive, negative ones cancel. */
 sh_reset(&game,6);game.god=1;game.stage_ticks=2700;
 for(unsigned i=0;i<5000&&!game.stage;i++){
  sh_step(&game,0);if(game.route==2||game.route==3){game.phase_ticks=300;stage1_mid_hit(&game,100000);}
 }
 CHECK(game.stage_spawned>300);CHECK(game.stage==1&&game.mid_end<4880&&game.post_start==4880&&game.stage_cursor[1]==stage_event_count[1]);

 /* End-to-end combat: use actual weapons, enemy HP, drops and pickup code.
  * Offline invincible autopilot follows a target; no Power/HP is injected. */
 sh_reset(&game,7);game.god=1;unsigned peak_power=game.power,peak_bullets=0;
 for(unsigned tick=0;tick<25000&&game.state==SH_PLAY;tick++){
  int target=-1;for(unsigned j=1;j<SH_ENEMIES;j++)if(game.enemies[j].active){target=j;break;}
  if(target<0&&game.enemies[0].active)target=0;
  if(target>=0){game.px=game.enemies[target].x-4*SH_Q;game.py=game.enemies[target].y+25*SH_Q;}
  sh_step(&game,SH_FIRE);if(game.power>peak_power)peak_power=game.power;if(game.active_bullets>peak_bullets)peak_bullets=game.active_bullets;
 }
 CHECK(game.state==SH_CLEAR&&game.stage_killed>0&&peak_power>=200&&game.dropped_spawns==0);
 put("AUTOPLAY_COMBAT_PASS kills=");num(game.stage_killed);put(" power_max=");num(peak_power);put(" bullets_max=");num(peak_bullets);put(" ticks=");num(game.ticks);put("\n");
 put("GAME_ORIGINAL_RULES_PASS hp damage ramp rank cadence power full_timeline midboss filler\n");
}
static void render_tests(void){
 uint32_t seq=0;sh_reset(&game,5);sh_skip_to_boss(&game);game.transition=0;game.boss_phase=CB_SNOW;game.cage_clock=100;game.cage_count=14;
 for(unsigned i=0;i<14;i++){game.cage_x[i]=60+(i%7)*50;game.cage_y[i]=90+(i/7)*200;}
 game.shots[0]=(sh_actor){.active=1,.hp=1,.x=222*SH_Q,.y=320*SH_Q,.vx=2*SH_Q,.vy=-10*SH_Q};
 for(unsigned i=0;i<160;i++){
  game.bullets[i]=(sh_bullet){.active=1,.kind=i%3+1};game.rain[i].x=((int)(i*71%510)-12)*65536;game.rain[i].y=((int)(i*31%584)-12)*65536;
 }
 for(unsigned i=0;i<8;i++)game.items[i]=(sh_actor){.active=1,.x=(160+i*16)*SH_Q,.y=250*SH_Q,.hp=i&1};
 for(unsigned test=0;test<4;test++){
  put("RENDER_CASE ");num(test);put("\n");game.background=test!=0;game.ticks=(test==3?479:test*137)*4;build(frames,0);
  unsigned half_items=0,half_shots=0;
  for(unsigned b=0;b<SH_BANDS;b++)for(unsigned j=0;j<frames[0].count[b];j++){
   const sl_descriptor *d=(const sl_descriptor *)(uintptr_t)sh_memory(0x700000+b*8192)+j;unsigned src=d->source_stride&8191u,alpha=(d->effect>>16)&255;
   if((src>=ga_textures[TEX_POWER].offset&&src<ga_textures[TEX_POWER].offset+144u)||(src>=ga_textures[TEX_POINT].offset&&src<ga_textures[TEX_POINT].offset+144u)){CHECK(alpha==128&&(d->effect&(1u<<25)));half_items++;}
   if(src>=ga_textures[TEX_SHOT].offset&&src<ga_textures[TEX_SHOT].offset+128u){CHECK(alpha==128||alpha==80||alpha==32);half_shots++;}
  }
  CHECK(half_items&&half_shots);
  CHECK(sh_reference(frames,SH_FB0)==FS_OK);CHECK(sh_render_panel(&frames[0].ui,SH_FB1,600,&seq)==FS_OK);
  sh_frame_job job;model_delay=5;CHECK(sh_frame_start(&job,frames,SH_FB1,&seq)==FS_OK);
  CHECK(sh_commands_begin(frames+1,0,&game)==FS_BUSY);
  CHECK(sh_commands_begin(frames+1,1,&game)==FS_OK);unsigned next=0;
  while(job.active){CHECK(sh_frame_poll(&job,&seq)>=0);if(next<SH_DRAW_ITEMS){unsigned end=next+32;if(end>SH_DRAW_ITEMS)end=SH_DRAW_ITEMS;
    CHECK(sh_commands_append(frames+1,&game,next,end)==FS_OK);next=end;}}
  while(next<SH_DRAW_ITEMS){unsigned end=next+32;if(end>SH_DRAW_ITEMS)end=SH_DRAW_ITEMS;CHECK(sh_commands_append(frames+1,&game,next,end)==FS_OK);next=end;}
  CHECK(sh_commands_finish(frames+1)==FS_OK);model_delay=0;
  for(unsigned y=0;y<480;y++)for(unsigned x=0;x<448;x++)CHECK(sh_memory(SH_FB0)[y*640+x]==sh_memory(SH_FB1)[y*640+x]);
 }
 put("GAME_RENDER_PASS background_wrap clipping ring beams trails alpha_key bank_ownership\n");
 /* Hex framebuffer for a deterministic asset/layout preview, NOT a board photo. */
 put("FRAME_HEX_BEGIN\n");const char *hex="0123456789abcdef";
 for(unsigned i=0;i<640*480;i++){unsigned v=sh_memory(SH_FB1)[i];char d[5]={hex[(v>>12)&15],hex[(v>>8)&15],hex[(v>>4)&15],hex[v&15],0};put(d);}put("\nFRAME_HEX_END\n");
}
void main(void){
 regs[0]=FS_ID;line_regs[0]=SL_ID;line_regs[0x70/4]=0x40101;
 memcpy((void *)(uintptr_t)sh_memory(GA_BASE),_binary_assets_bin_start,GA_BYTES+64);CHECK(sh_assets_valid());CHECK(sh_assets_init()==FS_OK);CHECK(sl_line_overlap(1)==FS_OK);
 logic_tests();render_tests();CHECK(model_sprite_pixels>0&&largest_band<512);CHECK(commands>0&&op_count[1]>0);
 put("TAISEI_LUNATIC_RV32_PASS checks=");num(checks);put(" max_band=");num(largest_band);put("\n");*(volatile uint32_t *)0x100000=0x5555;while(1){}
}
