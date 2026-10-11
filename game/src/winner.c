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

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

static UWORD s_uwFrameCnt;

static void winnerDrawFlagPanel(
	UBYTE ubTeamIdx, UWORD uwCenterX, UBYTE ubIsWinner
) {
	tBitMap *pBack = s_pBfr->pBack;
	UWORD uwFlagW = 60, uwFlagH = 40;
	teamDrawFlag(pBack, uwCenterX - uwFlagW / 2, 70, uwFlagW, uwFlagH, ubTeamIdx);

	UWORD uwFx = uwCenterX - uwFlagW / 2 - 2, uwFy = 68;
	UBYTE ubFrame = ubIsWinner ? COLOR_AMBER : COLOR_GRAY;
	blitRect(pBack, uwFx, uwFy, uwFlagW + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy + uwFlagH + 2, uwFlagW + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy, 2, uwFlagH + 4, ubFrame);
	blitRect(pBack, uwFx + uwFlagW + 2, uwFy, 2, uwFlagH + 4, ubFrame);
}

static void winnerDrawPage(tBitMap *pBack) {
	UWORD uwCx = GAME_SCREEN_WIDTH / 2;
	UBYTE ubLeft = g_sMatchConfig.ubTeamLeft;
	UBYTE ubRight = g_sMatchConfig.ubTeamRight;
	UBYTE ubDraw = g_uwScoreLeft > g_uwScoreRight ? 0 : (g_uwScoreRight > g_uwScoreLeft ? 1 : 2);

	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, COLOR_BG);
	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
	blitRect(pBack, 0, GAME_SCREEN_HEIGHT - 2, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);

	winnerDrawFlagPanel(ubLeft, 80, ubDraw == 0);
	winnerDrawFlagPanel(ubRight, 240, ubDraw == 1);

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
	UBYTE ubHeadColor = (ubDraw == 2) ? COLOR_WHITE : COLOR_AMBER;
	fontDrawStr(
		g_pGameFont, pBack, uwCx + 2, 173, szHead, COLOR_BLACK,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, uwCx, 171, szHead, ubHeadColor,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	if(ubDraw != 2) {
		fontDrawStr(
			g_pGameFont, pBack, uwCx, 187, "WINS!", COLOR_WHITE,
			FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}

	// Score
	static char szScore[16];
	sprintf(szScore, "%d : %d", g_uwScoreLeft, g_uwScoreRight);
	fontDrawStr(
		g_pGameFont, pBack, uwCx, 108, szScore, COLOR_WHITE,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);

	// Blinking prompt - draw over with bg when hidden
	UBYTE ubVisible = (s_uwFrameCnt & 0x1F) < 24;
	fontDrawStr(
		g_pGameFont, pBack, uwCx, 214, "PRESS FIRE FOR MENU",
		ubVisible ? COLOR_WHITE : COLOR_BG,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
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

	winnerDrawPage(s_pBfr->pBack);
	winnerDrawPage(s_pBfr->pFront);

	viewLoad(s_pView);
	logBlockEnd("gsWinnerCreate");
	systemUnuse();
}

void gsWinnerLoop(void) {
	++s_uwFrameCnt;

	if(keyUse(KEY_ESCAPE)) {
		sfxPlay(SFX_BACK, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}
	if(keyUse(KEY_RETURN) || keyUse(KEY_SPACE) || joyUse(JOY1_FIRE)) {
		sfxPlay(SFX_CONFIRM, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	// Redraw only on the off-page - never touch the displayed one
	winnerDrawPage(s_pBfr->pBack);

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
