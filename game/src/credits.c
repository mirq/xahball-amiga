/* XAHBall - credits screen: thanks + testing/support list */
#include "credits.h"

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
#include "game.h"
#include "sfx.h"

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

static const char *s_pLines[] = {
	"THANKS TO:",
	"TEHKAIN",
	"AND ALL ACE DEVELOPERS",
	"",
	"TESTING AND SUPPORT:",
	"KUBA_SLONKA  JUEN  PALUCH",
	"SC00TER  QUAD  OLEKW200I",
	"DJMARIO  JERICHO  DRAKO",
	"BARTEK2075  CUBASA99  MANDI",
	"KASHINIO  SUPERBONIO  FRANCDT",
	"KINIA  RJ  JACK_SWIDNIK",
	"MARX",
};
#define CREDIT_LINE_COUNT 12

static void creditsDrawPage(tBitMap *pTarget) {
	UWORD uwCenterX = GAME_SCREEN_WIDTH / 2;
	blitRect(pTarget, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, COLOR_BG);
	blitRect(pTarget, 0, 0, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
	blitRect(pTarget, 0, GAME_SCREEN_HEIGHT - 2, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
	fontDrawStr(
		g_pGameFont, pTarget, uwCenterX, 20, "CREDITS", COLOR_AMBER,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);

	UWORD uwY = 48;
	for(UBYTE i = 0; i < CREDIT_LINE_COUNT; ++i) {
		const char *szLine = s_pLines[i];
		if(szLine[0] == 0) {
			uwY += 6;
			continue;
		}
		UBYTE ubColor = COLOR_WHITE;
		if(i == 0 || i == 4) { ubColor = COLOR_ACCENT; }
		else if(i == 1 || i == 2) { ubColor = COLOR_AMBER; }
		fontDrawStr(
			g_pGameFont, pTarget, uwCenterX, uwY, szLine, ubColor,
			FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
		uwY += 14;
	}
	fontDrawStr(
		g_pGameFont, pTarget, uwCenterX, 232, "PRESS FIRE TO CONTINUE",
		COLOR_GRAY, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
}

void gsCreditsCreate(void) {
	logBlockBegin("gsCreditsCreate");

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

	creditsDrawPage(s_pBfr->pBack);
	creditsDrawPage(s_pBfr->pFront);

	viewLoad(s_pView);
	logBlockEnd("gsCreditsCreate");
	systemUnuse();
}

void gsCreditsLoop(void) {
	if(keyUse(KEY_ESCAPE) || keyUse(KEY_RETURN) || keyUse(KEY_SPACE) || joyUse(JOY1_FIRE)) {
		sfxPlay(SFX_CONFIRM, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}
	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsCreditsDestroy(void) {
	systemUse();
	logBlockBegin("gsCreditsDestroy");
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsCreditsDestroy");
}
