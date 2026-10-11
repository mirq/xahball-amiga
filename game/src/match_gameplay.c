/* XAHBall - match gameplay: per-frame rules and physics orchestration.
 * The bodies live in the flatworld engine; this file drives them: player
 * input/AI, kicks, ball friction caps, goal detection, kickoff lock and
 * the goal celebration step.
 */
#include "match_internal.h"

#include "ai.h"
#include "sfx.h"
#include "vecmath.h"

//---------------------------------------------------------------------- GLOBALS

// Kickoff formations (percent of pitch, from own goal side)
static const UBYTE s_pFormX[3][3] = {{25, 0, 0}, {12, 32, 0}, {10, 26, 30}};
static const UBYTE s_pFormY[3][3] = {{50, 0, 0}, {50, 50, 0}, {50, 28, 72}};

// Ball velocity baseline for bounce sfx (captured post-friction, pre-step)
static fix16_t s_fwBallPreVX, s_fwBallPreVY;
// frames until the bounce sample may retrigger
static UWORD s_uwBounceSfxCooldown;

//-------------------------------------------------------------------- FUNCTIONS

static fix16_t fwAbs16(fix16_t fwV) {
	return (fwV >= 0) ? fwV : fix16_sub(0, fwV);
}

// Match-start reset of gameplay-private bookkeeping (bounce sfx lockout)
void matchGameplayReset(void) {
	s_uwBounceSfxCooldown = 0;
	s_fwBallPreVX = 0;
	s_fwBallPreVY = 0;
}

void matchResetPositions(void) {
	UBYTE ubCount = g_sMatchConfig.ubPlayersPerSide;
	for(UBYTE ubSide = 0; ubSide < 2; ++ubSide) {
		for(UBYTE ubIdx = 0; ubIdx < ubCount; ++ubIdx) {
			tMatchPlayer *pP = &s_pPlayers[ubSide * SLOTS_PER_SIDE + ubIdx];
			UWORD uwFx = s_pFormX[ubCount - 1][ubIdx];
			UWORD uwFy = s_pFormY[ubCount - 1][ubIdx];
			UWORD uwX = (ubSide == 0)
				? PITCH_X0 + (PITCH_WIDTH * uwFx) / 100
				: PITCH_X0 + PITCH_WIDTH - (PITCH_WIDTH * uwFx) / 100;
			UWORD uwY = PITCH_Y0 + (PITCH_HEIGHT * uwFy) / 100;
			pP->sBody.sPos.X = fix16_from_int(uwX);
			pP->sBody.sPos.Y = fix16_from_int(uwY);
			pP->sBody.sVel.X = 0;
			pP->sBody.sVel.Y = 0;
			pP->sBob.sPos.uwX = uwX - SPRITE_BOX / 2;
			pP->sBob.sPos.uwY = uwY - SPRITE_BOX / 2;
		}
	}
	s_sBallBody.sPos.X = fix16_from_int(PITCH_CX);
	s_sBallBody.sPos.Y = fix16_from_int(PITCH_CY);
	s_sBallBody.sVel.X = 0;
	s_sBallBody.sVel.Y = 0;
	s_sBallBob.sPos.uwX = PITCH_CX - SPRITE_BOX / 2;
	s_sBallBob.sPos.uwY = PITCH_CY - SPRITE_BOX / 2;
}

// Per-side role assignment: nearest AI to the ball attacks, others defend.
// Hysteresis: the current attacker keeps the role unless beaten by 24px.
void matchAssignAiRoles(void) {
	for(UBYTE ubSide = 0; ubSide < 2; ++ubSide) {
		WORD wBestDist = 0x7FFF;
		tMatchPlayer *pBest = 0;
		for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
			tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
			if(pP->ubSide != ubSide) { continue; }
			if(g_sMatchConfig.ubSlotDevice[pP->ubSlot] != DEVICE_AI) { continue; }
			WORD wDx = fix16_to_int(fix16_sub(s_sBallBody.sPos.X, pP->sBody.sPos.X));
			WORD wDy = fix16_to_int(fix16_sub(s_sBallBody.sPos.Y, pP->sBody.sPos.Y));
			if(wDx < 0) { wDx = -wDx; }
			if(wDy < 0) { wDy = -wDy; }
			WORD wDist = wDx + wDy;
			if(pP->ubRole == AI_ROLE_ATTACK) {
				wDist -= 24;
				if(wDist < 0) { wDist = 0; }
			}
			if(wDist < wBestDist) {
				wBestDist = wDist;
				pBest = pP;
			}
		}
		// Apply: all AI on this side defend, the winner attacks
		for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
			tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
			if(pP->ubSide != ubSide) { continue; }
			if(g_sMatchConfig.ubSlotDevice[pP->ubSlot] != DEVICE_AI) { continue; }
			pP->ubRole = (pP == pBest) ? AI_ROLE_ATTACK : AI_ROLE_DEFEND;
		}
	}
}

void matchUpdatePlayer(tMatchPlayer *pP) {
	UBYTE ubDevice = g_sMatchConfig.ubSlotDevice[pP->ubSlot];
	UBYTE ubWantKick = 0;
	if(ubDevice == DEVICE_AI) {
		UBYTE ubIdx = pP->ubSlot % SLOTS_PER_SIDE;
		UBYTE ubCount = g_sMatchConfig.ubPlayersPerSide;
		WORD wFx = s_pFormX[ubCount - 1][ubIdx];
		WORD wFy = s_pFormY[ubCount - 1][ubIdx];
		fix16_t fwHomeX = (pP->ubSide == 0)
			? fix16_from_int(PITCH_X0 + (PITCH_WIDTH * wFx) / 100)
			: fix16_from_int(PITCH_X0 + PITCH_WIDTH - (PITCH_WIDTH * wFx) / 100);
		fix16_t fwHomeY = fix16_from_int(PITCH_Y0 + (PITCH_HEIGHT * wFy) / 100);
		aiUpdate(
			&pP->sBody, &s_sBallBody,
			fwHomeX, fwHomeY, pP->ubSide, pP->ubRole, pP->ubSlot & 1,
			&pP->sIn, &ubWantKick
		);
		// Emulate a human press: 2 frames held every 16, phase-shifted per
		// slot so two bots never kick in lockstep
		pP->sIn.ubKick = ubWantKick
			&& (((s_uwFrameTick + pP->ubSlot * 4) & 15) < KICK_PULSE_FRAMES);
	}
	else {
		inputPollDevice(ubDevice, &pP->sIn);
	}

	// HaxBall kick semantics: armed on press, one kick per press. Holding
	// without a ball in range keeps the kick armed until it connects.
	pP->ubKickPressed = 0;
	if(pP->sIn.ubKick) {
		if(!pP->ubKickPrev) {
			pP->ubKickConsumed = 0; // fresh press re-arms
		}
		if(!pP->ubKickConsumed) {
			pP->ubKickPressed = 1;
		}
	}
	pP->ubKickPrev = pP->sIn.ubKick;

	// Acceleration - reduced while charging a kick; diagonal input is
	// normalized so moving diagonally isn't sqrt(2) faster (HaxBall feel)
	fix16_t fwAccel = pP->sIn.ubKick ? KICK_ACCEL : PLAYER_ACCEL;
	if(pP->sIn.bDx && pP->sIn.bDy) {
		fwAccel = fix16_mul(fwAccel, DIAG_SCALE);
	}
	pP->sBody.sVel.X = fix16_add(pP->sBody.sVel.X, fix16_mul(fix16_from_int(pP->sIn.bDx), fwAccel));
	pP->sBody.sVel.Y = fix16_add(pP->sBody.sVel.Y, fix16_mul(fix16_from_int(pP->sIn.bDy), fwAccel));
	pP->sBody.sVel.X = fix16_mul(pP->sBody.sVel.X, PLAYER_DAMP);
	pP->sBody.sVel.Y = fix16_mul(pP->sBody.sVel.Y, PLAYER_DAMP);

	// Per-axis safety clamp: overflow-proof (squared-speed comparison would
	// wrap int32 above ~90 px/f and silently skip the cap)
	if(pP->sBody.sVel.X > SAFETY_MAXV) { pP->sBody.sVel.X = SAFETY_MAXV; }
	else if(pP->sBody.sVel.X < -SAFETY_MAXV) { pP->sBody.sVel.X = -SAFETY_MAXV; }
	if(pP->sBody.sVel.Y > SAFETY_MAXV) { pP->sBody.sVel.Y = SAFETY_MAXV; }
	else if(pP->sBody.sVel.Y < -SAFETY_MAXV) { pP->sBody.sVel.Y = -SAFETY_MAXV; }

	// Integration + wall/post/pair collisions happen in flatWorldStep

	if(pP->uwKickCooldown) { --pP->uwKickCooldown; }
	if(pP->uwKickAnim) { --pP->uwKickAnim; }
}

// Ball friction + safety clamp; integration/collisions happen in flatWorldStep.
void matchUpdateBallVelocity(void) {
	s_sBallBody.sVel.X = fix16_mul(s_sBallBody.sVel.X, BALL_DAMP);
	s_sBallBody.sVel.Y = fix16_mul(s_sBallBody.sVel.Y, BALL_DAMP);

	if(s_sBallBody.sVel.X > SAFETY_MAXV) { s_sBallBody.sVel.X = SAFETY_MAXV; }
	else if(s_sBallBody.sVel.X < -SAFETY_MAXV) { s_sBallBody.sVel.X = -SAFETY_MAXV; }
	if(s_sBallBody.sVel.Y > SAFETY_MAXV) { s_sBallBody.sVel.Y = SAFETY_MAXV; }
	else if(s_sBallBody.sVel.Y < -SAFETY_MAXV) { s_sBallBody.sVel.Y = -SAFETY_MAXV; }
}

// Bounce-sfx baseline, captured after friction AND kicks (call between
// matchLimitBallVelocity and flatWorldStep): only the world step's collision
// response should register as an impact - a kick has its own sample.
void matchCaptureBallBaseline(void) {
	s_fwBallPreVX = s_sBallBody.sVel.X;
	s_fwBallPreVY = s_sBallBody.sVel.Y;
}

// Keep every rendered step bounded, including velocity gained from body
// collisions. max + min/2 is a cheap conservative length approximation.
void matchLimitBallVelocity(void) {
	fix16_t fwAx = fwAbs16(s_sBallBody.sVel.X);
	fix16_t fwAy = fwAbs16(s_sBallBody.sVel.Y);
	fix16_t fwMax = (fwAx > fwAy) ? fwAx : fwAy;
	fix16_t fwMin = (fwAx > fwAy) ? fwAy : fwAx;
	fix16_t fwApprox = fix16_add(fwMax, fwMin >> 1);
	if(fwApprox > BALL_MAX_SPEED) {
		fix16_t fwScale = fix16_div(BALL_MAX_SPEED, fwApprox);
		s_sBallBody.sVel.X = fix16_mul(s_sBallBody.sVel.X, fwScale);
		s_sBallBody.sVel.Y = fix16_mul(s_sBallBody.sVel.Y, fwScale);
	}
}

// Bounce sound: fires when the world step changed the ball velocity by a
// meaningful amount (wall/post/disc impact). Threshold + retrigger lockout
// keep resting/rolling contact from machine-gunning the sample; volume
// scales with the impact magnitude.
void matchBallBounceSfx(void) {
	if(s_uwBounceSfxCooldown) {
		--s_uwBounceSfxCooldown;
		return;
	}
	fix16_t fwImpact = fix16_add(
		fwAbs16(fix16_sub(s_sBallBody.sVel.X, s_fwBallPreVX)),
		fwAbs16(fix16_sub(s_sBallBody.sVel.Y, s_fwBallPreVY))
	);
	if(fwImpact <= F16(0.3f)) { return; }
	UBYTE ubVol = 32 + fix16_to_int(fix16_mul(fwImpact, F16(8)));
	if(ubVol > 64) { ubVol = 64; }
	sfxPlay(SFX_BOUNCE, ubVol);
	s_uwBounceSfxCooldown = 6;
}

// Kickoff lock release check: the ball starts each kickoff at rest at the
// center, so ANY velocity change during the step means someone touched it.
// The baseline is exact (untouched resting ball has zero delta), so the
// threshold is tiny on purpose. Called right after flatWorldStep.
UBYTE matchKickoffTouchHappened(void) {
	fix16_t fwDx = fwAbs16(fix16_sub(s_sBallBody.sVel.X, s_fwBallPreVX));
	fix16_t fwDy = fwAbs16(fix16_sub(s_sBallBody.sVel.Y, s_fwBallPreVY));
	return fix16_add(fwDx, fwDy) > F16(0.02f);
}

// Goal: ball CENTER crossing the goal line inside the mouth (HaxBall rule)
void matchCheckGoal(void) {
	UBYTE ubInMouth = pitchIsInGoalMouth((UWORD)fix16_to_int(s_sBallBody.sPos.Y));
	UBYTE ubGoal = 0;
	if(ubInMouth && s_sBallBody.sPos.X < F16(PITCH_X0)) {
		s_ubGoalScorer = 1; // right side scores in the left goal
		g_uwScoreRight += 1;
		ubGoal = 1;
	}
	else if(ubInMouth && s_sBallBody.sPos.X > F16(PITCH_X1)) {
		s_ubGoalScorer = 0;
		g_uwScoreLeft += 1;
		ubGoal = 1;
	}
	if(!ubGoal) { return; }

	// Celebration: everyone freezes; ball either rolls into the net (net-boxed)
	// or freezes where it crossed the line (BALL FREEZE option)
	sfxPlay(SFX_GOAL, 64);
	s_sBallBody.ubWallMode = FLAT_WALL_NET;
	if(g_sMatchConfig.ubBallFreeze) {
		s_sBallBody.sVel.X = 0;
		s_sBallBody.sVel.Y = 0;
	}
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		pP->sBody.sVel.X = 0;
		pP->sBody.sVel.Y = 0;
	}
	// Conceding side kicks off and owns the center circle until first touch
	s_ubKickoffSide = 1 - s_ubGoalScorer;
	s_ubKickoffLocked = 1;
	s_ubPhase = PHASE_GOAL;
	s_uwPhaseTimer = GOAL_FRAMES;
}

// Kickoff rule (KICKOFF LOCK option): until the kickoff team first touches the
// ball, the OTHER team may not enter the center circle. Violators are pushed
// out radially with their inward velocity removed - a soft constraint the
// lock ends the moment any touch moves the ball.
void matchEnforceKickoffLock(void) {
	if(!g_sMatchConfig.ubKickoffLock || !s_ubKickoffLocked) { return; }
	tVector sC = initVector(PITCH_CX, PITCH_CY);
	fix16_t fwMinDist = fix16_from_int(CENTER_CIRCLE_R + PLAYER_R);
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		if(pP->ubSide == s_ubKickoffSide) { continue; }
		tVector sD = subVectors(pP->sBody.sPos, sC);
		fix16_t fwDist = vecLength(sD);
		if(fwDist >= fwMinDist || fwDist == 0) { continue; }
		tVector sN;
		if(!vecNormalize(&sN, sD)) { continue; }
		pP->sBody.sPos = addVectors(sC, scaleVector(sN, fwMinDist));
		fix16_t fwDot = vecDot(pP->sBody.sVel, sN);
		if(fwDot < 0) {
			pP->sBody.sVel = subVectors(pP->sBody.sVel, scaleVector(sN, fwDot));
		}
	}
}

// Kicks: edge-triggered, work up to rP+rB+4 from centers without contact.
// Plain disc contact is handled by the world's collision pass.
void matchTryKicks(void) {
	fix16_t fwKickRange = fix16_from_int(PLAYER_R + BALL_R + KICK_RANGE_EXTRA);
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		if(!pP->ubKickPressed || pP->uwKickCooldown) { continue; }
		// Kicks now run before integration for immediate visual response. Test
		// the positions this frame would reach so effective range remains the
		// same as when kicks ran after the world step.
		tVector sBallNext = addVectors(s_sBallBody.sPos, s_sBallBody.sVel);
		tVector sPlayerNext = addVectors(pP->sBody.sPos, pP->sBody.sVel);
		tVector sDir = subVectors(sBallNext, sPlayerNext);
		// Range-only test: squared compare, no sqrt (normalizes below).
		LONG lRange8 = fwKickRange >> 13;
		if(vecLengthSquared8(sDir) >= (ULONG)(lRange8 * lRange8)) { continue; }

		// Kick: additive impulse along body->ball normal (HaxBall semantics -
		// a ball already moving away gains its speed on top, an incoming one
		// isn't reset) plus a bit of shooter velocity; recoil pushes the
		// shooter back. Next frame's SAFETY_MAXV clamp bounds the result.
		tVector sN;
		if(!vecNormalize(&sN, sDir)) { continue; }
		pP->uwKickCooldown = KICK_COOLDOWN;
		pP->uwKickAnim = KICK_ANIM_FRAMES;
		pP->ubKickConsumed = 1;
		// A kick is a definitive first touch: release the kickoff clock-hold
		// here. The velocity-delta check after the world step can't see an
		// open-field kick (it happens before the baseline recapture), so
		// without this the match clock would stay frozen until the ball
		// happens to bounce off something.
		s_ubKickoffLocked = 0;
		s_sBallBody.sVel.X = fix16_add(s_sBallBody.sVel.X,
			fix16_add(fix16_mul(sN.X, KICK_POWER), fix16_mul(pP->sBody.sVel.X, F16(0.3f))));
		s_sBallBody.sVel.Y = fix16_add(s_sBallBody.sVel.Y,
			fix16_add(fix16_mul(sN.Y, KICK_POWER), fix16_mul(pP->sBody.sVel.Y, F16(0.3f))));

		pP->sBody.sVel.X = fix16_sub(pP->sBody.sVel.X, fix16_mul(sN.X, KICK_BACK));
		pP->sBody.sVel.Y = fix16_sub(pP->sBody.sVel.Y, fix16_mul(sN.Y, KICK_BACK));
		sfxPlay(SFX_KICK, 64);
	}
}

// Goal celebration: players freeze, ball (net-boxed) decelerates into the net.
void matchGoalCelebrationStep(void) {
	s_sBallBody.sVel.X = fix16_mul(s_sBallBody.sVel.X, F16(0.95f));
	s_sBallBody.sVel.Y = fix16_mul(s_sBallBody.sVel.Y, F16(0.95f));
	s_fwBallPreVX = s_sBallBody.sVel.X;
	s_fwBallPreVY = s_sBallBody.sVel.Y;
	flatWorldStep(&s_sWorld);
	matchBallBounceSfx();
}
