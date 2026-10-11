/* XAHBall - pitch geometry and background rendering
 * Classic HaxBall look: flat green pitch, 1px white border with rounded
 * corner arcs, halfway line + center circle, open goal mouths with posts
 * and a red goal frame. No stripes, no boxes, no net mesh.
 */
#include "pitch.h"

#include <ace/managers/blit.h>
#include "game.h"

UBYTE pitchIsInGoalMouth(UWORD uwY) {
	return (uwY > GOAL_Y0) && (uwY < GOAL_Y1);
}

static UWORD uwIntSqrt(ULONG ulVal) {
	ULONG ulRes = 0, ulBit = 1UL << 30;
	while(ulBit > ulVal) { ulBit >>= 2; }
	while(ulBit) {
		if(ulVal >= ulRes + ulBit) {
			ulVal -= ulRes + ulBit;
			ulRes = (ulRes >> 1) + ulBit;
		}
		else {
			ulRes >>= 1;
		}
		ulBit >>= 2;
	}
	return (UWORD)ulRes;
}

// Plots the 1px quarter arc of circle (wCx,wCy,r=wR) lying in the quadrant
// wQx/wQy (each +1 or -1). Axis endpoints included so arcs meet the straight
// border lines seamlessly.
static void pitchArcQuarter(
	tBitMap *pBfr, WORD wCx, WORD wCy, WORD wR, WORD wQx, WORD wQy
) {
	WORD wX = wR, wY = 0;
	WORD wErr = 1 - wR;
	while(wX >= wY) {
		WORD pDx[8] = {wX, -wX, wX, -wX, wY, -wY, wY, -wY};
		WORD pDy[8] = {wY, wY, -wY, -wY, wX, wX, -wX, -wX};
		for(UBYTE i = 0; i < 8; ++i) {
			if(pDx[i] * wQx >= 0 && pDy[i] * wQy >= 0) {
				blitRect(pBfr, wCx + pDx[i], wCy + pDy[i], 1, 1, MCOLOR_WHITE);
			}
		}
		++wY;
		if(wErr < 0) {
			wErr += 2 * wY + 1;
		}
		else {
			--wX;
			wErr += 2 * (wY - wX) + 1;
		}
	}
}

// Fills the corner-square area outside the arc with stadium color. Call after
// the flat pitch fill; wQx/wQy select the corner (+1 right, -1 left etc).
static void pitchCutCorner(tBitMap *pBfr, WORD wQx, WORD wQy) {
	WORD wCy = wQy < 0 ? PITCH_Y0 + CORNER_R : PITCH_Y1 - CORNER_R;
	for(WORD wDy = 1; wDy <= CORNER_R; ++wDy) {
		WORD wYRow = wCy + wQy * wDy; // row at distance wDy from corner center
		WORD wChord = uwIntSqrt((ULONG)CORNER_R * CORNER_R - (ULONG)wDy * wDy);
		WORD wCut = CORNER_R - wChord;
		if(!wCut) { continue; }
		WORD wX = wQx < 0 ? PITCH_X0 : PITCH_X1 - wCut + 1;
		blitRect(pBfr, wX, wYRow, wCut, 1, MCOLOR_STADIUM);
	}
}

static void pitchDrawBorder(tBitMap *pBfr) {
	// 1px border lines, interrupted at the goal mouths
	// Top / bottom
	blitRect(pBfr, PITCH_X0 + CORNER_R, PITCH_Y0, PITCH_WIDTH - 2 * CORNER_R + 1, 1, MCOLOR_WHITE);
	blitRect(pBfr, PITCH_X0 + CORNER_R, PITCH_Y1, PITCH_WIDTH - 2 * CORNER_R + 1, 1, MCOLOR_WHITE);
	// Left / right, split by the goal mouth
	blitRect(pBfr, PITCH_X0, PITCH_Y0 + CORNER_R, 1, GOAL_Y0 - (PITCH_Y0 + CORNER_R), MCOLOR_WHITE);
	blitRect(pBfr, PITCH_X0, GOAL_Y1 + 1, 1, (PITCH_Y1 - CORNER_R) - GOAL_Y1, MCOLOR_WHITE);
	blitRect(pBfr, PITCH_X1, PITCH_Y0 + CORNER_R, 1, GOAL_Y0 - (PITCH_Y0 + CORNER_R), MCOLOR_WHITE);
	blitRect(pBfr, PITCH_X1, GOAL_Y1 + 1, 1, (PITCH_Y1 - CORNER_R) - GOAL_Y1, MCOLOR_WHITE);
	// Rounded corner arcs
	pitchArcQuarter(pBfr, PITCH_X0 + CORNER_R, PITCH_Y0 + CORNER_R, CORNER_R, -1, -1);
	pitchArcQuarter(pBfr, PITCH_X1 - CORNER_R, PITCH_Y0 + CORNER_R, CORNER_R, 1, -1);
	pitchArcQuarter(pBfr, PITCH_X0 + CORNER_R, PITCH_Y1 - CORNER_R, CORNER_R, -1, 1);
	pitchArcQuarter(pBfr, PITCH_X1 - CORNER_R, PITCH_Y1 - CORNER_R, CORNER_R, 1, 1);
}

// Full 1px midpoint circle (center circle)
static void pitchCircle(tBitMap *pBfr, WORD wCx, WORD wCy, WORD wR) {
	pitchArcQuarter(pBfr, wCx, wCy, wR, 1, 1);
	pitchArcQuarter(pBfr, wCx, wCy, wR, -1, 1);
	pitchArcQuarter(pBfr, wCx, wCy, wR, 1, -1);
	pitchArcQuarter(pBfr, wCx, wCy, wR, -1, -1);
}

static void pitchDrawGoal(tBitMap *pBfr, UBYTE ubIsLeft) {
	// Goal frame outside the wall: 1px red back line + top/bottom rails
	UWORD uwBackX = ubIsLeft ? PITCH_X0 - GOAL_DEPTH : PITCH_X1 + GOAL_DEPTH - 1;
	blitRect(pBfr, uwBackX, GOAL_Y0, 1, GOAL_HEIGHT + 1, MCOLOR_POST);
	if(ubIsLeft) {
		blitRect(pBfr, uwBackX, GOAL_Y0, GOAL_DEPTH + 1, 1, MCOLOR_POST);
		blitRect(pBfr, uwBackX, GOAL_Y1, GOAL_DEPTH + 1, 1, MCOLOR_POST);
		// Posts: 2x2 squares on the mouth corners
		blitRect(pBfr, PITCH_X0 - 1, GOAL_Y0 - 1, 2, 2, MCOLOR_POST);
		blitRect(pBfr, PITCH_X0 - 1, GOAL_Y1 - 1, 2, 2, MCOLOR_POST);
	}
	else {
		blitRect(pBfr, PITCH_X1, GOAL_Y0, GOAL_DEPTH, 1, MCOLOR_POST);
		blitRect(pBfr, PITCH_X1, GOAL_Y1, GOAL_DEPTH, 1, MCOLOR_POST);
		blitRect(pBfr, PITCH_X1, GOAL_Y0 - 1, 2, 2, MCOLOR_POST);
		blitRect(pBfr, PITCH_X1, GOAL_Y1 - 1, 2, 2, MCOLOR_POST);
	}
}

void pitchDrawBg(tBitMap *pBfr) {
	// Stadium surround (whole screen; HUD/banner strips draw over the top)
	blitRect(pBfr, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, MCOLOR_STADIUM);

	// Flat green pitch, corners cut back to the surround (rounded look)
	blitRect(pBfr, PITCH_X0, PITCH_Y0, PITCH_WIDTH + 1, PITCH_HEIGHT + 1, MCOLOR_PITCH);
	pitchCutCorner(pBfr, -1, -1);
	pitchCutCorner(pBfr, 1, -1);
	pitchCutCorner(pBfr, -1, 1);
	pitchCutCorner(pBfr, 1, 1);

	pitchDrawBorder(pBfr);

	// Halfway line + center circle
	blitRect(pBfr, PITCH_CX, PITCH_Y0 + 1, 1, PITCH_HEIGHT - 2, MCOLOR_WHITE);
	pitchCircle(pBfr, PITCH_CX, PITCH_CY, CENTER_CIRCLE_R);

	pitchDrawGoal(pBfr, 1);
	pitchDrawGoal(pBfr, 0);
}
