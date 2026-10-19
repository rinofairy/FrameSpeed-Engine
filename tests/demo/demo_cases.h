#include "suite_demo.h"
#include "suite_control.h"
static unsigned render_cases;
static void pixels(void){for(unsigned i=SH_TOP*640;i<640*480;i++)CHECK(sh_memory(SH_FB0)[i]==sh_memory(SH_FB1)[i]);render_cases++;}
static void render_case(suite_control *s,uint32_t *seq){
 sh_trim_enable(0);sh_span_enable(0);s->hardware=0;CHECK(!suite_demo_render(&game,s,SH_FB0,600,seq));
 for(unsigned mode=0;mode<=2;mode++){
  sh_trim_enable(mode>=1);sh_span_enable(mode==2);s->hardware=0;CHECK(!suite_demo_render(&game,s,SH_FB1,600,seq));pixels();
  for(unsigned dma=0;dma<2;dma++){
   sh_command_dma(dma);s->hardware=1;CHECK(!suite_demo_render(&game,s,SH_FB1,600,seq));pixels();
   if(s->demo_kind==DEMO_SPRITES)CHECK(sh_stats.command_bytes==(dma?sh_stats.descriptors*16:0));
  }
 }
}
static void prepare_commands(sh_frame_commands *f,suite_control *s,unsigned tick,unsigned bank){
 CHECK(!sh_commands_begin(f,bank,s->bg_mode));suite_demo_prepare(&game,s,tick);
 CHECK(!sh_commands_append(f,&game,0,SH_BULLETS));CHECK(!sh_commands_finish(f));
}
static void overlap_cases(uint32_t *seq){
 sh_frame_commands lists[2];suite_control cfg[2];sh_frame_job job;
 for(unsigned c=0;c<12;c++){
  unsigned bg=c%3,trim=c%2;sh_trim_enable(trim);sh_span_enable(trim);
  for(unsigned k=0;k<2;k++){
   suite_init(cfg+k,1);cfg[k].bg_mode=(bg+k)%3;cfg[k].texture=k?3:2;
   cfg[k].count=c<3?0:c<6?8:c<9?704:SH_BULLETS;cfg[k].alpha=c%4==0?0:c%4==1?128:c%4==2?192:255;
  }
  sh_command_dma(c%2);prepare_commands(lists,cfg,123,0);
  for(unsigned frame=0;frame<4;frame++){
   unsigned a=frame&1,b=a^1,tick=123+frame;
   /* Delayed DMA executes only after next-frame CPU writes have begun. */
   sh_stats=(sh_render_stats){0};model_delay=5;
   CHECK(!sh_frame_start(&job,lists+a,SH_FB1,seq));
   CHECK(sh_commands_begin(lists+b,a,cfg[b].bg_mode)==FS_BUSY);
   CHECK(!sh_commands_begin(lists+b,b,cfg[b].bg_mode));suite_demo_prepare_begin(&game,cfg+b,tick+1);
   unsigned next=0;
   for(unsigned polls=0;;polls++){
    CHECK(polls<1000);int done=sh_frame_poll(&job,seq);CHECK(done>=0);
    if(next<SH_BULLETS){
     unsigned end=next+32;suite_demo_prepare_range(&game,cfg+b,tick+1,next,end);
     if(pending_start)busy_preparations++;
     CHECK(!sh_commands_append(lists+b,&game,next,end));next=end;
     if(next==SH_BULLETS)CHECK(!sh_commands_finish(lists+b));
    }
    if(done==1)break;
   }
   while(next<SH_BULLETS){unsigned end=next+32;suite_demo_prepare_range(&game,cfg+b,tick+1,next,end);CHECK(!sh_commands_append(lists+b,&game,next,end));next=end;if(next==SH_BULLETS)CHECK(!sh_commands_finish(lists+b));}
   CHECK(lists[b].ready);CHECK(sh_stats.bands==8);model_delay=0;
   /* Reference reconstructs current frame, not the prefetched scene. */
   suite_demo_prepare(&game,cfg+a,tick);sh_background_set_mode(cfg[a].bg_mode);
   cfg[a].hardware=0;CHECK(!suite_demo_render(&game,cfg+a,SH_FB0,600,seq));pixels();cfg[a].hardware=1;
  }
 }
 CHECK(busy_preparations>100);put("OVERLAP_OWNERSHIP_PASS delayed_frames=48 busy_chunks=");num(busy_preparations);put("\n");
}
int main(void){
 regs[0]=FS_ID;regs[1]=2;line_regs[0]=SL_ID;line_regs[1]=2;line_regs[0x70/4]=0x00040101;
 CHECK(!sl_line_overlap(0));CHECK(fs_read(0xf0)==0x00040100);
 CHECK(!sl_line_overlap(1));CHECK(fs_read(0xf0)==0x00040101);
 trim_unit_cases();sh_assets_init();trim_asset_cases();CHECK(span_upload==176);
 suite_control s;suite_init(&s,0);CHECK(s.hardware==1&&s.effect==4&&s.alpha==192&&s.texture==1&&s.mode==APP_DEMO);
 uint32_t seq=0;sh_renderer_demo(1);
 for(unsigned bg=0;bg<3;bg++)for(unsigned kind=0;kind<3;kind++)for(unsigned effect=1;effect<=4;effect++){
  memset(&game,0,sizeof(game));s.count=12;s.demo_kind=kind;s.effect=effect;s.alpha=128;s.bg_mode=bg;
  sh_background_set_mode(bg);suite_demo_prepare(&game,&s,45);render_case(&s,&seq);
 }
 CHECK(op_count[0]&&op_count[1]&&op_count[2]&&op_count[3]&&op_count[4]);
 /* UI saturation must match allocated objects, including repeated plus. */
 s.count=0;for(unsigned i=0;i<80;i++)CHECK(suite_command(&s,'+'));
 CHECK(s.count==2048);CHECK(suite_command(&s,'-')&&s.count==2016);
 s.demo_kind=DEMO_SPRITES;s.count=640;s.texture=1;s.effect=4;s.alpha=192;sh_background_set_mode(0);
 memset(&game,0,sizeof(game));suite_demo_prepare(&game,&s,100);render_case(&s,&seq);
 for(unsigned mode=0;mode<3;mode++){
  sh_trim_enable(mode>=1);sh_span_enable(mode==2);s.hardware=1;model_sprite_pixels=0;
  CHECK(!suite_demo_render(&game,&s,SH_FB1,600,&seq));
  unsigned expected=640*(mode==0?144:mode==1?40:30);CHECK(model_sprite_pixels==expected);
  put("ROW_SPAN_MODEL opt=");num(mode);put(" crystal_640_pixels=");num(model_sprite_pixels);put("\n");
 }
 for(unsigned texture_id=1;texture_id<=3;texture_id++)for(unsigned a=0;a<6;a++){
  s.texture=texture_id;s.alpha=a==0?0:a==1?1:a==2?127:a==3?128:a==4?254:255;s.count=4;
  suite_demo_prepare(&game,&s,0);
  game.bullets[0].x=-5*SH_Q;game.bullets[0].y=(SH_TOP-3)*SH_Q;
  game.bullets[1].x=-3*SH_Q;game.bullets[1].y=(SH_TOP-2)*SH_Q;
  game.bullets[2].x=638*SH_Q;game.bullets[2].y=478*SH_Q;
  game.bullets[3].x=636*SH_Q;game.bullets[3].y=475*SH_Q;render_case(&s,&seq);
 }
 /* Dense maximum-load, mixed shapes and band-boundary source row offsets. */
 s.count=1024;s.texture=3;s.alpha=192;s.effect=4;suite_demo_prepare(&game,&s,123);render_case(&s,&seq);
 s.count=2048;s.texture=2;s.alpha=192;s.effect=4;s.demo_kind=DEMO_SPRITES;
 suite_demo_prepare(&game,&s,123);render_case(&s,&seq);
 sl_descriptor d;CHECK(sl_sprite(&d,501,17,13,9,-2,-3,640,432,4,SH_KEY,128)==1);
 CHECK(!sl_attach_spans(&d,501,200));CHECK(sl_span_row(&d)==203);
 CHECK((d.source_stride&8191)==554&&((d.source_stride>>16)&16383)==17);
 CHECK((d.wh&65535)==11&&(d.wh>>16)==6);CHECK(sl_set_span_row(&d,256)==FS_BAD_PARAM);
 CHECK(sl_load_spans(0,1,SH_KEY)==FS_BAD_PARAM);
 sl_frame f={SH_FB1,1280,640,432,0,32,++seq,0};
 CHECK(sl_render_dma(&f,&d,1,FS_DMA_LO-4)==FS_BAD_PARAM);
 CHECK(sl_render_dma(&f,&d,1,FS_DMA_HI-4)==FS_BAD_PARAM);
 CHECK(sl_render_dma(&f,&d,1,0x720001)==FS_BAD_PARAM);
 CHECK(!sl_render_dma(&f,0,0,0));CHECK(!sl_wait(seq,100));
 CHECK(render_cases==513);overlap_cases(&seq);CHECK(render_cases==561);put("DEMO_OVERLAP_RV32_PASS render_cases=");num(render_cases);
 put(" trim_cases=");num(trim_cases_count);put("\n");return 0;
}
