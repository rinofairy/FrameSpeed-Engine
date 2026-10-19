#include "taisei_render.h"
#include "fs_font.h"
#include "sh_assets.h"
#include "taisei_rain.h"
#include "fs_scanline.h"
#include "sh_background.h"
#include "fs_sprite_trim.h"
#ifndef SH_OFFLINE_TEST
#include "bsp.h"
static uint64_t stamp(void){return clint_getTime(BSP_CLINT);}
static uint32_t elapsed(uint64_t t){return (uint32_t)((stamp()-t)*1000000u/BSP_CLINT_HZ);}
#else
static uint64_t stamp(void){return 0;}
static uint32_t elapsed(uint64_t t){(void)t;return 0;}
#endif
sh_render_stats sh_stats;
static int clear_enabled=1,background_is_black;
static int demo_mode;
static int command_dma=1;
void sh_command_dma(int enable){command_dma=enable!=0;}
int sh_command_dma_enabled(void){return command_dma;}
static int trim_enabled=1,trim_ready;
static fs_trim_bounds texture_bounds[SH_TEXTURES];
static int span_enabled=1,span_ready;
static uint32_t span_rows[SL_SPAN_ROWS],span_base[SH_TEXTURES];
void sh_span_enable(int enabled){span_enabled=enabled!=0;}
unsigned sh_demo_effect=4,sh_demo_alpha=192;
void sh_clear_enable(int enable){clear_enabled=enable!=0;}
int sh_clear_enabled(void){return clear_enabled && background_is_black;}
#define ODD_COPY_OFFSET 0x10002u
#define BANNER 0x620000u
const fs_image sh_textures[SH_TEXTURES]={
 {0x600000,32,32,64},{0x601000,32,40,64},{0x602000,8,16,16},
 {0x60a000,8,8,16},{0x602300,12,12,24},{0x602500,24,24,48},{0x602a00,12,12,24},
 {0x60b000,32,32,64}
};
/* Immutable compact atlas shared by Taisei sprites and the two-color demo. */
#define LINE_ATLAS 0x630000u
static uint32_t atlas_offset[SH_TEXTURES],line_count,line_dst,background_offset;
static int collecting;
static int build_only;
static unsigned command_bank,owned_banks;
static uint64_t list_start;
static unsigned bin_count[8];
static uint32_t command_address(unsigned bank,unsigned band){return (bank?0x710000u:0x720000u)+band*8192u;}
static sl_descriptor *bank_bucket(unsigned bank,unsigned band){return (sl_descriptor *)(uintptr_t)sh_memory(command_address(bank,band));}
static sl_descriptor *bucket(unsigned band){return bank_bucket(command_bank,band);}
static int queue_descriptor(const sl_descriptor *d){
 if(line_count>=SH_BULLETS)return FS_BAD_PARAM;line_count++;
 unsigned y=d->xy>>16,h=d->wh>>16,first=y/54,last=(y+h-1)/54;
 for(unsigned band=first;band<=last;band++){
  if(bin_count[band]==512)return FS_BAD_PARAM;
  sl_descriptor clipped=*d;unsigned yy=y,hh=h,top=band*54;
  if(yy<top){unsigned cut=top-yy;clipped.source_stride+=((d->source_stride>>16)&16383u)*cut;
   if(d->source_stride&SL_SPAN_ENABLE)sl_set_span_row(&clipped,sl_span_row(d)+cut);hh-=cut;yy=top;}
  if(hh>top+54-yy)hh=top+54-yy;
  clipped.xy=((yy-top)<<16)|(d->xy&65535);clipped.wh=(hh<<16)|(d->wh&65535);
  bucket(band)[bin_count[band]++]=clipped;
 }
 return FS_OK;
}
static int flush_lines(uint32_t *seq){
 if(!collecting)return FS_OK;collecting=0;sh_stats.list_us+=elapsed(list_start);
 for(unsigned band=0;band<8;band++){
  unsigned n=bin_count[band];sh_stats.descriptors+=n;sh_stats.bands++;
  sl_frame f={line_dst+band*54*1280,1280,640,54,
   sh_background_mode()==1?SL_DDR_BACKGROUND:sh_background_mode()==2?SL_CACHED_BACKGROUND(background_offset,band*54):0,32,++*seq,SH_NATIVE_BG+band*54*1280};
  uint64_t started=stamp();int r=command_dma?sl_render_dma(&f,bucket(band),n,0x720000u+band*8192u):sl_render(&f,bucket(band),n);
  sh_stats.submit_us+=elapsed(started);if(r)return r;
  started=stamp();r=sl_wait(f.seq,8000000);sh_stats.engine_wait_us+=elapsed(started);if(r)return r;
  sh_stats.core_cycles+=fs_read(0xc0);sh_stats.write_bytes+=fs_read(0xc8);sh_stats.read_bytes+=fs_read(0xc4);
  sh_stats.pixel_count+=fs_read(0xd4);sh_stats.skipped_pixels+=fs_read(0xf4);sh_stats.descriptor_visits+=fs_read(0xd8);
  sh_stats.command_bytes+=fs_read(0xf8);sh_stats.command_bursts+=fs_read(0xfc);
 }
 return FS_OK;
}
void sh_trim_enable(int enabled){
 enabled=enabled!=0;
 trim_enabled=enabled;
}
static int trim_texture(unsigned id,unsigned *sx,unsigned *sy,unsigned *w,unsigned *h,
 int *x,int *y,unsigned op){
 if(!trim_enabled || !trim_ready || (op!=2 && op!=4))return 1;
 /* This RV32 toolchain defines uint32_t as unsigned long, not unsigned int.
  * Use value conversions rather than aliasing unlike pointer types. */
 uint32_t tx=*sx,ty=*sy,tw=*w,th=*h;int32_t dx=*x,dy=*y;
 int r=fs_trim_region(texture_bounds+id,sh_textures[id].width,sh_textures[id].height,
   &tx,&ty,&tw,&th,&dx,&dy);
 if(r>=0){*sx=tx;*sy=ty;*w=tw;*h=th;*x=dx;*y=dy;}return r;
}
static int prev_state=-1,prev_hw=-1;
static uint32_t prev_phase=~0u,prev_boss_hp=~0u,prev_time=~0u,prev_god=~0u;
static uint32_t prev_fps=~0u,prev_score=~0u,prev_count=~0u,prev_hp=~0u;
static unsigned hud_present[2],dirty_lo[2][2],dirty_hi[2][2];
static void dirty(unsigned row,unsigned x,unsigned w){
 for(unsigned b=0;b<2;b++){if(x<dirty_lo[b][row])dirty_lo[b][row]=x;if(x+w>dirty_hi[b][row])dirty_hi[b][row]=x+w;}
}
static void erase_field(unsigned row,unsigned x,unsigned w){
 unsigned y=row?26:4;volatile uint16_t *p=sh_memory(SH_HUD);
 for(unsigned yy=y;yy<y+14;yy++)for(unsigned xx=x;xx<x+w;xx++)p[yy*640+xx]=0;
 dirty(row,x,w);
}
static void solid(uint32_t addr,unsigned w,unsigned h,uint16_t color){
 volatile uint16_t *p=sh_memory(addr);for(unsigned i=0;i<w*h;i++)p[i]=color;
}
static void label(uint32_t addr,unsigned stride,unsigned x,unsigned y,const char *s,uint16_t color){
 volatile uint16_t *p=sh_memory(addr);
 while(*s && x+10<=stride){unsigned c=(unsigned char)*s++;if(c>=128)c='?';
  /* Font rows contain five columns; all five must reach the framebuffer. */
  for(unsigned r=0;r<7;r++)for(unsigned k=0;k<5;k++)if(fs_font[c*8+r]&(1u<<(4-k)))
   for(unsigned dy=0;dy<2;dy++)for(unsigned dx=0;dx<2;dx++)p[(y+r*2+dy)*stride+x+k*2+dx]=color;
  x+=12;
 }
}
static void number(uint32_t a,unsigned stride,unsigned x,unsigned y,unsigned n){
 char b[12],s[12];unsigned k=0,j=0;do{b[k++]=(char)('0'+n%10);n/=10;}while(n);while(k)s[j++]=b[--k];s[j]=0;label(a,stride,x,y,s,0xffff);
}
void sh_assets_init(void){
 trim_ready=0;span_ready=0;
 for(unsigned id=0;id<SH_TEXTURES;id++){
  const fs_image *im=sh_textures+id;volatile uint16_t *p=sh_memory(im->data);
  if(id==SH_DUAL_TEX){for(unsigned y=0;y<32;y++)for(unsigned x=0;x<32;x++)p[y*32+x]=x<16?0xf800:0x07ff;}
  else sh_unpack_asset(id,p);
  /* Exact key equality: semi-transparent visible colors are never discarded.
   * Retain original pixels/stride; only submitted source crops become smaller.
   */
  if(fs_trim_measure(p,im->width,im->height,im->stride,SH_KEY,texture_bounds+id)){
   texture_bounds[id]=(fs_trim_bounds){0,0,im->width,im->height};
  }
  /* Same pixels, second halfword alignment; no rotation, snapping or shape specialization. */
  volatile uint16_t *odd=sh_memory(im->data+ODD_COPY_OFFSET);
  for(unsigned i=0;i<im->height*im->stride/2;i++)odd[i]=p[i];
 }
 trim_ready=1;
 unsigned used=0;
 for(unsigned id=0;id<SH_TEXTURES;id++){
  atlas_offset[id]=~0u;
  const fs_image *im=sh_textures+id;atlas_offset[id]=used;
  for(unsigned i=0;i<im->width*im->height;i++)sh_memory(LINE_ATLAS)[used++]=sh_memory(im->data)[i];
 }
 if(used&1)sh_memory(LINE_ATLAS)[used++]=SH_KEY;
 background_offset=used;
 for(unsigned i=0;i<64*54;i++)sh_memory(LINE_ATLAS)[used++]=sh_background_pixel(i%64,i/64);
 fs_cache_sync();
 if(sl_load(LINE_ATLAS,used/2,16,0x10000001u)){
#ifndef SH_OFFLINE_TEST
  bsp_printf("DISPLAY_FAIL atlas upload\r\n");while(1){}
#endif
 }
 /* Once per atlas, not once per sprite/frame. All instances share these rows. */
 unsigned row_count=0;
 for(unsigned id=0;id<SH_TEXTURES;id++){
  const fs_image *im=sh_textures+id;span_base[id]=~0u;
  if(row_count+im->height>SL_SPAN_ROWS)continue; /* Generic rectangular fallback. */
  span_base[id]=row_count;
  for(unsigned y=0;y<im->height;y++){
   unsigned first=im->width,last=0;
   for(unsigned x=0;x<im->width;x++)if(*sh_memory(im->data+y*im->stride+x*2)!=SH_KEY){
    if(x<first)first=x;last=x+1;
   }
   unsigned start=atlas_offset[id]+y*(im->stride/2);
   if(first==im->width)first=last=0;
   span_rows[row_count++]=(start+first)|((start+last)<<13);
  }
 }
 if(row_count && sl_load_spans(span_rows,row_count,SH_KEY)==FS_OK)span_ready=1;
 for(unsigned y=0;y<432;y++)for(unsigned x=0;x<640;x++)
  sh_memory(SH_BG)[y*640+x]=sh_background_pixel(x/10,y/8);
 /* Determine once for immutable boot background; non-black assets keep Copy. */
 background_is_black=1;
 for(unsigned i=0;i<640*(480-SH_TOP);i++)if(sh_memory(SH_BG)[i]){background_is_black=0;break;}
 prev_phase=prev_boss_hp=prev_time=prev_god=~0u;
 prev_state=prev_hw=-1;prev_fps=prev_score=prev_count=prev_hp=~0u;
 for(unsigned b=0;b<2;b++){hud_present[b]=0;for(unsigned r=0;r<2;r++){dirty_lo[b][r]=640;dirty_hi[b][r]=0;}}
 fs_cache_sync();
}
void sh_renderer_demo(int on){
 demo_mode=on!=0;prev_hw=prev_state=-1;
 prev_phase=prev_boss_hp=prev_time=prev_god=prev_fps=prev_score=prev_count=prev_hp=~0u;
 for(unsigned b=0;b<2;b++){hud_present[b]=0;for(unsigned r=0;r<2;r++){dirty_lo[b][r]=640;dirty_hi[b][r]=0;}}
}
/* Demo-only HUD: FPS, element count and CPU/FPGA mode. */
static void hud(const sh_game *g,int hw,unsigned fps){
 unsigned count=g->score;int first=prev_hw<0;
 if(first){solid(SH_HUD,640,SH_TOP,0);label(SH_HUD,640,8,4,"FPS",0xffff);label(SH_HUD,640,140,4,"LOAD",0xffff);}
 if(first || prev_fps!=fps){
  erase_field(0,56,72);number(SH_HUD,640,56,4,fps/10);
  unsigned digits=1;for(unsigned n=fps/10;n>=10;n/=10)digits++;
  label(SH_HUD,640,56+digits*12,4,".",0xffff);number(SH_HUD,640,68+digits*12,4,fps%10);
 }
 if(first || prev_count!=count){erase_field(0,200,60);number(SH_HUD,640,200,4,count);}
 if(first || prev_hw!=hw){erase_field(0,272,120);label(SH_HUD,640,272,4,hw?"MODE FPGA":"MODE CPU",0xffff);}
 prev_fps=fps;prev_count=count;prev_hw=hw;
}
static uint16_t blend(uint16_t a,uint16_t b,unsigned alpha){
 unsigned r=(((a>>11)&31)*alpha+((b>>11)&31)*(255-alpha)+127)/255;
 unsigned g=(((a>>5)&63)*alpha+((b>>5)&63)*(255-alpha)+127)/255;
 unsigned v=((a&31)*alpha+(b&31)*(255-alpha)+127)/255;
 return (uint16_t)((r<<11)|(g<<5)|v);
}
static int draw(const fs_surface *dst,const fs_image *src,unsigned sx,unsigned sy,unsigned w,unsigned h,
 int x,int y,unsigned op,unsigned alpha,int hw,uint32_t *seq){
 int id=-1;
 for(unsigned i=0;i<SH_TEXTURES;i++)if(src->data==sh_textures[i].data &&
   src->width==sh_textures[i].width && src->height==sh_textures[i].height && src->stride==sh_textures[i].stride){id=(int)i;break;}
 if(id>=0){
  int r=trim_texture((unsigned)id,&sx,&sy,&w,&h,&x,&y,op);
  if(r<0)return FS_BAD_PARAM;if(!r)return FS_OK;
 }
 if(hw){
  if(collecting){
   if(dst->data==line_dst && id>=0 && atlas_offset[id]!=~0u){
    sl_descriptor descriptor;
    int r=sl_sprite(&descriptor,atlas_offset[id]+sy*(src->stride/2)+sx,src->stride/2,
      w,h,x,y,640,432,op,SH_KEY,alpha);
    if(r==1 && span_enabled && span_ready && (op==2 || op==4) && span_base[id]!=~0u){
     /* sl_sprite already clips negative Y. Compensate metadata without division. */
     unsigned row=span_base[id]+sy+(y<0?(unsigned)(-(int64_t)y):0u);
     int rc=sl_set_span_row(&descriptor,row);if(rc)return rc;
    }
    if(r<0)return r;if(r)return queue_descriptor(&descriptor);return FS_OK;
   }
   if(build_only)return FS_BAD_PARAM; /* Never flush while another frame executes. */
   int r=flush_lines(seq);if(r)return r;
  }
  fs_image aligned=*src;
  /* Clipping shifts source/destination together, preserving this parity test.
   * Different stride parity retains the driver's fully generic fallback. */
  if(!(src->stride&3) && !(dst->stride&3))
   for(unsigned i=0;i<SH_TEXTURES;i++)if(src->data==sh_textures[i].data){
    uint32_t s=src->data+sy*src->stride+sx*2;
    uint32_t d=dst->data+(uint32_t)y*dst->stride+(uint32_t)x*2;
    if((s^d)&2)aligned.data+=ODD_COPY_OFFSET;
    break;
   }
  return fs_draw_region(dst,&aligned,sx,sy,w,h,x,y,op,SH_KEY,alpha,++*seq);
 }
 /* Known descriptors/crops here; clip signed destinations before pointer math. */
 for(unsigned row=0;row<h;row++){
  int yy=y+(int)row;if(yy<0 || yy>=(int)dst->height)continue;
  unsigned first=0,last=w;
  if(id>=0 && span_enabled && span_ready && (op==2 || op==4) && span_base[id]!=~0u){
   uint32_t entry=span_rows[span_base[id]+sy+row];
   unsigned start=atlas_offset[id]+(sy+row)*(src->stride/2)+sx;
   unsigned left=entry&8191u,right=entry>>13;
   if(right<=start || left>=start+w)continue;
   if(left>start)first=left-start;if(right<start+w)last=right-start;
  }
  for(unsigned col=first;col<last;col++){
   int xx=x+(int)col;if(xx<0 || xx>=(int)dst->width)continue;
   uint16_t fg=*sh_memory(src->data+(sy+row)*src->stride+(sx+col)*2);
   if((op==2 || op==4) && fg==SH_KEY)continue;
   volatile uint16_t *p=sh_memory(dst->data+(unsigned)yy*dst->stride+(unsigned)xx*2);
   *p=op>=3?blend(fg,*p,alpha):fg;
  }
 }
 return FS_OK;
}
int sh_render_header(const sh_game *g,uint32_t fb,int hw,uint32_t fps,uint32_t *seq){
 if((fb!=SH_FB0&&fb!=SH_FB1)||!g||!seq)return FS_BAD_PARAM;
 sh_stats=(sh_render_stats){0};if(!hw)fs_cache_sync();hud(g,hw,fps);
 fs_surface full={fb,640,480,1280};fs_image head={SH_HUD,640,SH_TOP,1280};unsigned bank=fb==SH_FB1;int rc=0;
 if(hw)fs_batch_begin_write_through();
 if(!hud_present[bank]){rc=draw(&full,&head,0,0,640,SH_TOP,0,0,1,255,hw,seq);hud_present[bank]=1;}
 else for(unsigned row=0;row<2&&!rc;row++)if(dirty_lo[bank][row]<dirty_hi[bank][row]){
  unsigned x=dirty_lo[bank][row],y=row?26:4;rc=draw(&full,&head,x,y,dirty_hi[bank][row]-x,row?22:14,x,y,1,255,hw,seq);
 }
 for(unsigned row=0;row<2;row++){dirty_lo[bank][row]=640;dirty_hi[bank][row]=0;}
 if(hw)fs_batch_end_write_through();else fs_cache_sync();return rc;
}
int sh_render_frame(const sh_game *g,uint32_t fb,int hw,uint32_t fps,uint32_t *seq){
 if((fb!=SH_FB0 && fb!=SH_FB1)||!g||!seq)return FS_BAD_PARAM;
 sh_stats=(sh_render_stats){0};
 /* CPU may read pixels last written by DMA; invalidate only on that path. */
 if(!hw)fs_cache_sync();
 uint64_t t=stamp();hud(g,hw,fps);
 sh_stats.hud_build_us=elapsed(t); /* CPU edits materials before DMA ownership. */
 fs_surface full={fb,640,480,1280},play={fb+SH_TOP*1280,640,480-SH_TOP,1280};
 fs_image bg={sh_background_mode()==1?SH_NATIVE_BG:SH_BG,640,480-SH_TOP,1280},head={SH_HUD,640,SH_TOP,1280};
 if(hw)fs_batch_begin_write_through();
 int rc=0;unsigned offset=0; /* frozen background, no animation */
#define DRAW(dst,src,sx,sy,w,h,x,y,op,a) do{rc=draw(dst,src,sx,sy,w,h,x,y,op,a,hw,seq);if(rc)goto done;}while(0)
 t=stamp();unsigned bank=fb==SH_FB1;
 if(!hud_present[bank]){DRAW(&full,&head,0,0,640,SH_TOP,0,0,1,255);hud_present[bank]=1;}
 else for(unsigned row=0;row<2;row++)if(dirty_lo[bank][row]<dirty_hi[bank][row]){
  unsigned x=dirty_lo[bank][row],y=row?26:4;
  DRAW(&full,&head,x,y,dirty_hi[bank][row]-x,row?22:14,(int)x,(int)y,1,255);
 }
 for(unsigned row=0;row<2;row++){dirty_lo[bank][row]=640;dirty_hi[bank][row]=0;}
 sh_stats.hud_copy_us=elapsed(t);t=stamp();
 if(hw){command_bank=0;collecting=1;line_count=0;line_dst=play.data;for(unsigned b=0;b<8;b++)bin_count[b]=0;list_start=stamp();}
 else if(!sh_background_enabled()){
  if(hw){
   const fs_command cmd={.op=0,.dst=play.data,.dst_stride=play.stride,
     .width=play.width,.height=play.height,.color=0,.seq=++*seq};
   rc=fs_submit(&cmd);if(rc)goto done;
   rc=fs_wait(cmd.seq,4000000);if(rc)goto done;
  }else solid(play.data,play.width,play.height,0);
 }else{
  if(offset)DRAW(&play,&bg,0,432-offset,640,offset,0,0,1,255);
  DRAW(&play,&bg,0,0,640,432-offset,0,(int)offset,1,255);
 }
 sh_stats.background_us=elapsed(t);t=stamp();
 for(unsigned i=0;i<SH_BULLETS;i++){
  const sh_actor *a=g->bullets+i;if(!a->active)continue;
  unsigned kind=g->rain[i].kind;
  const fs_image *im=sh_textures+(kind==2?SH_ORB_TEX:(kind==3||kind==4)?SH_PLAIN_TEX:SH_RAIN_TEX);
  DRAW(&play,im,0,0,im->width,im->height,a->x/SH_Q,a->y/SH_Q-SH_TOP,sh_demo_effect,sh_demo_alpha);
 }
 sh_stats.sprites_us=elapsed(t);
done:
 if(hw && !rc)rc=flush_lines(seq);
 sh_stats.sprites_us=elapsed(t);
 if(hw)fs_batch_end_write_through();else fs_cache_sync();
#undef DRAW
 return rc;
}

int sh_commands_begin(sh_frame_commands *f,unsigned bank,unsigned bg){
 if(!f || bank>1 || bg>2)return FS_BAD_PARAM;
 if(collecting || (owned_banks&(1u<<bank)))return FS_BUSY;
 *f=(sh_frame_commands){.bank=bank,.bg_mode=bg,.dma=(unsigned)command_dma};
 command_bank=bank;collecting=build_only=1;line_count=0;line_dst=SH_FB0+SH_TOP*1280;
 for(unsigned b=0;b<8;b++)bin_count[b]=0;
 return FS_OK;
}
int sh_commands_append(sh_frame_commands *f,const sh_game *g,unsigned first,unsigned end){
 if(!f || !g || !build_only || !collecting || f->bank!=command_bank || f->ready || first>end || end>SH_BULLETS)return FS_BAD_PARAM;
 uint64_t t=stamp();fs_surface play={line_dst,640,432,1280};uint32_t unused=0;
 for(unsigned i=first;i<end;i++){
  const sh_actor *a=g->bullets+i;if(!a->active)continue;
  unsigned kind=g->rain[i].kind;
  const fs_image *im=sh_textures+(kind==2?SH_ORB_TEX:(kind==3||kind==4)?SH_PLAIN_TEX:SH_RAIN_TEX);
  int r=draw(&play,im,0,0,im->width,im->height,a->x/SH_Q,a->y/SH_Q-SH_TOP,sh_demo_effect,sh_demo_alpha,1,&unused);
  if(r)return r;
 }
 f->list_us+=elapsed(t);return FS_OK;
}
int sh_commands_finish(sh_frame_commands *f){
 if(!f || !collecting || !build_only || f->bank!=command_bank || f->ready)return FS_BAD_PARAM;
 for(unsigned b=0;b<8;b++)f->count[b]=bin_count[b];
 f->descriptors=line_count;collecting=build_only=0;
 __asm__ volatile("fence rw,rw":::"memory");f->ready=1;return FS_OK;
}
static int start_band(sh_frame_job *j,uint32_t *seq){
 const sh_frame_commands *c=j->frame;unsigned b=j->band,n=c->count[b];
 sl_frame f={j->fb+(SH_TOP+b*54)*1280,1280,640,54,
  c->bg_mode==1?SL_DDR_BACKGROUND:c->bg_mode==2?SL_CACHED_BACKGROUND(background_offset,b*54):0,
  32,++*seq,SH_NATIVE_BG+b*54*1280};
 uint64_t t=stamp();int r=c->dma?sl_render_dma(&f,bank_bucket(c->bank,b),n,command_address(c->bank,b)):sl_render(&f,bank_bucket(c->bank,b),n);
 sh_stats.submit_us+=elapsed(t);if(r)return r;
 sh_stats.descriptors+=n;sh_stats.bands++;j->seq=f.seq;return FS_OK;
}
int sh_frame_start(sh_frame_job *j,const sh_frame_commands *f,uint32_t fb,uint32_t *seq){
 if(!j || !f || !seq || !f->ready || f->bank>1 || (fb!=SH_FB0 && fb!=SH_FB1))return FS_BAD_PARAM;
 if(owned_banks || (fs_read(0x84)&1))return FS_BUSY;
 *j=(sh_frame_job){.frame=f,.fb=fb,.active=1};owned_banks=1u<<f->bank;
 int r=start_band(j,seq);if(r){owned_banks=0;j->active=0;}return r;
}
int sh_frame_poll(sh_frame_job *j,uint32_t *seq){
 if(!j || !seq || !j->active)return FS_BAD_PARAM;
 uint32_t s=fs_read(0x84);if(s&1)return 0;
 if(!(s&2) || fs_read(0xcc)!=j->seq || (s&4))return FS_HW_ERROR;
 sh_stats.core_cycles+=fs_read(0xc0);sh_stats.write_bytes+=fs_read(0xc8);sh_stats.read_bytes+=fs_read(0xc4);
 sh_stats.pixel_count+=fs_read(0xd4);sh_stats.skipped_pixels+=fs_read(0xf4);sh_stats.descriptor_visits+=fs_read(0xd8);
 sh_stats.command_bytes+=fs_read(0xf8);sh_stats.command_bursts+=fs_read(0xfc);
 if(++j->band==8){j->active=0;owned_banks=0;return 1;}
 int r=start_band(j,seq);return r?r:0;
}
