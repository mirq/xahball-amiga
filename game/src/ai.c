/* XAHBall - AI bot logic
 * Attacker: targets a point behind the ball on the ball-to-goal line, swings
 * wide around the ball when on the wrong side, wants to kick when in range
 * and goal-side of the ball. Defenders: hold a spot between own goal and the
 * ball (blended with their formation home so multiple defenders spread out),
 * clear the ball when it threatens the own half. All Manhattan math, no trig.
 */
#include "ai.h"

#include <ace/managers/log.h>
#include "pitch.h"

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

void aiUpdate(
	const tFlatBody *pMe, const tFlatBody *pBall,
	fix16_t fwHomeX, fix16_t fwHomeY, UBYTE ubSide, UBYTE ubRole,
	UBYTE ubAim, tInputState *pIn, UBYTE *pubWantKick
) {
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

	fix16_t fwTargetX, fwTargetY;
	if(ubRole == AI_ROLE_ATTACK) {
		// Behind the ball, on the ball-to-goal line
		fix16_t fwGx = fix16_sub(fwBallX, fwAttackX);
		fix16_t fwGy = fix16_sub(fwBallY, fwGoalY);
		fix16_t fwGMag = fix16_add(aiAbs(fwGx), aiAbs(fwGy));
		if(fwGMag < F16(1)) { fwGMag = F16(1); }
		fwTargetX = fix16_add(fwBallX, fix16_mul(fwGx, fix16_div(F16(14), fwGMag)));
		fwTargetY = fix16_add(fwBallY, fix16_mul(fwGy, fix16_div(F16(14), fwGMag)));

		// Near the attacked goal: shift the approach line so kicks aim at a
		// post (ubAim picks the side) instead of straight at the keeper
		if(aiAbs(fix16_sub(fwBallX, fwAttackX)) < F16(70)) {
			fwTargetY = fix16_add(fwTargetY, ubAim ? F16(12) : F16(-12));
		}

		// Wrong side of the ball (ball between us and their goal)? Swing
		// wide around it instead of shoving it toward our own goal.
		UBYTE ubGoalSide = (ubSide == 0) ? (fwMyX < fwBallX) : (fwMyX > fwBallX);
		if(!ubGoalSide) {
			fwTargetX = fix16_add(fwBallX, (ubSide == 0) ? F16(14) : F16(-14));
			fwTargetY = fix16_add(fwBallY, (fwMyY >= fwBallY) ? F16(20) : F16(-20));
		}
		else if(fwDBall < F16(22)) {
			// Goal-side and in range - but kick only when the shot would
			// actually go goalwards (direction me->ball vs ball->goal)
			fix16_t fwKx, fwKy, fwGx, fwGy;
			aiDirTo(fwMyX, fwMyY, fwBallX, fwBallY, &fwKx, &fwKy);
			aiDirTo(fwBallX, fwBallY, fwAttackX, fwGoalY, &fwGx, &fwGy);
			if(fix16_add(fix16_mul(fwKx, fwGx), fix16_mul(fwKy, fwGy)) > F16(0.5f)) {
				*pubWantKick = 1;
			}
		}
	}
	else {
		// Between own goal and the ball, closer to the goal; blend with the
		// formation home Y so two defenders don't stack on one spot.
		fwTargetX = fix16_add(fwOwnGoalX, fix16_mul(fix16_sub(fwBallX, fwOwnGoalX), F16(0.3f)));
		fwTargetY = fix16_add(fwGoalY, fix16_mul(fix16_sub(fwBallY, fwGoalY), F16(0.45f)));
		fwTargetY = fix16_add(fix16_mul(fwTargetY, F16(0.7f)), fix16_mul(fwHomeY, F16(0.3f)));

		// Clear the ball when it looms in our half and we can reach it
		if(fwDBall < F16(20)) {
			UBYTE ubBallOwnHalf = (ubSide == 0)
				? (fwBallX < F16(PITCH_CX))
				: (fwBallX > F16(PITCH_CX));
			*pubWantKick = ubBallOwnHalf;
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
