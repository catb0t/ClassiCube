#ifndef CS_TYPEDEFS_H
#define CS_TYPEDEFS_H
/* Ensures variables are of a fixed size.
   Copyright 2017 ClassicalSharp | Licensed under BSD-3
*/

#if _MSC_VER
typedef unsigned __int8  UInt8;
typedef unsigned __int16 UInt16;
typedef unsigned __int32 UInt32;
typedef unsigned __int64 UInt64;

typedef signed __int8  Int8;
typedef signed __int16 Int16;
typedef signed __int32 Int32;
typedef signed __int64 Int64;
#elif (__STDC_VERSION__ >= 199901L)
#include <stdint.h>
typedef uint8_t  UInt8;
typedef uint16_t UInt16;
typedef uint32_t UInt32;
typedef uint64_t UInt64;

typedef int8_t Int8;
typedef int16_t Int16;
typedef int32_t Int32;
typedef int64_t Int64;
#else
#error "I didn't add typedefs for this compiler. You'll need to define them in Typedefs.h!'"
#endif

typedef float  Real32;
typedef double Real64;

typedef UInt8 bool;
#define true 1
#define false 0
#define NULL 0

typedef UInt8 BlockID;
typedef UInt8 EntityID;

#define USE16_BIT FALSE
#define STRING_SIZE 64

// TODO: put this in project defines, not here
#define USE_DX true
#endif