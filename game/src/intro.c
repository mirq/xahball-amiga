/* XAHBall - intro state: ACE logo splash, XAHBALL title art, bouncing ball
 * demo, credits. Three phases:
 *   PH_ACE  - ACE logo (own 4-color palette: A=red, C=green, E=blue),
 *             palette fade-in/out, "ace" sample
 *   PH_TITLE - full-screen XAHBALL title artwork (32 colors), fade in/out;
 *             palette is picked from the image itself, converted to
 *             data/title.plt and loaded at runtime via paletteLoadFromPath()
 *   PH_DEMO - bouncing ball + texts on the menu palette (original intro)
 * Double-buffered: static screens are composed on BOTH pages at phase entry,
 * animated bits (ball) are composed on the off-page each frame.
 * Any fire/return/escape advances to the next phase (demo -> menu).
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
#include <ace/utils/bitmap.h>
#include <ace/utils/custom.h>
#include <ace/utils/extview.h>
#include <ace/utils/palette.h>
#include <fixmath/fixmath.h>
#include "game.h"
#include "sfx.h"

#define INTRO_DEMO_TIMEOUT_FRAMES 400

// Phase durations (frames @50Hz)
#define INTRO_ACE_HOLD_FRAMES 215
#define INTRO_TITLE_FRAMES 250
// fades for the logo screens
#define INTRO_FADE_FRAMES 25

// 5 bitplanes = 32 colors: the title artwork needs them; the ACE splash and
// ball demo just use the lower entries of the same palette
#define INTRO_BPP 5

// Ball demo params
#define INTRO_BALL_R 8
#define INTRO_BALL_LEFT 28
#define INTRO_BALL_RIGHT 292
#define INTRO_BALL_TOP 128
#define INTRO_BALL_BOT 202

typedef enum tIntroPhase {
	INTRO_PH_ACE,
	INTRO_PH_TITLE,
	INTRO_PH_DEMO
} tIntroPhase;

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

static tIntroPhase s_ubPhase;
static UWORD s_uwFrameCnt;   // frames in the CURRENT phase

// ACE logo - GermZ-style copper implementation: the display palette stays
// ALL BLACK and the logo "colors" come from 30 copper blocks (3 scanlines
// each) overriding color registers 1/2/3 over the logo area. Letters sit in
// bitmap color indices: A=1, C=2, E/POWERED=3. The logo bitmap is converted
// at the screen's depth (5bpp, interleaved - see the padded ace.gpl) so the
// blitCopy() below takes the fast single-blit interleaved path.
// Flash-in order: A, C, E, POWERED; fade-out ramps every band to black.
static tBitMap *s_pAceLogo;
#define INTRO_ACE_W 160
#define INTRO_ACE_H 59
#define INTRO_ACE_BLOCKS 30
#define FLASH_START_FRAME_A 1
#define FLASH_START_FRAME_C 10
#define FLASH_START_FRAME_E 30
#define FLASH_START_FRAME_PWR 50
#define FLASH_RATIO_INACTIVE -1

typedef enum tStateAce {
	STATE_ACE_FADE_IN,
	STATE_ACE_FADE_OUT
} tStateAce;

static tCopBlock *s_pAceBlocks[INTRO_ACE_BLOCKS];
static const UWORD s_pAceColors[INTRO_ACE_BLOCKS] = {
	0xA00, 0xA00,
	0xC00, 0xC00, 0xC00,
	0xD40, 0xD40,
	0xE80, 0xE80, 0xE80,
	0xFB2, 0xFB2,
	0xFE5, 0xFE5, 0xFE5,
	0xCC2, 0xCC2,
	0x990, 0x990, 0x990,
	0x494, 0x494,
	0x099, 0x099, 0x099,
	0x077, 0x077,
	0x055, 0x055,
	0x055
};
static const UWORD s_uwColorPowered = 0x343;
static UWORD s_uwFlashFrame;
static BYTE s_bAceFadeoutRatio;
static BYTE s_bRatioFlashA;
static BYTE s_bRatioFlashC;
static BYTE s_bRatioFlashE;
static BYTE s_bRatioFlashPwr;
static tStateAce s_eStateAce;

static UWORD blendColors(UWORD uwColorSrc, UWORD uwColorDst, UBYTE ubRatio) {
	BYTE bSrcR = (uwColorSrc >> 8);
	BYTE bSrcG = ((uwColorSrc >> 4) & 0xF);
	BYTE bSrcB = ((uwColorSrc >> 0) & 0xF);
	BYTE bDstR = (uwColorDst >> 8);
	BYTE bDstG = ((uwColorDst >> 4) & 0xF);
	BYTE bDstB = ((uwColorDst >> 0) & 0xF);
	UBYTE ubR = bSrcR + ((bDstR - bSrcR) * ubRatio) / 15;
	UBYTE ubG = bSrcG + ((bDstG - bSrcG) * ubRatio) / 15;
	UBYTE ubB = bSrcB + ((bDstB - bSrcB) * ubRatio) / 15;
	return (ubR << 8) | (ubG << 4) | ubB;
}

// Enters the ACE logo phase: black display, logo drawn once (its index-0
// pixels are black on the black palette), 30 copper bands created
static void introEnterAce(void) {
	s_ubPhase = INTRO_PH_ACE;
	s_uwFrameCnt = 0;
	s_eStateAce = STATE_ACE_FADE_IN;
	s_uwFlashFrame = 1;
	s_bAceFadeoutRatio = 15; // GermZ: non-zero during fade-in so the phase-exit check stays false
	s_bRatioFlashA = FLASH_RATIO_INACTIVE;
	s_bRatioFlashC = FLASH_RATIO_INACTIVE;
	s_bRatioFlashE = FLASH_RATIO_INACTIVE;
	s_bRatioFlashPwr = FLASH_RATIO_INACTIVE;
	UWORD uwLogoOffsY = (GAME_SCREEN_HEIGHT - INTRO_ACE_H) / 2;

	for(UBYTE i = 0; i < INTRO_ACE_BLOCKS; ++i) {
		s_pAceBlocks[i] = copBlockCreate(
			s_pView->pCopList, 3, 0, s_pView->ubPosY + uwLogoOffsY + i * 2
		);
		copMove(s_pView->pCopList, s_pAceBlocks[i], &g_pCustom->color[1], 0x000);
		copMove(s_pView->pCopList, s_pAceBlocks[i], &g_pCustom->color[2], 0x000);
		copMove(s_pView->pCopList, s_pAceBlocks[i], &g_pCustom->color[3], 0x000);
	}

	for(UBYTE ubPage = 0; ubPage < 2; ++ubPage) {
		tBitMap *pPage = ubPage ? s_pBfr->pFront : s_pBfr->pBack;
		blitRect(pPage, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, 0);
		if(s_pAceLogo) {
			blitCopy(
				s_pAceLogo, 0, 0, pPage,
				(GAME_SCREEN_WIDTH - INTRO_ACE_W) / 2, uwLogoOffsY,
				INTRO_ACE_W, INTRO_ACE_H, MINTERM_COOKIE
			);
		}
	}
	sfxPlay(SFX_ACE, 64);
}

// Leaves the ACE phase: copper bands must go or they'd tint the next screen
static void introExitAce(void) {
	for(UBYTE i = 0; i < INTRO_ACE_BLOCKS; ++i) {
		copBlockDestroy(s_pView->pCopList, s_pAceBlocks[i]);
	}
}

// XAHBALL title artwork: full-screen 320x256, 32 colors. The palette is
// picked from the image itself (res/logo/title.gpl -> data/title.plt) and
// loaded at runtime - bitmap and palette can never drift apart.
static tBitMap *s_pTitleLogo;
static UWORD s_pTitlePal[32];
#define INTRO_TITLE_W GAME_SCREEN_WIDTH
#define INTRO_TITLE_H GAME_SCREEN_HEIGHT

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

// Current fade factor for a phase frame count (ramp up, hold, ramp down)
static UWORD introFadeFactor(UWORD uwFrame, UWORD uwTotal) {
	UWORD uwF = uwFrame;
	if(uwF > uwTotal - INTRO_FADE_FRAMES) {
		uwF = uwTotal - uwFrame;
	}
	if(uwF > INTRO_FADE_FRAMES) { uwF = INTRO_FADE_FRAMES; }
	return uwF;
}

// Brightness level (0..15) for paletteDimOcs() from a fade factor
static UBYTE introFadeLevel(UWORD uwFrame, UWORD uwTotal) {
	return (introFadeFactor(uwFrame, uwTotal) * 15) / INTRO_FADE_FRAMES;
}

// Enters the ball demo: restore menu palette + compose demo pages
static void introEnterDemo(void) {
	s_ubPhase = INTRO_PH_DEMO;
	s_uwFrameCnt = 0;
	gamePaletteLoad(s_pVPort);
	for(UBYTE i = 0; i < 32; ++i) {
		s_pVPort->pPalette[i] = s_pVPort->pPalette[i % 16];
	}
	for(UBYTE i = 0; i < 32; ++i) {
		g_pCustom->color[i] = s_pVPort->pPalette[i];
	}
	for(UBYTE ubPage = 0; ubPage < 2; ++ubPage) {
		introDrawBg(s_pPages[ubPage]);
		introDrawTexts(s_pPages[ubPage]);
		s_uwLastX[ubPage] = 0;
		s_uwLastY[ubPage] = 0;
		s_ubHasBall[ubPage] = 0;
	}
}

// The OCS blitter BLTSIZE height field is only 10 bits: max 1024 rows per
// blit. A full-screen interleaved 5bpp copy is 256*5 = 1280 rows - one
// blitCopyAligned() would overflow and copy just the first ~51 scanlines.
// Split into vertical chunks of max 1024/depth scanlines instead.
static void introBlitTitle(tBitMap *pDst) {
	const WORD wChunk = 1024 / s_pTitleLogo->Depth;
	for(WORD wY = 0; wY < INTRO_TITLE_H; wY += wChunk) {
		WORD wH = INTRO_TITLE_H - wY;
		if(wH > wChunk) {
			wH = wChunk;
		}
		blitCopyAligned(s_pTitleLogo, 0, wY, pDst, 0, wY, INTRO_TITLE_W, wH);
	}
}

// Compose the title screen on BOTH pages + set its 32-color palette
static void introEnterTitlePhase(void) {
	s_ubPhase = INTRO_PH_TITLE;
	s_uwFrameCnt = 0;
	for(UWORD i = 0; i < 32; ++i) {
		s_pVPort->pPalette[i] = s_pTitlePal[i];
	}
	// Both pages are composed here and never touched again - the title is
	// a static screen, so the loop only fades the palette in/out
	for(UBYTE ubPage = 0; ubPage < 2; ++ubPage) {
		blitRect(s_pPages[ubPage], 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, 0);
		introBlitTitle(s_pPages[ubPage]);
	}
	// black-out hardware colors; the loop fades them in
	paletteDimOcs(s_pTitlePal, g_pCustom->color, 32, 0);
}

// All intro blits assume interleaved bitmaps at the screen's depth: the .bm
// converter writes the interleaved flag into the file and bitmap_conv derives
// depth from palette size, but verify rather than trust - a planar or
// wrong-depth load would silently drop the fast interleaved blit path (and
// mismatched depths would log ACE's mixed-interleaved warning).
static void introValidateBitmap(const tBitMap *pBm, const char *szPath) {
	if(!pBm) {
		logWrite("ERR: intro bitmap '%s' failed to load\n", szPath);
		return;
	}
	if(!bitmapIsInterleaved(pBm)) {
		logWrite("ERR: intro bitmap '%s' is not interleaved\n", szPath);
	}
	if(pBm->Depth != INTRO_BPP) {
		logWrite(
			"ERR: intro bitmap '%s' depth %hu != screen depth %hu\n",
			szPath, (UWORD)pBm->Depth, (UWORD)INTRO_BPP
		);
	}
}

static void introValidateLayout(void) {
	introValidateBitmap(s_pAceLogo, "data/ace.bm");
	introValidateBitmap(s_pTitleLogo, "data/title.bm");
	if(s_pBfr->pBack && !bitmapIsInterleaved(s_pBfr->pBack)) {
		logWrite("ERR: intro framebuffer is not interleaved\n");
	}
}

void gsIntroCreate(void) {
	logBlockBegin("gsIntroCreate");

	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_DONE
	);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, INTRO_BPP,
		TAG_DONE
	);

	s_pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
		TAG_DONE
	);

	s_pTextBitMap = fontCreateTextBitMap(320, g_pGameFont->uwHeight + 8);

	s_pAceLogo = bitmapCreateFromPath("data/ace.bm", 0);
	s_pTitleLogo = bitmapCreateFromPath("data/title.bm", 0);
	// Palette comes from the title image's own .plt - no hand-typed table
	paletteLoadFromPath("data/title.plt", s_pTitlePal, 32);

	s_fwBallX = F16(160);
	s_fwBallY = F16(160);
	s_fwBallVX = F16(2.8f);
	s_fwBallVY = F16(-1.9f);

	// Page pointers for the demo-phase undraw bookkeeping
	s_pPageA = s_pBfr->pBack;
	s_pPageB = s_pBfr->pFront;
	s_pPages[0] = s_pPageA;
	s_pPages[1] = s_pPageB;

	introValidateLayout();
	introEnterAce();

	viewLoad(s_pView);
	logBlockEnd("gsIntroCreate");
	systemUnuse();
}

void gsIntroLoop(void) {
	++s_uwFrameCnt;

	// Any fire / return / escape advances to the next phase
	UBYTE ubSkip = keyUse(KEY_RETURN) || keyUse(KEY_ESCAPE) || keyUse(KEY_SPACE) ||
		joyUse(JOY1_FIRE);

	switch(s_ubPhase) {
		case INTRO_PH_ACE:
			if(s_eStateAce == STATE_ACE_FADE_IN) {
				++s_uwFlashFrame;

				if(s_uwFlashFrame >= FLASH_START_FRAME_A) {
					s_bRatioFlashA = MIN(16, s_bRatioFlashA + 1);
				}
				if(s_uwFlashFrame >= FLASH_START_FRAME_C) {
					s_bRatioFlashC = MIN(16, s_bRatioFlashC + 1);
				}
				if(s_uwFlashFrame >= FLASH_START_FRAME_E) {
					s_bRatioFlashE = MIN(16, s_bRatioFlashE + 1);
				}
				if(s_uwFlashFrame >= FLASH_START_FRAME_PWR) {
					s_bRatioFlashPwr = MIN(16, s_bRatioFlashPwr + 1);
				}
				if(s_uwFlashFrame >= INTRO_ACE_HOLD_FRAMES || ubSkip) {
					s_eStateAce = STATE_ACE_FADE_OUT;
					s_bAceFadeoutRatio = 15;
				}

				for(UBYTE i = 0; i < INTRO_ACE_BLOCKS; ++i) {
					if(s_bRatioFlashA != FLASH_RATIO_INACTIVE && s_bRatioFlashA < 16) {
						s_pAceBlocks[i]->pCmds[0].sMove.bfValue = blendColors(0xFFF, s_pAceColors[i], s_bRatioFlashA);
					}

					if(s_bRatioFlashC != FLASH_RATIO_INACTIVE && s_bRatioFlashC < 16) {
						s_pAceBlocks[i]->pCmds[1].sMove.bfValue = blendColors(0xFFF, s_pAceColors[i], s_bRatioFlashC);
					}

					if(i < 9) {
						// Watch out for "E" flash interfering here
						UWORD uwColor = (
							s_bRatioFlashPwr != FLASH_RATIO_INACTIVE ?
							blendColors(0xFFF, s_uwColorPowered, s_bRatioFlashPwr) : 0
						);
						s_pAceBlocks[i]->pCmds[2].sMove.bfValue = uwColor;
					}
					else {
						if(s_bRatioFlashE != FLASH_RATIO_INACTIVE && s_bRatioFlashE < 16) {
							s_pAceBlocks[i]->pCmds[2].sMove.bfValue = blendColors(0xFFF, s_pAceColors[i], s_bRatioFlashE);
						}
					}

					s_pAceBlocks[i]->ubUpdated = 2;
					s_pView->pCopList->ubStatus |= STATUS_UPDATE;
				}
			}
			else if(s_eStateAce == STATE_ACE_FADE_OUT) {
				// renders 15..1 (dimming) plus ONE extra fully-black frame -
				// without it the last drawn frame keeps a faint logo glow
				UBYTE ubRatio = (s_bAceFadeoutRatio > 0) ? s_bAceFadeoutRatio : 0;
				for(UBYTE i = 0; i < INTRO_ACE_BLOCKS; ++i) {
					s_pAceBlocks[i]->uwCurrCount = 0;
					copMove(
						s_pView->pCopList, s_pAceBlocks[i], &g_pCustom->color[1],
						blendColors(0x000, s_pAceColors[i], ubRatio)
					);
					copMove(
						s_pView->pCopList, s_pAceBlocks[i], &g_pCustom->color[2],
						blendColors(0x000, s_pAceColors[i], ubRatio)
					);
					copMove(
						s_pView->pCopList, s_pAceBlocks[i], &g_pCustom->color[3],
						blendColors(0x000, i < 9 ? s_uwColorPowered : s_pAceColors[i], ubRatio)
					);
				}

				// Must go NEGATIVE: -1 marks the fade-out done, the phase
				// switch below relies on it. Decrementing only while > 0
				// saturated the counter at 0 and the intro hung on black.
				--s_bAceFadeoutRatio;
			}

			if(s_bAceFadeoutRatio < 0) {
				introExitAce();
				// missing title.bm must not crash the blit - fall through
				if(s_pTitleLogo) {
					introEnterTitlePhase();
				}
				else {
					introEnterDemo();
				}
			}
			break;
		case INTRO_PH_TITLE:
			if(ubSkip || s_uwFrameCnt >= INTRO_TITLE_FRAMES) {
				introEnterDemo();
			}
			else {
				// Static screen: only the palette fades, ALL 32 entries -
				// dimming just 16 left the upper half of the 5bpp image
				// black while the lower half showed at half brightness
				paletteDimOcs(
					s_pTitlePal, g_pCustom->color, 32,
					introFadeLevel(s_uwFrameCnt, INTRO_TITLE_FRAMES)
				);
			}
			break;
		case INTRO_PH_DEMO:
			if(ubSkip) {
				sfxPlay(SFX_CONFIRM, 64);
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
				return;
			}
			if(s_uwFrameCnt >= INTRO_DEMO_TIMEOUT_FRAMES) {
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

			// Update ball ONLY on the off-page (pBack) - the displayed page
			// must never be touched or the beam catches the erase/draw. Each
			// page keeps its own last ball position: when a page becomes pBack
			// again, its own stale ball gets erased first.
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
			break;
	}

	// Swap buffers: the just-composed page becomes the displayed one
	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsIntroDestroy(void) {
	systemUse();
	logBlockBegin("gsIntroDestroy");
	fontDestroyTextBitMap(s_pTextBitMap);
	if(s_pAceLogo) {
		bitmapDestroy(s_pAceLogo);
		s_pAceLogo = 0;
	}
	if(s_pTitleLogo) {
		bitmapDestroy(s_pTitleLogo);
		s_pTitleLogo = 0;
	}
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsIntroDestroy");
}
