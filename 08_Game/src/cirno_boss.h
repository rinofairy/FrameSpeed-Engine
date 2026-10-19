/* AI-assisted bare-metal adaptation. Pinned upstream sources and MIT notices
 * are in ../upstream/taisei. See README for deliberate rendering differences. */
#ifndef CIRNO_BOSS_H
#define CIRNO_BOSS_H
#include "taisei_game.h"
enum { CB_ICE0,CB_RAIN,CB_ICE1,CB_SNOW,CB_CASCADE,CB_BLIZZARD,CB_PHASES };
const char *cb_name(unsigned phase);
int cb_sn(int);int cb_cs(int);int cb_angle(int,int);
void cb_reset(sh_game *g);
void cb_stage_wave(sh_game *g,int x,int y,unsigned pattern);
void cb_pool_reset(void);
unsigned cb_alloc_bullet(sh_game *g);
void cb_step(sh_game *g);
void cb_hit(sh_game *g,unsigned damage);
void cb_move(sh_game *g,unsigned i);
int cb_beam(const sh_game *g,unsigned i,int *x1,int *y1,int *x2,int *y2,int *hot);
int cb_beam_hit(const sh_game *g);
void cb_toggle_god(sh_game *g);
#endif
