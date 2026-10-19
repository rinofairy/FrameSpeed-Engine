#ifndef SUITE_SWITCH_H
#define SUITE_SWITCH_H
/* Executed from DDR: CPU RAM can be overwritten only after this jump. */
static inline __attribute__((noreturn)) void suite_switch_app(unsigned app){
 ((void (*)(unsigned))0x01000000u)(app);__builtin_unreachable();
}
#endif
