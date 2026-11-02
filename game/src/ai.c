/* XAHBall - AI bot logic
 * Attacker: targets a point behind the ball on the ball-to-goal line, swings
 * wide around the ball when on the wrong side, wants to kick when in range
 * and goal-side of the ball. Defenders: hold a spot between own goal and the
 * ball (blended with their formation home so multiple defenders spread out),
 * clear the ball when it threatens the own half. All Manhattan math, no trig.
 *
 * Positioning uses the ball's PREDICTED position (velocity * lead frames,
 * lead growing with distance - roughly the time the bot needs to get there),
 * so bots cut the ball off instead of trailing behind it. Kick decisions use
 * the real ball position.
 */
#include "ai.h"

#include <ace/managers/log.h>
#include "pitch.h"

#define KICK_PULSE_FRAMES 2

typedef struct _tAiParams {
	fix16_t fwLeadPerPx;  // prediction frames per px of bot->ball distance
	fix16_t fwLeadMax;    // prediction cap (frames)
	fix16_t fwShotAlign;  // min dot(me->ball, ball->goal) to shoot
	fix16_t fwAimOffset;  // approach shift toward a post near the goal
	UBYTE ubPulseMask;    // kick press cycle length - 1 (power of two)
	UBYTE ubKeeper;       // deepest defender keeps goal
} tAiParams;

// Tuned with tools/host_sim.c [5c]: longer prediction + quicker kick presses
// are what make HARD stronger. Lower shot alignment / wider post aim were
// tried and LOST games (wasted shots), so shot selection matches NORMAL.
static const tAiParams s_pAiParams[AI_LEVEL_COUNT] = {
	[AI_LEVEL_NORMAL] = {F16(0.12f), F16(8), F16(0.5f), F16(12), 15, 0},
	[AI_LEVEL_HARD] = {F16(0.25f), F16(18), F16(0.5f), F16(12), 7, 1},
};

// Home X within this distance of the own goal line = goalkeeper (HARD)
#define AI_KEEPER_HOME_DIST 40
// Keeper stands this far in front of the goal line
#define AI_KEEPER_LINE 18
// Keeper leaves the line to clear a ball this close (Manhattan)
#define AI_KEEPER_RUSH 44

static fix16_t aiAbs(fix16_t fwV) {
	return (fwV >= 0) ? fwV : fix16_sub(0, fwV);
}

static fix16_t aiClamp(fix16_t fwV, fix16_t fwMin, fix16_t fwMax) {
	if(fwV < fwMin) { return fwMin; }
	if(fwV > fwMax) { return fwMax; }
	return fwV;
}

// Approximate unit vector via Manhattan scaling (avoids trig/div stalls)
static void aiDirTo(
	fix16_t fwFromX, fix16_t fwFromY, fix16_t fwToX, fix16_t fwToY,
	fix16_t *pfwDx, fix16_t *pfwDy
) {
	fix16_t fwDx = fix16_sub(fwToX, fwFromX);
	fix16_t fwDy = fix16_sub(fwToY, fwFromY);
	fix16_t fwMag = fix16_add(aiAbs(fwDx), aiAbs(fwDy));
	if(fwMag < F16(2)) {
		*pfwDx = 0;
		*pfwDy = 0;
		return;
	}
	*pfwDx = fix16_div(fwDx, fwMag);
	*pfwDy = fix16_div(fwDy, fwMag);
}

// Kicking from pMe now sends the ball away from our own goal (side 0
// defends the left goal). Stops defenders from clearing into their own net.
static UBYTE aiKickIsSafe(
	fix16_t fwMyX, fix16_t fwMyY, fix16_t fwBallX, fix16_t fwBallY, UBYTE ubSide
) {
	fix16_t fwKx, fwKy;
	aiDirTo(fwMyX, fwMyY, fwBallX, fwBallY, &fwKx, &fwKy);
	return (ubSide == 0) ? (fwKx > F16(0.15f)) : (fwKx < F16(-0.15f));
}

// Ball close enough to a wall that bodies can pin it there
static UBYTE aiBallNearWall(fix16_t fwBallX, fix16_t fwBallY) {
	return fwBallX < F16(PITCH_X0 + 16) || fwBallX > F16(PITCH_X1 - 16) ||
		fwBallY < F16(PITCH_Y0 + 16) || fwBallY > F16(PITCH_Y1 - 16);
}

UBYTE aiKickPulse(UWORD uwFrameTick, UBYTE ubSlot, UBYTE ubLevel) {
	UBYTE ubMask = s_pAiParams[ubLevel].ubPulseMask;
	return ((uwFrameTick + ubSlot * 4) & ubMask) < KICK_PULSE_FRAMES;
}

void aiUpdate(
	const tFlatBody *pMe, const tFlatBody *pBall,
	fix16_t fwHomeX, fix16_t fwHomeY, UBYTE ubSide, UBYTE ubRole,
	UBYTE ubAim, UBYTE ubLevel, tInputState *pIn, UBYTE *pubWantKick
) {
	const tAiParams *pParams = &s_pAiParams[ubLevel];
	pIn->bDx = 0;
	pIn->bDy = 0;
	pIn->ubKick = 0;
	*pubWantKick = 0;

	fix16_t fwMyX = pMe->sPos.X, fwMyY = pMe->sPos.Y;
	fix16_t fwBallX = pBall->sPos.X, fwBallY = pBall->sPos.Y;
	fix16_t fwAttackX = (ubSide == 0) ? F16(PITCH_X1) : F16(PITCH_X0);
	fix16_t fwOwnGoalX = (ubSide == 0) ? F16(PITCH_X0) : F16(PITCH_X1);
	fix16_t fwGoalY = F16(PITCH_CY);

	// Manhattan distance bot -> ball
	fix16_t fwDBall = fix16_add(aiAbs(fix16_sub(fwBallX, fwMyX)), aiAbs(fix16_sub(fwBallY, fwMyY)));

	// Predicted ball position: lead grows with distance (time to get there).
	// Ball speed <= 4.75 px/f and lead <= 18 f keep the products in range.
	fix16_t fwLead = fix16_mul(fwDBall, pParams->fwLeadPerPx);
	if(fwLead > pParams->fwLeadMax) {
		fwLead = pParams->fwLeadMax;
	}
	fix16_t fwPredX = aiClamp(
		fix16_add(fwBallX, fix16_mul(pBall->sVel.X, fwLead)),
		F16(PITCH_X0 + 4), F16(PITCH_X1 - 4)
	);
	fix16_t fwPredY = aiClamp(
		fix16_add(fwBallY, fix16_mul(pBall->sVel.Y, fwLead)),
		F16(PITCH_Y0 + 4), F16(PITCH_Y1 - 4)
	);

	fix16_t fwTargetX, fwTargetY;
	if(ubRole == AI_ROLE_ATTACK) {
		// Behind the (predicted) ball, on the ball-to-goal line
		fix16_t fwGx = fix16_sub(fwPredX, fwAttackX);
		fix16_t fwGy = fix16_sub(fwPredY, fwGoalY);
		fix16_t fwGMag = fix16_add(aiAbs(fwGx), aiAbs(fwGy));
		if(fwGMag < F16(1)) { fwGMag = F16(1); }
		fix16_t fwBehind = fix16_div(F16(14), fwGMag); // one division, both axes
		fwTargetX = fix16_add(fwPredX, fix16_mul(fwGx, fwBehind));
		fwTargetY = fix16_add(fwPredY, fix16_mul(fwGy, fwBehind));

		// Near the attacked goal: shift the approach line so kicks aim at a
		// post (ubAim picks the side) instead of straight at the keeper
		if(aiAbs(fix16_sub(fwBallX, fwAttackX)) < F16(70)) {
			fwTargetY = fix16_add(
				fwTargetY, ubAim ? pParams->fwAimOffset : fix16_sub(0, pParams->fwAimOffset)
			);
		}

		// Wrong side of the ball (ball between us and their goal)? Swing
		// wide around it instead of shoving it toward our own goal: aim
		// beside the ball AND already past it on the goal side. (A detour
		// point on the wrong side - the old ball.x + 14 - is a fixed point:
		// the bot parked there for the rest of the match.) Pass on our own
		// side of the ball unless a wall leaves no room there.
		UBYTE ubGoalSide = (ubSide == 0) ? (fwMyX < fwBallX) : (fwMyX > fwBallX);
		if(!ubGoalSide) {
			fix16_t fwSideOffs = (fwMyY >= fwBallY) ? F16(22) : F16(-22);
			fix16_t fwPassY = fix16_add(fwBallY, fwSideOffs);
			if(fwPassY < F16(PITCH_Y0 + 14) || fwPassY > F16(PITCH_Y1 - 14)) {
				fwPassY = fix16_sub(fwBallY, fwSideOffs);
			}
			fwTargetX = fix16_add(fwPredX, (ubSide == 0) ? F16(-16) : F16(16));
			fwTargetY = fwPassY;
		}
		else if(fwDBall < F16(22)) {
			// Goal-side and in range - but kick only when the shot would
			// actually go goalwards (direction me->ball vs ball->goal)
			fix16_t fwKx, fwKy, fwGdx, fwGdy;
			aiDirTo(fwMyX, fwMyY, fwBallX, fwBallY, &fwKx, &fwKy);
			aiDirTo(fwBallX, fwBallY, fwAttackX, fwGoalY, &fwGdx, &fwGdy);
			if(fix16_add(fix16_mul(fwKx, fwGdx), fix16_mul(fwKy, fwGdy)) > pParams->fwShotAlign) {
				*pubWantKick = 1;
			}
			else if(aiBallNearWall(fwBallX, fwBallY)) {
				// No shot from here, but a ball against a wall/corner just
				// gets pinned while both attackers lean on it (an endless
				// stalemate) - hack it out, as long as it's not toward our
				// own goal
				*pubWantKick = aiKickIsSafe(fwMyX, fwMyY, fwBallX, fwBallY, ubSide);
			}
		}
	}
	else if(
		pParams->ubKeeper &&
		aiAbs(fix16_sub(fwHomeX, fwOwnGoalX)) < F16(AI_KEEPER_HOME_DIST)
	) {
		// Goalkeeper: guard the mouth on the ball-to-goal line, rush out
		// only for a ball that is already close
		UBYTE ubRush = fix16_add(
			aiAbs(fix16_sub(fwPredX, fwOwnGoalX)), aiAbs(fix16_sub(fwPredY, fwGoalY))
		) < F16(AI_KEEPER_RUSH);
		if(ubRush) {
			// Meet the ball from the goal side so contact pushes it out
			fwTargetX = fix16_add(fwPredX, (ubSide == 0) ? F16(-8) : F16(8));
			fwTargetY = fwPredY;
		}
		else {
			fwTargetX = fix16_add(fwOwnGoalX, (ubSide == 0) ? F16(AI_KEEPER_LINE) : F16(-AI_KEEPER_LINE));
			fwTargetY = aiClamp(
				fix16_add(fwGoalY, fix16_mul(fix16_sub(fwPredY, fwGoalY), F16(0.6f))),
				F16(GOAL_Y0 + 6), F16(GOAL_Y1 - 6)
			);
		}
		if(fwDBall < F16(22)) {
			*pubWantKick = aiKickIsSafe(fwMyX, fwMyY, fwBallX, fwBallY, ubSide);
		}
	}
	else {
		// Between own goal and the ball, closer to the goal; blend with the
		// formation home Y so two defenders don't stack on one spot.
		fwTargetX = fix16_add(fwOwnGoalX, fix16_mul(fix16_sub(fwPredX, fwOwnGoalX), F16(0.3f)));
		fwTargetY = fix16_add(fwGoalY, fix16_mul(fix16_sub(fwPredY, fwGoalY), F16(0.45f)));
		fwTargetY = fix16_add(fix16_mul(fwTargetY, F16(0.7f)), fix16_mul(fwHomeY, F16(0.3f)));

		// Clear the ball when it looms in our half and we can reach it -
		// but never toward our own goal
		if(fwDBall < F16(20)) {
			UBYTE ubBallOwnHalf = (ubSide == 0)
				? (fwBallX < F16(PITCH_CX))
				: (fwBallX > F16(PITCH_CX));
			*pubWantKick = ubBallOwnHalf &&
				aiKickIsSafe(fwMyX, fwMyY, fwBallX, fwBallY, ubSide);
		}
	}

	// Clamp targets inside the pitch - the behind-ball point must never
	// fall into a goal net or against a wall (bots would pin themselves
	// there while the ball sits in front of the goal)
	fwTargetX = aiClamp(fwTargetX, F16(PITCH_X0 + 14), F16(PITCH_X1 - 14));
	fwTargetY = aiClamp(fwTargetY, F16(PITCH_Y0 + 14), F16(PITCH_Y1 - 14));

	// Steer toward the target. Axis decisions come from the raw offset ratio
	// (press X when |dx| > |dy|/3) - the same 3:1 split as thresholding the
	// Manhattan-normalized direction, but without two fix16_div per frame.
	// Deadzone: within 2px Manhattan the bot coasts to a stop on its spot.
	fix16_t fwDx = fix16_sub(fwTargetX, fwMyX);
	fix16_t fwDy = fix16_sub(fwTargetY, fwMyY);
	fix16_t fwAbsX = aiAbs(fwDx);
	fix16_t fwAbsY = aiAbs(fwDy);
	if(fix16_add(fwAbsX, fwAbsY) < F16(2)) {
		return;
	}
	if(fwAbsX > fix16_mul(fwAbsY, F16(0.333f))) {
		pIn->bDx = (fwDx > 0) ? 1 : -1;
	}
	if(fwAbsY > fix16_mul(fwAbsX, F16(0.333f))) {
		pIn->bDy = (fwDy > 0) ? 1 : -1;
	}
}
