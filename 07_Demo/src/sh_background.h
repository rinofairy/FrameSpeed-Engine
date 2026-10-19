#ifndef SH_BACKGROUND_H
#define SH_BACKGROUND_H
#include <stdint.h>
#define SH_NATIVE_BG 0x740000u
int sh_background_mode(void);
void sh_background_set_mode(unsigned mode);
void sh_background_next(void);
void sh_background_enable(int on);
int sh_background_enabled(void);
uint16_t sh_background_pixel(unsigned x,unsigned y);
#endif
