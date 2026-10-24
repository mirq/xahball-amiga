/* XAHBall - team select state
 * Left team first, then right team, with the menu controls (keyboard or
 * joystick in port 2). Fire confirms, left/right cycles teams, ESC steps
 * back to the left pick / the menu.
 */
#include "teamsel.h"

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
#include "teams.h"
#include "ui.h"

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

typedef enum tPickPhase {
	PICK_LEFT,
	PICK_RIGHT,
} tPickPhase;

static tPickPhase s_ubPhase;
static UBYTE s_ubPickLeft;  // team idx currently highlighted for left
static UBYTE s_ubPickRight;
// Pages left to redraw (2 after every change - both buffers catch up)
static UBYTE s_ubDirty;

#define TEAMSEL_PANEL_Y UI_CONTENT_Y
#define TEAMSEL_PANEL_H 160
#define TEAMSEL_FLAG_W 96
#define TEAMSEL_FLAG_H 64
#define TEAMSEL_FLAG_Y (TEAMSEL_PANEL_Y + 34)

static void teamselDrawPanel(
	tBitMap *pBack, UBYTE ubTeamIdx, UWORD uwCenterX, UBYTE ubIsActive
) {
	UBYTE ubColorName = ubIsActive ? COLOR_WHITE : COLOR_GRAY;

	// Flag in a frame: amber = this side is picking now
	teamDrawFlag(
		pBack, uwCenterX - TEAMSEL_FLAG_W / 2, TEAMSEL_FLAG_Y,
		TEAMSEL_FLAG_W, TEAMSEL_FLAG_H, ubTeamIdx
	);
	UWORD uwFx = uwCenterX - TEAMSEL_FLAG_W / 2 - 2, uwFy = TEAMSEL_FLAG_Y - 2;
	UBYTE ubFrame = ubIsActive ? COLOR_AMBER : COLOR_GRAY;
	blitRect(pBack, uwFx, uwFy, TEAMSEL_FLAG_W + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy + TEAMSEL_FLAG_H + 2, TEAMSEL_FLAG_W + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy, 2, TEAMSEL_FLAG_H + 4, ubFrame);
	blitRect(pBack, uwFx + TEAMSEL_FLAG_W + 2, uwFy, 2, TEAMSEL_FLAG_H + 4, ubFrame);

	UWORD uwNameY = TEAMSEL_FLAG_Y + TEAMSEL_FLAG_H + 10;
	fontDrawStr(
		g_pGameFont, pBack, uwCenterX, uwNameY, g_pTeams[ubTeamIdx].szName,
		ubColorName, FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
	);
	if(ubIsActive) {
		fontDrawStr(
			g_pGameFont, pBack, uwCenterX - TEAMSEL_FLAG_W / 2 - 10, TEAMSEL_FLAG_Y + TEAMSEL_FLAG_H / 2 - 3,
			"<", COLOR_AMBER, FONT_COOKIE | FONT_LEFT, s_pTextBitMap
		);
		fontDrawStr(
			g_pGameFont, pBack, uwCenterX + TEAMSEL_FLAG_W / 2 + 6, TEAMSEL_FLAG_Y + TEAMSEL_FLAG_H / 2 - 3,
			">", COLOR_AMBER, FONT_COOKIE | FONT_LEFT, s_pTextBitMap
		);
	}
}

static void teamselDrawPage(tBitMap *pBack) {
	uiDrawPanel(pBack, 8, TEAMSEL_PANEL_Y, GAME_SCREEN_WIDTH - 16, TEAMSEL_PANEL_H, 1);

	fontDrawStr(
		g_pGameFont, pBack, GAME_SCREEN_WIDTH / 2, TEAMSEL_PANEL_Y + 6, "SELECT TEAMS", COLOR_AMBER,
		FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, 84, TEAMSEL_PANEL_Y + 20, "LEFT", COLOR_ACCENT,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, 236, TEAMSEL_PANEL_Y + 20, "RIGHT", COLOR_ACCENT,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);

	teamselDrawPanel(pBack, s_ubPickLeft, 84, s_ubPhase == PICK_LEFT);
	teamselDrawPanel(pBack, s_ubPickRight, 236, s_ubPhase == PICK_RIGHT);

	fontDrawStr(
		g_pGameFont, pBack, GAME_SCREEN_WIDTH / 2, TEAMSEL_FLAG_Y + TEAMSEL_FLAG_H / 2 - 3, "VS", COLOR_WHITE,
		FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, GAME_SCREEN_WIDTH / 2, TEAMSEL_PANEL_Y + TEAMSEL_PANEL_H - 16,
		(s_ubPhase == PICK_LEFT) ? "L/R: TEAM   FIRE: PICK" : "FIRE: KICK OFF   ESC: BACK",
		COLOR_GRAY, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
}

void gsTeamSelCreate(void) {
	logBlockBegin("gsTeamSelCreate");

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

	s_ubPhase = PICK_LEFT;
	s_ubPickLeft = g_sMatchConfig.ubTeamLeft;
	s_ubPickRight = g_sMatchConfig.ubTeamRight;

	uiDrawBackdrop(s_pBfr->pBack);
	uiDrawBackdrop(s_pBfr->pFront);
	teamselDrawPage(s_pBfr->pBack);
	teamselDrawPage(s_pBfr->pFront);
	s_ubDirty = 0;

	viewLoad(s_pView);
	logBlockEnd("gsTeamSelCreate");
	systemUnuse();
}

void gsTeamSelLoop(void) {
	tUiNav eNav = uiNavPoll();
	switch(eNav) {
		case UI_NAV_BACK:
			sfxPlay(SFX_BACK, 64);
			if(s_ubPhase == PICK_RIGHT) {
				s_ubPhase = PICK_LEFT;
				s_ubDirty = 2;
				break;
			}
			stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
			return;
		case UI_NAV_LEFT:
		case UI_NAV_RIGHT: {
			BYTE bDir = (eNav == UI_NAV_RIGHT) ? 1 : -1;
			UBYTE *pPick = (s_ubPhase == PICK_LEFT) ? &s_ubPickLeft : &s_ubPickRight;
			*pPick = (*pPick + bDir + TEAM_COUNT) % TEAM_COUNT;
			sfxPlay(SFX_MOVE, 48);
			s_ubDirty = 2;
			break;
		}
		case UI_NAV_FIRE:
			sfxPlay(SFX_CONFIRM, 64);
			if(s_ubPhase == PICK_LEFT) {
				g_sMatchConfig.ubTeamLeft = s_ubPickLeft;
				s_ubPhase = PICK_RIGHT;
				s_ubDirty = 2;
				break;
			}
			g_sMatchConfig.ubTeamRight = s_ubPickRight;
			stateChange(g_pGameStateManager, &g_pGameStates[STATE_MATCH]);
			return;
		default:
			break;
	}

	if(s_ubDirty) {
		teamselDrawPage(s_pBfr->pBack);
		--s_ubDirty;
	}

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsTeamSelDestroy(void) {
	systemUse();
	logBlockBegin("gsTeamSelDestroy");
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsTeamSelDestroy");
}
