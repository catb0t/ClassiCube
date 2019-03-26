#ifndef CC_MATH_H
#define CC_MATH_H
#include "Core.h"
/* Simple math functions and constants. Also implements a RNG algorithm, based on
      Java's implementation from https://docs.oracle.com/javase/7/docs/api/java/util/Random.html
   Copyright 2017 ClassicalSharp | Licensed under BSD-3
*/

#define MATH_PI 3.1415926535897931f
#define MATH_DEG2RAD (MATH_PI / 180.0f)
#define MATH_RAD2DEG (180.0f / MATH_PI)
#define MATH_LARGENUM 1000000000.0f
#define MATH_POS_INF ((float)(1e+300 * 1e+300))

#define Math_Deg2Packed(x) ((uint8_t)((x) * 256.0f / 360.0f))
#define Math_Packed2Deg(x) ((x) * 360.0f / 256.0f)
#define Math_AbsU(x) ((uint32_t)(x))

float      Math_AbsF(float x);
float      Math_SqrtF(float x);
float      Math_Mod1(float x);
int        Math_AbsI(int x);

CC_API double Math_Sin(double x);
CC_API double Math_Cos(double x);
float Math_SinF(float x);
float Math_CosF(float x);

/* Computes loge(x). Can also be used to approximate logy(x). */
/* e.g. for log3(x), use: Math_Log(x)/log(3) */
double Math_Log(double x);
/* Computes e^x. Can also be used to approximate y^x. */
/* e.g. for 3^x, use: Math_Exp(log(3)*x) */
double Math_Exp(double x);
double Math_FastTan(double x);

int Math_Floor(float value);
int Math_Ceil(float value);
int Math_Log2(uint32_t value);
int Math_CeilDiv(int a, int b);
int Math_Sign(float value);

float Math_Lerp(float a, float b, float t);
/* Linearly interpolates between a given angle range, adjusting if necessary. */
float Math_LerpAngle(float leftAngle, float rightAngle, float t);

int Math_NextPowOf2(int value);
uint32_t Math_NextPowOf2U(uint32_t value);
bool Math_IsPowOf2(int value);
#define Math_Clamp(val, min, max) val = val < (min) ? (min) : val;  val = val > (max) ? (max) : val;

typedef uint64_t RNGState;
/* Initialises RNG using seed from current UTC time. */
void Random_SeedFromCurrentTime(RNGState* rnd);
/* Initialised RNG using the given seed. */
CC_API void Random_Seed(RNGState* rnd, int seed);

/* Returns integer from 0 inclusive to n exclusive */
CC_API int Random_Next(RNGState* rnd, int n);
/* Returns real from 0 inclusive to 1 exclusive */
CC_API float Random_Float(RNGState* rnd);
/* Returns integer from min inclusive to max exclusive */
static CC_INLINE int Random_Range(RNGState* rnd, int min, int max) {
	return min + Random_Next(rnd, max - min);
}
#endif
