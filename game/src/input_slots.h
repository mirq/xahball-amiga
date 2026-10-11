/* XAHBall - per-slot input device abstraction */
#ifndef _XAHBALL_INPUT_SLOTS_H_
#define _XAHBALL_INPUT_SLOTS_H_

#include <ace/types.h>

//---------------------------------------------------------------------- DEFINES

typedef enum tDevice {
	DEVICE_AI = 0,
	DEVICE_JOY1,
	DEVICE_JOY2,
	DEVICE_JOY3,
	DEVICE_JOY4,
	DEVICE_KB1, // cursor keys, kick: right shift
	DEVICE_KB2, // wsad, kick: left shift
	DEVICE_COUNT
} tDevice;

//------------------------------------------------------------------------ TYPES

typedef struct _tInputState {
	BYTE bDx;    // -1..1
	BYTE bDy;    // -1..1
	UBYTE ubKick; // 1 when kick held
} tInputState;

//---------------------------------------------------------------------- GLOBALS

extern const char *g_pDeviceNames[DEVICE_COUNT];

//-------------------------------------------------------------------- FUNCTIONS

// Polls given device into pIn. AI devices return zeroes - ai.c handles them.
void inputPollDevice(UBYTE ubDevice, tInputState *pIn);

// Returns non-zero when any control on given device is pressed (menu nav)
UBYTE inputAnyDir(UBYTE ubDevice);
UBYTE inputAnyFire(UBYTE ubDevice);

// Clears cross-game bookkeeping (heal edge history, joy conflict latches).
// Call at match create, after keyReset().
void inputResetBookkeeping(void);

#endif // _XAHBALL_INPUT_SLOTS_H_
