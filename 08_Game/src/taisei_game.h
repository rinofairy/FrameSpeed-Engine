/* Bare-metal Taisei boss adaptation and project input/scheduler utilities. */
#ifndef TAISEI_GAME_H
#define TAISEI_GAME_H
#include <stdint.h>
#define SH_W 480
#define SH_VIEW_W 448
#define SH_VIEW_H 480
#define SH_H 560
#define SH_TOP 0
#define SH_ENEMIES 64
#define SH_SHOTS 96
#define SH_BULLETS 1280
#define SH_FX 64
#define SH_ITEMS 128
#define SH_Q 256
/* Visible center and collision radius in rendered pixels. */
#define SH_PLAYER_CENTER 16
#define SH_HIT_RADIUS 3
enum { SH_UP=1, SH_DOWN=2, SH_LEFT=4, SH_RIGHT=8, SH_FIRE=16, SH_SLOW=32 };
enum { SH_TITLE, SH_PLAY, SH_PAUSED, SH_OVER, SH_CLEAR };
typedef struct {
 int32_t x,y,vx,vy,ax,ay; /* Q16 for Taisei's small acceleration. */
} sh_rain_motion;
typedef struct {
 int32_t x,y,vx,vy; /* Q8 pixels; velocities are per fixed 1/60 s tick. */
 uint16_t age,cooldown;
 uint8_t active;uint32_t hp;
} sh_actor;
/* Bullets retain Q8 visible and Q16 motion positions; actor-only velocity
 * fields are removed, without changing the upstream motion precision. */
/* Q16 position lives once in rain[]; no redundant Q8 x/y copy. */
typedef struct {uint16_t age,cooldown;uint8_t active,hp,kind,pad;} sh_bullet;
typedef struct {int32_t tx,ty,ax,ay,origin_y;int32_t retention,attraction;uint16_t kind,exit_time,angle,round;uint8_t red,power,points,entered;uint16_t summon;} sh_enemy_motion;
typedef struct {uint32_t tick;uint16_t rank;uint8_t active,pad;} sh_volley;

typedef struct {
 sh_actor enemies[SH_ENEMIES],shots[SH_SHOTS];sh_bullet bullets[SH_BULLETS];
 sh_actor effects[SH_FX],items[SH_ITEMS];
 sh_enemy_motion em[SH_ENEMIES];sh_volley volleys[12];
 sh_rain_motion rain[SH_BULLETS];
 int32_t boss_target_x,boss_target_y;
 uint32_t rain_spawned,orb_spawned;
 int32_t px,py;
 uint32_t rng,ticks,score,dropped_spawns;
 uint32_t high_score,stage_ticks,graze,point_count,point_value,kills;
 uint16_t power,combo;uint8_t stage,background,slow;
 uint16_t active_bullets;
 uint16_t fire_cd,invincible,volley_cd,homing_cd,needle_cd,focus_cd;
 uint16_t respawn;
 uint8_t fire_side,previous_slow;uint16_t stage_cursor[3];
 uint32_t mid_start,mid_end,post_start,boss_power_damage,boss_damage_fraction;
 uint16_t mid_state,mid_wait,mid_index,mid_round,mid_generation,mid_thaw_generation,mid_thaw_index;
 uint32_t mid_thaw_due;int32_t mid_burst_angle;
 uint16_t route,route_tick;uint32_t stage_spawned,stage_killed,shot_spawned[4];
 uint8_t hp,state,level;
 uint32_t boss_hp,boss_max_hp,phase_ticks,phase_limit,boss_hits;
 uint16_t transition;
 uint8_t boss_phase,boss_captures,boss_timeouts,boss_misses,god,test_used;
 int16_t cage_x[14],cage_y[14];
 uint16_t cage_clock;
 uint8_t cage_count,cage_escapes;

} sh_game;
void sh_reset(sh_game *g,uint32_t seed);
void sh_reset_level(sh_game *g,uint32_t seed,unsigned level);
void sh_step(sh_game *g,uint32_t input);
static inline int32_t sh_bx(const sh_game *g,unsigned i){return g->rain[i].x/256;}
static inline int32_t sh_by(const sh_game *g,unsigned i){return g->rain[i].y/256;}
void sh_option_position(const sh_game *,unsigned,int32_t *,int32_t *);
void sh_enemy_damage(sh_game *,unsigned,unsigned);
void sh_step_begin(sh_game *g,uint32_t input);
void sh_step_bullets(sh_game *g,unsigned first,unsigned end);
void sh_award_phase(sh_game *g,int killed);
void sh_skip_to_boss(sh_game *g);
void sh_drop(sh_game *g,int x,int y,unsigned type,unsigned count);
void sh_score(sh_game *g,unsigned amount);
void sh_pause(sh_game *g);
unsigned sh_count(const sh_game *g);
/* UART: @HH sends full key bitmap (two hex digits), expires after 250 ms.
 * Ordinary terminal WASD/J are short pulses, not true key-up/down events. */
typedef struct { uint32_t mask,lease_ms; unsigned parse,nibble; } sh_input;
void sh_input_byte(sh_input *in,unsigned char c);
void sh_input_elapse(sh_input *in,uint32_t ms);
/* Fixed-rate scheduler: <=4 catch-up ticks; records dropped time explicitly. */
typedef struct { uint32_t phase,dropped_ticks; } sh_clock;
unsigned sh_clock_advance(sh_clock *clock,uint32_t elapsed_us);
#endif
