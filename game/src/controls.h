/* XAHBall - controller presets ("simple" setup) and slot descriptions.
 * Each team size offers a short list of common hardware setups; picking one
 * writes all six slot devices at once. Anything else is CUSTOM and edited
 * per slot on the advanced setup screen (setup.c).
 *
 * Ports are named like the Amiga labels them: PORT 2 is the joystick port
 * (ACE JOY1), PORT 1 the mouse port (ACE JOY2), PORT 3/4 the parallel
 * adapter (ACE JOY3/JOY4). The common setup keeps the mouse in port 1, so
 * presets fill port 2 first.
 */
#ifndef _XAHBALL_CONTROLS_H_
#define _XAHBALL_CONTROLS_H_

#include <ace/types.h>

//---------------------------------------------------------------------- DEFINES

#define CONTROL_PRESET_CUSTOM 0xFF

//-------------------------------------------------------------------- FUNCTIONS

// Number of presets for the given team size (1..3)
UBYTE controlsPresetCount(UBYTE ubPlayersPerSide);

// Short preset name for the menu value column ("CUSTOM" for the custom id)
const char *controlsPresetName(UBYTE ubPlayersPerSide, UBYTE ubPreset);

// Writes the preset's devices into g_sMatchConfig (no-op for CUSTOM)
void controlsApplyPreset(UBYTE ubPreset);

// Picks a valid preset after a team size change: keeps the same index when
// it exists (e.g. "1P VS CPU" stays 1P VS CPU), clamps otherwise. A CUSTOM
// setup stays custom - its now-hidden slots fall back to AI.
void controlsOnTeamSizeChange(void);

// Called when the advanced screen is left: marks the setup CUSTOM unless
// the edited slots still equal one of the presets
void controlsDetectPreset(void);

// One side's controllers, e.g. "JOY PORT 2 + WSAD" or "CPU" (static buffer
// per side - valid until the next call for the same side)
const char *controlsDescribeSide(UBYTE ubSide);

#endif // _XAHBALL_CONTROLS_H_
