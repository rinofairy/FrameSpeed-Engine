#include "suite_demo.h"
#include "sh_background.h"
void suite_demo_prepare_begin(sh_game *g,const suite_control *s,unsigned frame){
 g->state=SH_PLAY;g->level=7;g->boss_phase=0;g->hp=0;g->score=s->count;
 g->ticks=frame;sh_demo_effect=s->effect;sh_demo_alpha=s->alpha;
}
void suite_demo_prepare_range(sh_game *g,const suite_control *s,unsigned frame,unsigned first,unsigned end){
 if(end>SH_BULLETS)end=SH_BULLETS;
 for(unsigned i=first;i<end;i++){
  sh_actor *b=g->bullets+i;b->active=i<s->count;if(!b->active)continue;
  unsigned kind=s->texture==3?(i%3==0?2:i%3==1?4:1):s->texture,size=kind==2?24:12;
  g->rain[i].kind=kind;b->x=((i*73+frame*3)%(641-size))*SH_Q;b->y=(SH_TOP+(i*47+frame*2)%(433-size))*SH_Q;
 }
}
void suite_demo_prepare(sh_game *g,const suite_control *s,unsigned frame){
 suite_demo_prepare_begin(g,s,frame);suite_demo_prepare_range(g,s,frame,0,SH_BULLETS);
}
static uint16_t mix(uint16_t f,uint16_t b,unsigned a){unsigned r=(((f>>11)&31)*a+((b>>11)&31)*(255-a)+127)/255,g=(((f>>5)&63)*a+((b>>5)&63)*(255-a)+127)/255,v=((f&31)*a+(b&31)*(255-a)+127)/255;return r<<11|g<<5|v;}
static int fill(const fs_surface *d,unsigned x,unsigned y,unsigned w,unsigned h,uint16_t c,int hw,uint32_t *seq){
 if(hw){fs_command cmd={.op=0,.dst=d->data+y*d->stride+x*2,.dst_stride=d->stride,.width=w,.height=h,.color=c,.seq=++*seq};int r=fs_submit(&cmd);if(!r)r=fs_wait(cmd.seq,4000000);if(!r){sh_stats.core_cycles+=fs_read(0x40);sh_stats.read_bytes+=fs_read(0x44);sh_stats.write_bytes+=fs_read(0x48);}return r;}
 for(unsigned yy=0;yy<h;yy++)for(unsigned xx=0;xx<w;xx++)*sh_memory(d->data+(y+yy)*d->stride+(x+xx)*2)=c;return 0;
}
int suite_demo_render(const sh_game *g,const suite_control *s,uint32_t fb,unsigned fps,uint32_t *seq){
 int hw=s->hardware,rc;
 if(s->demo_kind==DEMO_SPRITES)return sh_render_frame(g,fb,hw,fps,seq);
 rc=sh_render_header(g,fb,hw,fps,seq);if(rc)return rc;
 fs_surface d={fb+SH_TOP*1280,640,432,1280};if(hw)fs_batch_begin_write_through();else fs_cache_sync();
 /* A separate real hardware Solid Fill clears the surface for both examples. */
 rc=fill(&d,0,0,640,432,0x0843,hw,seq);if(rc)goto done;
 for(unsigned i=0;i<s->count;i++){
  unsigned w=s->demo_kind==DEMO_FILL?32+(i%3)*16:32,h=s->demo_kind==DEMO_FILL?24+(i%3)*8:32;
  unsigned x=(i*73+g->ticks*3)%(641-w),y=(i*47+g->ticks*2)%(433-h);
  if(s->demo_kind==DEMO_FILL){static const uint16_t colors[]={0xf800,0x07e0,0x001f};rc=fill(&d,x,y,w,h,colors[i%3],hw,seq);}
  else if(hw){rc=fs_draw_image(&d,sh_textures+SH_DUAL_TEX,x,y,s->effect,0xf800,s->alpha,++*seq);if(!rc){sh_stats.core_cycles+=fs_read(0x40);sh_stats.read_bytes+=fs_read(0x44);sh_stats.write_bytes+=fs_read(0x48);}}
  else for(unsigned yy=0;yy<32;yy++)for(unsigned xx=0;xx<32;xx++){
   uint16_t fg=sh_memory(sh_textures[SH_DUAL_TEX].data)[yy*32+xx];if((s->effect==2||s->effect==4)&&fg==0xf800)continue;
   volatile uint16_t *p=sh_memory(d.data+(y+yy)*d.stride+(x+xx)*2);*p=s->effect>=3?mix(fg,*p,s->alpha):fg;
  }
  if(rc)break;
 }
done:if(hw)fs_batch_end_write_through();else fs_cache_sync();return rc;
}
