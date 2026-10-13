/* XAHBall - sound effect playback via ptplayer */
#include "sfx.h"

#include <ace/managers/log.h>
#include <ace/managers/ptplayer.h>
#include <ace/managers/system.h>

static tPtplayerSfx *s_pSfx[SFX_COUNT];

static const char *s_pSfxPaths[SFX_COUNT] = {
	"data/sfx/move.sfx",
	"data/sfx/confirm.sfx",
	"data/sfx/back.sfx",
	"data/sfx/kick.sfx",
	"data/sfx/bounce.sfx",
	"data/sfx/goal.sfx",
	"data/sfx/whistle.sfx",
	"data/sfx/ace.sfx",
};

void sfxCreate(void) {
	logBlockBegin("sfxCreate");
	ptplayerCreate(systemIsPal());
	for(UBYTE i = 0; i < SFX_COUNT; ++i) {
		// Samples must be in chip mem for ptplayer DMA (isFast = 0)
		s_pSfx[i] = ptplayerSfxCreateFromPath(s_pSfxPaths[i], 0);
		if(!s_pSfx[i]) {
			logWrite("ERR: could not load %s\n", s_pSfxPaths[i]);
		}
	}
	logBlockEnd("sfxCreate");
}

void sfxDestroy(void) {
	logBlockBegin("sfxDestroy");
	for(UBYTE i = 0; i < SFX_COUNT; ++i) {
		if(s_pSfx[i]) {
			ptplayerSfxDestroy(s_pSfx[i]);
			s_pSfx[i] = 0;
		}
	}
	ptplayerDestroy();
	logBlockEnd("sfxDestroy");
}

void sfxPlay(UBYTE ubSfxIdx, UBYTE ubVolume) {
	if(ubSfxIdx >= SFX_COUNT || !s_pSfx[ubSfxIdx]) {
		return;
	}
	ptplayerSfxPlay(
		s_pSfx[ubSfxIdx], PTPLAYER_SFX_CHANNEL_ANY, ubVolume ? ubVolume : 64, 1
	);
}
