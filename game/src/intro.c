/* XAHBall - intro state: ACE logo splash, then the XAHBALL title artwork
 * as the welcome screen. Two phases:
 *   PH_ACE   - ACE logo (own 4-color palette: A=red, C=green, E=blue),
 *              palette fade-in/out, "ace" sample. Fire skips it.
 *   PH_TITLE - full-screen title artwork (32 colors, tools/make_title.py),
 *              palette from data/title.plt. Fades in, then waits with a
 *              blinking "PRESS FIRE TO START". Fire fades out to the menu;
 *              idling INTRO_IDLE_DEMO_FRAMES fades out into a demo match.
 * Double-buffered: the artwork is composed on BOTH pages at phase entry;
 * the prompt is restored from the artwork + redrawn per page on blink edges.
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
#include "fastfix.h"
#include "game.h"
#include "match.h"
#include "sfx.h"

// Phase durations (frames @50Hz)
#define INTRO_ACE_HOLD_FRAMES 215
#define INTRO_IDLE_DEMO_FRAMES (50 * 20)
// fades for the logo screens
#define INTRO_FADE_FRAMES 25

// Welcome prompt band over the artwork's grass. Restores copy from the
// artwork with blitCopyAligned: x and width must be multiples of 16.
#define INTRO_PROMPT_Y 238
#define INTRO_PROMPT_X 64
#define INTRO_PROMPT_W 192
#define INTRO_PROMPT_H 8
// Music credit line, under the prompt band
#define INTRO_CREDIT_Y 248

// 5 bitplanes = 32 colors: the title artwork needs them; the ACE splash
// just uses the lower entries of the same palette
#define INTRO_BPP 5

typedef enum tIntroPhase {
	INTRO_PH_ACE,
	INTRO_PH_TITLE,
} tIntroPhase;

typedef enum tIntroExit {
	INTRO_EXIT_NONE,
	INTRO_EXIT_MENU,
	INTRO_EXIT_DEMO,
} tIntroExit;

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

// Welcome screen state
static tIntroExit s_eExit;
static UWORD s_uwExitFrame;  // fade-out frames left once an exit was chosen
static UBYTE s_ubPromptWhite; // brightest title palette entry (prompt text)
static UBYTE s_ubPromptDirty; // pages left to repaint after a blink edge
static tBitMap *s_pPages[2];

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

	// Prompt color: the brightest entry of the artwork's own palette
	UWORD uwBest = 0;
	s_ubPromptWhite = 1;
	for(UBYTE i = 1; i < 32; ++i) {
		UWORD uwC = s_pTitlePal[i];
		UWORD uwSum = ((uwC >> 8) & 0xF) + ((uwC >> 4) & 0xF) + (uwC & 0xF);
		if(uwSum > uwBest) {
			uwBest = uwSum;
			s_ubPromptWhite = i;
		}
	}
	// Static music credit in the bottom-right grass, below the prompt band
	// (the blink restore never touches it, so draw once on both pages)
	for(UBYTE ubPage = 0; ubPage < 2; ++ubPage) {
		fontDrawStr(
			g_pGameFont, s_pPages[ubPage], GAME_SCREEN_WIDTH - 4, INTRO_CREDIT_Y,
			"MUSIC BY X-TYPE", s_ubPromptWhite,
			FONT_COOKIE | FONT_RIGHT | FONT_SHADOW, s_pTextBitMap
		);
	}
	s_eExit = INTRO_EXIT_NONE;
	s_ubPromptDirty = 2;
}

// Blinking welcome prompt: on each blink edge both pages get the artwork
// restored under the prompt band, then the text if it's in its "on" half
static void introDrawPrompt(void) {
	UBYTE ubBlink = s_uwFrameCnt & 0x3F;
	if(ubBlink == 0 || ubBlink == 44) {
		s_ubPromptDirty = 2;
	}
	if(!s_ubPromptDirty) {
		return;
	}
	--s_ubPromptDirty;
	tBitMap *pBack = s_pBfr->pBack;
	blitCopyAligned(
		s_pTitleLogo, INTRO_PROMPT_X, INTRO_PROMPT_Y, pBack,
		INTRO_PROMPT_X, INTRO_PROMPT_Y, INTRO_PROMPT_W, INTRO_PROMPT_H
	);
	if(ubBlink < 44) {
		fontDrawStr(
			g_pGameFont, pBack, GAME_SCREEN_WIDTH / 2, INTRO_PROMPT_Y,
			"PRESS FIRE TO START", s_ubPromptWhite,
			FONT_COOKIE | FONT_HCENTER | FONT_SHADOW, s_pTextBitMap
		);
	}
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

	s_pPages[0] = s_pBfr->pBack;
	s_pPages[1] = s_pBfr->pFront;

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
				// missing title.bm must not crash the blit - skip to the menu
				if(s_pTitleLogo) {
					introEnterTitlePhase();
				}
				else {
					stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
					return;
				}
			}
			break;
		case INTRO_PH_TITLE:
			if(s_eExit == INTRO_EXIT_NONE) {
				if(ubSkip) {
					sfxPlay(SFX_CONFIRM, 64);
					s_eExit = INTRO_EXIT_MENU;
					s_uwExitFrame = INTRO_FADE_FRAMES / 2;
				}
				else if(s_uwFrameCnt >= INTRO_IDLE_DEMO_FRAMES) {
					s_eExit = INTRO_EXIT_DEMO;
					s_uwExitFrame = INTRO_FADE_FRAMES;
				}
			}
			if(s_eExit != INTRO_EXIT_NONE) {
				// Quick fade to black, then hand over
				if(!s_uwExitFrame) {
					if(s_eExit == INTRO_EXIT_DEMO) {
						matchStartDemo();
					}
					else {
						stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
					}
					return;
				}
				--s_uwExitFrame;
				UWORD uwTotal = (s_eExit == INTRO_EXIT_DEMO) ? INTRO_FADE_FRAMES : INTRO_FADE_FRAMES / 2;
				paletteDimOcs(s_pTitlePal, g_pCustom->color, 32, (s_uwExitFrame * 15) / uwTotal);
			}
			else if(s_uwFrameCnt <= INTRO_FADE_FRAMES) {
				// Fade in - ALL 32 entries: dimming just 16 left the upper
				// half of the 5bpp image black while the lower half showed
				paletteDimOcs(
					s_pTitlePal, g_pCustom->color, 32,
					(s_uwFrameCnt * 15) / INTRO_FADE_FRAMES
				);
			}
			introDrawPrompt();
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
