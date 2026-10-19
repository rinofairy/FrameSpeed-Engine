#ifndef SHOOTER_RENDER_H
#define SHOOTER_RENDER_H
#include "taisei_game.h"
#include "fs_draw.h"
#define SH_FB0 0x800000u
#define SH_FB1 0x900000u
#define SH_BG 0x680000u
#define SH_HUD 0xa00000u
#define SH_KEY 0xf81fu
enum { SH_PLAYER_TEX,SH_ENEMY_TEX,SH_SHOT_TEX,SH_HIT_TEX,SH_RAIN_TEX,SH_ORB_TEX,SH_PLAIN_TEX,SH_DUAL_TEX,SH_TEXTURES };
extern const fs_image sh_textures[SH_TEXTURES];
typedef struct {uint32_t hud_build_us,hud_copy_us,background_us,sprites_us;
 uint32_t list_us,bin_us,submit_us,engine_wait_us,core_cycles,write_bytes,descriptors,bands,read_bytes;
 uint32_t pixel_count,skipped_pixels,descriptor_visits,command_bytes,command_bursts;} sh_render_stats;
extern sh_render_stats sh_stats;
#ifdef SH_OFFLINE_TEST
volatile uint16_t *sh_memory(uint32_t address);
#else
static inline volatile uint16_t *sh_memory(uint32_t address){return (volatile uint16_t *)(uintptr_t)address;}
#endif
void sh_assets_init(void);
/* Same optimization for CPU and FPGA. Disable only for reference/profiling.
 * Metadata is computed at asset initialization with SH_KEY; no per-frame scan.
 */
void sh_trim_enable(int enabled);
void sh_span_enable(int enabled);
void sh_command_dma(int enabled);
int sh_command_dma_enabled(void);
void sh_renderer_demo(int enabled);
int sh_render_header(const sh_game *,uint32_t,int,uint32_t,uint32_t *);
extern unsigned sh_demo_effect,sh_demo_alpha;
void sh_clear_enable(int enable);
int sh_clear_enabled(void);
/* Shared draw list and blend rules for CPU and FPGA, no scene mutation.
 * Includes scrolling background, sprites, alpha effects, HUD, title/over overlay.
 * Actual frame switch is the platform's responsibility after success. */
int sh_render_frame(const sh_game *game,uint32_t fb,int hardware,uint32_t fps10,uint32_t *sequence);
/* Two independent DDR command lists. Framebuffer double buffering is separate.
 * Build only the CPU-owned bank. Finish publishes it; never edit an executing
 * bank. Service the executor between short CPU preparation chunks. */
typedef struct {
 unsigned bank,bg_mode,dma,ready,count[8],descriptors,list_us;
} sh_frame_commands;
typedef struct {
 const sh_frame_commands *frame;uint32_t fb,seq;unsigned band,active;
} sh_frame_job;
int sh_commands_begin(sh_frame_commands *,unsigned bank,unsigned background);
int sh_commands_append(sh_frame_commands *,const sh_game *,unsigned first,unsigned end);
int sh_commands_finish(sh_frame_commands *);
int sh_frame_start(sh_frame_job *,const sh_frame_commands *,uint32_t fb,uint32_t *seq);
/* 0 = in flight, 1 = complete, negative = driver error. No busy wait. */
int sh_frame_poll(sh_frame_job *,uint32_t *seq);
#endif
