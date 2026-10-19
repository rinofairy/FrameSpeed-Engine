#include "suite_switch.h"
/* RAM-only Demo. No game loop, SPI driver, erase or Flash programming. */
#include "bsp.h"
#include "taisei_render.h"
#include "suite_control.h"
#include "suite_demo.h"
#include "sh_background.h"
#include "asset_config.h"
#include "fs_scanline.h"
#include <string.h>
static sh_game scene;static suite_control control;
static unsigned overlap=1;
typedef struct {sh_frame_commands cmd;suite_control settings;unsigned tick,next,cpu_us,logic_us;} prepared_frame;
static prepared_frame prepared[2];static unsigned current_slot;
static uint32_t sequence,present_sequence,optimization=0,benchmark_freeze;
static char logbuf[1280];static unsigned loglen,logpos;
static uint64_t now(void){return clint_getTime(BSP_CLINT);}
static uint32_t usec(uint64_t ticks){return ticks*1000000u/BSP_CLINT_HZ;}
static void wr(unsigned off,uint32_t v){*(volatile uint32_t *)(FS_BASE+off)=v;}
static void pump(void){unsigned n=uart_writeAvailability(BSP_UART_TERMINAL);while(n--&&logpos<loglen)write_u32(logbuf[logpos++],BSP_UART_TERMINAL+UART_DATA);}
static void text(const char *s){while(*s&&loglen<sizeof(logbuf))logbuf[loglen++]=*s++;}
static void val(const char *s,uint32_t v){char d[10];unsigned n=0;text(s);do{d[n++]='0'+v%10;v/=10;}while(v);while(n&&loglen<sizeof(logbuf))logbuf[loglen++]=d[--n];}
static void fail(const char *s){bsp_printf("DISPLAY_FAIL %s\r\n",s);while(1){}}
static void set_optimization(unsigned v){optimization=v;sh_trim_enable(v>=1);sh_span_enable(v==2);}
static void prepare_begin(prepared_frame *p,unsigned bank,unsigned tick){
 p->settings=control;p->tick=tick;p->next=p->cpu_us=p->logic_us=0;
 if(sh_commands_begin(&p->cmd,bank,control.bg_mode))fail("command bank ownership");
 suite_demo_prepare_begin(&scene,&p->settings,tick);
}
static void prepare_step(prepared_frame *p){
 uint64_t t=now();unsigned end=p->next+32;if(end>SH_BULLETS)end=SH_BULLETS;
 suite_demo_prepare_range(&scene,&p->settings,p->tick,p->next,end);p->logic_us+=usec(now()-t);
 if(sh_commands_append(&p->cmd,&scene,p->next,end))fail("command chunk");
 p->next=end;if(end==SH_BULLETS && sh_commands_finish(&p->cmd))fail("command publish");
 p->cpu_us+=usec(now()-t);
}
static void prepare_all(prepared_frame *p){while(!p->cmd.ready)prepare_step(p);}
static void present(uint32_t fb){
 uint64_t t=now();while(fs_read(0x104)&2)if(usec(now()-t)>500000)fail("present busy");
 wr(0x108,fb);wr(0x10c,++present_sequence);__asm__ volatile("fence rw,rw":::"memory");wr(0x110,1);
 while(fs_read(0x114)!=present_sequence){pump();if(usec(now()-t)>500000)fail("present timeout");}
}
static int ram_background_ready(void){
 const volatile uint32_t *h=(const volatile uint32_t *)(uintptr_t)0x73ffc0;
 if(h[0]!=0x47425346 || h[1]!=1 || h[2]!=640 || h[3]!=432 || h[4]!=1280 || h[5]!=552960 || h[6]!=BG_CRC32)return 0;
 const volatile uint8_t *p=(const volatile uint8_t *)(uintptr_t)0x740000;uint32_t crc=~0u;
 for(unsigned i=0;i<552960;i++){crc^=p[i];for(unsigned k=0;k<8;k++)crc=(crc>>1)^((0u-(crc&1u))&0xedb88320u);}
 return ~crc==BG_CRC32;
}
static void startup_check(void){
 suite_control test=control;test.count=8;
 for(unsigned kind=0;kind<3;kind++){
  memset(&scene,0,sizeof(scene));test.demo_kind=kind;test.effect=kind==DEMO_COPY32?1:4;
  suite_demo_prepare(&scene,&test,45);test.hardware=0;set_optimization(0);
  if(suite_demo_render(&scene,&test,SH_FB0,600,&sequence))fail("CPU startup render");
  for(unsigned dma=0;dma<2;dma++){
   test.hardware=1;set_optimization(0);sh_command_dma(dma);
   for(unsigned line=0;line<2;line++){
    if(sl_line_overlap(line))fail("line overlap control");
    if(suite_demo_render(&scene,&test,SH_FB1,600,&sequence))fail("FPGA startup render");
    fs_cache_sync();for(unsigned i=SH_TOP*640;i<640*480;i++)if(sh_memory(SH_FB0)[i]!=sh_memory(SH_FB1)[i])fail("line overlap startup pixels");
   }
  }
 }
 bsp_printf("DEMO_PIXEL_MATCH fill_copy_key_alpha_apb_dma_dualline\r\n");
}
static void startup_overlap_check(void){
 suite_control test=control;test.count=8;test.demo_kind=DEMO_SPRITES;test.hardware=1;
 sh_frame_commands lists[2];set_optimization(0);sh_command_dma(0);
 suite_demo_prepare(&scene,&test,45);
 if(sh_commands_begin(lists,0,test.bg_mode)||sh_commands_append(lists,&scene,0,SH_BULLETS)||sh_commands_finish(lists))fail("startup command list");
 for(unsigned f=0;f<2;f++){
  unsigned a=f&1,b=a^1,next=0;sh_frame_job job;fs_batch_begin_write_through();
  if(sh_frame_start(&job,lists+a,SH_FB1,&sequence)||sh_commands_begin(lists+b,b,test.bg_mode))fail("startup overlap start");
  suite_demo_prepare_begin(&scene,&test,46+f);uint64_t t=now();int done=0;
  while(!done || next<SH_BULLETS){
   if(!done){int r=sh_frame_poll(&job,&sequence);if(r<0)fail("startup overlap run");done=r==1;}
   if(next<SH_BULLETS){unsigned end=next+32;suite_demo_prepare_range(&scene,&test,46+f,next,end);
    if(sh_commands_append(lists+b,&scene,next,end))fail("startup overlap chunk");next=end;
    if(next==SH_BULLETS && sh_commands_finish(lists+b))fail("startup overlap publish");}
   if(usec(now()-t)>500000)fail("startup overlap timeout");
  }
  fs_batch_end_write_through();suite_demo_prepare(&scene,&test,45+f);test.hardware=0;
  if(suite_demo_render(&scene,&test,SH_FB0,600,&sequence))fail("startup overlap reference");test.hardware=1;
  fs_cache_sync();for(unsigned i=SH_TOP*640;i<640*480;i++)if(sh_memory(SH_FB0)[i]!=sh_memory(SH_FB1)[i])fail("overlap startup pixels");
 }
 bsp_printf("DEMO_OVERLAP_PIXEL_MATCH banks=2 alpha_key=1\r\n");
}
void main(void){
 bsp_init();if(fs_read(0)!=FS_ID||fs_read(0x80)!=SL_ID||fs_read(0x100)!=0x46534401)fail("wrong bitstream");
 bsp_printf("DISPLAY_APP_BEGIN STAGE=49_RELEASE_DEMO\r\n");
 fs_cache_sync();int available=ram_background_ready();suite_init(&control,available);sh_background_set_mode(control.bg_mode);
 sh_assets_init();fs_texture_control(1,1);sh_renderer_demo(1);startup_check();startup_overlap_check();
 if(!fs_read(0xe8) || fs_read(0xe8)>SL_SPAN_ROWS || fs_read(0xec)!=SH_KEY)fail("row metadata upload");
 memset(&scene,0,sizeof(scene));set_optimization(0);sh_command_dma(0);
 unsigned fps10=0,frames=0,work_max=0,render_max=0,logic_max=0,over=0,mixed=1;
 uint64_t render_sum=0,work_sum=0,epoch=now(),sample=epoch;
 uint32_t back=SH_FB0,old_shown=fs_read(0x120),old_scans=fs_read(0x11c);
 bsp_printf("APP_MODE mode=DEMO\r\nKEYS 1=FILL 2=COPY32 3=SPRITES 4=CRYSTAL 5=BALL 6=MIXED +/-=LOAD A=ALPHA K=KEY []=OPACITY H=FPGA C=CPU B=BACKGROUND O=NO_TRIM T=BOUNDS V=ROW_SPANS F=FREEZE D=COMMAND_DMA P=APB_UPLOAD R=OVERLAP S=SERIAL J=DUAL_LINE U=SINGLE_LINE\r\nDISPLAY_READY\r\n");
 while(1){
  pump();unsigned changed=0;for(unsigned n=0;n<64&&uart_readOccupancy(BSP_UART_TERMINAL);n++){
   unsigned char c=uart_read(BSP_UART_TERMINAL);if(c>='A'&&c<='Z')c+='a'-'A';
   if(c=='m')suite_switch_app(0);
   if(c=='j'||c=='u'){if(sl_line_overlap(c=='j'))fail("line mode switch");changed=1;}
   else if(c=='r'||c=='s'){overlap=c=='r';changed=1;}
   else if(c=='d'||c=='p'){sh_command_dma(c=='d');changed=1;}
   else if(c=='o'||c=='t'||c=='v'){set_optimization(c=='o'?0:c=='t'?1:2);changed=1;}
   else if(c=='f'){benchmark_freeze^=1;changed=1;}
   else if(suite_command(&control,c))changed=1;
  }
  if(changed){mixed=1;prepared[0].cmd.ready=prepared[1].cmd.ready=0;}
  sh_background_set_mode(control.bg_mode);uint64_t begin=now();
  unsigned active_overlap=overlap&&control.hardware&&control.demo_kind==DEMO_SPRITES;
  unsigned prep_us=0,spill_us=0,logic=0;uint64_t rb=begin;
  if(active_overlap){
   prepared_frame *cur=prepared+current_slot,*next=prepared+(current_slot^1);
   if(!cur->cmd.ready){prepare_begin(cur,current_slot,benchmark_freeze?123:(now()-epoch)/(BSP_CLINT_HZ/60));prepare_all(cur);}
   prep_us=cur->cpu_us;logic=cur->logic_us;rb=now();
   /* HUD DMA completes before the line engine starts. Next-frame preparation
    * touches only scene data and the other DDR command bank, never this HUD. */
   unsigned saved=scene.score;scene.score=cur->settings.count;
   if(sh_render_header(&scene,back,1,fps10,&sequence))fail("overlap header");scene.score=saved;
   sh_stats.list_us=cur->cmd.list_us;fs_batch_begin_write_through();sh_frame_job job;
   if(sh_frame_start(&job,&cur->cmd,back,&sequence))fail("overlap start");
   unsigned tick=benchmark_freeze?123:(now()-epoch)/(BSP_CLINT_HZ/60)+1;
   if(!benchmark_freeze && tick<=cur->tick)tick=cur->tick+1;
   prepare_begin(next,current_slot^1,tick);uint64_t wait_start=now();
   for(;;){
    int rc=sh_frame_poll(&job,&sequence);if(rc<0)fail("overlap execution");if(rc==1)break;
    if(!next->cmd.ready)prepare_step(next);else pump();
    if(usec(now()-wait_start)>500000)fail("overlap timeout");
   }
   fs_batch_end_write_through();uint64_t tail=now();prepare_all(next);spill_us=usec(now()-tail);
   cur->cmd.ready=0;current_slot^=1;
  }else{
   prepared[0].cmd.ready=prepared[1].cmd.ready=0;
   suite_demo_prepare(&scene,&control,benchmark_freeze?123:(now()-epoch)/(BSP_CLINT_HZ/60));
   logic=usec(now()-begin);rb=now();
   if(suite_demo_render(&scene,&control,back,fps10,&sequence))fail("demo render");
   prep_us=logic+sh_stats.list_us;
  }
  if(logic>logic_max)logic_max=logic;
  unsigned render=usec(now()-rb),work=usec(now()-begin);
  render_sum+=render;work_sum+=work;frames++;if(render>render_max)render_max=render;
  if(work>work_max)work_max=work;if(work>16667)over++;
  present(back);back=back==SH_FB0?SH_FB1:SH_FB0;
  uint64_t t=now();unsigned span=usec(t-sample);
  if(span>=1000000){
   unsigned shown=fs_read(0x120),scans=fs_read(0x11c);fps10=((uint64_t)(shown-old_shown)*10000000+span/2)/span;
   if(logpos==loglen){
    logpos=loglen=0;text("DEMO_PERF");val(" mode=",control.hardware);val(" fps10=",fps10);val(" objects=",control.count);
    val(" demo_kind=",control.demo_kind);val(" texture=",control.texture);val(" effect=",control.demo_kind==DEMO_FILL?0:control.effect);
    val(" alpha=",control.alpha);val(" optimization=",optimization);val(" freeze=",benchmark_freeze);val(" bg_mode=",control.bg_mode);
    val(" command_dma=",sh_command_dma_enabled());
    val(" line_overlap=",fs_read(0xf0)&1u);
    val(" overlap=",active_overlap);val(" prep_us=",prep_us);val(" prep_spill_us=",spill_us);
    val(" work_max_us=",work_max);val(" work_avg_us=",frames?work_sum/frames:0);val(" render_max_us=",render_max);
    val(" render_avg_us=",frames?render_sum/frames:0);val(" logic_max_us=",logic_max);val(" over_budget=",over);val(" mixed=",mixed);
    val(" core_cycles=",sh_stats.core_cycles);val(" pixels=",sh_stats.pixel_count);val(" skipped_pixels=",sh_stats.skipped_pixels);
    val(" descriptor_visits=",sh_stats.descriptor_visits);val(" descriptors=",sh_stats.descriptors);
    val(" read_bytes=",sh_stats.read_bytes);val(" write_bytes=",sh_stats.write_bytes);
    val(" cmd_bytes=",sh_stats.command_bytes);val(" cmd_bursts=",sh_stats.command_bursts);
    val(" list_us=",sh_stats.list_us);val(" submit_us=",sh_stats.submit_us);val(" engine_wait_us=",sh_stats.engine_wait_us);
    val(" new_frames=",shown-old_shown);val(" scans=",scans-old_scans);val(" sample_us=",span);
    val(" underrun=",fs_read(0x124));val(" axi_errors=",fs_read(0x128));text("\r\n");
   }
   sample=t;old_shown=shown;old_scans=scans;frames=work_max=render_max=logic_max=over=mixed=0;render_sum=work_sum=0;
  }
 }
}
