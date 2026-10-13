/* XAHBall - main menu state */
#include "menu.h"

#include <ace/managers/blit.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include <ace/managers/ptplayer.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>
#include <ace/utils/font.h>
#include <stdio.h>
#include "game.h"
#include "menulist.h"
#include "sfx.h"
#include "teams.h"

typedef enum tMenuPage {
	MENU_PAGE_MAIN,
	MENU_PAGE_MODE,   // handled as value rows on main page - kept for clarity
} tMenuPage;

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;
static tMenuList *s_pMenuList;
// Menu music (chaosArena ptplayer pattern): the .mod structure sits in
// fast mem, its instruments in a chip samplepack
static tPtplayerMod *s_pMenuMod;
static tPtplayerSamplePack *s_pMenuSamples;

// Main page rows
typedef enum tMenuRow {
	MENU_ROW_MODE,
	MENU_ROW_TIME,
	MENU_ROW_FREEZE,
	MENU_ROW_LOCK,
	MENU_ROW_CONTROLLERS,
	MENU_ROW_CREDITS,
	MENU_ROW_START,
	MENU_ROW_QUIT,
	MENU_ROW_COUNT
} tMenuRow;

static void menuDrawBg(tBitMap *pTarget) {
	tBitMap *pBack = pTarget;

	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, COLOR_BG);

	// Top and bottom accents
	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
	blitRect(pBack, 0, GAME_SCREEN_HEIGHT - 2, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);

	// Mini pitch decoration at bottom center
	UWORD uwPitchX = GAME_SCREEN_WIDTH / 2 - 60;
	UWORD uwPitchY = 230;
	blitRect(pBack, uwPitchX, uwPitchY, 120, 18, COLOR_PITCH);
	blitRect(pBack, uwPitchX, uwPitchY, 120, 1, COLOR_WHITE);
	blitRect(pBack, uwPitchX, uwPitchY + 17, 120, 1, COLOR_WHITE);
	blitRect(pBack, uwPitchX + 59, uwPitchY + 3, 1, 12, COLOR_WHITE);
}

// Redraws all menu entries on BOTH buffer pages so double-buffered
// display stays consistent
static void menuUpdateEntries(void) {
	static char szBufMode[32];
	static char szBufTime[32];
	static char szBufFreeze[32];
	static char szBufLock[32];

	UBYTE ubPpS = g_sMatchConfig.ubPlayersPerSide;
	sprintf(szBufMode, "TEAM SIZE: %dVS%d", ubPpS, ubPpS);
	UWORD uwSecs = g_uwTimeOptions[g_sMatchConfig.ubTimeIdx];
	sprintf(szBufTime, "MATCH TIME: %d MIN", uwSecs / 60);
	sprintf(szBufFreeze, "BALL FREEZE: %s", g_sMatchConfig.ubBallFreeze ? "ON" : "OFF");
	sprintf(szBufLock, "KICKOFF LOCK: %s", g_sMatchConfig.ubKickoffLock ? "ON" : "OFF");

	menuListSetEntry(s_pMenuList, MENU_ROW_MODE, MENULIST_ENABLED, szBufMode);
	menuListSetEntry(s_pMenuList, MENU_ROW_TIME, MENULIST_ENABLED, szBufTime);
	menuListSetEntry(s_pMenuList, MENU_ROW_FREEZE, MENULIST_ENABLED, szBufFreeze);
	menuListSetEntry(s_pMenuList, MENU_ROW_LOCK, MENULIST_ENABLED, szBufLock);
	menuListSetEntry(s_pMenuList, MENU_ROW_CONTROLLERS, MENULIST_ENABLED, "CONTROLLERS");
	menuListSetEntry(s_pMenuList, MENU_ROW_CREDITS, MENULIST_ENABLED, "CREDITS");
	menuListSetEntry(s_pMenuList, MENU_ROW_START, MENULIST_ENABLED, "START MATCH");
	menuListSetEntry(s_pMenuList, MENU_ROW_QUIT, MENULIST_ENABLED, "QUIT");
	// NOTE: no menuListResetCount here - it would reset the selection to 0

	// Entry text bitmaps refreshed; display happens in the per-frame redraw
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
	s_pMenuList = menuListCreate(
		GAME_SCREEN_WIDTH / 2, 88, MENU_ROW_COUNT, 8,
		g_pGameFont, FONT_HCENTER | FONT_COOKIE | FONT_SHADOW,
		COLOR_WHITE, COLOR_GRAY, COLOR_AMBER,
		s_pBfr->pBack
	);
	menuListResetCount(s_pMenuList, MENU_ROW_COUNT);

	menuDrawBg(s_pBfr->pBack);
	menuDrawBg(s_pBfr->pFront);
	for(UBYTE ubPage = 0; ubPage < 2; ++ubPage) {
		tBitMap *pPage = ubPage ? s_pBfr->pFront : s_pBfr->pBack;
		fontDrawStr(
			g_pGameFont, pPage, GAME_SCREEN_WIDTH / 2, 36, "XAHBALL",
			COLOR_AMBER, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}
	menuUpdateEntries();

	// Music: load the mod + chip samplepack (disk I/O - OS still alive),
	// then start replay. ptplayerLoadMod also works after systemUnuse,
	// but loading files must happen while the OS is up.
	s_pMenuMod = ptplayerModCreateFromPath("data/menu.mod");
	s_pMenuSamples = ptplayerSampleDataCreateFromPath("data/menu.samplepack");
	if(s_pMenuMod && s_pMenuSamples) {
		ptplayerLoadMod(s_pMenuMod, s_pMenuSamples, 0);
		ptplayerEnableMusic(1);
	}
	else {
		logWrite("ERR: menu music assets failed to load - continuing silently\n");
		ptplayerModDestroy(s_pMenuMod);
		s_pMenuMod = 0;
	}

	viewLoad(s_pView);
	logBlockEnd("gsMenuCreate");
	systemUnuse();
}

void gsMenuLoop(void) {
	if(keyUse(KEY_ESCAPE)) {
		gameExit();
		return;
	}

	// Redraw everything on the off-page every frame - pages swap each frame,
	// so anything not redrawn per-frame would blink between old/new content
	menuDrawBg(s_pBfr->pBack);
	fontDrawStr(
		g_pGameFont, s_pBfr->pBack, GAME_SCREEN_WIDTH / 2, 36, "XAHBALL",
		COLOR_AMBER, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	s_pMenuList->pDestBitMap = s_pBfr->pBack;
	menuListDraw(s_pMenuList);

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

	// Navigation
	if(keyUse(KEY_UP) || keyUse(KEY_W) || joyUse(JOY1_UP)) {
		sfxPlay(SFX_MOVE, 48);
		menuListMove(s_pMenuList, -1);
	}
	else if(keyUse(KEY_DOWN) || keyUse(KEY_S) || joyUse(JOY1_DOWN)) {
		sfxPlay(SFX_MOVE, 48);
		menuListMove(s_pMenuList, +1);
	}
	else if(keyUse(KEY_LEFT) || keyUse(KEY_A) || joyUse(JOY1_LEFT) || keyUse(KEY_RIGHT) || keyUse(KEY_D) || joyUse(JOY1_RIGHT)) {
		// Value rows toggle on left/right - poll state, don't re-consume one-shots
		UBYTE ubRight = keyCheck(KEY_RIGHT) || keyCheck(KEY_D) || joyCheck(JOY1_RIGHT);
		BYTE bDir = ubRight ? 1 : -1;
		switch(s_pMenuList->ubSelected) {
			case MENU_ROW_MODE: {
				UBYTE ubVal = g_sMatchConfig.ubPlayersPerSide;
				ubVal = (ubVal == 3) ? 1 : ubVal + bDir;
				if(ubVal < 1) { ubVal = 3; }
				g_sMatchConfig.ubPlayersPerSide = ubVal;
				break;
			}
			case MENU_ROW_TIME: {
				BYTE bVal = g_sMatchConfig.ubTimeIdx + bDir;
				if(bVal < 0) { bVal = TIME_OPTION_COUNT - 1; }
				if(bVal >= TIME_OPTION_COUNT) { bVal = 0; }
				g_sMatchConfig.ubTimeIdx = bVal;
				break;
			}
			case MENU_ROW_FREEZE:
				g_sMatchConfig.ubBallFreeze = !g_sMatchConfig.ubBallFreeze;
				break;
			case MENU_ROW_LOCK:
				g_sMatchConfig.ubKickoffLock = !g_sMatchConfig.ubKickoffLock;
				break;
			default:
				break;
		}
		menuUpdateEntries();
		sfxPlay(SFX_MOVE, 48);
	}
	else if(keyUse(KEY_RETURN) || keyUse(KEY_X) || joyUse(JOY1_FIRE)) {
		switch(s_pMenuList->ubSelected) {
			case MENU_ROW_MODE: {
				UBYTE ubVal = g_sMatchConfig.ubPlayersPerSide;
				g_sMatchConfig.ubPlayersPerSide = (ubVal == 3) ? 1 : ubVal + 1;
				break;
			}
			case MENU_ROW_TIME: {
				g_sMatchConfig.ubTimeIdx = (g_sMatchConfig.ubTimeIdx + 1) % TIME_OPTION_COUNT;
				break;
			}
			case MENU_ROW_FREEZE:
				g_sMatchConfig.ubBallFreeze = !g_sMatchConfig.ubBallFreeze;
				break;
			case MENU_ROW_LOCK:
				g_sMatchConfig.ubKickoffLock = !g_sMatchConfig.ubKickoffLock;
				break;
			case MENU_ROW_CONTROLLERS:
				sfxPlay(SFX_CONFIRM, 64);
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_SETUP]);
				return;
			case MENU_ROW_CREDITS:
				sfxPlay(SFX_CONFIRM, 64);
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_CREDITS]);
				return;
			case MENU_ROW_START:
				sfxPlay(SFX_CONFIRM, 64);
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_TEAMSEL]);
				return;
			case MENU_ROW_QUIT:
				gameExit();
				return;
		}
		menuUpdateEntries();
		sfxPlay(SFX_MOVE, 48);
	}

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsMenuDestroy(void) {
	systemUse();
	logBlockBegin("gsMenuDestroy");
	// Stop replay FIRST (ptplayer keeps referencing the sample pack while
	// playing), then free the mod, then each samplepack chip buffer and
	// the pack struct itself
	ptplayerStop();
	if(s_pMenuSamples) {
		for(UBYTE i = 0; i < s_pMenuSamples->ubSampleCount; ++i) {
			memFree(
				s_pMenuSamples->pSamples[i].pData,
				s_pMenuSamples->pSamples[i].uwWordLength * sizeof(UWORD)
			);
		}
		memFree(s_pMenuSamples, sizeof(*s_pMenuSamples));
		s_pMenuSamples = 0;
	}
	if(s_pMenuMod) {
		ptplayerModDestroy(s_pMenuMod);
		s_pMenuMod = 0;
	}
	menuListDestroy(s_pMenuList);
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsMenuDestroy");
}
