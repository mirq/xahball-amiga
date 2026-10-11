/* XAHBall - intro state: title, bouncing ball, credits
 * Double-buffered: ball and texts are composed on BOTH buffer pages each
 * frame (each page erases its own last ball position), then pages swap via
 * viewProcessManagers. This keeps the disc free of beam-catch flicker.
 */
#include "intro.h"

#include <ace/managers/blit.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/log.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/extview.h>
#include <fixmath/fixmath.h>
#include "game.h"
#include "sfx.h"

#define INTRO_TIMEOUT_FRAMES 400

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

static UWORD s_uwFrameCnt;

// Bouncing ball demo params
#define INTRO_BALL_R 8
#define INTRO_BALL_LEFT 28
#define INTRO_BALL_RIGHT 292
#define INTRO_BALL_TOP 128
#define INTRO_BALL_BOT 202
static fix16_t s_fwBallX, s_fwBallY, s_fwBallVX, s_fwBallVY;

// Per-page last drawn ball position (double-buffer undraw bookkeeping)
static tBitMap *s_pPageA, *s_pPageB;
static UWORD s_uwLastX[2], s_uwLastY[2];
static UBYTE s_ubHasBall[2];
static tBitMap *s_pPages[2];

static void introDrawBg(tBitMap *pTarget) {
	// Stadium backdrop
	blitRect(pTarget, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, COLOR_BG);

	// Decorative pitch green band with stripes at bottom
	for(UWORD uwX = 0; uwX < GAME_SCREEN_WIDTH; uwX += 80) {
		blitRect(pTarget, uwX, 208, 40, 48, COLOR_PITCH);
		blitRect(pTarget, uwX + 40, 208, 40, 48, COLOR_PITCH_DARK);
	}
	blitRect(pTarget, 0, 206, GAME_SCREEN_WIDTH, 2, COLOR_BLACK);

	// Amber title bars
	blitRect(pTarget, 0, 40, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
	blitRect(pTarget, 0, 106, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
}

// Draws filled ellipse (round-looking on hi-res PAL 1:2 pixels).
static void introDrawDisc(
	tBitMap *pBfr, UWORD uwCx, UWORD uwCy, UWORD uwR, UBYTE ubColor
) {
	for(WORD wDy = -((WORD)uwR); wDy <= (WORD)uwR; ++wDy) {
		WORD wHalf = 0;
		// dx^2 + 4*dy^2 <= r^2
		WORD wD2 = uwR * uwR - 4 * wDy * wDy;
		if(wD2 > 0) {
			wHalf = 1;
			while(wHalf * wHalf <= wD2) {
				++wHalf;
			}
			--wHalf;
		}
		// Skip zero-width rows - zero-width blit corrupts memory
		if(wHalf < 1) {
			continue;
		}
		blitRect(pBfr, uwCx - wHalf, uwCy + wDy, 2 * wHalf, 1, ubColor);
	}
}

static void introDrawBall(tBitMap *pTarget, UWORD uwCx, UWORD uwCy) {
	introDrawDisc(pTarget, uwCx, uwCy, INTRO_BALL_R, COLOR_BLACK);
	introDrawDisc(pTarget, uwCx, uwCy, INTRO_BALL_R - 2, COLOR_WHITE);
}

// Draws overlay texts on given page. Called for both pages every frame so
// double-buffer pages stay consistent (blink handled by drawing over with bg).
static void introDrawTexts(tBitMap *pTarget) {
	UWORD uwCenterX = GAME_SCREEN_WIDTH / 2;

	fontDrawStr(
		g_pGameFont, pTarget, uwCenterX + 2, 62, "XAHBALL", COLOR_BLACK,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pTarget, uwCenterX, 60, "XAHBALL", COLOR_AMBER,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pTarget, uwCenterX, 84, "A HAXBALL CLONE FOR AMIGA", COLOR_WHITE,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);

	// Blinking prompt - draw over with bg color when hidden
	UBYTE ubPromptVisible = (s_uwFrameCnt & 0x1F) < 24;
	fontDrawStr(
		g_pGameFont, pTarget, uwCenterX, 152, "PRESS FIRE TO START",
		ubPromptVisible ? COLOR_WHITE : COLOR_BG,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pTarget, uwCenterX, 232, "GAME BY MIRQ", COLOR_AMBER,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
}

void gsIntroCreate(void) {
	logBlockBegin("gsIntroCreate");

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
	s_fwBallX = F16(160);
	s_fwBallY = F16(160);
	s_fwBallVX = F16(2.8f);
	s_fwBallVY = F16(-1.9f);

	// Compose identical background on both buffer pages
	s_pPageA = s_pBfr->pBack;
	s_pPageB = s_pBfr->pFront;
	s_pPages[0] = s_pPageA;
	s_pPages[1] = s_pPageB;
	for(UBYTE ubPage = 0; ubPage < 2; ++ubPage) {
		introDrawBg(s_pPages[ubPage]);
		introDrawTexts(s_pPages[ubPage]);
		s_uwLastX[ubPage] = 0;
		s_uwLastY[ubPage] = 0;
		s_ubHasBall[ubPage] = 0;
	}

	viewLoad(s_pView);
	logBlockEnd("gsIntroCreate");
	systemUnuse();
}

void gsIntroLoop(void) {
	++s_uwFrameCnt;

	// Skip on any fire / return / escape
	if(
		keyUse(KEY_RETURN) || keyUse(KEY_ESCAPE) || keyUse(KEY_SPACE) ||
		joyUse(JOY1_FIRE)
	) {
		sfxPlay(SFX_CONFIRM, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	if(s_uwFrameCnt >= INTRO_TIMEOUT_FRAMES) {
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	// Ball demo physics - bounce inside box
	fix16_t fwNextX = s_fwBallX + s_fwBallVX;
	fix16_t fwNextY = s_fwBallY + s_fwBallVY;
	UBYTE ubBounced = 0;
	if(
		fwNextX < F16(INTRO_BALL_LEFT + INTRO_BALL_R) ||
		fwNextX > F16(INTRO_BALL_RIGHT - INTRO_BALL_R)
	) {
		s_fwBallVX = -s_fwBallVX;
		ubBounced = 1;
	}
	if(
		fwNextY < F16(INTRO_BALL_TOP + INTRO_BALL_R) ||
		fwNextY > F16(INTRO_BALL_BOT - INTRO_BALL_R)
	) {
		s_fwBallVY = -s_fwBallVY;
		ubBounced = 1;
	}
	if(ubBounced) {
		sfxPlay(SFX_BOUNCE, 40);
		fwNextX = s_fwBallX + s_fwBallVX;
		fwNextY = s_fwBallY + s_fwBallVY;
	}
	s_fwBallX = fwNextX;
	s_fwBallY = fwNextY;

	// Update ball ONLY on the off-page (pBack) - the displayed page must
	// never be touched or the beam catches the erase/draw and the disc
	// blinks. Each page keeps its own last ball position: when a page
	// becomes pBack again, its own stale ball gets erased first.
	tBitMap *pDraw = s_pBfr->pBack;
	UBYTE ubPage = (pDraw == s_pPageB) ? 1 : 0;
	UWORD uwX = fix16_to_int(s_fwBallX);
	UWORD uwY = fix16_to_int(s_fwBallY);
	if(s_ubHasBall[ubPage]) {
		blitRect(
			pDraw,
			s_uwLastX[ubPage] - INTRO_BALL_R - 1,
			s_uwLastY[ubPage] - INTRO_BALL_R - 1,
			2 * INTRO_BALL_R + 3, 2 * INTRO_BALL_R + 3, COLOR_BG
		);
	}
	introDrawBall(pDraw, uwX, uwY);
	s_uwLastX[ubPage] = uwX;
	s_uwLastY[ubPage] = uwY;
	s_ubHasBall[ubPage] = 1;

	introDrawTexts(pDraw);

	// Swap buffers: the just-composed page becomes the displayed one
	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsIntroDestroy(void) {
	systemUse();
	logBlockBegin("gsIntroDestroy");
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsIntroDestroy");
}
