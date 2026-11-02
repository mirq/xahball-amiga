/* XAHBall - fast fix16 multiply/divide for the 68000.
 * ACE's libfixmath fix16_mul does a full int64 multiply (__muldi3 in
 * libgcc: several 32x32 software multiplies, ~1-2k cycles per call on a
 * 68000) plus overflow checks. The game calls it hundreds of times a frame,
 * which alone pushed a 2-player match past one PAL frame.
 *
 * Also replaces fix16_div (see fix16FastDiv below).
 *
 * fix16FastMul builds the same product from four 16x16 hardware multiplies
 * (muls.w/mulu.w). It is BIT-EXACT with the library under
 * FIXMATH_NO_ROUNDING (floor of product >> 16) for every result that fits
 * in fix16 range; out-of-range products wrap instead of returning
 * fix16_overflow (the game never relies on that - see vecmath.c).
 *
 * Include this instead of <fixmath/fixmath.h>: it redirects fix16_mul and
 * fix16_div. tools/host_sim.c compiles the same header (C fallbacks), so
 * host simulation stays bit-identical to the Amiga build.
 */
#ifndef _XAHBALL_FASTFIX_H_
#define _XAHBALL_FASTFIX_H_

#include <fixmath/fixmath.h>
#include <stdint.h>

// 16x16->32 hardware multiplies. Bartman's GCC turns even a plain
// (int32_t)int16 * int16 into a __mulsi3 call (~300+ cycles), so the
// 68000 instructions are spelled out. Host builds (tools/host_sim.c) get
// the equivalent C.
static inline int32_t fixMulS16(int16_t wA, int16_t wB) {
#if defined(__mc68000__) || defined(__m68k__)
	int32_t lRes = wA;
	__asm__("muls.w %1,%0" : "+d"(lRes) : "dmi"(wB) : "cc");
	return lRes;
#else
	return (int32_t)wA * wB;
#endif
}

static inline uint32_t fixMulU16(uint16_t uwA, uint16_t uwB) {
#if defined(__mc68000__) || defined(__m68k__)
	uint32_t ulRes = uwA;
	__asm__("mulu.w %1,%0" : "+d"(ulRes) : "dmi"(uwB) : "cc");
	return ulRes;
#else
	return (uint32_t)uwA * uwB;
#endif
}

// (s * b) >> 16 for a fraction s = sh*2^16 + sl with sh in {0, -1}, i.e.
// |s| < 1: damping constants, unit normals, mass shares, restitution - one
// side of most game multiplies. Two hardware multiplies instead of four:
// sh = 0 drops the hi and sh*bl terms; sh = -1 makes them sum to -b.
static inline fix16_t fix16MulFrac(uint16_t uwSl, int16_t wSh, fix16_t fwB) {
	int16_t wBh = (int16_t)(fwB >> 16);
	uint32_t ulRes = (uint32_t)fixMulS16((int16_t)uwSl, wBh)
		+ (fixMulU16(uwSl, (uint16_t)fwB) >> 16);
	if(uwSl & 0x8000) { ulRes += (uint32_t)wBh << 16; }
	if(wSh) { ulRes -= (uint32_t)fwB; }
	return (fix16_t)ulRes;
}

static inline fix16_t fix16FastMul(fix16_t fwA, fix16_t fwB) {
	int16_t wAh = (int16_t)(fwA >> 16);
	int16_t wBh = (int16_t)(fwB >> 16);
	uint16_t uwAl = (uint16_t)fwA;
	uint16_t uwBl = (uint16_t)fwB;
	// High word 0 or -1 <=> (high + 1) in {0, 1} as unsigned
	if((uint16_t)(wAh + 1) <= 1) { return fix16MulFrac(uwAl, wAh, fwB); }
	if((uint16_t)(wBh + 1) <= 1) { return fix16MulFrac(uwBl, wBh, fwA); }

	// a = ah*2^16 + al (al unsigned), same for b. Then
	// (a*b) >> 16 = (ah*bh << 16) + ah*bl + al*bh + (al*bl >> 16), exactly,
	// since every dropped bit lives in the al*bl term.
	uint32_t ulHi = (uint32_t)fixMulS16(wAh, wBh);
	// Signed 16x16 for the cross terms; an unsigned low half >= 0x8000 reads
	// as (x - 2^16) when signed, so add the other high half back << 16
	uint32_t ulMid = (uint32_t)fixMulS16(wAh, (int16_t)uwBl)
		+ (uint32_t)fixMulS16((int16_t)uwAl, wBh);
	if(uwBl & 0x8000) { ulMid += (uint32_t)wAh << 16; }
	if(uwAl & 0x8000) { ulMid += (uint32_t)wBh << 16; }
	uint32_t ulLo = fixMulU16(uwAl, uwBl);
	return (fix16_t)((ulHi << 16) + ulMid + (ulLo >> 16));
}

#define fix16_mul(a, b) fix16FastMul((a), (b))

// 32/16 hardware divide: returns quotient in the low word, remainder in the
// high word. Precondition: (ulN >> 16) < uwD (quotient fits 16 bits).
static inline uint32_t fixDivU16(uint32_t ulN, uint16_t uwD) {
#if defined(__mc68000__) || defined(__m68k__)
	__asm__("divu.w %1,%0" : "+d"(ulN) : "dmi"(uwD) : "cc");
	return ulN;
#else
	return ((ulN % uwD) << 16) | (ulN / uwD);
#endif
}

// fix16 division: EXACT trunc(a * 2^16 / b). The library runs 1-3 rounds of
// software 32/32 divide + modulo + clz (~2k+ cycles on a 68000), and its
// "kick-start" shortcut for divisors >= 16px returns one ulp too high in
// ~0.1% of cases; this is Knuth's algorithm D with 16-bit digits (Hacker's
// Delight divlu): two divu.w plus a few mulu.w. Verified against 64-bit
// math on ~84M random pairs. Zero divisors and results that would overflow
// fix16 go to the library, so its special return values are kept.
static inline fix16_t fix16FastDiv(fix16_t fwA, fix16_t fwB) {
	uint32_t ulA = (fwA >= 0) ? (uint32_t)fwA : -(uint32_t)fwA;
	uint32_t ulB = (fwB >= 0) ? (uint32_t)fwB : -(uint32_t)fwB;
	// |q| < 2^31 iff A < B * 2^15. Also rules out b == 0.
	if((ulA >> 15) >= ulB) {
		return (fix16_div)(fwA, fwB);
	}

	// Dividend A * 2^16 as u1:u0, with u1 < B (from the check above)
	uint32_t ulU1 = ulA >> 16;
	uint32_t ulU0 = ulA << 16;

	// Normalize: divisor top bit set (68000 has no clz instruction)
	uint16_t uwS = 0;
	uint32_t ulV = ulB;
	if(!(ulV & 0xFFFF0000)) { ulV <<= 16; uwS = 16; }
	if(!(ulV & 0xFF000000)) { ulV <<= 8; uwS += 8; }
	while(!(ulV & 0x80000000)) { ulV <<= 1; ++uwS; }
	uint16_t uwVn1 = (uint16_t)(ulV >> 16);
	uint16_t uwVn0 = (uint16_t)ulV;
	uint32_t ulUn32 = uwS ? ((ulU1 << uwS) | (ulU0 >> (32 - uwS))) : ulU1;
	uint32_t ulUn10 = ulU0 << uwS;
	uint16_t uwUn1 = (uint16_t)(ulUn10 >> 16);
	uint16_t uwUn0 = (uint16_t)ulUn10;

	// High quotient digit. Estimate from the top divisor digit, capped at
	// 0xFFFF (divu.w would overflow), then at most two corrections.
	uint32_t ulQ1, ulRhat;
	if((ulUn32 >> 16) >= uwVn1) {
		ulQ1 = 0xFFFF;
		ulRhat = ulUn32 - fixMulU16(0xFFFF, uwVn1);
	}
	else {
		uint32_t ulDr = fixDivU16(ulUn32, uwVn1);
		ulQ1 = ulDr & 0xFFFF;
		ulRhat = ulDr >> 16;
	}
	while(ulRhat < 0x10000 &&
		fixMulU16((uint16_t)ulQ1, uwVn0) > ((ulRhat << 16) | uwUn1)) {
		--ulQ1;
		ulRhat += uwVn1;
	}
	// Partial remainder (mod 2^32 arithmetic is exact here, true value < v)
	uint32_t ulUn21 = (ulUn32 << 16) + uwUn1
		- ((fixMulU16((uint16_t)ulQ1, uwVn1) << 16) + fixMulU16((uint16_t)ulQ1, uwVn0));

	// Low quotient digit, same procedure
	uint32_t ulQ0;
	if((ulUn21 >> 16) >= uwVn1) {
		ulQ0 = 0xFFFF;
		ulRhat = ulUn21 - fixMulU16(0xFFFF, uwVn1);
	}
	else {
		uint32_t ulDr = fixDivU16(ulUn21, uwVn1);
		ulQ0 = ulDr & 0xFFFF;
		ulRhat = ulDr >> 16;
	}
	while(ulRhat < 0x10000 &&
		fixMulU16((uint16_t)ulQ0, uwVn0) > ((ulRhat << 16) | uwUn0)) {
		--ulQ0;
		ulRhat += uwVn1;
	}

	uint32_t ulQ = (ulQ1 << 16) | ulQ0;
	return ((fwA ^ fwB) < 0) ? -(fix16_t)ulQ : (fix16_t)ulQ;
}

#define fix16_div(a, b) fix16FastDiv((a), (b))

#endif // _XAHBALL_FASTFIX_H_
