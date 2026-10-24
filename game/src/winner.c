/* XAHBall - winner screen: flags, result, back to menu */
#include "winner.h"

#include <ace/managers/blit.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/log.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>
#include <ace/utils/font.h>
#include <stdio.h>
#include "game.h"
#include "sfx.h"
#include "teams.h"
#include "ui.h"

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

static UWORD s_uwFrameCnt;
// Pages left to redraw: the panel only changes when the prompt blinks
static UBYTE s_ubDirty;

#define WINNER_FLAG_W 64
#define WINNER_FLAG_H 42
#define WINNER_FLAG_Y (UI_CONTENT_Y + 22)

static void winnerDrawFlagPanel(
	tBitMap *pBack, UBYTE ubTeamIdx, UWORD uwCenterX, UBYTE ubIsWinner
) {
	teamDrawFlag(
		pBack, uwCenterX - WINNER_FLAG_W / 2, WINNER_FLAG_Y,
		WINNER_FLAG_W, WINNER_FLAG_H, ubTeamIdx
	);
	UWORD uwFx = uwCenterX - WINNER_FLAG_W / 2 - 2, uwFy = WINNER_FLAG_Y - 2;
	UBYTE ubFrame = ubIsWinner ? COLOR_AMBER : COLOR_GRAY;
	blitRect(pBack, uwFx, uwFy, WINNER_FLAG_W + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy + WINNER_FLAG_H + 2, WINNER_FLAG_W + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy, 2, WINNER_FLAG_H + 4, ubFrame);
	blitRect(pBack, uwFx + WINNER_FLAG_W + 2, uwFy, 2, WINNER_FLAG_H + 4, ubFrame);
	fontDrawStr(
		g_pGameFont, pBack, uwCenterX, WINNER_FLAG_Y + WINNER_FLAG_H + 8,
		g_pTeams[ubTeamIdx].szCode, ubIsWinner ? COLOR_AMBER : COLOR_ACCENT,
		FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
	);
}

// Panel only - the backdrop is drawn once per page at create
static void winnerDrawPage(tBitMap *pBack) {
	UWORD uwCx = GAME_SCREEN_WIDTH / 2;
	UBYTE ubLeft = g_sMatchConfig.ubTeamLeft;
	UBYTE ubRight = g_sMatchConfig.ubTeamRight;
	UBYTE ubDraw = g_uwScoreLeft > g_uwScoreRight ? 0 : (g_uwScoreRight > g_uwScoreLeft ? 1 : 2);

	uiDrawPanel(pBack, 8, UI_CONTENT_Y, GAME_SCREEN_WIDTH - 16, 160, 1);
	fontDrawStr(
		g_pGameFont, pBack, uwCx, UI_CONTENT_Y + 6, "FULL TIME", COLOR_ACCENT,
		FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
	);

	winnerDrawFlagPanel(pBack, ubLeft, 84, ubDraw == 0);
	winnerDrawFlagPanel(pBack, ubRight, 236, ubDraw == 1);

	// Score between the flags
	static char szScore[16];
	sprintf(szScore, "%d : %d", g_uwScoreLeft, g_uwScoreRight);
	fontDrawStr(
		g_pGameFont, pBack, uwCx, WINNER_FLAG_Y + WINNER_FLAG_H / 2 - 3, szScore, COLOR_WHITE,
		FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
	);

	// Headline
	const char *szHead;
	if(ubDraw == 2) {
		szHead = "DRAW!";
	}
	else if(ubDraw == 0) {
		szHead = g_pTeams[ubLeft].szName;
	}
	else {
		szHead = g_pTeams[ubRight].szName;
	}
	UWORD uwHeadY = WINNER_FLAG_Y + WINNER_FLAG_H + 30;
	fontDrawStr(
		g_pGameFont, pBack, uwCx, uwHeadY, szHead, (ubDraw == 2) ? COLOR_WHITE : COLOR_AMBER,
		FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
	);
	if(ubDraw != 2) {
		fontDrawStr(
			g_pGameFont, pBack, uwCx, uwHeadY + 12, "WINS!", COLOR_WHITE,
			FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
		);
	}

	// Blinking prompt - hidden phase simply skips it (panel fill erased it)
	if((s_uwFrameCnt & 0x1F) < 24) {
		fontDrawStr(
			g_pGameFont, pBack, uwCx, UI_CONTENT_Y + 144, "PRESS FIRE FOR MENU",
			COLOR_GRAY, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}
}

void gsWinnerCreate(void) {
	logBlockBegin("gsWinnerCreate");

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
	s_uwFrameCnt = 0;

	uiDrawBackdrop(s_pBfr->pBack);
	uiDrawBackdrop(s_pBfr->pFront);
	winnerDrawPage(s_pBfr->pBack);
	winnerDrawPage(s_pBfr->pFront);
	s_ubDirty = 0;

	viewLoad(s_pView);
	logBlockEnd("gsWinnerCreate");
	systemUnuse();
}

void gsWinnerLoop(void) {
	++s_uwFrameCnt;

	tUiNav eNav = uiNavPoll();
	if(eNav == UI_NAV_FIRE || eNav == UI_NAV_BACK) {
		sfxPlay(eNav == UI_NAV_FIRE ? SFX_CONFIRM : SFX_BACK, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	// Prompt blink edge (visible <-> hidden) -> both pages redraw once
	UBYTE ubPhase = s_uwFrameCnt & 0x1F;
	if(ubPhase == 0 || ubPhase == 24) {
		s_ubDirty = 2;
	}
	if(s_ubDirty) {
		winnerDrawPage(s_pBfr->pBack);
		--s_ubDirty;
	}

#ifdef XAHBALL_AUTOTEST
	// Time-up path probe: bleep regularly and watch the audio DMA/volume
	// registers; draw the verdict on screen for screenshot verification.
	{
		static UBYTE s_ubAudioSeen = 0;
		if((s_uwFrameCnt & 31) == 0) {
			sfxPlay(SFX_CONFIRM, 64);
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
#endif

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsWinnerDestroy(void) {
	systemUse();
	logBlockBegin("gsWinnerDestroy");
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsWinnerDestroy");
}
