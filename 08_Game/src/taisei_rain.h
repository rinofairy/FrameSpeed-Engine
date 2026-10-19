/* Fixed-point adaptation; Taisei MIT notices in taisei_rain.c. */
#ifndef TAISEI_RAIN_H
#define TAISEI_RAIN_H
#include "taisei_game.h"
#define TR_LEVEL 4
#define TR_IS_LEVEL(g) ((g)->level>=4 && (g)->level<=7)
#define TR_DURATION 3600u
void tr_reset(sh_game *g);
void tr_emit(sh_game *g);
void tr_move(sh_game *g,unsigned index);
unsigned tr_bullet_size(const sh_game *g,unsigned index);
#endif
