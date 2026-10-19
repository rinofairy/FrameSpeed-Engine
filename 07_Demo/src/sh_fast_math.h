#ifndef SH_FAST_MATH_H
#define SH_FAST_MATH_H
#include <stdint.h>
static inline int sh_mix5(int velocity,int target){
 if(velocity>=-268435455&&velocity<=268435455&&target>=-268435455&&target<=268435455)
  return (velocity*4+target)/5;
 return (int)(((int64_t)velocity*4+target)/5);
}
#endif
