/* Bare-metal Taisei boss adaptation and project input/scheduler utilities. */
#ifndef TAISEI_GAME_H
#define TAISEI_GAME_H
#include <stdint.h>
#define SH_W 640
#define SH_H 480
#define SH_TOP 48
#define SH_ENEMIES 24
#define SH_SHOTS 96
#define SH_BULLETS 2048
#define SH_FX 16
#define SH_ITEMS 8
#define SH_Q 256
/* Visible center and collision radius in rendered pixels. */
#define SH_PLAYER_CENTER 16
#define SH_HIT_RADIUS 3
enum { SH_UP=1, SH_DOWN=2, SH_LEFT=4, SH_RIGHT=8, SH_FIRE=16, SH_SLOW=32 };
enum { SH_TITLE, SH_PLAY, SH_PAUSED, SH_OVER, SH_CLEAR };
/* FS_DEMO_COMPACT is for this Demo-only build, never the game build. */
typedef struct {
#ifndef FS_DEMO_COMPACT
 int32_t x,y,vx,vy,ax,ay; /* Q16 for Taisei's small acceleration. */
#endif
 uint8_t kind;
} sh_rain_motion;
typedef struct {
 int32_t x,y; /* Q8 display positions. */
#ifndef FS_DEMO_COMPACT
 int32_t vx,vy; /* Only the game needs velocity, age and cooldown. */
 uint16_t age,cooldown;
#endif
 uint8_t active,hp;
} sh_actor;
typedef struct {
 sh_actor enemies[SH_ENEMIES],shots[SH_SHOTS],bullets[SH_BULLETS];
 sh_actor effects[SH_FX],items[SH_ITEMS];
 sh_rain_motion rain[SH_BULLETS];
 int32_t boss_target_x,boss_target_y;
 uint32_t rain_spawned,orb_spawned;
 int32_t px,py;
 uint32_t rng,ticks,score,dropped_spawns;
 uint16_t fire_cd,invincible;
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
