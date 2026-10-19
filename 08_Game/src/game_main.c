#include "suite_switch.h"
/* RAM-only Lunatic game; CPU prepares frame N+1 while FPGA draws N. */
#include "bsp.h"
#include "taisei_render.h"
#include "cirno_boss.h"
#include <string.h>
static sh_game game;static sh_input input;static sh_clock clock60;
static uint32_t sequence,present_sequence;
static uint64_t logic_time;
typedef struct {sh_frame_commands cmd;unsigned step,steps,bullet,draw,started,logic_us,prepare_us,begin_us;} prepared;
static prepared frames[2];
static char logbuf[1024];static unsigned loglen,logpos;
static uint64_t now(void){return clint_getTime(BSP_CLINT);}
static uint32_t usec(uint64_t ticks){return ticks*1000000u/BSP_CLINT_HZ;}
static void pump(void){unsigned n=uart_writeAvailability(BSP_UART_TERMINAL);while(n--&&logpos<loglen)write_u32(logbuf[logpos++],BSP_UART_TERMINAL+UART_DATA);}
static void text(const char *s){while(*s&&loglen<sizeof(logbuf))logbuf[loglen++]=*s++;}
static void val(const char *s,uint32_t v){char d[10];unsigned n=0;text(s);do{d[n++]='0'+v%10;v/=10;}while(v);while(n&&loglen<sizeof(logbuf))logbuf[loglen++]=d[--n];}
static void fail(const char *s){bsp_printf("DISPLAY_FAIL %s\r\n",s);while(1){}}
static void present(uint32_t fb){
 uint64_t t=now();while(fs_read(0x104)&2)if(usec(now()-t)>500000)fail("present busy");
 *(volatile uint32_t *)(FS_BASE+0x108)=fb;*(volatile uint32_t *)(FS_BASE+0x10c)=++present_sequence;
 __asm__ volatile("fence rw,rw":::"memory");*(volatile uint32_t *)(FS_BASE+0x110)=1;
 while(fs_read(0x114)!=present_sequence){pump();if(usec(now()-t)>500000)fail("present timeout");}
}
static void prepare_begin(prepared *p,unsigned bank,uint64_t t){
 memset(p,0,sizeof(*p));p->cmd.bank=bank;unsigned dt=usec(t-logic_time);logic_time=t;
 sh_input_elapse(&input,dt/1000);p->steps=sh_clock_advance(&clock60,dt);
}
static void prepare_step(prepared *p){
 uint64_t t=now();
 if(p->step<p->steps){
  if(!p->started){sh_step_begin(&game,input.mask);p->begin_us+=usec(now()-t);p->started=1;p->bullet=0;}
  else{unsigned end=p->bullet+32;if(end>SH_BULLETS)end=SH_BULLETS;sh_step_bullets(&game,p->bullet,end);p->bullet=end;
   if(end==SH_BULLETS){p->step++;p->started=0;}}
  p->logic_us+=usec(now()-t);
 }else{
  if(!p->started){unsigned bank=p->cmd.bank;if(sh_commands_begin(&p->cmd,bank,&game))fail("bank begin");p->started=1;}
  unsigned end=p->draw+32;if(end>SH_DRAW_ITEMS)end=SH_DRAW_ITEMS;
  if(sh_commands_append(&p->cmd,&game,p->draw,end))fail("command overflow");p->draw=end;
  if(end==SH_DRAW_ITEMS&&sh_commands_finish(&p->cmd))fail("command publish");
 }
 p->prepare_us+=usec(now()-t);
}
static void prepare_all(prepared *p){while(!p->cmd.ready)prepare_step(p);}
static void selftest(void){
 sh_reset(&game,1);game.shots[0]=(sh_actor){.active=1,.x=117*SH_Q,.y=242*SH_Q,.vy=-10*SH_Q};
 game.items[0]=(sh_actor){.active=1,.x=80*SH_Q,.y=105*SH_Q,.hp=1};
 game.enemies[1]=(sh_actor){.active=1,.x=200*SH_Q,.y=80*SH_Q};
 game.bullets[0]=(sh_bullet){.active=1,.kind=2};game.rain[0].x=-4*65536;game.rain[0].y=28*65536;
 game.bullets[1]=(sh_bullet){.active=1,.kind=1};game.rain[1].x=474*65536;game.rain[1].y=554*65536;
 sh_frame_commands c;if(sh_commands_begin(&c,0,&game)||sh_commands_append(&c,&game,0,SH_DRAW_ITEMS)||sh_commands_finish(&c))fail("startup commands");
 for(unsigned bg=0;bg<2;bg++){
  c.ui.background=bg;c.scroll=137;if(sh_reference(&c,SH_FB0))fail("CPU pixels");
  sh_frame_job job;fs_batch_begin_write_through();if(sh_frame_start(&job,&c,SH_FB1,&sequence))fail("startup start");
  uint64_t t=now();while(job.active){int rc=sh_frame_poll(&job,&sequence);if(rc<0)fail("startup execute");if(usec(now()-t)>500000)fail("startup timeout");}
  fs_batch_end_write_through();fs_cache_sync();
  for(unsigned y=0;y<480;y++)for(unsigned x=0;x<448;x++)if(sh_memory(SH_FB0)[y*640+x]!=sh_memory(SH_FB1)[y*640+x])fail("CPU FPGA pixel mismatch");
 }
 bsp_printf("GAME_PIXEL_MATCH clips_bands_trails_alpha_key_scrolling_background\r\n");
}
void main(void){
 bsp_init();if(fs_read(0)!=FS_ID||fs_read(0x80)!=SL_ID||fs_read(0x100)!=0x46534401)fail("wrong bitstream");
 bsp_printf("DISPLAY_APP_BEGIN STAGE=49_RELEASE_GAME_ALPHA50\r\n");fs_cache_sync();
 if(!sh_assets_valid())fail("asset CRC");bsp_printf("GAME_ASSETS_CRC_OK\r\n");
 if(sh_assets_init()||sl_line_overlap(1))fail("atlas/dual line");fs_texture_control(1,1);selftest();
 sh_reset(&game,0xfaceb00c);logic_time=now();clock60.phase=500000;prepare_begin(frames,0,logic_time);prepare_all(frames);
 unsigned current=0,fps10=0,count=0,max_work=0,max_logic=0,max_prep=0,max_begin=0,over=0;
 uint64_t sample=now(),work_sum=0;uint32_t shown=fs_read(0x120),scans=fs_read(0x11c),back=SH_FB0;
 bsp_printf("APP_MODE mode=GAME\r\nKEYS WASD=move J=fire Shift=half_speed @HH=key_bitmap I=invincible P=pause R=restart B=background N=practice_final T=practice_mid_when_invincible\r\nDISPLAY_READY\r\n");
 while(1){
  pump();for(unsigned n=0;n<64&&uart_readOccupancy(BSP_UART_TERMINAL);n++){
   unsigned char c=uart_read(BSP_UART_TERMINAL);
   if(c=='@'||input.parse){sh_input_byte(&input,c);continue;}
   if(c>='A'&&c<='Z')c+='a'-'A';
   if(c=='m')suite_switch_app(0);
   if(c=='i')cb_toggle_god(&game);else if(c=='p')sh_pause(&game);
   else if(c=='r'){uint32_t high=game.high_score;sh_reset(&game,0xfaceb00c);game.high_score=high;clock60=(sh_clock){.phase=500000};logic_time=now();}
   else if(c=='b')game.background^=1;
   else if(c=='t'&&game.god){unsigned power=game.power;sh_reset(&game,0xfaceb00c);game.god=game.test_used=1;game.power=power;game.stage_ticks=2700;}
   else if(c=='n'&&game.god){game.test_used=1;if(!game.stage)sh_skip_to_boss(&game);else{game.transition=0;game.phase_ticks=game.phase_limit;}}
   else sh_input_byte(&input,c);
  }
  uint64_t start=now();prepared *cur=frames+current,*next=frames+(current^1);
  /* Current bank/snapshot stay immutable while next-game state is advanced. */
  if(sh_render_panel(&cur->cmd.ui,back,fps10,&sequence))fail("panel copy");
  fs_batch_begin_write_through();sh_frame_job job;
  if(sh_frame_start(&job,&cur->cmd,back,&sequence))fail("frame start");
  prepare_begin(next,current^1,start);
  while(job.active){int rc=sh_frame_poll(&job,&sequence);if(rc<0)fail("frame execution");
   if(!next->cmd.ready)prepare_step(next);else pump();if(usec(now()-start)>500000)fail("frame timeout");}
  fs_batch_end_write_through();prepare_all(next);
  unsigned work=usec(now()-start);if(work>max_work)max_work=work;
  if(next->begin_us>max_begin)max_begin=next->begin_us;
  if(next->logic_us>max_logic)max_logic=next->logic_us;if(next->prepare_us>max_prep)max_prep=next->prepare_us;
  if(work>16667)over++;work_sum+=work;count++;
  present(back);back=back==SH_FB0?SH_FB1:SH_FB0;cur->cmd.ready=0;current^=1;
  uint64_t t=now();unsigned span=usec(t-sample);
  if(span>=1000000){
   unsigned newshown=fs_read(0x120),newscans=fs_read(0x11c);fps10=((uint64_t)(newshown-shown)*10000000+span/2)/span;
   if(logpos==loglen){
    logpos=loglen=0;text("GAME_PERF");val(" fps10=",fps10);val(" objects=",sh_count(&game));val(" bullets=",game.active_bullets);
    val(" stage=",game.stage);val(" route=",game.route);val(" stage_tick=",game.stage_ticks);val(" enemy_spawned=",game.stage_spawned);val(" enemy_killed=",game.stage_killed);val(" phase=",game.boss_phase);val(" phase_tick=",game.phase_ticks);val(" tick=",game.ticks);
    val(" background=",game.background);val(" power=",game.power);val(" points=",game.point_count);val(" graze=",game.graze);
    val(" score=",game.score);val(" hp=",game.hp);val(" god=",game.god);val(" state=",game.state);val(" boss_hp=",game.boss_hp);
    val(" work_max_us=",max_work);val(" work_avg_us=",count?work_sum/count:0);val(" logic_max_us=",max_logic);val(" begin_max_us=",max_begin);val(" prepare_max_us=",max_prep);
    val(" core_cycles=",sh_stats.core_cycles);val(" descriptors=",sh_stats.descriptors);val(" submit_us=",sh_stats.submit_us);val(" hud_us=",sh_stats.hud_us);
    val(" over_budget=",over);val(" dropped_ticks=",clock60.dropped_ticks);val(" dropped_spawns=",game.dropped_spawns);
    val(" new_frames=",newshown-shown);val(" scans=",newscans-scans);val(" sample_us=",span);
    val(" underrun=",fs_read(0x124));val(" axi_errors=",fs_read(0x128));text("\r\n");
   }
   sample=t;shown=newshown;scans=newscans;count=max_work=max_logic=max_prep=max_begin=over=0;work_sum=0;
  }
 }
}
