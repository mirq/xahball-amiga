/* XAHBall - disc sprites (pre-generated interleaved .bm + .msk assets)
 * Assets are converted at build time by bitmap_conv (INTERLEAVED + MASKS).
 * The .msk mask comes out as a 4bpp interleaved bitmap whose planes repeat
 * the silhouette - exactly the layout the BOB blitter expects.
 */
#include "sprites.h"

#include <ace/managers/blit.h>
#include <ace/managers/log.h>

tDiscSprite *discSpriteLoad(const char *szBmPath, const char *szMskPath) {
	logBlockBegin("discSpriteLoad");

	tDiscSprite *pSprite = memAllocFastClear(sizeof(tDiscSprite));
	pSprite->pFrames = bitmapCreateFromPath(szBmPath, 0);
	pSprite->pMasks = bitmapCreateFromPath(szMskPath, 0);
	if(!pSprite->pFrames || !pSprite->pMasks) {
		logWrite("ERR: sprite load failed: %s / %s\n", szBmPath, szMskPath);
		memFree(pSprite, sizeof(tDiscSprite));
		logBlockEnd("discSpriteLoad");
		return 0;
	}
	pSprite->uwWidth = pSprite->pFrames->BytesPerRow * 8;
	pSprite->uwHeight = pSprite->pFrames->Rows;

	logBlockEnd("discSpriteLoad");
	return pSprite;
}

void discSpriteDestroy(tDiscSprite *pSprite) {
	if(pSprite) {
		bitmapDestroy(pSprite->pFrames);
		bitmapDestroy(pSprite->pMasks);
		memFree(pSprite, sizeof(tDiscSprite));
	}
}

void discSpriteDraw(
	const tDiscSprite *pSprite, tBitMap *pDst, WORD wDstX, WORD wDstY
) {
	blitUnsafeCopyMask(
		pSprite->pFrames, 0, 0,
		pDst, wDstX, wDstY,
		pSprite->uwWidth, pSprite->uwHeight,
		pSprite->pMasks->Planes[0]
	);
}
