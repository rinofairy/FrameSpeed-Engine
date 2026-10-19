#ifndef TAISEI_RENDER_H
#define TAISEI_RENDER_H
#include "taisei_game.h"
#include "fs_draw.h"
#include "fs_scanline.h"
#include "game_assets.h"
#define SH_FB0 0x800000u
#define SH_FB1 0x900000u
#define SH_HUD 0xa00000u
#define SH_KEY 0xf81fu
#define SH_BANDS 16
#define SH_BAND_H 30
#define SH_DRAW_ITEMS (64+SH_SHOTS*3+SH_BULLETS+SH_ENEMIES+1+SH_ITEMS+SH_FX+16+644)
#ifdef SH_OFFLINE_TEST
volatile uint16_t *sh_memory(uint32_t address);
#else
static inline volatile uint16_t *sh_memory(uint32_t address){return (volatile uint16_t *)(uintptr_t)address;}
#endif
/* HUD snapshot belongs to the rendered frame, not the concurrently updated game. */
typedef struct {uint32_t score,high,graze,points,power,hp,count,stage,phase,seconds,state,god,background,tick;} sh_ui_state;
typedef struct {unsigned bank,ready,count[SH_BANDS],descriptors,list_us,scroll;sh_ui_state ui;} sh_frame_commands;
typedef struct {const sh_frame_commands *frame;uint32_t fb,seq;unsigned band,active;} sh_frame_job;
typedef struct {uint32_t core_cycles,read_bytes,write_bytes,descriptors,pixels,submit_us,list_us,visits,hud_us;} sh_render_stats;
extern sh_render_stats sh_stats;
int sh_assets_init(void);
int sh_assets_valid(void);
void sh_snapshot(sh_ui_state *,const sh_game *);
int sh_commands_begin(sh_frame_commands *,unsigned bank,const sh_game *);
int sh_commands_append(sh_frame_commands *,const sh_game *,unsigned first,unsigned end);
int sh_commands_finish(sh_frame_commands *);
int sh_render_panel(const sh_ui_state *,uint32_t fb,unsigned fps10,uint32_t *seq);
int sh_frame_start(sh_frame_job *,const sh_frame_commands *,uint32_t fb,uint32_t *seq);
int sh_frame_poll(sh_frame_job *,uint32_t *seq);
int sh_reference(const sh_frame_commands *,uint32_t fb);
#endif
