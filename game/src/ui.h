/* XAHBall - shared look of the menu-style screens (menu, controller setup,
 * team select, winner, credits): stadium backdrop + framed night-blue panels.
 * All of them use the menu palette (gamePaletteLoad).
 *
 * Drawing contract (double buffering, see ai-agent-instructions.md §2):
 * uiDrawBackdrop() is a full-screen copy (~80 KB of blitter traffic) - call
 * it once per page at state create, never per frame. Per-frame / on-change
 * redraws only refill a panel (uiDrawPanel) and draw text on top of it.
 */
#ifndef _XAHBALL_UI_H_
#define _XAHBALL_UI_H_

#include <ace/utils/bitmap.h>

//---------------------------------------------------------------------- DEFINES

// Backdrop layout: XAHBALL logo in the top band, crowd + pitch below it.
// Screen content goes into panels below UI_CONTENT_Y.
#define UI_CONTENT_Y 92

typedef enum tUiNav {
	UI_NAV_NONE,
	UI_NAV_UP,
	UI_NAV_DOWN,
	UI_NAV_LEFT,
	UI_NAV_RIGHT,
	UI_NAV_FIRE,
	UI_NAV_BACK,
} tUiNav;

//-------------------------------------------------------------------- FUNCTIONS

// One-shot menu navigation: arrows or WSAD, Return/Space/X = fire, ESC =
// back, joystick in port 2. Port 1 is left out on purpose: with a mouse
// plugged in there, mouse motion reads as joystick directions.
tUiNav uiNavPoll(void);

// True on any fresh key press or port 1/2 fire button (attract-mode exit)
UBYTE uiAnyInput(void);

// Loads the backdrop (chip RAM, kept for the whole session). Call once
// while the OS is alive; a missing file degrades to a flat fill.
void uiCreate(void);
void uiDestroy(void);

// Full-screen backdrop copy onto pDst
void uiDrawBackdrop(tBitMap *pDst);

// Night-blue panel with a 1px border (amber when highlighted)
void uiDrawPanel(
	tBitMap *pDst, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH, UBYTE ubHighlight
);

// Restores a backdrop rectangle (e.g. under blinking text outside panels).
// uwX and uwW must be multiples of 16.
void uiRestoreBackdrop(tBitMap *pDst, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH);

#endif // _XAHBALL_UI_H_
