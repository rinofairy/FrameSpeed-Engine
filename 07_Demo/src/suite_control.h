#ifndef SUITE_CONTROL_H
#define SUITE_CONTROL_H
#include <stdint.h>
enum { APP_MENU,APP_DEMO,APP_GAME };
enum { DEMO_FILL,DEMO_COPY32,DEMO_SPRITES };
typedef struct {unsigned mode,selected,demo_kind,count,texture,hardware,effect,alpha,bg_mode,native_available;} suite_control;
void suite_init(suite_control *s,int native_available);
int suite_command(suite_control *s,unsigned char key);
#endif
