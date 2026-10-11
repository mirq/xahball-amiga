/* XAHBall - flat body implementation */
#include "flatbody.h"

//---------------------------------------------------------------------- FUNCTIONS

void flatBodyInit(
	tFlatBody *pBody, int x, int y, int radius, int mass,
	fix16_t fwBounce, fix16_t fwWallBounce, UBYTE isStatic
) {
	pBody->sPos = initVector(x, y);
	pBody->sVel = initVector(0, 0);
	pBody->fwRadius = fix16_from_int(radius);
	pBody->fwMass = fix16_from_int(mass);
	// Inverse mass is read on every body pair every frame - fix16_div is far
	// too dear for that on a 68000, so it's computed once here
	pBody->fwInvMass = (isStatic || mass == 0) ? 0 : fix16_div(F16(1), pBody->fwMass);
	pBody->fwBounce = fwBounce;
	pBody->fwWallBounce = fwWallBounce;
	pBody->isStatic = isStatic;
	pBody->ubWallMode = FLAT_WALL_PLAY;
}
