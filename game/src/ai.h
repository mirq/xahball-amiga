/* XAHBall - AI bot logic for unassigned slots
 * Role-based: the nearest bot attacks (gets behind the ball relative to the
 * attacked goal, kicks when goal-side, in range and roughly aimed at the
 * goal), the rest defend on the ball-to-goal line. Trig-free fixed point
 * throughout. The actual kick is edge-triggered by the caller (match.c
 * pulses ubKick from ubWantKick).
 */
#ifndef _XAHBALL_AI_H_
#define _XAHBALL_AI_H_

#include "flatbody.h"
#include "input_slots.h"

//---------------------------------------------------------------------- DEFINES

#define AI_ROLE_DEFEND 0
#define AI_ROLE_ATTACK 1

//---------------------------------------------------------------------- FUNCTIONS

// Computes bot input. ubSide: 0 = left (attacks right), 1 = right.
// ubRole: AI_ROLE_ATTACK chases and shoots, AI_ROLE_DEFEND holds the line.
// fwHomeX/fwHomeY: formation position used to spread defenders out.
// ubAim: 0/1 - which goal corner the bot prefers to shoot at.
// pubWantKick receives 1 when the bot wants to kick this frame.
void aiUpdate(
	const tFlatBody *pMe, const tFlatBody *pBall,
	fix16_t fwHomeX, fix16_t fwHomeY, UBYTE ubSide, UBYTE ubRole,
	UBYTE ubAim, tInputState *pIn, UBYTE *pubWantKick
);

#endif // _XAHBALL_AI_H_
