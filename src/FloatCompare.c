#include "FloatCompare.h"

bool Compare_Eps (const float a, const float b, const float ce) {
  return Math_AbsF(a - b) < ce;
}
