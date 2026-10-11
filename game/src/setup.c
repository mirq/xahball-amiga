/* XAHBall - controller setup state
 * Lists player slots for the current team size; each slot cycles through
 * available devices. Only slots of active team size are shown.
 */
#include "setup.h"

#include <ace/managers/blit.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/log.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/extview.h>
#include <ace/utils/font.h>
#include <stdio.h>
#include "game.h"
#include "input_slots.h"
#include "sfx.h"

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

// Visible rows map row -> slot index in g_sMatchConfig.ubSlotDevice
static UBYTE s_pVisibleRows[2 * SLOTS_PER_SIDE];
static UBYTE s_ubRowCount;
static UBYTE s_ubRow; // currently selected row

static const char *s_pDeviceDesc[DEVICE_COUNT] = {
	"AI BOT",
	"JOYSTICK 1",
	"JOYSTICK 2",
	"JOYSTICK 3 (PARALLEL)",
	"JOYSTICK 4 (PARALLEL)",
	"ARROWS + R-SHIFT",
	"WASD + L-SHIFT",
};

// Slots active for the current team size: 0..n-1 (team A), 3..3+n-1 (team B)
static UBYTE setupIsSlotActive(UBYTE ubSlot) {
	return (ubSlot % SLOTS_PER_SIDE) < g_sMatchConfig.ubPlayersPerSide;
}

// Leaving the screen: removed players (inactive slots) lose their device, and
// each non-AI device stays with its first ACTIVE slot in slot order, later
// duplicates revert to AI. While editing, duplicates are deliberately allowed
// - no checks while cycling. Restricting this to active slots matters: a
// hidden slot from a larger team size must never keep or steal a device from
// a visible player.
static void setupResolveDuplicates(void) {
	for(UBYTE i = 0; i < SLOT_COUNT; ++i) {
		if(!setupIsSlotActive(i)) {
			g_sMatchConfig.ubSlotDevice[i] = DEVICE_AI;
		}
	}
	for(UBYTE ubDev = DEVICE_AI + 1; ubDev < DEVICE_COUNT; ++ubDev) {
		UBYTE ubTaken = 0;
		for(UBYTE i = 0; i < SLOT_COUNT; ++i) {
			if(g_sMatchConfig.ubSlotDevice[i] == ubDev) {
				if(ubTaken || !setupIsSlotActive(i)) {
					g_sMatchConfig.ubSlotDevice[i] = DEVICE_AI;
				}
				else {
					ubTaken = 1;
				}
			}
		}
	}
}

static void setupRefreshRows(void) {
	s_ubRowCount = 0;
	for(UBYTE i = 0; i < g_sMatchConfig.ubPlayersPerSide; ++i) {
		s_pVisibleRows[s_ubRowCount++] = i; // team A (left)
	}
	for(UBYTE i = 0; i < g_sMatchConfig.ubPlayersPerSide; ++i) {
		s_pVisibleRows[s_ubRowCount++] = SLOTS_PER_SIDE + i; // team B (right)
	}
	if(s_ubRow >= s_ubRowCount) {
		s_ubRow = 0;
	}
}

static void setupDrawPage(tBitMap *pBack) {
	UWORD uwCenterX = GAME_SCREEN_WIDTH / 2;

	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, COLOR_BG);
	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
	blitRect(pBack, 0, GAME_SCREEN_HEIGHT - 2, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);

	fontDrawStr(
		g_pGameFont, pBack, uwCenterX, 24, "CONTROLLERS", COLOR_AMBER,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);

	UWORD uwLineStep = g_pGameFont->uwHeight + 8;
	UWORD uwTopA = 64;
	UWORD uwTopB = uwTopA + (SLOTS_PER_SIDE + 1) * uwLineStep;

	fontDrawStr(
		g_pGameFont, pBack, 12, uwTopA - uwLineStep, "TEAM A (LEFT)", COLOR_WHITE,
		FONT_COOKIE | FONT_LEFT, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, 12, uwTopB - uwLineStep, "TEAM B (RIGHT)", COLOR_WHITE,
		FONT_COOKIE | FONT_LEFT, s_pTextBitMap
	);

	for(UBYTE ubRowIdx = 0; ubRowIdx < s_ubRowCount; ++ubRowIdx) {
		UBYTE ubSlot = s_pVisibleRows[ubRowIdx];
		UBYTE ubIsTeamA = ubSlot < SLOTS_PER_SIDE;
		UWORD uwY = (ubIsTeamA ? uwTopA : uwTopB) + (ubSlot % SLOTS_PER_SIDE) * uwLineStep;
		UBYTE ubIsSel = (s_ubRow == ubRowIdx);
		char szLabel[12];
		sprintf(szLabel, "PLAYER %d", (ubSlot % SLOTS_PER_SIDE) + 1);
		fontDrawStr(
			g_pGameFont, pBack, 12, uwY, szLabel,
			ubIsSel ? COLOR_AMBER : COLOR_GRAY,
			FONT_COOKIE | FONT_LEFT, s_pTextBitMap
		);
		fontDrawStr(
			g_pGameFont, pBack, 72, uwY, g_pDeviceNames[g_sMatchConfig.ubSlotDevice[ubSlot]],
			ubIsSel ? COLOR_AMBER : COLOR_WHITE,
			FONT_COOKIE | FONT_LEFT, s_pTextBitMap
		);
	}

	// Hint + selected device description
	UBYTE ubSelSlot = s_pVisibleRows[s_ubRow];
	fontDrawStr(
		g_pGameFont, pBack, uwCenterX, 202, s_pDeviceDesc[g_sMatchConfig.ubSlotDevice[ubSelSlot]],
		COLOR_ACCENT, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, uwCenterX, 222, "L/R: DEVICE  U/D: SLOT  FIRE: DONE, DUPES->AI",
		COLOR_GRAY, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
}

void gsSetupCreate(void) {
	logBlockBegin("gsSetupCreate");

	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_DONE
	);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, GAME_BPP,
		TAG_DONE
	);
	gamePaletteLoad(s_pVPort);

	s_pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
		TAG_DONE
	);

	s_pTextBitMap = fontCreateTextBitMap(320, g_pGameFont->uwHeight + 8);
	setupRefreshRows();

	setupDrawPage(s_pBfr->pBack);
	setupDrawPage(s_pBfr->pFront);

	viewLoad(s_pView);
	logBlockEnd("gsSetupCreate");
	systemUnuse();
}

void gsSetupLoop(void) {
	if(keyUse(KEY_ESCAPE)) {
		setupResolveDuplicates();
		sfxPlay(SFX_BACK, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	// Per-frame off-page redraw - pages swap every frame
	setupDrawPage(s_pBfr->pBack);

	if(keyUse(KEY_UP) || keyUse(KEY_W) || joyUse(JOY1_UP)) {
		s_ubRow = (s_ubRow + s_ubRowCount - 1) % s_ubRowCount;
		sfxPlay(SFX_MOVE, 48);
	}
	else if(keyUse(KEY_DOWN) || keyUse(KEY_S) || joyUse(JOY1_DOWN)) {
		s_ubRow = (s_ubRow + 1) % s_ubRowCount;
		sfxPlay(SFX_MOVE, 48);
	}
	else if(keyUse(KEY_LEFT) || keyUse(KEY_RIGHT) || keyUse(KEY_A) || keyUse(KEY_D) || joyUse(JOY1_LEFT) || joyUse(JOY1_RIGHT)) {
		UBYTE ubRight = keyCheck(KEY_RIGHT) || keyCheck(KEY_D) || joyCheck(JOY1_RIGHT);
		BYTE bDir = ubRight ? 1 : -1;
		UBYTE ubSlot = s_pVisibleRows[s_ubRow];
		BYTE bDev = g_sMatchConfig.ubSlotDevice[ubSlot] + bDir;
		if(bDev < 0) { bDev = DEVICE_COUNT - 1; }
		if(bDev >= DEVICE_COUNT) { bDev = DEVICE_AI; }
		// No uniqueness check here - duplicates are allowed while editing and
		// resolved to AI by setupResolveDuplicates() when leaving the screen
		g_sMatchConfig.ubSlotDevice[ubSlot] = bDev;
		sfxPlay(SFX_MOVE, 48);
	}
	else if(keyUse(KEY_RETURN) || keyUse(KEY_X) || joyUse(JOY1_FIRE)) {
		setupResolveDuplicates();
		sfxPlay(SFX_CONFIRM, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsSetupDestroy(void) {
	systemUse();
	logBlockBegin("gsSetupDestroy");
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsSetupDestroy");
}
