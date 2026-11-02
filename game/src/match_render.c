/* XAHBall - match rendering: sprites, bobs, HUD, banner, input debug.
 * Everything that touches the double-buffered display from the match state
 * lives here. The input-state overlay (F1) is a diagnostic aid for real
 * hardware - its saved-background restore keeps the strip clean on toggle-off.
 */
#include "match_internal.h"

#include <ace/managers/blit.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/timer.h>
#include <ace/utils/bitmap.h>
#include <ace/utils/custom.h>
#include <stdio.h>
#include "sprites.h"
#include "teams.h"

//---------------------------------------------------------------------- GLOBALS
// (render-private state)

// Numbered kit sprites, [side][slot 0..2] - the slot number is baked into
// the sprite so players are distinguishable on the pitch
static tDiscSprite *s_pSpriteKit[2][SLOTS_PER_SIDE];
static tDiscSprite *s_pSpriteKitFlash[2][SLOTS_PER_SIDE];
static tDiscSprite *s_pSpriteKitReady[2][SLOTS_PER_SIDE];
static tDiscSprite *s_pSpriteBall;

// Per PHYSICAL page: banner strip on that page holds text that must clear.
// A one-shot flag would wipe only the current pBack on the PLAY transition -
// the other page keeps the last countdown digit, which then blinks forever.
static UBYTE s_ubBannerDirty[2];

// Last score/timer values drawn on each double-buffer page - the HUD only
// redraws a page when its values change (both pages must catch up after swaps)
static UWORD s_pHudScoreL[2] = {0xFFFF, 0xFFFF};
static UWORD s_pHudScoreR[2] = {0xFFFF, 0xFFFF};
static UWORD s_pHudSecs[2] = {0xFFFF, 0xFFFF};
// The two PHYSICAL buffers, captured at create. pFront/pBack swap every frame
// and are never equal to each other, so comparing those can't identify a page.
static tBitMap *s_pHudPage[2];

// F1 toggles the input-state overlay (real-hardware debugging - no bridge).
// Row 1: raw ACE manager/pin states (lit letter = held). Row 2: per-slot
// polled input. If a letter stays lit while the control is physically
// released, the state layer is stuck (lost key-up / stuck pin); if nothing
// is lit but a player still drifts, the game logic is at fault instead.
static UBYTE s_ubInputDebug;
// Clean pitch under the overlay strip, captured at create - used to restore
// both pages when the overlay is toggled off (nothing else repaints there)
static tBitMap *s_pInputDbgBg;

// Frame-time stats (shown on the overlay's top row). uwDrops counts vblanks
// that passed with no loop iteration - each one is a frame the game ran
// late. Line counts are raster lines (313 per PAL frame) from loop start to
// just before vPortWaitForEnd; peak >= ~290 means that frame dropped.
static UWORD s_uwStatLastVbl;
static UBYTE s_ubStatPrimed;
static UWORD s_uwStatFrames;
static UWORD s_uwStatDrops;
static UWORD s_uwStatLines;
static UWORD s_uwStatPeak;
static ULONG s_ulStatSum;
static UWORD s_uwStatSamples;

//-------------------------------------------------------------------- FUNCTIONS

// Overlay strip geometry - shared by draw, capture and restore paths
static UWORD matchInputDebugStripY(void) {
	return GAME_SCREEN_HEIGHT - 3 * g_pGameFont->uwHeight - 6;
}

static UWORD matchInputDebugStripH(void) {
	return 3 * g_pGameFont->uwHeight + 6;
}

// Writes a palette slot to the viewport AND the hardware color register:
// viewLoad only programs colors at state entry, mid-match changes need the
// direct write (safe during the post-vPortWaitForEnd section)
static void matchPaletteWrite(UBYTE ubIdx, UWORD uwRGB) {
	s_pVPort->pPalette[ubIdx] = uwRGB;
	g_pCustom->color[ubIdx] = uwRGB;
}

// HUD team code colors: the kit slot, or white for kits too dark to read on
// the black HUD strip (Germany's 0x222)
static UBYTE s_pHudCodeColor[2];

static UBYTE matchHudColorForKit(UWORD uwKit, UBYTE ubKitSlot) {
	UWORD uwLum = ((uwKit >> 8) & 0xF) + ((uwKit >> 4) & 0xF) + (uwKit & 0xF);
	return (uwLum < 12) ? MCOLOR_WHITE : ubKitSlot;
}

// Resolves kit RGB values for the dynamic palette slots; away kit kicks in
// when both teams would wear the same color
void matchAssignKits(void) {
	tTeam *pLeft = &g_pTeams[g_sMatchConfig.ubTeamLeft];
	tTeam *pRight = &g_pTeams[g_sMatchConfig.ubTeamRight];
	UWORD uwKitL = pLeft->uwKitRGB;
	UWORD uwKitR = pRight->uwKitRGB;
	if(uwKitL == uwKitR) {
		uwKitR = pRight->uwAltRGB;
		if(uwKitR == uwKitL) {
			uwKitL = pLeft->uwAltRGB;
		}
	}
	matchPaletteWrite(MCOLOR_KIT_A, uwKitL);
	matchPaletteWrite(MCOLOR_KIT_B, uwKitR);
	s_pHudCodeColor[0] = matchHudColorForKit(uwKitL, MCOLOR_KIT_A);
	s_pHudCodeColor[1] = matchHudColorForKit(uwKitR, MCOLOR_KIT_B);
}

void matchRenderLoadSprites(void) {
	static const char *s_pSideCodes[2] = {"A", "B"};
	static const char *s_pVariants[3] = {"", "-flash", "-ready"};
	char szBm[32], szMsk[32];
	for(UBYTE ubSide = 0; ubSide < 2; ++ubSide) {
		for(UBYTE ubNum = 0; ubNum < SLOTS_PER_SIDE; ++ubNum) {
			for(UBYTE ubVar = 0; ubVar < 3; ++ubVar) {
				sprintf(szBm, "data/team%s%d%s.bm",
					s_pSideCodes[ubSide], ubNum + 1, s_pVariants[ubVar]);
				sprintf(szMsk, "data/team%s%d%s.msk",
					s_pSideCodes[ubSide], ubNum + 1, s_pVariants[ubVar]);
				tDiscSprite **ppSpr = (ubVar == 0) ? &s_pSpriteKit[ubSide][ubNum]
					: (ubVar == 1) ? &s_pSpriteKitFlash[ubSide][ubNum]
					: &s_pSpriteKitReady[ubSide][ubNum];
				*ppSpr = discSpriteLoad(szBm, szMsk);
			}
		}
	}
	s_pSpriteBall = discSpriteLoad("data/ball.bm", "data/ball.msk");
}

void matchRenderDestroySprites(void) {
	for(UBYTE ubSide = 0; ubSide < 2; ++ubSide) {
		for(UBYTE ubNum = 0; ubNum < SLOTS_PER_SIDE; ++ubNum) {
			discSpriteDestroy(s_pSpriteKit[ubSide][ubNum]);
			discSpriteDestroy(s_pSpriteKitFlash[ubSide][ubNum]);
			discSpriteDestroy(s_pSpriteKitReady[ubSide][ubNum]);
		}
	}
	discSpriteDestroy(s_pSpriteBall);
}

// Bob registration - bobManagerCreate must have run already
void matchRenderInitBobs(void) {
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		tDiscSprite *pSpr = s_pSpriteKit[pP->ubSide][pP->ubSlot % SLOTS_PER_SIDE];
		bobInit(
			&pP->sBob, SPRITE_BOX, SPRITE_BOX, 1,
			bobCalcFrameAddress(pSpr->pFrames, 0),
			bobCalcFrameAddress(pSpr->pMasks, 0),
			pP->sBob.sPos.uwX, pP->sBob.sPos.uwY
		);
	}
	bobInit(
		&s_sBallBob, SPRITE_BOX, SPRITE_BOX, 1,
		bobCalcFrameAddress(s_pSpriteBall->pFrames, 0),
		bobCalcFrameAddress(s_pSpriteBall->pMasks, 0),
		s_sBallBob.sPos.uwX, s_sBallBob.sPos.uwY
	);
	bobReallocateBuffers();
}

void matchRenderInitPages(void) {
	s_pHudPage[0] = s_pBfr->pFront;
	s_pHudPage[1] = s_pBfr->pBack;
	// Force a HUD redraw on both pages even if a previous match ended with
	// the same values (trackers are static and outlive this state)
	for(UBYTE ubI = 0; ubI < 2; ++ubI) {
		s_pHudScoreL[ubI] = 0xFFFF;
		s_pHudScoreR[ubI] = 0xFFFF;
		s_pHudSecs[ubI] = 0xFFFF;
		s_ubBannerDirty[ubI] = 0;
	}
}

// Snapshot of the clean overlay-strip region, taken after the pitch draw -
// the strip is otherwise only painted while the overlay is enabled
void matchRenderCaptureDbgBg(void) {
	s_pInputDbgBg = bitmapCreate(
		GAME_SCREEN_WIDTH, matchInputDebugStripH(), GAME_BPP, BMF_INTERLEAVED
	);
	blitCopyAligned(
		s_pBfr->pBack, 0, matchInputDebugStripY(), s_pInputDbgBg, 0, 0,
		GAME_SCREEN_WIDTH, matchInputDebugStripH()
	);
}

// F1 overlay toggle - works in every phase. Turning it off restores the saved
// strip content on BOTH pages, since no per-frame code repaints that region
// while the overlay is disabled.
void matchRenderToggleInputDebug(void) {
	s_ubInputDebug = !s_ubInputDebug;
	if(s_ubInputDebug) {
		// Fresh stats per overlay session
		s_ubStatPrimed = 0;
		s_uwStatFrames = 0;
		s_uwStatDrops = 0;
		s_uwStatPeak = 0;
		s_ulStatSum = 0;
		s_uwStatSamples = 0;
	}
	if(!s_ubInputDebug && s_pInputDbgBg) {
		blitCopyAligned(
			s_pInputDbgBg, 0, 0, s_pBfr->pBack, 0, matchInputDebugStripY(),
			GAME_SCREEN_WIDTH, matchInputDebugStripH()
		);
		blitCopyAligned(
			s_pInputDbgBg, 0, 0, s_pBfr->pFront, 0, matchInputDebugStripY(),
			GAME_SCREEN_WIDTH, matchInputDebugStripH()
		);
	}
}

UBYTE matchInputDebugActive(void) {
	return s_ubInputDebug;
}

// Beam position as one monotonic "raster line" count: vblank counter * 313
// + current line. Raw reads only - the multiply happens at draw time, so a
// mark costs a few register reads (timerGetPrec used __mulsi3 + __udivsi3,
// ~2 lines per call, and skewed the very numbers it measured).
typedef struct tStatStamp {
	UWORD uwVbl;
	UWORD uwLine;
} tStatStamp;

static inline tStatStamp statStamp(void) {
	tStatStamp sStamp;
	UWORD uwV1, uwH;
	do {
		uwV1 = g_pCustom->vposr;
		uwH = g_pCustom->vhposr;
		sStamp.uwVbl = (UWORD)g_sTimerManager.uwFrameCounter;
	} while(uwV1 != g_pCustom->vposr);
	sStamp.uwLine = ((uwV1 & 1) << 8) | (uwH >> 8);
	return sStamp;
}

static UWORD statLinesBetween(tStatStamp sFrom, tStatStamp sTo) {
	UWORD uwFrames = sTo.uwVbl - sFrom.uwVbl;
	if(uwFrames > 8) {
		return 0xFFFF;
	}
	return (UWORD)(uwFrames * 313 + sTo.uwLine - sFrom.uwLine);
}

static tStatStamp s_sStatStart;
static UBYTE s_ubStatSkip;

void matchFrameStatsBegin(void) {
	UWORD uwVbl = (UWORD)g_sTimerManager.uwFrameCounter;
	if(s_ubStatPrimed && !s_ubStatSkip) {
		UWORD uwDelta = uwVbl - s_uwStatLastVbl;
		if(uwDelta > 1) {
			s_uwStatDrops += uwDelta - 1;
		}
		++s_uwStatFrames;
	}
	s_ubStatPrimed = 1;
	s_ubStatSkip = 0;
	s_uwStatLastVbl = uwVbl;
	s_sStatStart = statStamp();
}

void matchBobKick(void) {
	if(blitIsIdle()) {
		bobProcessNext();
	}
}

void matchFrameStatsSkipNext(void) {
	s_ubStatSkip = 1;
}

void matchFrameStatsEnd(void) {
	UWORD uwLines = statLinesBetween(s_sStatStart, statStamp());
	if(uwLines == 0xFFFF) {
		return; // stale sample (state entry)
	}
	s_uwStatLines = uwLines;
	s_ulStatSum += uwLines;
	++s_uwStatSamples;
	if(s_uwStatLines > s_uwStatPeak) {
		s_uwStatPeak = s_uwStatLines;
	}
}

// Bob positions never leave the screen - the BOB blitter does not clip and
// a negative uwX would blit garbage into random memory
static UWORD uwBobClamp(WORD wCoord, WORD wMax) {
	if(wCoord < 0) { return 0; }
	if(wCoord > wMax) { return wMax; }
	return (UWORD)wCoord;
}

// fix16 round-to-nearest (truncation shimmers by 1px while moving)
static WORD wRoundToInt(fix16_t fwV) {
	return (WORD)((fwV + 0x8000) >> 16);
}

void matchSyncBobPositions(void) {
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		pP->sBob.sPos.uwX = uwBobClamp(
			wRoundToInt(pP->sBody.sPos.X) - SPRITE_BOX / 2, GAME_SCREEN_WIDTH - SPRITE_BOX
		);
		pP->sBob.sPos.uwY = uwBobClamp(
			wRoundToInt(pP->sBody.sPos.Y) - SPRITE_BOX / 2, GAME_SCREEN_HEIGHT - SPRITE_BOX
		);
		// Frame select: kick flash, kick-ready (white outline) or normal,
		// numbered per slot
		tDiscSprite *pSpr;
		UBYTE ubIdx = pP->ubSlot % SLOTS_PER_SIDE;
		if(pP->uwKickAnim) {
			pSpr = s_pSpriteKitFlash[pP->ubSide][ubIdx];
		}
		else if(pP->sIn.ubKick) {
			pSpr = s_pSpriteKitReady[pP->ubSide][ubIdx];
		}
		else {
			pSpr = s_pSpriteKit[pP->ubSide][ubIdx];
		}
		bobSetFrame(
			&pP->sBob,
			bobCalcFrameAddress(pSpr->pFrames, 0),
			bobCalcFrameAddress(pSpr->pMasks, 0)
		);
	}
	s_sBallBob.sPos.uwX = uwBobClamp(
		wRoundToInt(s_sBallBody.sPos.X) - SPRITE_BOX / 2, GAME_SCREEN_WIDTH - SPRITE_BOX
	);
	s_sBallBob.sPos.uwY = uwBobClamp(
		wRoundToInt(s_sBallBody.sPos.Y) - SPRITE_BOX / 2, GAME_SCREEN_HEIGHT - SPRITE_BOX
	);
}

// Draws static HUD parts (panel, divider, team codes) - once per page
void matchDrawHudStatic(tBitMap *pTarget) {
	// HUD strip + banner strip share a black background, thin gray divider
	blitRect(pTarget, 0, 0, GAME_SCREEN_WIDTH, BANNER_Y1, MCOLOR_BLACK);
	blitRect(pTarget, 0, HUD_HEIGHT, GAME_SCREEN_WIDTH, 1, MCOLOR_HUD_GRAY);
	fontDrawStr(
		g_pGameFont, pTarget, 100, 4, g_pTeams[g_sMatchConfig.ubTeamLeft].szCode,
		s_pHudCodeColor[0], FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pTarget, 220, 4, g_pTeams[g_sMatchConfig.ubTeamRight].szCode,
		s_pHudCodeColor[1], FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
}

// Per-frame HUD update: erase + redraw ONLY on change. Score/timer are
// event-driven - sprintf + blits run on goals and once per second, not every
// frame. With double buffering each page must draw the current values once
// after a swap, hence the per-page trackers.
void matchDrawHud(void) {
	tBitMap *pBack = s_pBfr->pBack;
	UBYTE ubPage = (pBack == s_pHudPage[0]) ? 0 : 1;

	if(g_uwScoreLeft != s_pHudScoreL[ubPage] || g_uwScoreRight != s_pHudScoreR[ubPage]) {
		s_pHudScoreL[ubPage] = g_uwScoreLeft;
		s_pHudScoreR[ubPage] = g_uwScoreRight;
		static char szScore[16];
		sprintf(szScore, "%d  %d", g_uwScoreLeft, g_uwScoreRight);
		blitRect(pBack, 120, 2, 80, HUD_HEIGHT - 4, MCOLOR_BLACK);
		fontDrawStr(
			g_pGameFont, pBack, 160, 4, szScore, MCOLOR_WHITE,
			FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}

	UWORD uwSecs = s_ulFramesLeft / 50;
	if(uwSecs != s_pHudSecs[ubPage]) {
		s_pHudSecs[ubPage] = uwSecs;
		static char szTime[16];
		sprintf(szTime, "%02d:%02d", uwSecs / 60, uwSecs % 60);
		blitRect(pBack, 256, 2, 64, HUD_HEIGHT - 4, MCOLOR_BLACK);
		fontDrawStr(
			g_pGameFont, pBack, 288, 4, szTime, MCOLOR_WHITE,
			FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}
}

// Banner strip phase text (countdown, GOAL!, FULL TIME, PAUSED). Composed on
// pBack after the bobs like any other per-frame content - the strip is
// sprite-free by construction (pitch top margin), so no restore games.
void matchDrawBanner(void) {
	static const UWORD uwBannerH = BANNER_Y1 - BANNER_Y0 - 1;
	tBitMap *pBack = s_pBfr->pBack;
	UBYTE ubPage = (pBack == s_pHudPage[0]) ? 0 : 1;

	if(s_ubPhase == PHASE_PLAY) {
		// Clear each page exactly once after the transition - the other page
		// still shows the last countdown digit until it becomes pBack again
		if(s_ubBannerDirty[ubPage]) {
			blitRect(pBack, 0, BANNER_Y0 + 1, GAME_SCREEN_WIDTH, uwBannerH, MCOLOR_BLACK);
			s_ubBannerDirty[ubPage] = 0;
			// Attract mode keeps its label up during play (every PLAY
			// stretch follows a kickoff, which marks both pages dirty)
			if(g_ubDemoMode) {
				fontDrawStr(
					g_pGameFont, pBack, GAME_SCREEN_WIDTH / 2, BANNER_Y0 + 2,
					"DEMO - PRESS FIRE", MCOLOR_GRAY_L,
					FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
				);
			}
		}
		return;
	}
	blitRect(pBack, 0, BANNER_Y0 + 1, GAME_SCREEN_WIDTH, uwBannerH, MCOLOR_BLACK);
	s_ubBannerDirty[ubPage] = 1;

	UWORD uwCx = GAME_SCREEN_WIDTH / 2;
	static char szBuf[24];
	switch(s_ubPhase) {
		case PHASE_KICKOFF: {
			UBYTE ubCount = (s_uwPhaseTimer / 50) + 1;
			if(ubCount > 3) { ubCount = 3; }
			szBuf[0] = '0' + ubCount;
			szBuf[1] = 0;
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2, szBuf, MCOLOR_AMBER,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		}
		case PHASE_GOAL: {
			UBYTE ubTeamIdx = s_ubGoalScorer
				? g_sMatchConfig.ubTeamRight : g_sMatchConfig.ubTeamLeft;
			sprintf(szBuf, "GOAL!  %s", g_pTeams[ubTeamIdx].szCode);
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2, szBuf, MCOLOR_AMBER,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		}
		case PHASE_FULLTIME:
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2, "FULL TIME", MCOLOR_WHITE,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		case PHASE_PAUSED:
			fontDrawStr(
				g_pGameFont, pBack, uwCx, BANNER_Y0 + 2,
				"PAUSED - FIRE: RESUME  ESC: QUIT", MCOLOR_AMBER,
				FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
			);
			break;
		default:
			break;
	}
}

// Input-state overlay. Runs AFTER the bob pass: bob bg saves are captured
// pre-erase, so undraw always restores a clean pitch and no text ever leaks
// through the save/restore cycle. The full-strip erase makes this a solid
// debug bar - discs crossing it get their bottoms clipped, which is fine for
// a diagnostic aid.
void matchDrawInputDebug(void) {
	static char szRaw[48];
	static char szSlots[64];
	char *p = szRaw;

	UWORD uwY = matchInputDebugStripY() + 2;
	blitRect(
		s_pBfr->pBack, 0, matchInputDebugStripY(), GAME_SCREEN_WIDTH,
		matchInputDebugStripH(), MCOLOR_BLACK
	);

	// Row 0: average / last / peak work lines per frame (of 313) and
	// dropped vblanks out of measured frames
	sprintf(
		szSlots, "AVG:%lu LAST:%u PEAK:%u DROP:%u/%u",
		s_uwStatSamples ? s_ulStatSum / s_uwStatSamples : 0UL,
		s_uwStatLines, s_uwStatPeak, s_uwStatDrops, s_uwStatFrames
	);
	fontDrawStr(
		g_pGameFont, s_pBfr->pBack, 1, uwY, szSlots,
		s_uwStatDrops ? MCOLOR_AMBER : MCOLOR_WHITE, FONT_COOKIE, s_pTextBitMap
	);
	uwY += g_pGameFont->uwHeight + 1;

	*p++ = '2'; *p++ = ':';
	*p++ = keyCheck(KEY_W) ? 'W' : '.';       *p++ = keyCheck(KEY_A) ? 'A' : '.';
	*p++ = keyCheck(KEY_S) ? 'S' : '.';       *p++ = keyCheck(KEY_D) ? 'D' : '.';
	*p++ = keyCheck(KEY_LSHIFT) ? 'K' : '.';
	*p++ = ' ';
	*p++ = '1'; *p++ = ':';
	*p++ = keyCheck(KEY_UP) ? 'U' : '.';      *p++ = keyCheck(KEY_DOWN) ? 'D' : '.';
	*p++ = keyCheck(KEY_LEFT) ? 'L' : '.';    *p++ = keyCheck(KEY_RIGHT) ? 'R' : '.';
	*p++ = keyCheck(KEY_RSHIFT) ? 'K' : '.';
	*p++ = ' ';
	*p++ = 'J'; *p++ = '1'; *p++ = ':';
	*p++ = joyCheck(JOY1_UP) ? 'U' : '.';     *p++ = joyCheck(JOY1_DOWN) ? 'D' : '.';
	*p++ = joyCheck(JOY1_LEFT) ? 'L' : '.';   *p++ = joyCheck(JOY1_RIGHT) ? 'R' : '.';
	*p++ = joyCheck(JOY1_FIRE) ? 'F' : '.';
	*p++ = ' ';
	*p++ = 'J'; *p++ = '2'; *p++ = ':';
	*p++ = joyCheck(JOY2_UP) ? 'U' : '.';     *p++ = joyCheck(JOY2_DOWN) ? 'D' : '.';
	*p++ = joyCheck(JOY2_LEFT) ? 'L' : '.';   *p++ = joyCheck(JOY2_RIGHT) ? 'R' : '.';
	*p++ = joyCheck(JOY2_FIRE) ? 'F' : '.';
	*p = 0;

	fontDrawStr(
		g_pGameFont, s_pBfr->pBack, 1, uwY, szRaw,
		MCOLOR_WHITE, FONT_COOKIE, s_pTextBitMap
	);

	p = szSlots;
	for(UBYTE ubI = 0; ubI < s_ubActiveCount && ubI < 5; ++ubI) {
		tMatchPlayer *pP = &s_pPlayers[s_pActiveSlots[ubI]];
		p += sprintf(
			p, "%u:%+d%+d%u ", pP->ubSlot,
			(int)pP->sIn.bDx, (int)pP->sIn.bDy, pP->sIn.ubKick
		);
	}
	if(p > szSlots) {
		*(--p) = 0; // trim the trailing space
		fontDrawStr(
			g_pGameFont, s_pBfr->pBack, 1, GAME_SCREEN_HEIGHT - g_pGameFont->uwHeight - 1,
			szSlots, MCOLOR_AMBER, FONT_COOKIE, s_pTextBitMap
		);
	}
}

// Frees render-private resources (called from gsMatchDestroy). View and text
// bitmap are owned by the state shell itself.
void matchRenderDestroy(void) {
	if(s_pInputDbgBg) {
		bitmapDestroy(s_pInputDbgBg);
		s_pInputDbgBg = 0;
	}
	matchRenderDestroySprites();
}
