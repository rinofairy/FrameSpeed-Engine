/* Expanded from pinned src/stages/stage1/timeline.c, MIT. */
#ifndef STAGE_EVENTS_H
#define STAGE_EVENTS_H
#include <stdint.h>
typedef struct {uint16_t tick,group;uint8_t kind,flags,power,points;int16_t x,y,tx,ty,vx,vy,ax,ay;uint16_t exit_time,section;} sh_stage_event;
#define STAGE_EVENTS_ADDR 0x5e1000u
#define STAGE_EVENT_COUNT 525
static const unsigned stage_event_count[3]={125,308,92};
static const unsigned stage_event_offset[3]={0,125,433};
#endif
