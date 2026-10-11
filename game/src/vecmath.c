/* XAHBall - vector helpers implementation.
 * ACE fix16_mul returns fix16_overflow whenever the PRODUCT exceeds fix16
 * range, so squaring raw position deltas (up to ~300px = ~2e7 raw, squared
 * ~5e9) is illegal even though the 64-bit intermediate exists. Length is
 * therefore computed on 1/8px integers (deltas >> 13, squares < 2^25) -
 * same trick as the pre-flatworld physics. Dots/normalizes only ever touch
 * unit vectors and velocities (< 8 px/f), which are fix16_mul-safe.
 */
#include "vecmath.h"

//---------------------------------------------------------------------- FUNCTIONS

// Bit-by-bit integer sqrt, no FPU
static UWORD uwVecIntSqrt(ULONG ulVal) {
	ULONG ulRes = 0, ulBit = 1UL << 30;
	while(ulBit > ulVal) { ulBit >>= 2; }
	while(ulBit) {
		if(ulVal >= ulRes + ulBit) {
			ulVal -= ulRes + ulBit;
			ulRes = (ulRes >> 1) + ulBit;
		}
		else {
			ulRes >>= 1;
		}
		ulBit >>= 2;
	}
	return (UWORD)ulRes;
}

ULONG vecLengthSquared8(tVector sVec) {
	LONG lX8 = sVec.X >> 13; // delta in 1/8 px units
	LONG lY8 = sVec.Y >> 13;
	return (ULONG)(lX8 * lX8 + lY8 * lY8);
}

fix16_t vecLengthFromD2(ULONG ulD2) {
	if(!ulD2) { return 0; }
	// 1/8px length back to fix16 px
	return (fix16_t)(uwVecIntSqrt(ulD2) << 13);
}

fix16_t vecLength(tVector sVec) {
	return vecLengthFromD2(vecLengthSquared8(sVec));
}

fix16_t vecDistance(tVector vecA, tVector vecB) {
	tVector sD = subVectors(vecA, vecB);
	return vecLength(sD);
}

fix16_t vecDot(tVector vecA, tVector vecB) {
	return fix16_add(fix16_mul(vecA.X, vecB.X), fix16_mul(vecA.Y, vecB.Y));
}

fix16_t vecCross(tVector vecA, tVector vecB) {
	return fix16_sub(fix16_mul(vecA.X, vecB.Y), fix16_mul(vecA.Y, vecB.X));
}

UBYTE vecNormalizeWithLength(tVector *pOut, tVector sVec, fix16_t fwLen) {
	if(fwLen == 0) {
		return 0;
	}
	// Single software division (reciprocal) + two multiplies instead of
	// two divisions. fwLen is >= 1/8px whenever nonzero, so 1/fwLen fits.
	fix16_t fwInv = fix16_div(F16(1), fwLen);
	pOut->X = fix16_mul(sVec.X, fwInv);
	pOut->Y = fix16_mul(sVec.Y, fwInv);
	return 1;
}

UBYTE vecNormalize(tVector *pOut, tVector sVec) {
	return vecNormalizeWithLength(pOut, sVec, vecLength(sVec));
}
