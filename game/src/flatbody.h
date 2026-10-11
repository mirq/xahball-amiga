/* XAHBall - flat rigid body: position/velocity vectors, mass, radius,
 * bounce coefficient, wall-interaction mode. Same layout as amiga-shooter's
 * FlatBody, with bounce + wall mode added for full collision response.
 */
#ifndef _XAHBALL_FLATBODY_H_
#define _XAHBALL_FLATBODY_H_

#include "ace/types.h"
#include "fixmath/fixmath.h"
#include "vector.h"

//---------------------------------------------------------------------- DEFINES

// How a body interacts with the pitch boundary (goal mouths / nets)
#define FLAT_WALL_PLAY   0 // goal mouths fully open (goal check is caller's job)
#define FLAT_WALL_NET    1 // boxed by the net (ball during goal celebration)
#define FLAT_WALL_PLAYER 2 // may enter the mouth, net boxes it inside

//------------------------------------------------------------------------ TYPES

typedef struct tFlatBody {
	tVector sPos;
	tVector sVel;
	fix16_t fwRadius;  // fix16 px
	fix16_t fwMass;    // 0 = static (infinite mass)
	fix16_t fwInvMass; // precomputed 1/mass (0 when static) - solver uses it every pair
	fix16_t fwBounce;  // disc-vs-disc restitution contribution (bCoef)
	fix16_t fwWallBounce; // restitution vs walls/posts
	UBYTE ubWallMode;  // FLAT_WALL_*
	UBYTE isStatic;    // 1: never integrates, immovable in collisions
} tFlatBody;

//---------------------------------------------------------------------- FUNCTIONS

// Initializes an inline body (no heap - bodies live for the whole match)
void flatBodyInit(
	tFlatBody *pBody, int x, int y, int radius, int mass,
	fix16_t fwBounce, fix16_t fwWallBounce, UBYTE isStatic
);

#endif // _XAHBALL_FLATBODY_H_
