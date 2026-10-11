/* XAHBall - match state: pitch, physics, scoring, HUD
 * All entity motion is fix16 (16.16) fixed point, 50Hz PAL ticks, running on
 * the flat-world physics engine (vector/vecmath/flatbody/flatworld - same
 * layout as amiga-shooter's physics module, with full collision response).
 *
 * Glitch-killer architecture: phase texts (countdown/GOAL!/PAUSED) render in
 * a dedicated banner strip (y 16..31) that no sprite box can reach, drawn on
 * pBack during normal compose - no overlay restores, no vblank races, no
 * fighting the BOB system. HUD is above the banner for the same reason.
 */
#include "match.h"

#include <ace/managers/blit.h>
#include <ace/managers/bob.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/log.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>
#include <ace/utils/bitmap.h>
#include <ace/utils/font.h>
#include <stdio.h>
#include <fixmath/fixmath.h>
#include "ai.h"
#include "game.h"
#include "input_slots.h"
#include "flatbody.h"
#include "flatworld.h"
#include "vecmath.h"
#include "pitch.h"
#include "sfx.h"
#include "sprites.h"
#include "teams.h"

//---------------------------------------------------------------------- DEFINES

#define PLAYER_R 8
#define BALL_R 5
#define SPRITE_BOX 16 // disc sprite box size (matches 16x16 assets)

// Physics constants - HaxBall-flavored, tuned for 50Hz PAL + 288px pitch
#define PLAYER_ACCEL F16(0.15f)
#define KICK_ACCEL F16(0.10f)   // accel while charging a kick (HaxBall kickingAccel)
#define PLAYER_DAMP F16(0.94f)  // no hard speed cap: top speed emerges ~2.5 px/f
#define BALL_DAMP F16(0.99f)
#define SAFETY_MAXV F16(6.0f)   // per-axis hard clamp, unreachable in normal play
#define DIAG_SCALE F16(0.7071f) // normalized diagonal input (no sqrt2 speed boost)
#define KICK_POWER F16(4.5f)    // HaxBall-ratio punchy shot
#define KICK_BACK F16(0.35f)    // shooter recoil
#define KICK_RANGE_EXTRA 4      // kick range: PLAYER_R + BALL_R + this
#define BALL_MASS 1   // player mass 2, ball mass 1 (HaxBall)
#define PLAYER_MASS 2
#define BALL_BOUNCE F16(1.0f)   // disc-pair restitution = bounceA * bounceB
#define PLAYER_BOUNCE F16(0.5f)
#define PLAYER_WALL_BOUNCE F16(0.0f) // players slide along walls, never bounce
#define BALL_WALL_BOUNCE F16(1.0f)   // * world WALL_BOUNCE 0.7 => 0.7 effective
#define BALL_MAX_SPEED F16(4.75f) // post-kick speed cap: bounds 50 Hz travel per frame
#define KICK_COOLDOWN 8
#define KICK_ANIM_FRAMES 8
#define KICK_PULSE_FRAMES 2     // AI kick press length within a 16-frame cycle

#define KICKOFF_FRAMES 150
#define GOAL_FRAMES 140
#define FULLTIME_FRAMES 130

//------------------------------------------------------------------------ TYPES

typedef enum tMatchPhase {
	PHASE_KICKOFF,
	PHASE_PLAY,
	PHASE_GOAL,
	PHASE_FULLTIME,
	PHASE_PAUSED
} tMatchPhase;

typedef struct _tMatchPlayer {
	tFlatBody sBody;
	UBYTE ubSlot;        // index into g_sMatchConfig.ubSlotDevice
	UBYTE ubSide;        // 0 left, 1 right
	UBYTE ubActive;
	UBYTE ubRole;        // AI: AI_ROLE_*
	UBYTE ubKickPrev;    // kick held last frame (edge detection)
	UBYTE ubKickConsumed; // current kick press already fired
	UBYTE ubKickPressed; // kick armed (held, not yet fired this press)
	UWORD uwKickCooldown;
	UWORD uwKickAnim;
	tBob sBob;
	tInputState sIn;
} tMatchPlayer;

//---------------------------------------------------------------------- GLOBALS

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

static tMatchPlayer s_pPlayers[SLOT_COUNT];
static tFlatBody s_sBallBody;
static tBob s_sBallBob;
static tFlatWorld s_sWorld;

// Numbered kit sprites, [side][slot 0..2] - the slot number is baked into
// the sprite so players are distinguishable on the pitch
static tDiscSprite *s_pSpriteKit[2][SLOTS_PER_SIDE];
static tDiscSprite *s_pSpriteKitFlash[2][SLOTS_PER_SIDE];
static tDiscSprite *s_pSpriteKitReady[2][SLOTS_PER_SIDE];
static tDiscSprite *s_pSpriteBall;

static tMatchPhase s_ubPhase;
static UWORD s_uwPhaseTimer;
static ULONG s_ulFramesLeft;  // match time remaining in frames
static UBYTE s_ubGoalScorer;  // side that scored the pending goal
static UBYTE s_ubKickoffSide; // side that kicks off (conceding team after a goal)
static UBYTE s_ubKickoffLocked; // 1 until the kickoff team first touches the ball
static fix16_t s_fwBallPreVX, s_fwBallPreVY; // ball velocity baseline for bounce sfx (post-friction)
static UWORD s_uwBounceSfxCooldown; // frames until the bounce sample may retrigger
static UWORD s_uwFrameTick;
// Per PHYSICAL page: banner strip on that page holds text that must clear.
// A one-shot flag would wipe only the current pBack on the PLAY transition -
// the other page keeps the last countdown digit, which then blinks forever.
static UBYTE s_ubBannerDirty[2];
// F1 toggles the input-state overlay (real-hardware debugging - no bridge).
// Row 1: raw ACE manager/pin states (lit letter = held). Row 2: per-slot
// polled input. If a letter stays lit while the control is physically
// released, the state layer is stuck (lost key-up / stuck pin); if nothing
// is lit but a player still drifts, the game logic is at fault instead.
static UBYTE s_ubInputDebug;
// Clean pitch under the overlay strip, captured at create - used to restore
// both pages when the overlay is toggled off (nothing else repaints there)
static tBitMap *s_pInputDbgBg;

// Overlay strip geometry - shared by draw, capture and restore paths
static UWORD matchInputDebugStripY(void) {
	return GAME_SCREEN_HEIGHT - 2 * g_pGameFont->uwHeight - 5;
}

static UWORD matchInputDebugStripH(void) {
	return 2 * g_pGameFont->uwHeight + 5;
}

// Last score/timer values drawn on each double-buffer page - the HUD only
// redraws a page when its values change (both pages must catch up after swaps)
static UWORD s_pHudScoreL[2] = {0xFFFF, 0xFFFF};
static UWORD s_pHudScoreR[2] = {0xFFFF, 0xFFFF};
static UWORD s_pHudSecs[2] = {0xFFFF, 0xFFFF};
// The two PHYSICAL buffers, captured at create. pFront/pBack swap every frame
// and are never equal to each other, so comparing those can't identify a page.
static tBitMap *s_pHudPage[2];

// Active player slots: team A occupies 0..n-1, team B occupies 3..3+n-1
static UBYTE s_pActiveSlots[2 * SLOTS_PER_SIDE];
static UBYTE s_ubActiveCount;

// Kickoff formations (percent of pitch, from own goal side)
static const UBYTE s_pFormX[3][3] = {{25, 0, 0}, {12, 32, 0}, {10, 26, 30}};
static const UBYTE s_pFormY[3][3] = {{50, 0, 0}, {50, 50, 0}, {50, 28, 72}};

//-------------------------------------------------------------------- FUNCTIONS

// Writes a palette slot to the viewport AND the hardware color register:
// viewLoad only programs colors at state entry, mid-match changes need the
// direct write (safe during the post-vPortWaitForEnd section)
static void matchPaletteWrite(UBYTE ubIdx, UWORD uwRGB) {
	s_pVPort->pPalette[ubIdx] = uwRGB;
	g_pCustom->color[ubIdx] = uwRGB;
}

// Resolves kit RGB values for the dynamic palette slots; away kit kicks in
// when both teams would wear the same color
static void matchAssignKits(void) {
	tTeam *pLeft = &g_pTeams[g_sMatchConfig.ubTeamLeft];
	tTeam *pRight = &g_pTeams[g_sMatchConfig.ubTeamRight];
	UWORD uwKitL = pLeft->uwKitRGB;
	UWORD uwKitR = pRight->uwKitRGB;
	if(uwKitL == uwKitR) {
		uwKitR = pRight->uwAltRGB;
		if(uwKitR == uwKitL) {
			uwKitL = pLeft->uwAltRGB;
		}
	}
	matchPaletteWrite(MCOLOR_KIT_A, uwKitL);
	matchPaletteWrite(MCOLOR_KIT_B, uwKitR);
}

// Team A slots are 0..n-1, team B slots are 3..3+n-1 (SLOTS_PER_SIDE apart!)
static void matchBuildActiveSlots(void) {
	UBYTE ubCount = g_sMatchConfig.ubPlayersPerSide;
	s_ubActiveCount = 0;
	for(UBYTE ubIdx = 0; ubIdx < ubCount; ++ubIdx) {
		s_pActiveSlots[s_ubActiveCount++] = ubIdx;                    // team A
	}
	for(UBYTE ubIdx = 0; ubIdx < ubCount; ++ubIdx) {
		s_pActiveSlots[s_ubActiveCount++] = SLOTS_PER_SIDE + ubIdx;   // team B
	}
}

static void matchResetPositions(void) {
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
static void matchAssignAiRoles(void) {
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

static void matchUpdatePlayer(tMatchPlayer *pP) {
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
static void matchUpdateBallVelocity(void) {
	s_sBallBody.sVel.X = fix16_mul(s_sBallBody.sVel.X, BALL_DAMP);
	s_sBallBody.sVel.Y = fix16_mul(s_sBallBody.sVel.Y, BALL_DAMP);

	if(s_sBallBody.sVel.X > SAFETY_MAXV) { s_sBallBody.sVel.X = SAFETY_MAXV; }
	else if(s_sBallBody.sVel.X < -SAFETY_MAXV) { s_sBallBody.sVel.X = -SAFETY_MAXV; }
	if(s_sBallBody.sVel.Y > SAFETY_MAXV) { s_sBallBody.sVel.Y = SAFETY_MAXV; }
	else if(s_sBallBody.sVel.Y < -SAFETY_MAXV) { s_sBallBody.sVel.Y = -SAFETY_MAXV; }

}

// Bounce sound: fires when the world step changed the ball velocity by a
// meaningful amount (wall/post/disc impact). Threshold + retrigger lockout
// keep resting/rolling contact from machine-gunning the sample; volume
// scales with the impact magnitude.
static fix16_t fwAbs16(fix16_t fwV) {
	return (fwV >= 0) ? fwV : fix16_sub(0, fwV);
}

// Keep every rendered step bounded, including velocity gained from body
// collisions. max + min/2 is a cheap conservative length approximation.
static void matchLimitBallVelocity(void) {
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

static void matchBallBounceSfx(void) {
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

// Goal: ball CENTER crossing the goal line inside the mouth (HaxBall rule)
static void matchCheckGoal(void) {
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
static void matchEnforceKickoffLock(void) {
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
static void matchTryKicks(void) {
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

// Bob positions never leave the screen - the BOB blitter does not clip and
// a negative uwX would blit garbage into random memory
static UWORD uwBobClamp(WORD wCoord, WORD wMax) {
	if(wCoord < 0) { return 0; }
	if(wCoord > wMax) { return wMax; }
	return (UWORD)wCoord;
}

// fix16 round-to-nearest (truncation shimmers by 1px while moving)
static WORD wRoundToInt(fix16_t fwV) {
	return (WORD)((fwV + 0x8000) >> 16);
}

static void matchSyncBobPositions(void) {
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		pP->sBob.sPos.uwX = uwBobClamp(
			wRoundToInt(pP->sBody.sPos.X) - SPRITE_BOX / 2, GAME_SCREEN_WIDTH - SPRITE_BOX
		);
		pP->sBob.sPos.uwY = uwBobClamp(
			wRoundToInt(pP->sBody.sPos.Y) - SPRITE_BOX / 2, GAME_SCREEN_HEIGHT - SPRITE_BOX
		);
		// Frame select: kick flash, kick-ready (white outline) or normal,
		// numbered per slot
		tDiscSprite *pSpr;
		UBYTE ubIdx = pP->ubSlot % SLOTS_PER_SIDE;
		if(pP->uwKickAnim) {
			pSpr = s_pSpriteKitFlash[pP->ubSide][ubIdx];
		}
		else if(pP->sIn.ubKick) {
			pSpr = s_pSpriteKitReady[pP->ubSide][ubIdx];
		}
		else {
			pSpr = s_pSpriteKit[pP->ubSide][ubIdx];
		}
		bobSetFrame(
			&pP->sBob,
			bobCalcFrameAddress(pSpr->pFrames, 0),
			bobCalcFrameAddress(pSpr->pMasks, 0)
		);
	}
	s_sBallBob.sPos.uwX = uwBobClamp(
		wRoundToInt(s_sBallBody.sPos.X) - SPRITE_BOX / 2, GAME_SCREEN_WIDTH - SPRITE_BOX
	);
	s_sBallBob.sPos.uwY = uwBobClamp(
		wRoundToInt(s_sBallBody.sPos.Y) - SPRITE_BOX / 2, GAME_SCREEN_HEIGHT - SPRITE_BOX
	);
}

// Draws static HUD parts (panel, divider, team codes) - once per page
static void matchDrawHudStatic(tBitMap *pTarget) {
	// HUD strip + banner strip share a black background, thin gray divider
	blitRect(pTarget, 0, 0, GAME_SCREEN_WIDTH, BANNER_Y1, MCOLOR_BLACK);
	blitRect(pTarget, 0, HUD_HEIGHT, GAME_SCREEN_WIDTH, 1, MCOLOR_HUD_GRAY);
	fontDrawStr(
		g_pGameFont, pTarget, 100, 4, g_pTeams[g_sMatchConfig.ubTeamLeft].szCode,
		MCOLOR_KIT_A, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pTarget, 220, 4, g_pTeams[g_sMatchConfig.ubTeamRight].szCode,
		MCOLOR_KIT_B, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
}

// Per-frame HUD update: erase + redraw ONLY on change. Score/timer are
// event-driven - sprintf + blits run on goals and once per second, not every
// frame. With double buffering each page must draw the current values once
// after a swap, hence the per-page trackers.
static void matchDrawHud(void) {
	tBitMap *pBack = s_pBfr->pBack;
	UBYTE ubPage = (pBack == s_pHudPage[0]) ? 0 : 1;

	if(g_uwScoreLeft != s_pHudScoreL[ubPage] || g_uwScoreRight != s_pHudScoreR[ubPage]) {
		s_pHudScoreL[ubPage] = g_uwScoreLeft;
		s_pHudScoreR[ubPage] = g_uwScoreRight;
		static char szScore[16];
		sprintf(szScore, "%d  %d", g_uwScoreLeft, g_uwScoreRight);
		blitRect(pBack, 120, 2, 80, HUD_HEIGHT - 4, MCOLOR_BLACK);
		fontDrawStr(
			g_pGameFont, pBack, 160, 4, szScore, MCOLOR_WHITE,
			FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}

	UWORD uwSecs = s_ulFramesLeft / 50;
	if(uwSecs != s_pHudSecs[ubPage]) {
		s_pHudSecs[ubPage] = uwSecs;
		static char szTime[16];
		sprintf(szTime, "%02d:%02d", uwSecs / 60, uwSecs % 60);
		blitRect(pBack, 256, 2, 64, HUD_HEIGHT - 4, MCOLOR_BLACK);
		fontDrawStr(
			g_pGameFont, pBack, 288, 4, szTime, MCOLOR_WHITE,
			FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}
}

// Banner strip phase text (countdown, GOAL!, FULL TIME, PAUSED). Composed on
// pBack after the bobs like any other per-frame content - the strip is
// sprite-free by construction (pitch top margin), so no restore games.
static void matchDrawBanner(void) {
	static const UWORD uwBannerH = BANNER_Y1 - BANNER_Y0 - 1;
	tBitMap *pBack = s_pBfr->pBack;
	UBYTE ubPage = (pBack == s_pHudPage[0]) ? 0 : 1;

	if(s_ubPhase == PHASE_PLAY) {
		// Clear each page exactly once after the transition - the other page
		// still shows the last countdown digit until it becomes pBack again
		if(s_ubBannerDirty[ubPage]) {
			blitRect(pBack, 0, BANNER_Y0 + 1, GAME_SCREEN_WIDTH, uwBannerH, MCOLOR_BLACK);
			s_ubBannerDirty[ubPage] = 0;
		}
		return;
	}
	blitRect(pBack, 0, BANNER_Y0 + 1, GAME_SCREEN_WIDTH, uwBannerH, MCOLOR_BLACK);
	s_ubBannerDirty[ubPage] = 1;

	UWORD uwCx = GAME_SCREEN_WIDTH / 2;
	static char szBuf[24];
	switch(s_ubPhase) {
		case PHASE_KICKOFF: {
			UBYTE ubCount = (s_uwPhaseTimer / 50) + 1;
			if(ubCount > 3) { ubCount = 3; }
			szBuf[0] = '0' + ubCount;
			szBuf[1] = 0;
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2, szBuf, MCOLOR_AMBER,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		}
		case PHASE_GOAL: {
			UBYTE ubTeamIdx = s_ubGoalScorer
				? g_sMatchConfig.ubTeamRight : g_sMatchConfig.ubTeamLeft;
			sprintf(szBuf, "GOAL!  %s", g_pTeams[ubTeamIdx].szCode);
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2, szBuf, MCOLOR_AMBER,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		}
		case PHASE_FULLTIME:
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2, "FULL TIME", MCOLOR_WHITE,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		case PHASE_PAUSED:
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2,
				"PAUSED - FIRE: RESUME  ESC: QUIT", MCOLOR_AMBER,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		default:
			break;
	}
}

// Input-state overlay, toggled with F1 in any phase. Runs AFTER the bob
// pass: bob bg saves are captured pre-erase, so undraw always restores a
// clean pitch and no text ever leaks through the save/restore cycle. The
// full-strip erase makes this a solid debug bar - discs crossing it get
// their bottoms clipped, which is fine for a diagnostic aid.
static void matchDrawInputDebug(void) {
	static char szRaw[48];
	static char szSlots[64];
	char *p = szRaw;

	UWORD uwY = matchInputDebugStripY() + 2;
	blitRect(
		s_pBfr->pBack, 0, matchInputDebugStripY(), GAME_SCREEN_WIDTH,
		matchInputDebugStripH(), MCOLOR_BLACK
	);

	*p++ = '2'; *p++ = ':';
	*p++ = keyCheck(KEY_W) ? 'W' : '.';       *p++ = keyCheck(KEY_A) ? 'A' : '.';
	*p++ = keyCheck(KEY_S) ? 'S' : '.';       *p++ = keyCheck(KEY_D) ? 'D' : '.';
	*p++ = keyCheck(KEY_LSHIFT) ? 'K' : '.';
	*p++ = ' ';
	*p++ = '1'; *p++ = ':';
	*p++ = keyCheck(KEY_UP) ? 'U' : '.';      *p++ = keyCheck(KEY_DOWN) ? 'D' : '.';
	*p++ = keyCheck(KEY_LEFT) ? 'L' : '.';    *p++ = keyCheck(KEY_RIGHT) ? 'R' : '.';
	*p++ = keyCheck(KEY_RSHIFT) ? 'K' : '.';
	*p++ = ' ';
	*p++ = 'J'; *p++ = '1'; *p++ = ':';
	*p++ = joyCheck(JOY1_UP) ? 'U' : '.';     *p++ = joyCheck(JOY1_DOWN) ? 'D' : '.';
	*p++ = joyCheck(JOY1_LEFT) ? 'L' : '.';   *p++ = joyCheck(JOY1_RIGHT) ? 'R' : '.';
	*p++ = joyCheck(JOY1_FIRE) ? 'F' : '.';
	*p++ = ' ';
	*p++ = 'J'; *p++ = '2'; *p++ = ':';
	*p++ = joyCheck(JOY2_UP) ? 'U' : '.';     *p++ = joyCheck(JOY2_DOWN) ? 'D' : '.';
	*p++ = joyCheck(JOY2_LEFT) ? 'L' : '.';   *p++ = joyCheck(JOY2_RIGHT) ? 'R' : '.';
	*p++ = joyCheck(JOY2_FIRE) ? 'F' : '.';
	*p = 0;

	fontDrawStr(
		g_pGameFont, s_pBfr->pBack, 1, uwY, szRaw,
		MCOLOR_WHITE, FONT_COOKIE, s_pTextBitMap
	);

	p = szSlots;
	for(UBYTE ubI = 0; ubI < s_ubActiveCount && ubI < 5; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		p += sprintf(
			p, "%u:%+d%+d%u ", pP->ubSlot,
			(int)pP->sIn.bDx, (int)pP->sIn.bDy, pP->sIn.ubKick
		);
	}
	if(p > szSlots) {
		*(--p) = 0; // trim the trailing space
		fontDrawStr(
			g_pGameFont, s_pBfr->pBack, 1, GAME_SCREEN_HEIGHT - g_pGameFont->uwHeight - 1,
			szSlots, MCOLOR_AMBER, FONT_COOKIE, s_pTextBitMap
		);
	}}

void gsMatchCreate(void) {
	logBlockBegin("gsMatchCreate");

	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_DONE
	);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, GAME_BPP,
		TAG_DONE
	);
	matchPaletteLoad(s_pVPort);

	s_pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
		TAG_DONE
	);

	s_pTextBitMap = fontCreateTextBitMap(320, g_pGameFont->uwHeight + 8);

	// Physical buffer identities for the per-page HUD trackers
	s_pHudPage[0] = s_pBfr->pFront;
	s_pHudPage[1] = s_pBfr->pBack;

	// Kits with clash resolution; palette writes land in the viewport
	// palette before viewLoad applies them
	matchAssignKits();

	// Numbered kit sprites for every slot of both sides
	static const char *s_pSideCodes[2] = {"A", "B"};
	static const char *s_pVariants[3] = {"", "-flash", "-ready"};
	char szBm[32], szMsk[32];
	for(UBYTE ubSide = 0; ubSide < 2; ++ubSide) {
		for(UBYTE ubNum = 0; ubNum < SLOTS_PER_SIDE; ++ubNum) {
			for(UBYTE ubVar = 0; ubVar < 3; ++ubVar) {
				sprintf(szBm, "data/team%s%d%s.bm",
					s_pSideCodes[ubSide], ubNum + 1, s_pVariants[ubVar]);
				sprintf(szMsk, "data/team%s%d%s.msk",
					s_pSideCodes[ubSide], ubNum + 1, s_pVariants[ubVar]);
				tDiscSprite **ppSpr = (ubVar == 0) ? &s_pSpriteKit[ubSide][ubNum]
					: (ubVar == 1) ? &s_pSpriteKitFlash[ubSide][ubNum]
					: &s_pSpriteKitReady[ubSide][ubNum];
				*ppSpr = discSpriteLoad(szBm, szMsk);
			}
		}
	}
	s_pSpriteBall = discSpriteLoad("data/ball.bm", "data/ball.msk");

	// Init players + active slot list
	UBYTE ubCount = g_sMatchConfig.ubPlayersPerSide;
	matchBuildActiveSlots();
	for(UBYTE ubI = 0; ubI < SLOT_COUNT; ++ubI) {
		s_pPlayers[ubI].ubSlot = ubI;
		s_pPlayers[ubI].ubSide = ubI / SLOTS_PER_SIDE;
		s_pPlayers[ubI].ubActive = (ubI % SLOTS_PER_SIDE) < ubCount;
		s_pPlayers[ubI].ubRole = AI_ROLE_DEFEND;
		s_pPlayers[ubI].ubKickPrev = 0;
		s_pPlayers[ubI].ubKickConsumed = 0;
		s_pPlayers[ubI].ubKickPressed = 0;
		s_pPlayers[ubI].uwKickCooldown = 0;
		s_pPlayers[ubI].uwKickAnim = 0;
		flatBodyInit(
			&s_pPlayers[ubI].sBody, PITCH_CX, PITCH_CY, PLAYER_R, PLAYER_MASS,
			PLAYER_BOUNCE, PLAYER_WALL_BOUNCE, 0
		);
		s_pPlayers[ubI].sBody.ubWallMode = FLAT_WALL_PLAYER;
	}
	flatBodyInit(
		&s_sBallBody, PITCH_CX, PITCH_CY, BALL_R, BALL_MASS,
		BALL_BOUNCE, BALL_WALL_BOUNCE, 0
	);
	s_sBallBody.ubWallMode = FLAT_WALL_PLAY;

	// World owns integration + the full collision pass; only ACTIVE player
	// bodies take part (inactive slots must not collide with anything)
	flatWorldInit(&s_sWorld);
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		flatWorldAdd(&s_sWorld, &s_pPlayers[s_pActiveSlots[ubI]].sBody);
	}
	flatWorldAdd(&s_sWorld, &s_sBallBody);
	matchResetPositions();

	// BOB manager on both buffers, then register bobs
	bobManagerCreate(s_pBfr->pFront, s_pBfr->pBack, GAME_SCREEN_HEIGHT);
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		tDiscSprite *pSpr = s_pSpriteKit[pP->ubSide][pP->ubSlot % SLOTS_PER_SIDE];
		bobInit(
			&pP->sBob, SPRITE_BOX, SPRITE_BOX, 1,
			bobCalcFrameAddress(pSpr->pFrames, 0),
			bobCalcFrameAddress(pSpr->pMasks, 0),
			pP->sBob.sPos.uwX, pP->sBob.sPos.uwY
		);
	}
	bobInit(
		&s_sBallBob, SPRITE_BOX, SPRITE_BOX, 1,
		bobCalcFrameAddress(s_pSpriteBall->pFrames, 0),
		bobCalcFrameAddress(s_pSpriteBall->pMasks, 0),
		s_sBallBob.sPos.uwX, s_sBallBob.sPos.uwY
	);
	bobReallocateBuffers();

	// Match state
	g_uwScoreLeft = 0;
	g_uwScoreRight = 0;
	s_ulFramesLeft = (ULONG)g_uwTimeOptions[g_sMatchConfig.ubTimeIdx] * 50;
	s_ubPhase = PHASE_KICKOFF;
	s_uwPhaseTimer = KICKOFF_FRAMES;
	s_ubGoalScorer = 0;
	s_ubKickoffSide = 0; // left team kicks off the match
	s_ubKickoffLocked = 1;
	s_uwFrameTick = 0;
	s_ubBannerDirty[0] = 0;
	s_ubBannerDirty[1] = 0;
	s_uwBounceSfxCooldown = 0;
	// Fresh input world per game: the key state table is only ever written
	// on key events, so a release lost in a previous game (or menu) would
	// carry a stuck direction into this match until that key is pressed
	// again. Wipe it - anything physically held right now re-asserts on the
	// next event anyway. Bookkeeping (heal edges, joy latches) follows.
	keyReset();
	inputResetBookkeeping();
	// Force a HUD redraw on both pages even if a previous match ended with
	// the same values (trackers are static and outlive this state)
	for(UBYTE ubI = 0; ubI < 2; ++ubI) {
		s_pHudScoreL[ubI] = 0xFFFF;
		s_pHudScoreR[ubI] = 0xFFFF;
		s_pHudSecs[ubI] = 0xFFFF;
	}

	// Static layers on both pages: pitch below, HUD + banner strip on top
	pitchDrawBg(s_pBfr->pBack);
	pitchDrawBg(s_pBfr->pFront);
	matchDrawHudStatic(s_pBfr->pBack);
	matchDrawHudStatic(s_pBfr->pFront);

	// Save the clean overlay-strip region (input debug bar) for toggle-off
	s_pInputDbgBg = bitmapCreate(
		GAME_SCREEN_WIDTH, matchInputDebugStripH(), GAME_BPP, BMF_INTERLEAVED
	);
	blitCopyAligned(
		s_pBfr->pBack, 0, matchInputDebugStripY(), s_pInputDbgBg, 0, 0,
		GAME_SCREEN_WIDTH, matchInputDebugStripH()
	);

	viewLoad(s_pView);
	logBlockEnd("gsMatchCreate");
	systemUnuse();
}

void gsMatchLoop(void) {
	// Sync bob double-buffer index after simplebuffer page swap
	bobSetCurrentBuffer(s_pBfr->pBack);
	++s_uwFrameTick;

	// Input-state overlay toggle - works in every phase. Turning it off
	// restores the saved strip content on BOTH pages, since no per-frame
	// code repaints that region while the overlay is disabled.
	if(keyUse(KEY_F1)) {
		s_ubInputDebug = !s_ubInputDebug;
		if(!s_ubInputDebug && s_pInputDbgBg) {
			blitCopyAligned(
				s_pInputDbgBg, 0, 0, s_pBfr->pBack, 0, matchInputDebugStripY(),
				GAME_SCREEN_WIDTH, matchInputDebugStripH()
			);
			blitCopyAligned(
				s_pInputDbgBg, 0, 0, s_pBfr->pFront, 0, matchInputDebugStripY(),
				GAME_SCREEN_WIDTH, matchInputDebugStripH()
			);
		}
	}

	// Pause handling during play
	if(s_ubPhase == PHASE_PLAY || s_ubPhase == PHASE_PAUSED) {
		if(keyUse(KEY_ESCAPE) || keyUse(KEY_P)) {
			if(s_ubPhase == PHASE_PLAY) {
				s_ubPhase = PHASE_PAUSED;
				sfxPlay(SFX_BACK, 64);
			}
			else {
				// ESC while paused quits to menu
				sfxPlay(SFX_BACK, 64);
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
				return;
			}
		}
		else if(s_ubPhase == PHASE_PAUSED && (keyUse(KEY_RETURN) || joyUse(JOY1_FIRE))) {
			s_ubPhase = PHASE_PLAY;
			sfxPlay(SFX_CONFIRM, 64);
		}
	}
	else if(keyUse(KEY_ESCAPE)) {
		// ESC during kickoff/goal/fulltime quits to menu
		sfxPlay(SFX_BACK, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	switch(s_ubPhase) {
		case PHASE_KICKOFF:
			--s_uwPhaseTimer;
			if(!s_uwPhaseTimer) {
				sfxPlay(SFX_WHISTLE, 64);
				s_ubPhase = PHASE_PLAY;
			}
			break;
		case PHASE_PLAY: {
#ifdef XAHBALL_AUTOTEST
			// Automated time-up path probe: shorten the match clock once play
			// starts so FULL TIME -> winner happens naturally in ~6s, and draw
			// the kickoff-lock state + remaining clock in the banner strip.
			// L1 = clock held (ball untouched), L0 = clock running.
			{
				static UWORD s_uwAutoQuit = 0;
				++s_uwAutoQuit;
				if(s_uwAutoQuit == 10) {
					s_ulFramesLeft = 300;
				}
				static char szAuto[24];
				sprintf(szAuto, "L%u  C%u", s_ubKickoffLocked,
					(UWORD)(s_ulFramesLeft / 50));
				fontDrawStr(
					g_pGameFont, s_pBfr->pBack, 160, BANNER_Y0 + 2, szAuto,
					MCOLOR_AMBER, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
				);
			}
#endif
			matchAssignAiRoles();
			for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
				matchUpdatePlayer(&s_pPlayers[s_pActiveSlots[ubI]]);
			}
			matchUpdateBallVelocity();
			matchTryKicks();
			matchLimitBallVelocity();

			// Baseline after friction and kicks: only collision response should
			// trigger the bounce sample.
			s_fwBallPreVX = s_sBallBody.sVel.X;
			s_fwBallPreVY = s_sBallBody.sVel.Y;

			// Full physics pass: integrate + walls/corners/nets + posts +
			// every body pair (mass-based impulses)
			flatWorldStep(&s_sWorld);
			matchBallBounceSfx();

			// Kickoff lock: ball starts each kickoff at rest at the center, so
			// ANY velocity change means the kickoff team touched it - release
			// the circle exclusion. The baseline is exact (untouched resting
			// ball has zero delta), so the threshold is tiny on purpose.
			if(s_ubKickoffLocked) {
				fix16_t fwDx = fix16_sub(s_sBallBody.sVel.X, s_fwBallPreVX);
				fix16_t fwDy = fix16_sub(s_sBallBody.sVel.Y, s_fwBallPreVY);
				fwDx = (fwDx >= 0) ? fwDx : fix16_sub(0, fwDx);
				fwDy = (fwDy >= 0) ? fwDy : fix16_sub(0, fwDy);
				if(fix16_add(fwDx, fwDy) > F16(0.02f)) { s_ubKickoffLocked = 0; }
			}
			matchEnforceKickoffLock();

			// Goal: ball CENTER crossing the goal line inside the mouth
			matchCheckGoal();

			// Match clock: runs only in play AND only from the first ball touch
			// of each kickoff onward - the kickoff wait never eats match time.
			// s_ubKickoffLocked releases on any ball movement (kick or contact)
			// earlier this frame, so the clock starts the instant play begins.
			if(s_ubPhase == PHASE_PLAY && !s_ubKickoffLocked) {
				if(s_ulFramesLeft) {
					--s_ulFramesLeft;
				}
				if(!s_ulFramesLeft) {
					sfxPlay(SFX_WHISTLE, 64);
					s_ubPhase = PHASE_FULLTIME;
					s_uwPhaseTimer = FULLTIME_FRAMES;
				}
			}
			break;
		}
		case PHASE_GOAL:
			// Celebration: players freeze, ball rolls into the net
			s_sBallBody.sVel.X = fix16_mul(s_sBallBody.sVel.X, F16(0.95f));
			s_sBallBody.sVel.Y = fix16_mul(s_sBallBody.sVel.Y, F16(0.95f));
			s_fwBallPreVX = s_sBallBody.sVel.X;
			s_fwBallPreVY = s_sBallBody.sVel.Y;
			flatWorldStep(&s_sWorld);
			matchBallBounceSfx();
			--s_uwPhaseTimer;
			if(!s_uwPhaseTimer) {
				// Conceding side kicks off
				s_sBallBody.ubWallMode = FLAT_WALL_PLAY;
				matchResetPositions();
				s_ubPhase = PHASE_KICKOFF;
				s_uwPhaseTimer = KICKOFF_FRAMES;
			}
			break;
		case PHASE_FULLTIME:
			--s_uwPhaseTimer;
			if(!s_uwPhaseTimer) {
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_WINNER]);
				return;
			}
			break;
		default:
			break;
	}

	// Render: HUD first (sprite-free zone), then bobs, then banner text,
	// then the optional input overlay LAST - after the bob pass, so the
	// erase bar can't get baked into any bob save/undraw buffer
	matchDrawHud();
	matchSyncBobPositions();
	bobBegin(s_pBfr->pBack);
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		bobPush(&s_pPlayers[s_pActiveSlots[ubI]].sBob);
	}
	bobPush(&s_sBallBob);
	bobEnd();
	matchDrawBanner();
	if(s_ubInputDebug) {
		matchDrawInputDebug();
	}

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsMatchDestroy(void) {
	systemUse();
	logBlockBegin("gsMatchDestroy");
	// NOTE: no sfx channel cleanup here. Samples are short (<=1.4s) and the
	// CIA interrupt keeps mixing them across state transitions by design -
	// AMIner (reference ACE game) never touches channels on exits either.
	// ptplayerSfxStopOnChannel() parks a channel with wordLength=1, which
	// makes chan_sfx_only() call startSfx() on EVERY tick forever: the
	// channel stays occupied at priority 1 and DMA-toggles 50x/s (audible on
	// real hardware). ptplayer itself stays up - it is app-global.
	bobManagerDestroy();

	for(UBYTE ubSide = 0; ubSide < 2; ++ubSide) {
		for(UBYTE ubNum = 0; ubNum < SLOTS_PER_SIDE; ++ubNum) {
			discSpriteDestroy(s_pSpriteKit[ubSide][ubNum]);
			discSpriteDestroy(s_pSpriteKitFlash[ubSide][ubNum]);
			discSpriteDestroy(s_pSpriteKitReady[ubSide][ubNum]);
		}
	}
	discSpriteDestroy(s_pSpriteBall);

	fontDestroyTextBitMap(s_pTextBitMap);
	if(s_pInputDbgBg) {
		bitmapDestroy(s_pInputDbgBg);
		s_pInputDbgBg = 0;
	}
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsMatchDestroy");
}
