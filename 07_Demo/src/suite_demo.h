#ifndef SUITE_DEMO_H
#define SUITE_DEMO_H
#include "taisei_render.h"
#include "suite_control.h"
void suite_demo_prepare(sh_game *,const suite_control *,unsigned frame);
void suite_demo_prepare_begin(sh_game *,const suite_control *,unsigned frame);
void suite_demo_prepare_range(sh_game *,const suite_control *,unsigned frame,unsigned first,unsigned end);
int suite_demo_render(const sh_game *,const suite_control *,uint32_t fb,unsigned fps,uint32_t *seq);
#endif
