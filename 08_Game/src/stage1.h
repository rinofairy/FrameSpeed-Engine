/* Taisei Project stage1 fixed-point port. MIT upstream notices retained. */
#ifndef SH_STAGE1_H
#define SH_STAGE1_H
#include "taisei_game.h"
enum {ST_BURST=1,ST_CIRCLETOSS,ST_SINE,ST_CIRCLE,ST_DROP,ST_MULTI,ST_INSTANT,ST_WAVE,ST_EXPLOSION,ST_TRI};
enum {ST_CRYSTAL=16,ST_RICE,ST_THICK,ST_BALL,ST_BIG,ST_FLAKE,ST_SPLIT,ST_FROG,ST_FROZEN,ST_THAW,ST_MID_RICE};
void stage1_step(sh_game *);
void stage1_bullet_move(sh_game *,unsigned);
void stage1_mid_hit(sh_game *,unsigned);
unsigned stage1_enemy_hp(unsigned);
#endif
