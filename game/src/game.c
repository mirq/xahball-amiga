/* XAHBall - game state table and generic main wiring */
#include "game.h"

#include <ace/managers/game.h>
#include <ace/managers/joy.h>
#include <ace/managers/key.h>
#include <ace/managers/ptplayer.h>
#include <ace/managers/system.h>
#include <ace/utils/font.h>
#include "intro.h"
#include "input_slots.h"
#include "menu.h"
#include "setup.h"
#include "teamsel.h"
#include "credits.h"
#include "match.h"
#include "winner.h"
#include "sfx.h"

const UWORD g_uwTimeOptions[TIME_OPTION_COUNT] = {60, 120, 180, 300};

tStateManager *g_pGameStateManager = 0;
tState g_pGameStates[STATE_COUNT] = {
	[STATE_INTRO] = {.cbCreate = gsIntroCreate, .cbLoop = gsIntroLoop, .cbDestroy = gsIntroDestroy},
	[STATE_MENU] = {.cbCreate = gsMenuCreate, .cbLoop = gsMenuLoop, .cbDestroy = gsMenuDestroy},
	[STATE_SETUP] = {.cbCreate = gsSetupCreate, .cbLoop = gsSetupLoop, .cbDestroy = gsSetupDestroy},
	[STATE_TEAMSEL] = {.cbCreate = gsTeamSelCreate, .cbLoop = gsTeamSelLoop, .cbDestroy = gsTeamSelDestroy},
	[STATE_CREDITS] = {.cbCreate = gsCreditsCreate, .cbLoop = gsCreditsLoop, .cbDestroy = gsCreditsDestroy},
	[STATE_MATCH] = {.cbCreate = gsMatchCreate, .cbLoop = gsMatchLoop, .cbDestroy = gsMatchDestroy},
	[STATE_WINNER] = {.cbCreate = gsWinnerCreate, .cbLoop = gsWinnerLoop, .cbDestroy = gsWinnerDestroy},
};

tMatchConfig g_sMatchConfig = {
	.ubPlayersPerSide = 1,
	.ubTimeIdx = 1, // 2 minutes
	.ubSlotDevice = {0, 0, 0, 0, 0, 0}, // filled in createGameStates
	.ubTeamLeft = 0,
	.ubTeamRight = 1,
	.ubBallFreeze = 0, // ball rolls into the net by default
	.ubKickoffLock = 1, // kickoff circle rule on by default
};

tFont *g_pGameFont = 0;

UWORD g_uwScoreLeft = 0;
UWORD g_uwScoreRight = 0;

#define GENERIC_MAIN_LOOP_CONDITION gameIsRunning() && g_pGameStateManager->pCurrent
#include <ace/generic/main.h>

void genericCreate(void) {
	joyOpen();
	joyEnableParallel(); // JOY3/JOY4 via parallel port, ignore if absent
	keyCreate();

	// Default controller assignment: first slot on KB2 (WSAD + X), rest AI.
	// KB1's arrow keys are the most universal on real hardware, but fs-uae
	// test rigs swallow them for joystick emulation - KB2 works everywhere.
	g_sMatchConfig.ubSlotDevice[0] = DEVICE_KB2;
	for(UBYTE i = 1; i < SLOT_COUNT; ++i) {
		g_sMatchConfig.ubSlotDevice[i] = DEVICE_AI;
	}

	sfxCreate();

	g_pGameFont = fontCreateFromPath("data/silkscreen.fnt");

	createGameStates();
	stateChange(g_pGameStateManager, &g_pGameStates[STATE_INTRO]);
}

void genericProcess(void) {
	joyProcess();
	keyProcess();
	ptplayerProcess();
	stateProcess(g_pGameStateManager);
}

void genericDestroy(void) {
	destroyGameStates();
	sfxDestroy();
	if(g_pGameFont) {
		fontDestroy(g_pGameFont);
		g_pGameFont = 0;
	}
	keyDestroy();
	joyClose();
}

void createGameStates(void) {
	g_pGameStateManager = stateManagerCreate();
}

void destroyGameStates(void) {
	stateManagerDestroy(g_pGameStateManager);
}

// Menu/screen palette (also the RGB source for flag-derived colors like the
// per-country ball bands)
const UWORD g_pMenuPalette[16] = {
	0x000, // 0 black
	0xFFF, // 1 white
	0x0A0, // 2 pitch green
	0x080, // 3 pitch dark green
	0x138, // 4 bg (stadium / menu)
	0x666, // 5 gray
	0xF00, // 6 red
	0xC03, // 7 maroon
	0xF80, // 8 orange
	0xFF0, // 9 yellow
	0x0D0, // 10 green
	0x111, // 11 near-black
	0x06F, // 12 azure
	0x009, // 13 navy
	0x2CE, // 14 light blue accent
	0xFA0, // 15 amber
};

void gamePaletteLoad(tVPort *pVPort) {
	for(UBYTE i = 0; i < 16; ++i) {
		pVPort->pPalette[i] = g_pMenuPalette[i];
	}
}

// Match screen palette: pitch/stadium + dedicated dynamic slots for kits and
// ball bands. Indices are MCOLOR_* (game.h). Dynamic slots carry UNIQUE
// placeholder RGBs - bitmap_conv maps sprite pixels by color value, so
// duplicates would collapse onto the static black/white/red slots. Kit lines
// are just MCOLOR_BLACK, ball base is MCOLOR_WHITE (both static).
void matchPaletteLoad(tVPort *pVPort) {
	static const UWORD pPalette[16] = {
		0x000, // 0 black: outlines, HUD bg, text
		0xFFF, // 1 white: pitch lines, text, ball base, kick-ready outline
		0x785, // 2 pitch green (HaxBall-like muted grass)
		0x453, // 3 stadium surround (darker grass)
		0xE10, // 4 kit A fill (dynamic placeholder, unique)
		0x18E, // 5 kit B fill (dynamic placeholder, unique)
		0x666, // 6 HUD gray divider
		0xFA0, // 7 amber: GOAL!/countdown text, kick flash outline
		0xF00, // 8 goal posts + frame
		0xB8C, // 9 spare
		0x7A4, // 10 spare
		0x4A7, // 11 spare
		0x2CE, // 12 light blue accent spare
		0x111, // 13 near-black spare
		0x888, // 14 light gray spare
		0x222, // 15 dark spare
	};
	for(UBYTE i = 0; i < 16; ++i) {
		pVPort->pPalette[i] = pPalette[i];
	}
}
