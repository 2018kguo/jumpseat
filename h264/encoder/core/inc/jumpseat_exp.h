// jumpseat experiments: JUMPSEAT_EXP is a bitmask of encoder changes under
// test in bench/. Not for shipping: winners become plain code.
#ifndef JUMPSEAT_EXP_H
#define JUMPSEAT_EXP_H
#include <stdlib.h>
static inline int JumpseatExp (int iBit) {
  static int s_iMask = -1;
  if (s_iMask < 0) {
    const char* p = getenv ("JUMPSEAT_EXP");
    s_iMask = p ? atoi (p) : 0;
  }
  return (s_iMask >> iBit) & 1;
}
#endif
