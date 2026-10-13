/* XAHBall - sound effect playback via ptplayer */
#ifndef _XAHBALL_SFX_H_
#define _XAHBALL_SFX_H_

#include <ace/types.h>

//---------------------------------------------------------------------- DEFINES

typedef enum tSfx {
	SFX_MOVE,
	SFX_CONFIRM,
	SFX_BACK,
	SFX_KICK,
	SFX_BOUNCE,
	SFX_GOAL,
	SFX_WHISTLE,
	SFX_ACE,
	SFX_COUNT
} tSfx;

//------------------------------------------------------------------------ TYPES

//---------------------------------------------------------------------- GLOBALS

//-------------------------------------------------------------------- FUNCTIONS

// Creates ptplayer manager (sfx-only mode) and loads all samples
void sfxCreate(void);
void sfxDestroy(void);

// Plays given sfx at volume 0..64
void sfxPlay(UBYTE ubSfxIdx, UBYTE ubVolume);

#endif // _XAHBALL_SFX_H_
