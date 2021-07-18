#ifndef _RANGE_H
#define _RANGE_H

#include <stdbool.h>

typedef struct {
  size_t base;
  size_t size;
} range_t;

inline bool InRange(range_t &r, size_t value, bool inclusive_low, bool inclusive_hi);
inline bool InRange(range_t &r, size_t value, bool inclusive_low, bool inclusive_hi) {
  register bool low_match = false;
  
  if (inclusive_low && value >= r.base)
    low_match = true;
  else if (!inclusive_low && value > r.base)
    low_match = true;

  if (!low_match) return false;

  if ((inclusive_hi && value <= (r.base + r.size)) ||
    (!inclusive_hi && value < (r.base + r.size)))
      return true;

  return false;
}

#endif
