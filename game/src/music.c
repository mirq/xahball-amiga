/* XAHBall - menu music (chaosArena ptplayer pattern): the .mod structure
 * sits in fast mem, its instruments in a chip samplepack (mod_tool split).
 */
#include "music.h"

#include <ace/managers/log.h>
#include <ace/managers/memory.h>
#include <ace/managers/ptplayer.h>

static tPtplayerMod *s_pMod;
static tPtplayerSamplePack *s_pSamples;
static UBYTE s_isLoadFailed;
static UBYTE s_isPlaying;

static void musicFreeSamples(void) {
	if(s_pSamples) {
		for(UBYTE i = 0; i < s_pSamples->ubSampleCount; ++i) {
			memFree(
				s_pSamples->pSamples[i].pData,
				s_pSamples->pSamples[i].uwWordLength * sizeof(UWORD)
			);
		}
		memFree(s_pSamples, sizeof(*s_pSamples));
		s_pSamples = 0;
	}
}

void musicPlay(void) {
	if(s_isPlaying) {
		return; // keep the tune going across menu screens
	}
	if(!s_pMod && !s_isLoadFailed) {
		s_pMod = ptplayerModCreateFromPath("data/menu.mod");
		s_pSamples = ptplayerSampleDataCreateFromPath("data/menu.samplepack");
		if(!s_pMod || !s_pSamples) {
			// Don't retry (and re-read the floppy) on every menu entry
			logWrite("ERR: menu music assets failed to load - continuing silently\n");
			s_isLoadFailed = 1;
			musicFreeSamples();
			if(s_pMod) {
				ptplayerModDestroy(s_pMod);
				s_pMod = 0;
			}
		}
	}
	if(s_pMod) {
		ptplayerLoadMod(s_pMod, s_pSamples, 0);
		ptplayerEnableMusic(1);
		s_isPlaying = 1;
	}
}

void musicStop(void) {
	if(s_pMod && s_isPlaying) {
		ptplayerStop();
	}
	s_isPlaying = 0;
}

void musicDestroy(void) {
	// Stop replay FIRST - ptplayer keeps referencing the samplepack while
	// playing - then the chip samples, then the mod structure
	musicStop();
	musicFreeSamples();
	if(s_pMod) {
		ptplayerModDestroy(s_pMod);
		s_pMod = 0;
	}
}
