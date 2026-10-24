/* XAHBall - advanced (custom) controller setup state
 * Lists player slots for the current team size; each slot cycles through
 * available devices. Only slots of active team size are shown. The simple
 * presets live in the main menu (controls.c); leaving this screen re-checks
 * whether the edited setup still equals a preset, else it becomes CUSTOM.
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
#include "controls.h"
#include "game.h"
#include "input_slots.h"
#include "sfx.h"
#include "ui.h"

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

// Visible rows map row -> slot index in g_sMatchConfig.ubSlotDevice
static UBYTE s_pVisibleRows[2 * SLOTS_PER_SIDE];
static UBYTE s_ubRowCount;
static UBYTE s_ubRow; // currently selected row
// Pages left to redraw (2 after every change - both buffers catch up)
static UBYTE s_ubDirty;

static const char *s_pDeviceDesc[DEVICE_COUNT] = {
	"COMPUTER PLAYER",
	"JOYSTICK IN PORT 2 - GAME PORT",
	"JOYSTICK IN PORT 1 - MOUSE PORT",
	"PARALLEL ADAPTER - PORT 3",
	"PARALLEL ADAPTER - PORT 4",
	"ARROWS  KICK: RIGHT SHIFT",
	"W S A D  KICK: LEFT SHIFT",
};

#define SETUP_PANEL_X 24
#define SETUP_PANEL_Y UI_CONTENT_Y
#define SETUP_PANEL_W 272
#define SETUP_PANEL_H 160
#define SETUP_ROW_STEP 11
#define SETUP_LABEL_X (SETUP_PANEL_X + 20)
#define SETUP_VALUE_CX (SETUP_PANEL_X + 180)

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

// Another visible slot uses the same (non-AI) device - it will fall back to
// CPU on exit, so warn while editing
static UBYTE setupIsDuplicate(UBYTE ubSlot) {
	UBYTE ubDev = g_sMatchConfig.ubSlotDevice[ubSlot];
	if(ubDev == DEVICE_AI) {
		return 0;
	}
	for(UBYTE i = 0; i < s_ubRowCount; ++i) {
		UBYTE ubOther = s_pVisibleRows[i];
		if(ubOther != ubSlot && g_sMatchConfig.ubSlotDevice[ubOther] == ubDev) {
			return 1;
		}
	}
	return 0;
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

static void setupDrawText(tBitMap *pDst, UWORD uwX, UWORD uwY, const char *sz, UBYTE ubColor, UBYTE ubFlags) {
	fontDrawStr(g_pGameFont, pDst, uwX, uwY, sz, ubColor, ubFlags | FONT_COOKIE, s_pTextBitMap);
}

static void setupDrawPanel(tBitMap *pDst) {
	UWORD uwCenterX = GAME_SCREEN_WIDTH / 2;
	uiDrawPanel(pDst, SETUP_PANEL_X, SETUP_PANEL_Y, SETUP_PANEL_W, SETUP_PANEL_H, 1);
	setupDrawText(pDst, uwCenterX, SETUP_PANEL_Y + 6, "CUSTOM CONTROLS", COLOR_AMBER, FONT_HCENTER | FONT_SHADOW);

	// Team headers + rows: each team block = header line + up to 3 rows
	UWORD uwTopA = SETUP_PANEL_Y + 20;
	UWORD uwTopB = uwTopA + (g_sMatchConfig.ubPlayersPerSide + 1) * SETUP_ROW_STEP + 4;
	setupDrawText(pDst, SETUP_LABEL_X - 8, uwTopA, "LEFT TEAM", COLOR_ACCENT, FONT_LEFT);
	setupDrawText(pDst, SETUP_LABEL_X - 8, uwTopB, "RIGHT TEAM", COLOR_ACCENT, FONT_LEFT);

	for(UBYTE ubRowIdx = 0; ubRowIdx < s_ubRowCount; ++ubRowIdx) {
		UBYTE ubSlot = s_pVisibleRows[ubRowIdx];
		UBYTE ubIsTeamA = ubSlot < SLOTS_PER_SIDE;
		UWORD uwY = (ubIsTeamA ? uwTopA : uwTopB) + (ubSlot % SLOTS_PER_SIDE + 1) * SETUP_ROW_STEP;
		UBYTE ubIsSel = (s_ubRow == ubRowIdx);
		char szLabel[12];
		sprintf(szLabel, "PLAYER %d", (ubSlot % SLOTS_PER_SIDE) + 1);
		if(ubIsSel) {
			blitRect(pDst, SETUP_PANEL_X + 3, uwY - 2, SETUP_PANEL_W - 6, 10, COLOR_BG);
		}
		setupDrawText(pDst, SETUP_LABEL_X, uwY, szLabel, ubIsSel ? COLOR_AMBER : COLOR_WHITE, FONT_LEFT | FONT_SHADOW);
		UBYTE ubDevColor = setupIsDuplicate(ubSlot) ? COLOR_RED
			: (ubIsSel ? COLOR_WHITE : COLOR_ACCENT);
		setupDrawText(
			pDst, SETUP_VALUE_CX, uwY, g_pDeviceNames[g_sMatchConfig.ubSlotDevice[ubSlot]],
			ubDevColor, FONT_HCENTER | FONT_SHADOW
		);
		if(ubIsSel) {
			setupDrawText(pDst, SETUP_VALUE_CX - 52, uwY, "<", COLOR_AMBER, FONT_LEFT);
			setupDrawText(pDst, SETUP_VALUE_CX + 48, uwY, ">", COLOR_AMBER, FONT_LEFT);
		}
	}

	// Selected device description (or a duplicate warning) + key hint
	UBYTE ubSelSlot = s_pVisibleRows[s_ubRow];
	UWORD uwInfoY = SETUP_PANEL_Y + SETUP_PANEL_H - 30;
	blitRect(pDst, SETUP_PANEL_X + 8, uwInfoY - 5, SETUP_PANEL_W - 16, 1, COLOR_GRAY);
	if(setupIsDuplicate(ubSelSlot)) {
		setupDrawText(pDst, uwCenterX, uwInfoY, "ALSO USED BY ANOTHER PLAYER", COLOR_RED, FONT_HCENTER);
	}
	else {
		setupDrawText(
			pDst, uwCenterX, uwInfoY, s_pDeviceDesc[g_sMatchConfig.ubSlotDevice[ubSelSlot]],
			COLOR_WHITE, FONT_HCENTER
		);
	}
	setupDrawText(pDst, uwCenterX, uwInfoY + 14, "L/R: DEVICE   FIRE: DONE", COLOR_GRAY, FONT_HCENTER);
}

static void setupLeave(UBYTE ubSfx) {
	setupResolveDuplicates();
	controlsDetectPreset();
	sfxPlay(ubSfx, 64);
	stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
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

	uiDrawBackdrop(s_pBfr->pBack);
	uiDrawBackdrop(s_pBfr->pFront);
	setupDrawPanel(s_pBfr->pBack);
	setupDrawPanel(s_pBfr->pFront);
	s_ubDirty = 0;

	viewLoad(s_pView);
	logBlockEnd("gsSetupCreate");
	systemUnuse();
}

void gsSetupLoop(void) {
	tUiNav eNav = uiNavPoll();
	switch(eNav) {
		case UI_NAV_BACK:
			setupLeave(SFX_BACK);
			return;
		case UI_NAV_FIRE:
			setupLeave(SFX_CONFIRM);
			return;
		case UI_NAV_UP:
			s_ubRow = (s_ubRow + s_ubRowCount - 1) % s_ubRowCount;
			sfxPlay(SFX_MOVE, 48);
			s_ubDirty = 2;
			break;
		case UI_NAV_DOWN:
			s_ubRow = (s_ubRow + 1) % s_ubRowCount;
			sfxPlay(SFX_MOVE, 48);
			s_ubDirty = 2;
			break;
		case UI_NAV_LEFT:
		case UI_NAV_RIGHT: {
			// No uniqueness check here - duplicates are allowed while editing
			// (shown red) and resolved to AI by setupResolveDuplicates()
			UBYTE ubSlot = s_pVisibleRows[s_ubRow];
			UBYTE *pDev = &g_sMatchConfig.ubSlotDevice[ubSlot];
			*pDev = (*pDev + DEVICE_COUNT + (eNav == UI_NAV_RIGHT ? 1 : -1)) % DEVICE_COUNT;
			sfxPlay(SFX_MOVE, 48);
			s_ubDirty = 2;
			break;
		}
		default:
			break;
	}

	if(s_ubDirty) {
		setupDrawPanel(s_pBfr->pBack);
		--s_ubDirty;
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
