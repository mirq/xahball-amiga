/* XAHBall - flat world implementation: full collision response */
#include "flatworld.h"

#include "vecmath.h"

//---------------------------------------------------------------------- DEFINES

#define WALL_BOUNCE F16(0.7f)  // pitch walls & frame restitution
#define NET_BOUNCE F16(0.4f)   // goal net (dampened catch)

//---------------------------------------------------------------------- FUNCTIONS

// Resolve one axis while retaining the part of this frame's travel that came
// after impact. Snapping to the boundary discards that travel and makes fast
// bounces visibly pause for one frame.
static void flatCollideAxisMin(
	fix16_t *pfwPos, fix16_t *pfwVel, fix16_t fwLimit, fix16_t fwRest
) {
	if(*pfwPos >= fwLimit) { return; }
	fix16_t fwOver = fix16_sub(fwLimit, *pfwPos);
	if(*pfwVel < 0) {
		*pfwPos = fix16_add(fwLimit, fix16_mul(fwOver, fwRest));
		*pfwVel = -fix16_mul(*pfwVel, fwRest);
	}
	else {
		*pfwPos = fwLimit;
	}
}

static void flatCollideAxisMax(
	fix16_t *pfwPos, fix16_t *pfwVel, fix16_t fwLimit, fix16_t fwRest
) {
	if(*pfwPos <= fwLimit) { return; }
	fix16_t fwOver = fix16_sub(*pfwPos, fwLimit);
	if(*pfwVel > 0) {
		*pfwPos = fix16_sub(fwLimit, fix16_mul(fwOver, fwRest));
		*pfwVel = -fix16_mul(*pfwVel, fwRest);
	}
	else {
		*pfwPos = fwLimit;
	}
}

void flatWorldInit(tFlatWorld *pWorld) {
	pWorld->ubBodyCount = 0;
	// Goal posts: static discs on the four mouth corners (matches the 2x2
	// px posts drawn by pitch.c)
	flatBodyInit(&pWorld->pPosts[0], PITCH_X0, GOAL_Y0, POST_R, 0, WALL_BOUNCE, WALL_BOUNCE, 1);
	flatBodyInit(&pWorld->pPosts[1], PITCH_X0, GOAL_Y1, POST_R, 0, WALL_BOUNCE, WALL_BOUNCE, 1);
	flatBodyInit(&pWorld->pPosts[2], PITCH_X1, GOAL_Y0, POST_R, 0, WALL_BOUNCE, WALL_BOUNCE, 1);
	flatBodyInit(&pWorld->pPosts[3], PITCH_X1, GOAL_Y1, POST_R, 0, WALL_BOUNCE, WALL_BOUNCE, 1);
}

void flatWorldAdd(tFlatWorld *pWorld, tFlatBody *pBody) {
	if(pWorld->ubBodyCount < FLAT_WORLD_MAX_BODIES) {
		pWorld->pBodies[pWorld->ubBodyCount++] = pBody;
	}
}

// Constraint against a static circle. ubInside=1: body must stay INSIDE
// radius fwMinDist (rounded corner arcs); ubInside=0: outside (posts).
// Violations are corrected positionally and reflected with fwRest.
static void flatCollideStaticDisc(
	tFlatBody *pBody, WORD wCx, WORD wCy, fix16_t fwMinDist, fix16_t fwRest, UBYTE ubInside
) {
	tVector sCenter = initVector(wCx, wCy);
	tVector sD = subVectors(pBody->sPos, sCenter);
	fix16_t fwDist = vecLength(sD);
	if(ubInside) {
		if(fwDist <= fwMinDist) { return; }
	}
	else {
		if(fwDist == 0 || fwDist >= fwMinDist) { return; }
	}
	tVector sN;
	if(!vecNormalizeWithLength(&sN, sD, fwDist)) { return; }
	fix16_t fwDot = vecDot(pBody->sVel, sN);
	UBYTE ubViolates = ubInside ? (fwDot > 0) : (fwDot < 0);
	fix16_t fwOver = ubInside ? fix16_sub(fwDist, fwMinDist) : fix16_sub(fwMinDist, fwDist);
	if(ubViolates) {
		fwOver = fix16_add(fwOver, fix16_mul(fwOver, fwRest));
	}
	if(ubInside) {
		pBody->sPos = subVectors(pBody->sPos, scaleVector(sN, fwOver));
	}
	else {
		pBody->sPos = addVectors(pBody->sPos, scaleVector(sN, fwOver));
	}
	if(!ubViolates) { return; }
	pBody->sVel = subVectors(
		pBody->sVel, scaleVector(sN, fix16_mul(fwDot, fix16_add(F16(1), fwRest)))
	);
}

// Rounded pitch bounds for one body, honoring its wall mode
static void flatCollideBounds(tFlatBody *pBody) {
	fix16_t fwR = pBody->fwRadius;
	fix16_t fwRest = fix16_mul(pBody->fwWallBounce, WALL_BOUNCE);
	if(pBody->ubWallMode == FLAT_WALL_NET) {
		fwRest = fix16_mul(pBody->fwWallBounce, NET_BOUNCE);
	}

	// Rounded corners first - the arc supersedes the straight walls of its
	// corner square
	if(pBody->sPos.X < fix16_from_int(PITCH_X0 + CORNER_R)) {
		if(pBody->sPos.Y < fix16_from_int(PITCH_Y0 + CORNER_R)) {
			flatCollideStaticDisc(pBody, PITCH_X0 + CORNER_R, PITCH_Y0 + CORNER_R,
				fix16_sub(fix16_from_int(CORNER_R), fwR), fwRest, 1);
		}
		else if(pBody->sPos.Y > fix16_from_int(PITCH_Y1 - CORNER_R)) {
			flatCollideStaticDisc(pBody, PITCH_X0 + CORNER_R, PITCH_Y1 - CORNER_R,
				fix16_sub(fix16_from_int(CORNER_R), fwR), fwRest, 1);
		}
	}
	else if(pBody->sPos.X > fix16_from_int(PITCH_X1 - CORNER_R)) {
		if(pBody->sPos.Y < fix16_from_int(PITCH_Y0 + CORNER_R)) {
			flatCollideStaticDisc(pBody, PITCH_X1 - CORNER_R, PITCH_Y0 + CORNER_R,
				fix16_sub(fix16_from_int(CORNER_R), fwR), fwRest, 1);
		}
		else if(pBody->sPos.Y > fix16_from_int(PITCH_Y1 - CORNER_R)) {
			flatCollideStaticDisc(pBody, PITCH_X1 - CORNER_R, PITCH_Y1 - CORNER_R,
				fix16_sub(fix16_from_int(CORNER_R), fwR), fwRest, 1);
		}
	}

	UBYTE ubInMouth = pitchIsInGoalMouth((UWORD)fix16_to_int(pBody->sPos.Y));

	// Left wall / goal
	if(pBody->sPos.X < fix16_add(fix16_from_int(PITCH_X0), fwR)) {
		if(pBody->ubWallMode == FLAT_WALL_PLAY) {
			if(!ubInMouth) {
				flatCollideAxisMin(&pBody->sPos.X, &pBody->sVel.X,
					fix16_add(fix16_from_int(PITCH_X0), fwR), fwRest);
			}
		}
		else {
			if(pBody->sPos.X < fix16_from_int(PITCH_X0)) {
				// past the goal line: net back wall + side nets
				flatCollideAxisMin(&pBody->sPos.X, &pBody->sVel.X,
					fix16_add(fix16_from_int(PITCH_X0 - GOAL_DEPTH), fwR), fwRest);
				flatCollideAxisMin(&pBody->sPos.Y, &pBody->sVel.Y,
					fix16_add(fix16_from_int(GOAL_Y0), fwR), fwRest);
				flatCollideAxisMax(&pBody->sPos.Y, &pBody->sVel.Y,
					fix16_sub(fix16_from_int(GOAL_Y1), fwR), fwRest);
			}
			else if(!ubInMouth) {
				flatCollideAxisMin(&pBody->sPos.X, &pBody->sVel.X,
					fix16_add(fix16_from_int(PITCH_X0), fwR), fwRest);
			}
		}
	}
	// Right wall / goal
	else if(pBody->sPos.X > fix16_sub(fix16_from_int(PITCH_X1), fwR)) {
		if(pBody->ubWallMode == FLAT_WALL_PLAY) {
			if(!ubInMouth) {
				flatCollideAxisMax(&pBody->sPos.X, &pBody->sVel.X,
					fix16_sub(fix16_from_int(PITCH_X1), fwR), fwRest);
			}
		}
		else {
			if(pBody->sPos.X > fix16_from_int(PITCH_X1)) {
				flatCollideAxisMax(&pBody->sPos.X, &pBody->sVel.X,
					fix16_sub(fix16_from_int(PITCH_X1 + GOAL_DEPTH), fwR), fwRest);
				flatCollideAxisMin(&pBody->sPos.Y, &pBody->sVel.Y,
					fix16_add(fix16_from_int(GOAL_Y0), fwR), fwRest);
				flatCollideAxisMax(&pBody->sPos.Y, &pBody->sVel.Y,
					fix16_sub(fix16_from_int(GOAL_Y1), fwR), fwRest);
			}
			else if(!ubInMouth) {
				flatCollideAxisMax(&pBody->sPos.X, &pBody->sVel.X,
					fix16_sub(fix16_from_int(PITCH_X1), fwR), fwRest);
			}
		}
	}

	// Top / bottom walls
	flatCollideAxisMin(&pBody->sPos.Y, &pBody->sVel.Y,
		fix16_add(fix16_from_int(PITCH_Y0), fwR), fwRest);
	flatCollideAxisMax(&pBody->sPos.Y, &pBody->sVel.Y,
		fix16_sub(fix16_from_int(PITCH_Y1), fwR), fwRest);
}

// Hardcoded contact shares + restitution: the only dynamic bodies in play
// are players (mass 2, inv 1/2) and the ball (mass 1, inv 1), plus static
// posts (inv 0). Exact fixed-point shares, no per-contact divisions.
// Player/player: 1/2 + 1/2, restitution 0.5*0.5 = 0.25.
// Player/ball: light body moves 2/3, heavy 1/3, restitution 0.5*1.0 = 0.5.
// Static post: dynamic body moves fully, restitution from its wall bounce
// (players slide off with 0, the ball dings off with 0.7).
#define SHARE_HALF ((fix16_t)0x00008000)
#define SHARE_ONE_THIRD ((fix16_t)0x00005555)
#define SHARE_TWO_THIRD ((fix16_t)0x0000AAAB)
#define REST_PP ((fix16_t)0x00004000)
#define REST_PB ((fix16_t)0x00008000)

// Disc vs disc: positional correction + impulse, split by mass share.
// Static bodies have infinite mass (share 0, immovable).
static void flatCollideBodies(tFlatBody *pA, tFlatBody *pB) {
	// Radii are small positives: plain addition, identical to fix16_add.
	fix16_t fwMin = pA->fwRadius + pB->fwRadius;
	tVector sD = subVectors(pB->sPos, pA->sPos);
	// AABB broad-phase: |dx| >= minDist already proves no overlap.
	// Kills nearly every pair test with four integer compares, no sqrt.
	if(sD.X >= fwMin || sD.X <= -fwMin) { return; }
	if(sD.Y >= fwMin || sD.Y <= -fwMin) { return; }
	ULONG ulD2 = vecLengthSquared8(sD);
	LONG lMin8 = fwMin >> 13;
	if(ulD2 >= (ULONG)(lMin8 * lMin8)) { return; }
	fix16_t fwDist = vecLengthFromD2(ulD2);
	if(fwDist == 0) { return; }

	tVector sN;
	if(!vecNormalizeWithLength(&sN, sD, fwDist)) { return; }

	// Preserve the post-impact part of this frame's travel when approaching;
	// correction-only contacts still stop exactly at the surface.
	tVector sRv = subVectors(pB->sVel, pA->sVel);
	fix16_t fwVn = vecDot(sRv, sN);
	fix16_t fwShareA, fwShareB, fwRest;
	if(pA->isStatic || pB->isStatic) {
		tFlatBody *pDyn = pA->isStatic ? pB : pA;
		UBYTE ubDynIsA = !pA->isStatic;
		fwShareA = ubDynIsA ? F16(1) : 0;
		fwShareB = ubDynIsA ? 0 : F16(1);
		fwRest = fix16_mul(pDyn->fwWallBounce, WALL_BOUNCE);
	}
	else if(pA->fwInvMass == pB->fwInvMass) {
		fwShareA = SHARE_HALF;
		fwShareB = SHARE_HALF;
		fwRest = REST_PP;
	}
	else if(pA->fwInvMass > pB->fwInvMass) {
		// A is lighter (ball), B heavier (player)
		fwShareA = SHARE_TWO_THIRD;
		fwShareB = SHARE_ONE_THIRD;
		fwRest = REST_PB;
	}
	else {
		fwShareA = SHARE_ONE_THIRD;
		fwShareB = SHARE_TWO_THIRD;
		fwRest = REST_PB;
	}
	fix16_t fwOver = fix16_sub(fwMin, fwDist);
	if(fwVn < 0) {
		fwOver = fix16_add(fwOver, fix16_mul(fwOver, fwRest));
	}
	pA->sPos = subVectors(pA->sPos, scaleVector(sN, fix16_mul(fwOver, fwShareA)));
	pB->sPos = addVectors(pB->sPos, scaleVector(sN, fix16_mul(fwOver, fwShareB)));

	// Impulse along the normal, only when approaching. Restitution against a
	// static disc (goal post) comes from the dynamic body's wall bounce, so
	// players slide off posts like walls while the ball dings off them.
	if(fwVn >= 0) { return; }
	fix16_t fwImpact = fix16_mul(fix16_sub(0, fwVn), fix16_add(F16(1), fwRest));
	pA->sVel = subVectors(pA->sVel, scaleVector(sN, fix16_mul(fwImpact, fwShareA)));
	pB->sVel = addVectors(pB->sVel, scaleVector(sN, fix16_mul(fwImpact, fwShareB)));
}

void flatWorldStep(tFlatWorld *pWorld) {
	UBYTE ubCount = pWorld->ubBodyCount;

	// Integrate dynamic bodies
	for(UBYTE i = 0; i < ubCount; ++i) {
		tFlatBody *pBody = pWorld->pBodies[i];
		if(pBody->isStatic) { continue; }
		pBody->sPos = addVectors(pBody->sPos, pBody->sVel);
	}

	// Bounds + posts + pairs
	for(UBYTE i = 0; i < ubCount; ++i) {
		tFlatBody *pBody = pWorld->pBodies[i];
		if(pBody->isStatic) { continue; }
		flatCollideBounds(pBody);
		// Posts sit on the goal lines at the mouth corners (max contact
		// distance 10px): skip all four unless the body is near a mouth.
		if(pBody->sPos.X < fix16_from_int(PITCH_X0 + 16) ||
			pBody->sPos.X > fix16_from_int(PITCH_X1 - 16)) {
			if(pBody->sPos.Y > fix16_from_int(GOAL_Y0 - 16) &&
				pBody->sPos.Y < fix16_from_int(GOAL_Y1 + 16)) {
				for(UBYTE p = 0; p < FLAT_WORLD_POST_COUNT; ++p) {
					flatCollideBodies(pBody, &pWorld->pPosts[p]);
				}
			}
		}
	}
	for(UBYTE i = 0; i < ubCount; ++i) {
		for(UBYTE j = i + 1; j < ubCount; ++j) {
			flatCollideBodies(pWorld->pBodies[i], pWorld->pBodies[j]);
		}
	}
	// Pair separation can push a body past the wall clamp applied earlier
	// (player squeezing the ball against a wall trembles ±1px without this).
	// Velocity was already resolved, so this pass only re-clamps positions.
	for(UBYTE i = 0; i < ubCount; ++i) {
		tFlatBody *pBody = pWorld->pBodies[i];
		if(pBody->isStatic) { continue; }
		flatCollideBounds(pBody);
	}
}
