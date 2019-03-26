#ifndef CC_FLOAT_COMPARE_H
#define CC_FLOAT_COMPARE_H
#include "Core.h"
#include "ExtMath.h"

#ifndef FLOAT_COMPARE_EPS
#define FLOAT_COMPARE_EPS ((float) 1e-11)
#endif

bool Compare_Eps (const float a, const float b, const float ce);

#ifndef CMP_EPS
#define CMP_EPS(a, b) (Compare_Eps(a, b, FLOAT_COMPARE_EPS))
#endif

#endif /* end of include guard: CC_FLOAT_COMPARE_H */
