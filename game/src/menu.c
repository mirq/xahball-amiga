/* XAHBall - main menu state
 * Two pages in one panel over the stadium backdrop:
 *   MAIN  - start, team size, controls (simple presets / custom), CPU level,
 *           match time, rules page, credits, quit
 *   RULES - ball freeze, kickoff lock
 * Value rows cycle with left/right (fire cycles forward too). Fire on
 * CONTROLS opens the advanced per-slot setup screen. The panel below the
 * rows always summarizes who controls each side.
 * Idling on the menu for MENU_DEMO_IDLE_FRAMES starts an all-AI demo match.
 */
#include "menu.h"

#include <ace/managers/blit.h>
#include <ace/managers/game.h>
#include <ace/managers/log.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>
#include <ace/utils/font.h>
#include <stdio.h>
#include "ai.h"
#include "controls.h"
#include "game.h"
#include "match.h"
#include "music.h"
#include "sfx.h"
#include "ui.h"

#define MENU_DEMO_IDLE_FRAMES (50 * 25)

// Panel geometry - everything below the backdrop's logo band
#define MENU_PANEL_X 32
#define MENU_PANEL_Y UI_CONTENT_Y
#define MENU_PANEL_W 256
#define MENU_PANEL_H 158
#define MENU_ROW_Y0 (MENU_PANEL_Y + 8)
#define MENU_ROW_STEP 12
#define MENU_LABEL_X (MENU_PANEL_X + 14)
#define MENU_VALUE_CX (MENU_PANEL_X + 186)
#define MENU_ARROW_L (MENU_PANEL_X + 132)
#define MENU_ARROW_R (MENU_PANEL_X + MENU_PANEL_W - 14)
#define MENU_INFO_Y (MENU_PANEL_Y + 112)

typedef enum tMenuPage {
	MENU_PAGE_MAIN,
	MENU_PAGE_RULES,
} tMenuPage;

typedef enum tMainRow {
	MAIN_ROW_START,
	MAIN_ROW_SIZE,
	MAIN_ROW_CONTROLS,
	MAIN_ROW_AI,
	MAIN_ROW_TIME,
	MAIN_ROW_RULES,
	MAIN_ROW_CREDITS,
	MAIN_ROW_QUIT,
	MAIN_ROW_COUNT
} tMainRow;

typedef enum tRulesRow {
	RULES_ROW_FREEZE,
	RULES_ROW_LOCK,
	RULES_ROW_BACK,
	RULES_ROW_COUNT
} tRulesRow;

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

static tMenuPage s_ePage;
static UBYTE s_ubRow;
// Selection survives trips to setup/credits/match (the state is recreated)
static UBYTE s_ubMainRow = MAIN_ROW_START;
// Pages left to redraw: the panel only changes on input, but both
// double-buffer pages must catch up -> 2 on every change
static UBYTE s_ubDirty;
static UWORD s_uwIdleFrames;

static const char *s_pAiLevelNames[AI_LEVEL_COUNT] = {"NORMAL", "HARD"};

static UBYTE menuRowCount(void) {
	return (s_ePage == MENU_PAGE_MAIN) ? MAIN_ROW_COUNT : RULES_ROW_COUNT;
}

// Row label + value text (value 0 = plain action row)
static void menuRowTexts(UBYTE ubRow, const char **pLabel, const char **pValue) {
	static char szValue[24];
	*pValue = 0;
	if(s_ePage == MENU_PAGE_RULES) {
		switch(ubRow) {
			case RULES_ROW_FREEZE:
				*pLabel = "BALL FREEZE";
				*pValue = g_sMatchConfig.ubBallFreeze ? "ON" : "OFF";
				break;
			case RULES_ROW_LOCK:
				*pLabel = "KICKOFF LOCK";
				*pValue = g_sMatchConfig.ubKickoffLock ? "ON" : "OFF";
				break;
			default:
				*pLabel = "BACK";
				break;
		}
		return;
	}
	switch(ubRow) {
		case MAIN_ROW_START: *pLabel = "START MATCH"; break;
		case MAIN_ROW_SIZE:
			*pLabel = "TEAM SIZE";
			sprintf(szValue, "%d VS %d",
				g_sMatchConfig.ubPlayersPerSide, g_sMatchConfig.ubPlayersPerSide);
			*pValue = szValue;
			break;
		case MAIN_ROW_CONTROLS:
			*pLabel = "CONTROLS";
			*pValue = controlsPresetName(
				g_sMatchConfig.ubPlayersPerSide, g_sMatchConfig.ubControlPreset
			);
			break;
		case MAIN_ROW_AI:
			*pLabel = "CPU LEVEL";
			*pValue = s_pAiLevelNames[g_sMatchConfig.ubAiLevel];
			break;
		case MAIN_ROW_TIME:
			*pLabel = "MATCH TIME";
			sprintf(szValue, "%d MIN", g_uwTimeOptions[g_sMatchConfig.ubTimeIdx] / 60);
			*pValue = szValue;
			break;
		case MAIN_ROW_RULES: *pLabel = "RULES"; break;
		case MAIN_ROW_CREDITS: *pLabel = "CREDITS"; break;
		default: *pLabel = "QUIT"; break;
	}
}

static void menuDrawText(tBitMap *pDst, UWORD uwX, UWORD uwY, const char *sz, UBYTE ubColor, UBYTE ubFlags) {
	if(sz[0]) { // empty string = zero-width blit = memory corruption
		fontDrawStr(g_pGameFont, pDst, uwX, uwY, sz, ubColor, ubFlags | FONT_COOKIE, s_pTextBitMap);
	}
}

// Context hint for the selected row, shown at the bottom of the panel
static const char *menuHint(void) {
	if(s_ePage == MENU_PAGE_RULES) {
		switch(s_ubRow) {
			case RULES_ROW_FREEZE: return "BALL STOPS DEAD ON A GOAL";
			case RULES_ROW_LOCK: return "KEEP OUT OF CIRCLE AT KICKOFF";
			default: return "FIRE: BACK TO MAIN MENU";
		}
	}
	switch(s_ubRow) {
		case MAIN_ROW_CONTROLS: return "L/R: PRESET   FIRE: CUSTOM SETUP";
		case MAIN_ROW_SIZE:
		case MAIN_ROW_AI:
		case MAIN_ROW_TIME: return "LEFT/RIGHT: CHANGE";
		default: return "FIRE: SELECT";
	}
}

static void menuDrawPanel(tBitMap *pDst) {
	uiDrawPanel(pDst, MENU_PANEL_X, MENU_PANEL_Y, MENU_PANEL_W, MENU_PANEL_H, 1);

	for(UBYTE ubRow = 0; ubRow < menuRowCount(); ++ubRow) {
		const char *szLabel, *szValue;
		menuRowTexts(ubRow, &szLabel, &szValue);
		UWORD uwY = MENU_ROW_Y0 + ubRow * MENU_ROW_STEP;
		UBYTE ubSel = (ubRow == s_ubRow);
		if(ubSel) {
			blitRect(pDst, MENU_PANEL_X + 3, uwY - 2, MENU_PANEL_W - 6, 10, COLOR_BG);
		}
		menuDrawText(pDst, MENU_LABEL_X, uwY, szLabel, ubSel ? COLOR_AMBER : COLOR_WHITE, FONT_LEFT | FONT_SHADOW);
		if(szValue) {
			menuDrawText(pDst, MENU_VALUE_CX, uwY, szValue, ubSel ? COLOR_WHITE : COLOR_ACCENT, FONT_HCENTER | FONT_SHADOW);
			if(ubSel) {
				menuDrawText(pDst, MENU_ARROW_L, uwY, "<", COLOR_AMBER, FONT_LEFT);
				menuDrawText(pDst, MENU_ARROW_R, uwY, ">", COLOR_AMBER, FONT_LEFT);
			}
		}
	}

	// Who controls each side - always visible, so presets need no guessing
	UWORD uwY = MENU_INFO_Y;
	blitRect(pDst, MENU_PANEL_X + 8, uwY - 5, MENU_PANEL_W - 16, 1, COLOR_GRAY);
	menuDrawText(pDst, MENU_LABEL_X, uwY, "LEFT", COLOR_ACCENT, FONT_LEFT);
	menuDrawText(pDst, MENU_LABEL_X + 40, uwY, controlsDescribeSide(0), COLOR_WHITE, FONT_LEFT);
	menuDrawText(pDst, MENU_LABEL_X, uwY + 10, "RIGHT", COLOR_ACCENT, FONT_LEFT);
	menuDrawText(pDst, MENU_LABEL_X + 40, uwY + 10, controlsDescribeSide(1), COLOR_WHITE, FONT_LEFT);
	menuDrawText(pDst, GAME_SCREEN_WIDTH / 2, uwY + 26, menuHint(), COLOR_GRAY, FONT_HCENTER);
}

static void menuSetPage(tMenuPage ePage, UBYTE ubRow) {
	if(s_ePage == MENU_PAGE_MAIN) {
		s_ubMainRow = s_ubRow;
	}
	s_ePage = ePage;
	s_ubRow = ubRow;
	s_ubDirty = 2;
}

// Left/right (and fire) on a value row. Returns 1 if the row has a value.
static UBYTE menuChangeValue(BYTE bDir) {
	if(s_ePage == MENU_PAGE_RULES) {
		switch(s_ubRow) {
			case RULES_ROW_FREEZE:
				g_sMatchConfig.ubBallFreeze = !g_sMatchConfig.ubBallFreeze;
				return 1;
			case RULES_ROW_LOCK:
				g_sMatchConfig.ubKickoffLock = !g_sMatchConfig.ubKickoffLock;
				return 1;
			default:
				return 0;
		}
	}
	switch(s_ubRow) {
		case MAIN_ROW_SIZE: {
			BYTE bVal = g_sMatchConfig.ubPlayersPerSide + bDir;
			if(bVal < 1) { bVal = SLOTS_PER_SIDE; }
			if(bVal > SLOTS_PER_SIDE) { bVal = 1; }
			g_sMatchConfig.ubPlayersPerSide = bVal;
			controlsOnTeamSizeChange();
			return 1;
		}
		case MAIN_ROW_CONTROLS: {
			// Presets 0..n-1, then CUSTOM (keeps the current slot devices)
			BYTE bCount = controlsPresetCount(g_sMatchConfig.ubPlayersPerSide);
			BYTE bVal = (g_sMatchConfig.ubControlPreset == CONTROL_PRESET_CUSTOM)
				? bCount : g_sMatchConfig.ubControlPreset;
			bVal += bDir;
			if(bVal < 0) { bVal = bCount; }
			if(bVal > bCount) { bVal = 0; }
			controlsApplyPreset((bVal == bCount) ? CONTROL_PRESET_CUSTOM : bVal);
			return 1;
		}
		case MAIN_ROW_AI:
			g_sMatchConfig.ubAiLevel = (g_sMatchConfig.ubAiLevel + AI_LEVEL_COUNT + bDir) % AI_LEVEL_COUNT;
			return 1;
		case MAIN_ROW_TIME:
			g_sMatchConfig.ubTimeIdx = (g_sMatchConfig.ubTimeIdx + TIME_OPTION_COUNT + bDir) % TIME_OPTION_COUNT;
			return 1;
		default:
			return 0;
	}
}

// Fire on an action row. Returns 1 when the state changed (caller returns).
static UBYTE menuActivate(void) {
	if(s_ePage == MENU_PAGE_RULES) {
		if(s_ubRow == RULES_ROW_BACK) {
			sfxPlay(SFX_BACK, 64);
			menuSetPage(MENU_PAGE_MAIN, MAIN_ROW_RULES);
		}
		else if(menuChangeValue(1)) {
			sfxPlay(SFX_MOVE, 48);
			s_ubDirty = 2;
		}
		return 0;
	}
	switch(s_ubRow) {
		case MAIN_ROW_START:
			sfxPlay(SFX_CONFIRM, 64);
			stateChange(g_pGameStateManager, &g_pGameStates[STATE_TEAMSEL]);
			return 1;
		case MAIN_ROW_CONTROLS:
			sfxPlay(SFX_CONFIRM, 64);
			stateChange(g_pGameStateManager, &g_pGameStates[STATE_SETUP]);
			return 1;
		case MAIN_ROW_RULES:
			sfxPlay(SFX_CONFIRM, 64);
			menuSetPage(MENU_PAGE_RULES, 0);
			return 0;
		case MAIN_ROW_CREDITS:
			sfxPlay(SFX_CONFIRM, 64);
			stateChange(g_pGameStateManager, &g_pGameStates[STATE_CREDITS]);
			return 1;
		case MAIN_ROW_QUIT:
			gameExit();
			return 1;
		default:
			if(menuChangeValue(1)) {
				sfxPlay(SFX_MOVE, 48);
				s_ubDirty = 2;
			}
			return 0;
	}
}

void gsMenuCreate(void) {
	logBlockBegin("gsMenuCreate");

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

	s_ePage = MENU_PAGE_MAIN;
	s_ubRow = s_ubMainRow;
	s_uwIdleFrames = 0;
	s_ubDirty = 0;
	uiDrawBackdrop(s_pBfr->pBack);
	uiDrawBackdrop(s_pBfr->pFront);
	menuDrawPanel(s_pBfr->pBack);
	menuDrawPanel(s_pBfr->pFront);

	// Disk I/O on the first visit (OS still alive here), resident after
	musicPlay();

	viewLoad(s_pView);
	logBlockEnd("gsMenuCreate");
	systemUnuse();
}

void gsMenuLoop(void) {
#ifdef XAHBALL_AUTOTEST
	// Automated audio-loss probe: enter a match once, and after returning
	// play a menu bleep every ~0.6s while watching the audio DMA + volume
	// registers. A playing sample shows up as enabled channel DMA with a
	// non-zero volume - if no play is ever seen, ptplayer is dead.
	{
		static UWORD s_uwAutoMenu = 0;
		static UBYTE s_ubAudioSeen = 0;
		++s_uwAutoMenu;
		if(s_uwAutoMenu == 80) {
			stateChange(g_pGameStateManager, &g_pGameStates[STATE_MATCH]);
			return;
		}
		else if(s_uwAutoMenu > 80) {
			UWORD uwInMatch = s_uwAutoMenu - 80;
			if((uwInMatch & 31) == 0) {
				sfxPlay(SFX_MOVE, 64);
			}
			UWORD uwDma = g_pCustom->dmaconr;
			UWORD uwVol = g_pCustom->aud[0].ac_vol | g_pCustom->aud[1].ac_vol
				| g_pCustom->aud[2].ac_vol | g_pCustom->aud[3].ac_vol;
			if((uwDma & 0x000F) && uwVol) {
				s_ubAudioSeen = 1;
			}
			blitRect(
				s_pBfr->pBack, 8, 8, 120, 14,
				s_ubAudioSeen ? COLOR_GREEN : COLOR_RED
			);
			fontDrawStr(
				g_pGameFont, s_pBfr->pBack, 68, 10,
				s_ubAudioSeen ? "AUDIO OK" : "AUDIO DEAD",
				COLOR_WHITE, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
		}
	}
#endif

	tUiNav eNav = uiNavPoll();
	if(eNav == UI_NAV_NONE) {
		if(++s_uwIdleFrames >= MENU_DEMO_IDLE_FRAMES) {
			matchStartDemo();
			return;
		}
	}
	else {
		s_uwIdleFrames = 0;
	}

	switch(eNav) {
		case UI_NAV_UP:
			s_ubRow = (s_ubRow + menuRowCount() - 1) % menuRowCount();
			sfxPlay(SFX_MOVE, 48);
			s_ubDirty = 2;
			break;
		case UI_NAV_DOWN:
			s_ubRow = (s_ubRow + 1) % menuRowCount();
			sfxPlay(SFX_MOVE, 48);
			s_ubDirty = 2;
			break;
		case UI_NAV_LEFT:
		case UI_NAV_RIGHT:
			if(menuChangeValue(eNav == UI_NAV_RIGHT ? 1 : -1)) {
				sfxPlay(SFX_MOVE, 48);
				s_ubDirty = 2;
			}
			break;
		case UI_NAV_FIRE:
			if(menuActivate()) {
				return;
			}
			break;
		case UI_NAV_BACK:
			if(s_ePage == MENU_PAGE_RULES) {
				sfxPlay(SFX_BACK, 64);
				menuSetPage(MENU_PAGE_MAIN, MAIN_ROW_RULES);
			}
			else {
				gameExit();
				return;
			}
			break;
		default:
			break;
	}

	if(s_ubDirty) {
		menuDrawPanel(s_pBfr->pBack);
		--s_ubDirty;
	}

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsMenuDestroy(void) {
	systemUse();
	logBlockBegin("gsMenuDestroy");
	if(s_ePage == MENU_PAGE_MAIN) {
		s_ubMainRow = s_ubRow;
	}
	musicStop();
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsMenuDestroy");
}
