/* XAHBall - national team definitions */
#ifndef _XAHBALL_TEAMS_H_
#define _XAHBALL_TEAMS_H_

#include <ace/types.h>
#include <ace/utils/bitmap.h>

//---------------------------------------------------------------------- DEFINES

#define TEAM_COUNT 12

typedef enum tFlagType {
	FLAG_H2,     // two horizontal stripes (Poland)
	FLAG_H3,     // three horizontal stripes
	FLAG_V3,     // three vertical stripes
	FLAG_CROSS,  // background with centered cross (England)
	FLAG_NORDIC, // nordic cross
	FLAG_NORDIC2,// nordic cross with fimbriation (Norway)
	FLAG_CZ,     // white/red with blue triangle
	FLAG_US      // stripes with canton
} tFlagType;

//------------------------------------------------------------------------ TYPES

typedef struct _tTeam {
	char *szCode;         // 3-letter code, e.g. "POL"
	char *szName;         // full name, e.g. "POLAND"
	UBYTE ubDiscColor;    // flag/disc color index (menu, flags)
	UBYTE ubAltColor;     // away kit index if home colors clash
	UWORD uwKitRGB;       // kit fill RGB (12-bit, dynamic palette slot)
	UWORD uwAltRGB;       // away kit RGB if home colors clash
	tFlagType ubFlagType;
	UBYTE pFlagColors[3]; // stripe colors for flag types that use them
} tTeam;

//---------------------------------------------------------------------- GLOBALS

extern tTeam g_pTeams[TEAM_COUNT];

//-------------------------------------------------------------------- FUNCTIONS

// Draws team flag at given position into bitmap
void teamDrawFlag(tBitMap *pBfr, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH, UBYTE ubTeamIdx);

#endif // _XAHBALL_TEAMS_H_
