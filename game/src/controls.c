/* XAHBall - controller presets ("simple" setup) and slot descriptions */
#include "controls.h"

#include <string.h>
#include "game.h"
#include "input_slots.h"

typedef struct _tControlPreset {
	const char *szName;
	UBYTE pDevices[SLOT_COUNT]; // slots 0..2 team A (left), 3..5 team B
} tControlPreset;

#define AI DEVICE_AI
#define P2 DEVICE_JOY1 // joystick port
#define P1 DEVICE_JOY2 // mouse port
#define P3 DEVICE_JOY3 // parallel adapter
#define P4 DEVICE_JOY4 // parallel adapter
#define ARROWS DEVICE_KB1
#define WSAD DEVICE_KB2

// Index 0 is always the single-player game, so a team size change keeps
// "1P VS CPU" selected
static const tControlPreset s_pPresets1[] = {
	{"1P VS CPU", {P2, AI, AI, AI, AI, AI}},
	{"JOY VS JOY", {P2, AI, AI, P1, AI, AI}},
	{"JOY VS KEYS", {P2, AI, AI, WSAD, AI, AI}},
};
static const tControlPreset s_pPresets2[] = {
	{"1P VS CPU", {P2, AI, AI, AI, AI, AI}},
	{"2 JOY 2 KEYS", {P2, WSAD, AI, P1, ARROWS, AI}},
	{"4 JOYSTICKS", {P2, P4, AI, P1, P3, AI}},
};
static const tControlPreset s_pPresets3[] = {
	{"1P VS CPU", {P2, AI, AI, AI, AI, AI}},
	{"4 JOY 2 KEYS", {P2, P4, WSAD, P1, P3, ARROWS}},
};

#undef AI
#undef P1
#undef P2
#undef P3
#undef P4
#undef ARROWS
#undef WSAD

static const tControlPreset *s_pPresetTables[3] = {
	s_pPresets1, s_pPresets2, s_pPresets3
};
static const UBYTE s_pPresetCounts[3] = {
	sizeof(s_pPresets1) / sizeof(s_pPresets1[0]),
	sizeof(s_pPresets2) / sizeof(s_pPresets2[0]),
	sizeof(s_pPresets3) / sizeof(s_pPresets3[0]),
};

// Short device names for the per-side summary
static const char *s_pDeviceShort[DEVICE_COUNT] = {
	"CPU", "PORT 2", "PORT 1", "PORT 3", "PORT 4", "ARROWS", "WSAD",
};

UBYTE controlsPresetCount(UBYTE ubPlayersPerSide) {
	return s_pPresetCounts[ubPlayersPerSide - 1];
}

const char *controlsPresetName(UBYTE ubPlayersPerSide, UBYTE ubPreset) {
	if(ubPreset == CONTROL_PRESET_CUSTOM) {
		return "CUSTOM";
	}
	return s_pPresetTables[ubPlayersPerSide - 1][ubPreset].szName;
}

void controlsApplyPreset(UBYTE ubPreset) {
	g_sMatchConfig.ubControlPreset = ubPreset;
	if(ubPreset == CONTROL_PRESET_CUSTOM) {
		return;
	}
	const tControlPreset *pPreset =
		&s_pPresetTables[g_sMatchConfig.ubPlayersPerSide - 1][ubPreset];
	for(UBYTE i = 0; i < SLOT_COUNT; ++i) {
		g_sMatchConfig.ubSlotDevice[i] = pPreset->pDevices[i];
	}
}

void controlsOnTeamSizeChange(void) {
	UBYTE ubPreset = g_sMatchConfig.ubControlPreset;
	if(ubPreset == CONTROL_PRESET_CUSTOM) {
		for(UBYTE i = 0; i < SLOT_COUNT; ++i) {
			if((i % SLOTS_PER_SIDE) >= g_sMatchConfig.ubPlayersPerSide) {
				g_sMatchConfig.ubSlotDevice[i] = DEVICE_AI;
			}
		}
		return;
	}
	UBYTE ubCount = controlsPresetCount(g_sMatchConfig.ubPlayersPerSide);
	if(ubPreset >= ubCount) {
		ubPreset = ubCount - 1;
	}
	controlsApplyPreset(ubPreset);
}

void controlsDetectPreset(void) {
	UBYTE ubSize = g_sMatchConfig.ubPlayersPerSide;
	const tControlPreset *pTable = s_pPresetTables[ubSize - 1];
	for(UBYTE ubPreset = 0; ubPreset < controlsPresetCount(ubSize); ++ubPreset) {
		if(!memcmp(pTable[ubPreset].pDevices, g_sMatchConfig.ubSlotDevice, SLOT_COUNT)) {
			g_sMatchConfig.ubControlPreset = ubPreset;
			return;
		}
	}
	g_sMatchConfig.ubControlPreset = CONTROL_PRESET_CUSTOM;
}

const char *controlsDescribeSide(UBYTE ubSide) {
	// "PORT 2 + PORT 4 + ARROWS" is the longest possible text
	static char pBuffers[2][32];
	char *szOut = pBuffers[ubSide];
	UBYTE ubHumans = 0;
	szOut[0] = 0;
	for(UBYTE i = 0; i < g_sMatchConfig.ubPlayersPerSide; ++i) {
		UBYTE ubDevice = g_sMatchConfig.ubSlotDevice[ubSide * SLOTS_PER_SIDE + i];
		if(ubDevice == DEVICE_AI) {
			continue;
		}
		if(ubHumans++) {
			strcat(szOut, " + ");
		}
		strcat(szOut, s_pDeviceShort[ubDevice]);
	}
	if(!ubHumans) {
		strcpy(szOut, "CPU");
	}
	else if(ubHumans < g_sMatchConfig.ubPlayersPerSide) {
		strcat(szOut, " + CPU");
	}
	return szOut;
}
