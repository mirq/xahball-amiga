/* XAHBall - 2D vector math on fix16 (16.16), same engine layout as
 * amiga-shooter's physics/vector module.
 */
#ifndef _XAHBALL_VECTOR_H_
#define _XAHBALL_VECTOR_H_

#include <fixmath/fixmath.h>

//------------------------------------------------------------------------ TYPES

typedef struct tVector {
	fix16_t X;
	fix16_t Y;
} tVector;

//---------------------------------------------------------------------- FUNCTIONS

// By-value helpers (the shooter heap-allocated every op; at 50 fps on a
// 68000 we must not allocate per call)
tVector initVector(int x, int y);
tVector addVectors(tVector vecA, tVector vecB);
tVector subVectors(tVector vecA, tVector vecB);
tVector scaleVector(tVector vecA, fix16_t fwScale);

#endif // _XAHBALL_VECTOR_H_
