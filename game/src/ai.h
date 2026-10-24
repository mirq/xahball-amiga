/* XAHBall - AI bot logic for unassigned slots
 * Role-based: the nearest bot attacks (gets behind the ball relative to the
 * attacked goal, kicks when goal-side, in range and roughly aimed at the
 * goal), the rest defend on the ball-to-goal line. Trig-free fixed point
 * throughout. The actual kick is edge-triggered by the caller (match.c
 * pulses ubKick from ubWantKick via aiKickPulse).
 *
 * Levels (AI_LEVEL_*): both intercept the ball's predicted position, never
 * clear it toward their own goal and hack a wall-pinned ball loose. HARD
 * looks further ahead, reacts faster on the kick button and its deepest
 * defender keeps goal.
 */
#ifndef _XAHBALL_AI_H_
#define _XAHBALL_AI_H_

#include "flatbody.h"
#include "input_slots.h"

//---------------------------------------------------------------------- DEFINES

#define AI_ROLE_DEFEND 0
#define AI_ROLE_ATTACK 1

// Computer player skill (g_sMatchConfig.ubAiLevel)
#define AI_LEVEL_NORMAL 0
#define AI_LEVEL_HARD 1
#define AI_LEVEL_COUNT 2

//---------------------------------------------------------------------- FUNCTIONS

// Computes bot input. ubSide: 0 = left (attacks right), 1 = right.
// ubRole: AI_ROLE_ATTACK chases and shoots, AI_ROLE_DEFEND holds the line.
// fwHomeX/fwHomeY: formation position used to spread defenders out (and to
// pick the HARD goalkeeper: the defender whose home is deepest).
// ubAim: 0/1 - which goal corner the bot prefers to shoot at.
// ubLevel: AI_LEVEL_*.
// pubWantKick receives 1 when the bot wants to kick this frame.
void aiUpdate(
	const tFlatBody *pMe, const tFlatBody *pBall,
	fix16_t fwHomeX, fix16_t fwHomeY, UBYTE ubSide, UBYTE ubRole,
	UBYTE ubAim, UBYTE ubLevel, tInputState *pIn, UBYTE *pubWantKick
);

// Emulates a human press of the kick button: held for a couple of frames
// per cycle, phase-shifted per slot so bots never kick in lockstep. HARD
// presses twice as often (faster reaction to a ball in range).
UBYTE aiKickPulse(UWORD uwFrameTick, UBYTE ubSlot, UBYTE ubLevel);

#endif // _XAHBALL_AI_H_
