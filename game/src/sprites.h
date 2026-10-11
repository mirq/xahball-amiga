/* XAHBall - disc sprites (pre-generated interleaved .bm + .msk assets)
 * Team discs use dynamic match palette slots (MCOLOR_KIT_A/B) so a single
 * sprite asset serves every team - the match writes kit RGB values into the
 * viewport palette before viewLoad.
 */
#ifndef _XAHBALL_SPRITES_H_
#define _XAHBALL_SPRITES_H_

#include <ace/types.h>
#include <ace/managers/bob.h>
#include <ace/utils/bitmap.h>

//---------------------------------------------------------------------- DEFINES

//------------------------------------------------------------------------ TYPES

// A disc sprite: interleaved frame bitmap + interleaved mask bitmap
typedef struct _tDiscSprite {
	tBitMap *pFrames; // 4bpp interleaved, single frame
	tBitMap *pMasks;  // 4bpp interleaved mask (silhouette repeated per plane)
	UWORD uwWidth;
	UWORD uwHeight;
} tDiscSprite;

//---------------------------------------------------------------------- GLOBALS

//-------------------------------------------------------------------- FUNCTIONS

// Loads a disc sprite from converted .bm/.msk assets
tDiscSprite *discSpriteLoad(const char *szBmPath, const char *szMskPath);
void discSpriteDestroy(tDiscSprite *pSprite);

// Draws sprite directly with blitUnsafeCopyMask (for static/UI usage)
void discSpriteDraw(
	const tDiscSprite *pSprite, tBitMap *pDst, WORD wDstX, WORD wDstY
);

#endif // _XAHBALL_SPRITES_H_
