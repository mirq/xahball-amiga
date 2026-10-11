/* XAHBall - vector helpers: length, distance, normalize, dot, cross.
 * Same engine layout as amiga-shooter's physics/vecmath, by-value.
 */
#ifndef _XAHBALL_VECMATH_H_
#define _XAHBALL_VECMATH_H_

#include "ace/types.h"
#include "vector.h"

//---------------------------------------------------------------------- FUNCTIONS

fix16_t vecLength(tVector sVec);
fix16_t vecDistance(tVector vecA, tVector vecB);
fix16_t vecDot(tVector vecA, tVector vecB);
fix16_t vecCross(tVector vecA, tVector vecB);

// Squared length in 1/8px units (no sqrt): for broad-phase range tests.
// Compare against (fwRange >> 13)^2. Same quantization as vecLength.
ULONG vecLengthSquared8(tVector sVec);

// Length from a squared 1/8px value (single sqrt, no re-squaring).
fix16_t vecLengthFromD2(ULONG ulD2);

// Out-parameter normalize (heap-free): writes unit vector into *pOut.
// Returns 0 when the input length is zero (pOut untouched).
UBYTE vecNormalize(tVector *pOut, tVector sVec);

// Normalize with an already-known length (saves the second sqrt +
// one software division: single reciprocal + two multiplies).
// Returns 0 when fwLen is zero (pOut untouched).
UBYTE vecNormalizeWithLength(tVector *pOut, tVector sVec, fix16_t fwLen);

#endif // _XAHBALL_VECMATH_H_
