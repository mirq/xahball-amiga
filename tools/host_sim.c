/* XAHBall - host-side deterministic physics/game-logic simulation.
 * Compiles the REAL game sources (flatworld/vector/vecmath/flatbody/ai +
 * ACE fixmath) natively and mirrors gsMatchLoop's PHASE_PLAY order to verify
 * game logic without an emulator: containment, corner curls, post dings,
 * goal detection, kick semantics and full AI-vs-AI matches.
 *
 * Build & run from repo root:
 *   gcc -O2 -Wall -Wextra -I game/src -I <ACE>/include \
 *       tools/host_sim.c game/src/flatworld.c game/src/vector.c \
 *       game/src/vecmath.c game/src/flatbody.c game/src/ai.c \
 *       <ACE>/src/fixmath/fix16.c <ACE>/src/fixmath/fix16_sqrt.c \
 *       -o /tmp/host_sim && /tmp/host_sim
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "flatworld.h"
#include "pitch.h"
#include "vecmath.h"

static double F2F(fix16_t v) { return (double)v / 65536.0; }

// Trivial predicate from pitch.c (kept here to avoid pulling in ACE blit)
UBYTE pitchIsInGoalMouth(UWORD uwY) {
	return (uwY > GOAL_Y0) && (uwY < GOAL_Y1);
}

//----------------------------------------------------------------------
// Constants mirrored from match.c - keep in sync!
#define PLAYER_R 8
#define BALL_R 5
#define PLAYER_MASS 2
#define BALL_MASS 1
#define PLAYER_ACCEL F16(0.15f)
#define KICK_ACCEL F16(0.10f)
#define PLAYER_DAMP F16(0.94f)
#define BALL_DAMP F16(0.99f)
#define SAFETY_MAXV F16(6.0f)
#define DIAG_SCALE F16(0.7071f)
#define KICK_POWER F16(4.5f)
#define KICK_BACK F16(0.35f)
#define BALL_MAX_SPEED F16(4.75f)
#define KICK_RANGE_EXTRA 4
#define KICK_COOLDOWN 8
#define KICK_PULSE_FRAMES 2

#define SLOTS_PER_SIDE 3
#define KICKOFF_FRAMES 150
#define GOAL_FRAMES 140

typedef struct {
	tFlatBody sBody;
	UBYTE ubSide;
	UBYTE ubSlot;
	UBYTE ubRole;
	UBYTE ubKickPrev;
	UBYTE ubKickConsumed;
	UBYTE ubKickPressed;
	UWORD uwKickCooldown;
	tInputState sIn;
} tSimPlayer;

static tFlatWorld s_sWorld;
static UWORD s_uwFrameTick;
static int s_iFailures;
static int s_iSpam;

#define CHECK(cond, msg) do { \
	if(!(cond)) { ++s_iSpam; if(s_iSpam < 8) { printf("  FAIL: %s\n", msg); } ++s_iFailures; } \
} while(0)

// matchSyncBobPositions operates on 16px boxes; entity centers must stay in
// a range that keeps those boxes on-screen (bob blitter doesn't clip)
static void simClampCheck(tFlatBody *pB) {
	if(pB->sPos.X < F16(8) || pB->sPos.X > F16(312) || pB->sPos.Y < F16(8) || pB->sPos.Y > F16(248)) {
		++s_iSpam;
		if(s_iSpam < 8) {
			printf("  FAIL: body (%.1f, %.1f) outside bob-safe bounds\n", F2F(pB->sPos.X), F2F(pB->sPos.Y));
		}
		++s_iFailures;
	}
}

// Mirrors matchUpdatePlayer: input/accel/damp only - world integrates
static void simUpdatePlayerHome(tSimPlayer *pP, const tFlatBody *pBall, fix16_t fwHomeX, fix16_t fwHomeY) {
	UBYTE ubWantKick = 0;
	aiUpdate(
		&pP->sBody, pBall, fwHomeX, fwHomeY,
		pP->ubSide, pP->ubRole, pP->ubSlot & 1, &pP->sIn, &ubWantKick
	);
	pP->sIn.ubKick = ubWantKick && (((s_uwFrameTick + pP->ubSlot * 4) & 15) < KICK_PULSE_FRAMES);

	pP->ubKickPressed = 0;
	if(pP->sIn.ubKick) {
		if(!pP->ubKickPrev) { pP->ubKickConsumed = 0; }
		if(!pP->ubKickConsumed) { pP->ubKickPressed = 1; }
	}
	pP->ubKickPrev = pP->sIn.ubKick;

	fix16_t fwAccel = pP->sIn.ubKick ? KICK_ACCEL : PLAYER_ACCEL;
	if(pP->sIn.bDx && pP->sIn.bDy) {
		fwAccel = fix16_mul(fwAccel, DIAG_SCALE);
	}
	pP->sBody.sVel.X = fix16_add(pP->sBody.sVel.X, fix16_mul(fix16_from_int(pP->sIn.bDx), fwAccel));
	pP->sBody.sVel.Y = fix16_add(pP->sBody.sVel.Y, fix16_mul(fix16_from_int(pP->sIn.bDy), fwAccel));
	pP->sBody.sVel.X = fix16_mul(pP->sBody.sVel.X, PLAYER_DAMP);
	pP->sBody.sVel.Y = fix16_mul(pP->sBody.sVel.Y, PLAYER_DAMP);

	if(pP->sBody.sVel.X > SAFETY_MAXV) { pP->sBody.sVel.X = SAFETY_MAXV; }
	else if(pP->sBody.sVel.X < -SAFETY_MAXV) { pP->sBody.sVel.X = -SAFETY_MAXV; }
	if(pP->sBody.sVel.Y > SAFETY_MAXV) { pP->sBody.sVel.Y = SAFETY_MAXV; }
	else if(pP->sBody.sVel.Y < -SAFETY_MAXV) { pP->sBody.sVel.Y = -SAFETY_MAXV; }

	if(pP->uwKickCooldown) { --pP->uwKickCooldown; }
}

// Mirrors matchUpdateBallVelocity
static void simBallDamp(tFlatBody *pB) {
	pB->sVel.X = fix16_mul(pB->sVel.X, BALL_DAMP);
	pB->sVel.Y = fix16_mul(pB->sVel.Y, BALL_DAMP);
	if(pB->sVel.X > SAFETY_MAXV) { pB->sVel.X = SAFETY_MAXV; }
	else if(pB->sVel.X < -SAFETY_MAXV) { pB->sVel.X = -SAFETY_MAXV; }
	if(pB->sVel.Y > SAFETY_MAXV) { pB->sVel.Y = SAFETY_MAXV; }
	else if(pB->sVel.Y < -SAFETY_MAXV) { pB->sVel.Y = -SAFETY_MAXV; }
}

// Mirrors matchLimitBallVelocity
static void simLimitBall(tFlatBody *pB) {
	fix16_t fwAx = fix16_abs(pB->sVel.X);
	fix16_t fwAy = fix16_abs(pB->sVel.Y);
	fix16_t fwMax = (fwAx > fwAy) ? fwAx : fwAy;
	fix16_t fwMin = (fwAx > fwAy) ? fwAy : fwAx;
	fix16_t fwApprox = fix16_add(fwMax, fwMin >> 1);
	if(fwApprox > BALL_MAX_SPEED) {
		fix16_t fwScale = fix16_div(BALL_MAX_SPEED, fwApprox);
		pB->sVel.X = fix16_mul(pB->sVel.X, fwScale);
		pB->sVel.Y = fix16_mul(pB->sVel.Y, fwScale);
	}
}

// Mirrors matchTryKicks
static void simTryKicks(tSimPlayer *pP, tFlatBody *pB) {
	if(!pP->ubKickPressed || pP->uwKickCooldown) { return; }
	tVector sBallNext = addVectors(pB->sPos, pB->sVel);
	tVector sPlayerNext = addVectors(pP->sBody.sPos, pP->sBody.sVel);
	tVector sDir = subVectors(sBallNext, sPlayerNext);
	LONG lRange8 = fix16_from_int(PLAYER_R + BALL_R + KICK_RANGE_EXTRA) >> 13;
	if(vecLengthSquared8(sDir) >= (ULONG)(lRange8 * lRange8)) { return; }
	tVector sN;
	if(!vecNormalize(&sN, sDir)) { return; }
	pP->uwKickCooldown = KICK_COOLDOWN;
	pP->ubKickConsumed = 1;
	// Additive kick (mirrors match.c): ball keeps its velocity and gains the
	// kick impulse plus a share of the shooter velocity
	pB->sVel.X = fix16_add(pB->sVel.X,
		fix16_add(fix16_mul(sN.X, KICK_POWER), fix16_mul(pP->sBody.sVel.X, F16(0.3f))));
	pB->sVel.Y = fix16_add(pB->sVel.Y,
		fix16_add(fix16_mul(sN.Y, KICK_POWER), fix16_mul(pP->sBody.sVel.Y, F16(0.3f))));
	pP->sBody.sVel.X = fix16_sub(pP->sBody.sVel.X, fix16_mul(sN.X, KICK_BACK));
	pP->sBody.sVel.Y = fix16_sub(pP->sBody.sVel.Y, fix16_mul(sN.Y, KICK_BACK));
}

static UWORD uwRndState = 0x1234;
static UWORD uwRnd(void) {
	uwRndState = uwRndState * 31821 + 12349;
	return uwRndState;
}

// Formation homes per count (mirrors match.c s_pFormX/s_pFormY, % of pitch)
static const UWORD c_pFormX[3][3] = {{25, 0, 0}, {12, 32, 0}, {10, 26, 30}};
static const UWORD c_pFormY[3][3] = {{50, 0, 0}, {50, 50, 0}, {50, 28, 72}};

static fix16_t fwHomeXOf(UBYTE ubSide, UBYTE ubIdx, UBYTE ubCount) {
	UWORD uwFx = c_pFormX[ubCount - 1][ubIdx];
	return (ubSide == 0)
		? fix16_from_int(PITCH_X0 + (PITCH_WIDTH * uwFx) / 100)
		: fix16_from_int(PITCH_X1 - (PITCH_WIDTH * uwFx) / 100);
}

static fix16_t fwHomeYOf(UBYTE ubSide, UBYTE ubIdx, UBYTE ubCount) {
	UWORD uwFy = c_pFormY[ubCount - 1][ubIdx];
	return fix16_from_int(PITCH_Y0 + (PITCH_HEIGHT * uwFy) / 100);
}

// Full AI-vs-AI match: tSimPlayer arrays [side][idx]
static int simFullMatchN(int iSeed, int iMaxFrames, int iCount) {
	tSimPlayer sP[2][3];
	tFlatBody sBall;
	memset(sP, 0, sizeof(sP));
	uwRndState = (UWORD)(0x1234 + iSeed * 7919);

	flatWorldInit(&s_sWorld);
	for(int sd = 0; sd < 2; ++sd) {
		for(int i = 0; i < iCount; ++i) {
			flatBodyInit(&sP[sd][i].sBody, 0, 0, PLAYER_R, PLAYER_MASS,
				F16(0.5f), F16(0.0f), 0);
			sP[sd][i].sBody.ubWallMode = FLAT_WALL_PLAYER;
			sP[sd][i].ubSide = sd;
			sP[sd][i].ubSlot = sd * SLOTS_PER_SIDE + i;
			sP[sd][i].sBody.sPos.X = (sd == 0)
				? fix16_from_int(PITCH_X0 + (PITCH_WIDTH * c_pFormX[iCount - 1][i]) / 100)
				: fix16_from_int(PITCH_X1 - (PITCH_WIDTH * c_pFormX[iCount - 1][i]) / 100);
			sP[sd][i].sBody.sPos.Y = fwHomeYOf(sd, i, iCount);
			flatWorldAdd(&s_sWorld, &sP[sd][i].sBody);
		}
	}
	flatBodyInit(&sBall, PITCH_CX, PITCH_CY, BALL_R, BALL_MASS,
		F16(1.0f), F16(1.0f), 0);
	sBall.ubWallMode = FLAT_WALL_PLAY;
	flatWorldAdd(&s_sWorld, &sBall);

	int iPhase = 0; // 0 kickoff, 1 play, 2 goal
	UWORD uwPhaseTimer = KICKOFF_FRAMES;
	int iScoreL = 0, iScoreR = 0;
	fix16_t fwMaxSpd2 = 0;
	int iKicks = 0;

	for(int f = 0; f < iMaxFrames; ++f) {
		++s_uwFrameTick;

		if(iPhase == 0) {
			if(--uwPhaseTimer == 0) { iPhase = 1; }
			continue;
		}
		// Role assignment per side (mirrors matchAssignAiRoles with hysteresis)
		for(int sd = 0; sd < 2; ++sd) {
			int iBest = -1;
			WORD wBest = 0x7FFF;
			for(int i = 0; i < iCount; ++i) {
				WORD wDx = fix16_to_int(sBall.sPos.X - sP[sd][i].sBody.sPos.X);
				WORD wDy = fix16_to_int(sBall.sPos.Y - sP[sd][i].sBody.sPos.Y);
				WORD wDist = (wDx < 0 ? -wDx : wDx) + (wDy < 0 ? -wDy : wDy);
				if(sP[sd][i].ubRole == AI_ROLE_ATTACK) {
					wDist -= 24;
					if(wDist < 0) { wDist = 0; }
				}
				if(wDist < wBest) { wBest = wDist; iBest = i; }
			}
			for(int i = 0; i < iCount; ++i) {
				sP[sd][i].ubRole = (i == iBest) ? AI_ROLE_ATTACK : AI_ROLE_DEFEND;
			}
		}
		if(iPhase == 1) {
			for(int sd = 0; sd < 2; ++sd) {
				for(int i = 0; i < iCount; ++i) {
					simUpdatePlayerHome(&sP[sd][i], &sBall,
						fwHomeXOf(sd, i, iCount), fwHomeYOf(sd, i, iCount));
				}
			}
			simBallDamp(&sBall);
			for(int sd = 0; sd < 2; ++sd) {
				for(int i = 0; i < iCount; ++i) {
					simTryKicks(&sP[sd][i], &sBall);
					if(sP[sd][i].uwKickCooldown == KICK_COOLDOWN) { ++iKicks; }
				}
			}
			simLimitBall(&sBall);
			flatWorldStep(&s_sWorld);
			fix16_t fwS2 = fix16_mul(sBall.sVel.X, sBall.sVel.X)
				+ fix16_mul(sBall.sVel.Y, sBall.sVel.Y);
			if(fwS2 > fwMaxSpd2) { fwMaxSpd2 = fwS2; }
			for(int sd = 0; sd < 2; ++sd) {
				for(int i = 0; i < iCount; ++i) {
					simClampCheck(&sP[sd][i].sBody);
				}
			}
			simClampCheck(&sBall);
		}
		else if(iPhase == 2) {
			// celebration: players frozen, ball rolls into the net
			sBall.sVel.X = fix16_mul(sBall.sVel.X, F16(0.95f));
			sBall.sVel.Y = fix16_mul(sBall.sVel.Y, F16(0.95f));
			sBall.ubWallMode = FLAT_WALL_NET;
			flatWorldStep(&s_sWorld);
		}

		// Goal check: ball CENTER crossing the goal line inside the mouth
		UBYTE ubInMouth = pitchIsInGoalMouth((UWORD)fix16_to_int(sBall.sPos.Y));
		int iGoal = 0;
		if(iPhase == 1 && ubInMouth && sBall.sPos.X < F16(PITCH_X0)) { iGoal = 1; }
		else if(iPhase == 1 && ubInMouth && sBall.sPos.X > F16(PITCH_X1)) { iGoal = 2; }
		if(iGoal) {
			if(iGoal == 1) { ++iScoreR; } else { ++iScoreL; }
			iPhase = 2;
			uwPhaseTimer = GOAL_FRAMES;
		}
		else if(iPhase == 2 && --uwPhaseTimer == 0) {
			// kickoff reset
			for(int sd = 0; sd < 2; ++sd) {
				for(int i = 0; i < iCount; ++i) {
					sP[sd][i].sBody.sPos.X = (sd == 0)
						? fix16_from_int(PITCH_X0 + (PITCH_WIDTH * c_pFormX[iCount - 1][i]) / 100)
						: fix16_from_int(PITCH_X1 - (PITCH_WIDTH * c_pFormX[iCount - 1][i]) / 100);
					sP[sd][i].sBody.sPos.Y = fwHomeYOf(sd, i, iCount);
					sP[sd][i].sBody.sVel.X = 0;
					sP[sd][i].sBody.sVel.Y = 0;
				}
			}
			sBall.sPos.X = fix16_from_int(PITCH_CX);
			sBall.sPos.Y = fix16_from_int(PITCH_CY);
			sBall.sVel.X = 0;
			sBall.sVel.Y = 0;
			sBall.ubWallMode = FLAT_WALL_PLAY;
			iPhase = 0;
			uwPhaseTimer = KICKOFF_FRAMES;
		}
	}
	printf("  %dv%d seed %d: %d frames -> %d : %d (kicks %d, max ball speed %.2f)\n",
		iCount, iCount, iSeed, iMaxFrames, iScoreL, iScoreR, iKicks, F2F(fix16_sqrt(fwMaxSpd2)));
	return iScoreL + iScoreR;
}

static int simFullMatch(int iSeed, int iMaxFrames) {
	return simFullMatchN(iSeed, iMaxFrames, 1);
}

int main(void) {
	printf("=== XAHBall host sim ===\n");

	// 1) Containment fuzz: random ball states, must stay bounded & stable
	{
		printf("[1] ball containment fuzz\n");
		int iBad = 0;
		for(int t = 0; t < 2000; ++t) {
			tFlatWorld sW;
			flatWorldInit(&sW);
			tFlatBody b;
			flatBodyInit(&b, 0, 0, BALL_R, BALL_MASS, F16(1.0f), F16(1.0f), 0);
			b.sPos.X = fix16_from_int(20 + (int)(uwRnd() % 280));
			b.sPos.Y = fix16_from_int(45 + (int)(uwRnd() % 198));
			double dAng = (uwRnd() % 360) * 0.01745329;
			fix16_t fwS = fix16_div(fix16_from_int(uwRnd() % 40), F16(10));
			b.sVel.X = fix16_mul(fwS, (fix16_t)(cos(dAng) * 65536.0));
			b.sVel.Y = fix16_mul(fwS, (fix16_t)(sin(dAng) * 65536.0));
			tFlatBody p;
			flatBodyInit(&p, PITCH_CX, PITCH_CY, PLAYER_R, PLAYER_MASS, F16(0.5f), F16(0.0f), 0);
			p.ubWallMode = FLAT_WALL_PLAYER;
			flatWorldAdd(&sW, &p);
			flatWorldAdd(&sW, &b);
			for(int f = 0; f < 300; ++f) {
				simBallDamp(&b);
				simLimitBall(&b);
				flatWorldStep(&sW);
				if(pitchIsInGoalMouth((UWORD)fix16_to_int(b.sPos.Y)) &&
					(b.sPos.X < F16(PITCH_X0) || b.sPos.X > F16(PITCH_X1))) {
					// goal registered in the real game: net takes over, then reset
					b.ubWallMode = FLAT_WALL_NET;
					flatWorldStep(&sW);
					b.ubWallMode = FLAT_WALL_PLAY;
					b.sPos.X = fix16_from_int(PITCH_CX);
					b.sPos.Y = fix16_from_int(PITCH_CY);
					b.sVel.X = 0;
					b.sVel.Y = 0;
					continue;
				}
				simClampCheck(&b);
			}
		}
		CHECK(iBad == 0, "out-of-bounds in fuzz");
		printf("  fuzz done, %d bad runs\n", iBad);
	}

	// 1b) Garbage-velocity recovery: insane velocity must be capped
	{
		printf("[1b] garbage velocity recovery\n");
		tFlatBody b;
		flatBodyInit(&b, PITCH_CX, PITCH_CY, BALL_R, BALL_MASS, F16(1.0f), F16(1.0f), 0);
		b.sVel.X = (fix16_t)0x4A850000;  // ~7200 px/f
		b.sVel.Y = -(fix16_t)0x52C00000; // ~-8560 px/f
		simBallDamp(&b);
		simLimitBall(&b);
		fix16_t fwSpd2 = fix16_mul(b.sVel.X, b.sVel.X) + fix16_mul(b.sVel.Y, b.sVel.Y);
		CHECK(fwSpd2 <= fix16_mul(SAFETY_MAXV, SAFETY_MAXV) * 4 + F16(0.1f),
			"garbage velocity not capped");
		printf("  capped to (%.2f, %.2f)\n", F2F(b.sVel.X), F2F(b.sVel.Y));
	}

	// 2) Corner curl: 45-degree shot into the top-left corner stays in the arc
	{
		printf("[2] rounded corner physics\n");
		tFlatWorld sW;
		flatWorldInit(&sW);
		tFlatBody b;
		flatBodyInit(&b, 100, 70, BALL_R, BALL_MASS, F16(1.0f), F16(1.0f), 0);
		b.sVel.X = fix16_mul(F16(3.0f), F16(-0.7071f));
		b.sVel.Y = fix16_mul(F16(3.0f), F16(-0.7071f));
		flatWorldAdd(&sW, &b);
		int iViolated = 0;
		for(int f = 0; f < 120; ++f) {
			simBallDamp(&b);
			flatWorldStep(&sW);
			if(b.sPos.X < fix16_from_int(PITCH_X0 + CORNER_R) && b.sPos.Y < fix16_from_int(PITCH_Y0 + CORNER_R)) {
				tVector sD = subVectors(b.sPos, initVector(PITCH_X0 + CORNER_R, PITCH_Y0 + CORNER_R));
				fix16_t fwD = vecLength(sD);
				fix16_t fwMax = fix16_from_int(CORNER_R - BALL_R);
				if(fwD > fwMax + F16(1.2f)) { ++iViolated; }
			}
		}
		CHECK(iViolated == 0, "corner arc violated");
		CHECK(b.sVel.X > 0 && b.sVel.Y > 0, "no corner bounce");
		printf("  corner OK, exits at (%.1f, %.1f)\n", F2F(b.sPos.X), F2F(b.sPos.Y));
	}

	// 3) Post ding: shot at the left post bounces back into play
	{
		printf("[3] goal post collision\n");
		tFlatWorld sW;
		flatWorldInit(&sW);
		tFlatBody b;
		flatBodyInit(&b, PITCH_X0 + 12, GOAL_Y0 - 6, BALL_R, BALL_MASS, F16(1.0f), F16(1.0f), 0);
		b.sVel.X = F16(-3.0f);
		b.sVel.Y = F16(0.6f);
		flatWorldAdd(&sW, &b);
		for(int f = 0; f < 60; ++f) {
			simBallDamp(&b);
			flatWorldStep(&sW);
		}
		CHECK(b.sPos.X > F16(PITCH_X0), "post shot should not enter goal");
		printf("  post OK, ball at (%.1f, %.1f) vx %.2f\n",
			F2F(b.sPos.X), F2F(b.sPos.Y), F2F(b.sVel.X));
	}

	// 4) Goal detection: center-crossing in mouth scores, off-mouth does not
	{
		printf("[4] goal detection\n");
		tFlatWorld sW;
		flatWorldInit(&sW);
		tFlatBody b;
		flatBodyInit(&b, 0, 0, BALL_R, BALL_MASS, F16(1.0f), F16(1.0f), 0);
		flatWorldAdd(&sW, &b);

		b.sPos.X = fix16_from_int(PITCH_X0 + 20);
		b.sPos.Y = fix16_from_int((GOAL_Y0 + GOAL_Y1) / 2);
		b.sVel.X = F16(-3.0f);
		b.sVel.Y = 0;
		int iGoal = 0;
		for(int f = 0; f < 30 && !iGoal; ++f) {
			simBallDamp(&b);
			flatWorldStep(&sW);
			if(pitchIsInGoalMouth((UWORD)fix16_to_int(b.sPos.Y)) && b.sPos.X < F16(PITCH_X0)) { iGoal = 1; }
		}
		CHECK(iGoal, "left-mouth shot must score");
		printf("  goal fires at x=%.1f\n", F2F(b.sPos.X));

		b.sPos.X = fix16_from_int(PITCH_X0 + 20);
		b.sPos.Y = fix16_from_int(GOAL_Y0 - 10);
		b.sVel.X = F16(-3.0f);
		b.sVel.Y = 0;
		iGoal = 0;
		for(int f = 0; f < 60; ++f) {
			simBallDamp(&b);
			flatWorldStep(&sW);
			if(pitchIsInGoalMouth((UWORD)fix16_to_int(b.sPos.Y)) && b.sPos.X < F16(PITCH_X0)) { iGoal = 1; }
		}
		CHECK(!iGoal, "off-mouth shot must NOT score");
		CHECK(b.sPos.X > F16(PITCH_X0), "off-mouth shot must bounce back");
		printf("  off-mouth OK, ball at x=%.1f\n", F2F(b.sPos.X));
	}

	// 5) Full AI vs AI matches - goals must actually happen
	{
		printf("[5] AI vs AI matches (1v1)\n");
		int iTotal = 0;
		for(int s = 1; s <= 5; ++s) {
			iTotal += simFullMatch(s, 50 * 180);
		}
		CHECK(iTotal > 0, "no goals in 5 AI matches");

		printf("[5b] AI vs AI matches (2v2, roles + formation)\n");
		int iTotal2 = 0;
		for(int s = 1; s <= 5; ++s) {
			iTotal2 += simFullMatchN(s + 50, 50 * 180, 2);
		}
		printf("  2v2 total goals: %d\n", iTotal2);
	}

	// 6) Head-on disc collision momentum sanity (overlapping discs)
	{
		printf("[6] disc collision\n");
		tFlatBody a, b;
		flatBodyInit(&a, 100, 144, PLAYER_R, PLAYER_MASS, F16(0.5f), 0, 0);
		flatBodyInit(&b, 112, 144, PLAYER_R, PLAYER_MASS, F16(0.5f), 0, 0);
		a.sVel.X = F16(2.0f);
		b.sVel.X = F16(-2.0f);
		tFlatWorld sW;
		flatWorldInit(&sW);
		flatWorldAdd(&sW, &a);
		flatWorldAdd(&sW, &b);
		flatWorldStep(&sW);
		fix16_t fwSum = a.sVel.X + b.sVel.X;
		CHECK(fwSum <= F16(0.01f) && fwSum >= F16(-0.01f), "momentum not conserved");
		CHECK(a.sVel.X < 0 && b.sVel.X > 0, "no bounce-back");
		printf("  collision OK, va=%.2f vb=%.2f\n", F2F(a.sVel.X), F2F(b.sVel.X));
	}

	printf("=== %s (%d failures) ===\n", s_iFailures ? "FAILED" : "ALL PASSED", s_iFailures);
	return s_iFailures ? 1 : 0;
}
