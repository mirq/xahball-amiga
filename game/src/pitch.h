/* XAHBall - pitch geometry and background rendering */
#ifndef _XAHBALL_PITCH_H_
#define _XAHBALL_PITCH_H_

#include <ace/types.h>
#include <ace/utils/bitmap.h>

//---------------------------------------------------------------------- DEFINES

// Match screen: 320x256. Top HUD strip, banner strip (phase texts), pitch.
// Pitch top margin is sized so no sprite box can ever reach the banner -
// the banner is erased/redrawn every frame without any bob interaction.
#define HUD_HEIGHT 16
#define BANNER_Y0 16
#define BANNER_Y1 32

// Pitch inner walls (ball/players bounce off these). Rounded corners!
#define PITCH_X0 16
#define PITCH_Y0 40
#define PITCH_X1 304
#define PITCH_Y1 248

// Rounded corner radius (quarter-circle arcs at the 4 pitch corners)
#define CORNER_R 20

// Goal mouth on left/right walls; goal frame extends GOAL_DEPTH outside
#define GOAL_HEIGHT 64
#define GOAL_Y0 ((PITCH_Y0 + PITCH_Y1 - GOAL_HEIGHT) / 2)
#define GOAL_Y1 (GOAL_Y0 + GOAL_HEIGHT)
#define GOAL_DEPTH 10

// Goal post radius (matches the 2x2 px posts drawn by pitch.c)
#define POST_R 2

// Center circle radius (kickoff lock constrains against it)
#define CENTER_CIRCLE_R 26

#define PITCH_WIDTH (PITCH_X1 - PITCH_X0)
#define PITCH_HEIGHT (PITCH_Y1 - PITCH_Y0)
#define PITCH_CX ((PITCH_X0 + PITCH_X1) / 2)
#define PITCH_CY ((PITCH_Y0 + PITCH_Y1) / 2)

//------------------------------------------------------------------------ TYPES

//---------------------------------------------------------------------- GLOBALS

//-------------------------------------------------------------------- FUNCTIONS

// Renders full pitch background into given bitmap (draw once at match start)
void pitchDrawBg(tBitMap *pBfr);

// Returns 1 if given y is inside the goal mouth opening
UBYTE pitchIsInGoalMouth(UWORD uwY);

#endif // _XAHBALL_PITCH_H_
