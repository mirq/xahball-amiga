/* XAHBall - per-slot input device abstraction */
#include "input_slots.h"

#include <ace/managers/key.h>
#include <ace/managers/joy.h>

const char *g_pDeviceNames[DEVICE_COUNT] = {
	"AI BOT",
	"JOY1",
	"JOY2",
	"JOY3",
	"JOY4",
	"KB1:ARROWS",
	"KB2:WSAD",
};

// Direction slots in the per-device tables
#define DIR_L 0
#define DIR_R 1
#define DIR_U 2
#define DIR_D 3
#define DIR_COUNT 4

// Direction -> rawkey per keyboard device set
static const UBYTE s_pDirKeys[2][DIR_COUNT] = {
	{KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN}, // KB1
	{KEY_A, KEY_D, KEY_W, KEY_S},            // KB2
};

// Joystick direction/fire codes per port (enum values are per-port, so they
// live in tables instead of arithmetic)
static const UBYTE s_pJoyDirs[4][DIR_COUNT] = {
	{JOY1_LEFT, JOY1_RIGHT, JOY1_UP, JOY1_DOWN},
	{JOY2_LEFT, JOY2_RIGHT, JOY2_UP, JOY2_DOWN},
	{JOY3_LEFT, JOY3_RIGHT, JOY3_UP, JOY3_DOWN},
	{JOY4_LEFT, JOY4_RIGHT, JOY4_UP, JOY4_DOWN},
};
static const UBYTE s_pJoyFire[4] = {
	JOY1_FIRE, JOY2_FIRE, JOY3_FIRE, JOY4_FIRE
};

// ----------------------------------------------------------------------------
// Opposite-direction resolution - DISABLED (kept for reference, see history).
// While both ends of an axis read held, the freshly pressed end was to win
// and the other suppressed; measured in play it did not help.
// ----------------------------------------------------------------------------
#if defined(__GNUC__)
__attribute__((unused))
#endif
static void inputResolveOpposite(UBYTE ubDevice, const UBYTE *pCur, UBYTE *pOut) {
	static const UBYTE pOpp[DIR_COUNT] = {DIR_R, DIR_L, DIR_D, DIR_U};
	static UBYTE s_pUbPrevDir[DEVICE_COUNT][DIR_COUNT];
	static UBYTE s_pUAxisWinner[DEVICE_COUNT][2] = {
		{0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF},
		{0xFF, 0xFF}, {0xFF, 0xFF}, {0xFF, 0xFF},
	};
	const UBYTE *pPrev = s_pUbPrevDir[ubDevice];
	for(UBYTE i = 0; i < DIR_COUNT; i += 2) { // once per axis: L and U ends
		UBYTE ubOpp = pOpp[i];
		UBYTE *pWinner = &s_pUAxisWinner[ubDevice][i / 2];
		if(!pCur[i] && !pCur[ubOpp]) {
			// Axis idle - drop any stale winner
			*pWinner = 0xFF;
			continue;
		}
		if(pCur[i] && pCur[ubOpp]) {
			// Both ends active: a fresh press (if any) decides the axis
			UBYTE ubFresh = (!pPrev[i] ? 1 : 0) | (!pPrev[ubOpp] ? 2 : 0);
			if(ubFresh) {
				*pWinner = (ubFresh & 2) ? 1 : 0;
			}
			else if(*pWinner == 0xFF) {
				// Both held with no history (e.g. first poll after a state
				// change) - default to the second end (R/D)
				*pWinner = 1;
			}
			if(*pWinner == 0) {
				pOut[ubOpp] = 0;
			}
			else {
				pOut[i] = 0;
			}
		}
		else {
			// Single end held: it wins outright - and if the previous
			// winner was just released, the survivor RESUMES here
			*pWinner = pCur[ubOpp] ? 1 : 0;
		}
	}
}

void inputPollDevice(UBYTE ubDevice, tInputState *pIn) {
	// One pass: read the four direction states into pDir, then derive the
	// axis values once at the end. Opposite ends cancel out naturally
	// (both held = 1 - 1 = 0: stand still), which is the plain behavior.
	UBYTE pDir[DIR_COUNT] = {0, 0, 0, 0};
	UBYTE ubFire = 0;

	switch(ubDevice) {
		case DEVICE_JOY1:
		case DEVICE_JOY2:
		case DEVICE_JOY3:
		case DEVICE_JOY4: {
			const UBYTE *pDirs = s_pJoyDirs[ubDevice - DEVICE_JOY1];
			for(UBYTE i = 0; i < DIR_COUNT; ++i) {
				pDir[i] = joyCheck(pDirs[i]);
			}
			ubFire = joyCheck(s_pJoyFire[ubDevice - DEVICE_JOY1]);
			break;
		}
		case DEVICE_KB1:
		case DEVICE_KB2: {
			const UBYTE *pKeys = s_pDirKeys[ubDevice - DEVICE_KB1];
			for(UBYTE i = 0; i < DIR_COUNT; ++i) {
				pDir[i] = keyCheck(pKeys[i]);
			}
			ubFire = keyCheck(ubDevice == DEVICE_KB1 ? KEY_RSHIFT : KEY_LSHIFT);
			break;
		}
		default:
			break;
	}

	pIn->bDx = pDir[DIR_R] - pDir[DIR_L];
	pIn->bDy = pDir[DIR_D] - pDir[DIR_U];
	pIn->ubKick = ubFire;
}

UBYTE inputAnyDir(UBYTE ubDevice) {
	tInputState sIn;
	inputPollDevice(ubDevice, &sIn);
	return sIn.bDx || sIn.bDy;
}

UBYTE inputAnyFire(UBYTE ubDevice) {
	tInputState sIn;
	inputPollDevice(ubDevice, &sIn);
	return sIn.ubKick;
}

// Kept for API compatibility (match.c calls it at match start). The disabled
// resolver was the only state that needed clearing - nothing to do now.
void inputResetBookkeeping(void) {
}
