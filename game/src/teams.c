/* XAHBall - national team definitions */
#include "teams.h"

#include <ace/managers/blit.h>
#include "game.h"

// Kit RGBs land in dynamic palette slots at match start; disc index is used
// for flag stripes. Away kit kicks in when two teams' kits clash.
tTeam g_pTeams[TEAM_COUNT] = {
	// szCode, szName, disc, alt, kitRGB, altRGB, flagType, flagColors
	{"POL", "POLAND", COLOR_WHITE, COLOR_RED, 0xFFF, 0xF00, FLAG_H2, {COLOR_WHITE, COLOR_RED, 0}},
	{"GER", "GERMANY", COLOR_BLACK, COLOR_GRAY, 0x222, 0x666, FLAG_H3, {COLOR_BLACK, COLOR_RED, COLOR_ORANGE}},
	{"ENG", "ENGLAND", COLOR_WHITE, COLOR_RED, 0xFFF, 0xF00, FLAG_CROSS, {COLOR_WHITE, COLOR_RED, 0}},
	{"ITA", "ITALY", COLOR_AZURE, COLOR_WHITE, 0x06F, 0xFFF, FLAG_V3, {COLOR_GREEN, COLOR_WHITE, COLOR_RED}},
	{"ESP", "SPAIN", COLOR_MAROON, COLOR_YELLOW, 0xC03, 0xFF0, FLAG_H3, {COLOR_RED, COLOR_YELLOW, COLOR_RED}},
	{"NED", "NETHERLANDS", COLOR_ORANGE, COLOR_WHITE, 0xF80, 0xFFF, FLAG_H3, {COLOR_RED, COLOR_WHITE, COLOR_NAVY}},
	{"SWE", "SWEDEN", COLOR_YELLOW, COLOR_NAVY, 0xFF0, 0x009, FLAG_NORDIC, {COLOR_NAVY, COLOR_YELLOW, 0}},
	{"CZE", "CZECH REP.", COLOR_RED, COLOR_NAVY, 0xF00, 0x009, FLAG_CZ, {COLOR_WHITE, COLOR_RED, COLOR_NAVY}},
	{"DEN", "DENMARK", COLOR_RED, COLOR_WHITE, 0xF00, 0xFFF, FLAG_NORDIC, {COLOR_RED, COLOR_WHITE, 0}},
	{"FIN", "FINLAND", COLOR_NAVY, COLOR_WHITE, 0x009, 0xFFF, FLAG_NORDIC, {COLOR_WHITE, COLOR_NAVY, 0}},
	{"USA", "UNITED STATES", COLOR_NAVY, COLOR_WHITE, 0x009, 0xFFF, FLAG_US, {COLOR_RED, COLOR_WHITE, COLOR_NAVY}},
	{"NOR", "NORWAY", COLOR_RED, COLOR_NAVY, 0xF00, 0x009, FLAG_NORDIC2, {COLOR_RED, COLOR_WHITE, COLOR_NAVY}},
};

static void flagFill(tBitMap *pBfr, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH, UBYTE ubColor) {
	blitRect(pBfr, uwX, uwY, uwW, uwH, ubColor);
}

void teamDrawFlag(tBitMap *pBfr, UWORD uwX, UWORD uwY, UWORD uwW, UWORD uwH, UBYTE ubTeamIdx) {
	tTeam *pTeam = &g_pTeams[ubTeamIdx];
	UBYTE *pCols = pTeam->pFlagColors;

	switch(pTeam->ubFlagType) {
		case FLAG_H2:
			flagFill(pBfr, uwX, uwY, uwW, uwH / 2, pCols[0]);
			flagFill(pBfr, uwX, uwY + uwH / 2, uwW, uwH - uwH / 2, pCols[1]);
			break;
		case FLAG_H3:
			flagFill(pBfr, uwX, uwY, uwW, uwH / 3, pCols[0]);
			flagFill(pBfr, uwX, uwY + uwH / 3, uwW, uwH / 3, pCols[1]);
			flagFill(pBfr, uwX, uwY + 2 * (uwH / 3), uwW, uwH - 2 * (uwH / 3), pCols[2]);
			break;
		case FLAG_V3:
			flagFill(pBfr, uwX, uwY, uwW / 3, uwH, pCols[0]);
			flagFill(pBfr, uwX + uwW / 3, uwY, uwW / 3, uwH, pCols[1]);
			flagFill(pBfr, uwX + 2 * (uwW / 3), uwY, uwW - 2 * (uwW / 3), uwH, pCols[2]);
			break;
		case FLAG_CROSS: {
			// Background with centered St George cross
			flagFill(pBfr, uwX, uwY, uwW, uwH, pCols[0]);
			UWORD uwMidY = uwY + uwH / 2, uwMidX = uwX + uwW / 2;
			UWORD uwThick = uwH / 5 ? uwH / 5 : 1;
			flagFill(pBfr, uwX, uwMidY - uwThick / 2, uwW, uwThick, pCols[1]);
			flagFill(pBfr, uwMidX - uwThick / 2, uwY, uwThick, uwH, pCols[1]);
			break;
		}
		case FLAG_NORDIC: {
			// Cross shifted towards hoist
			flagFill(pBfr, uwX, uwY, uwW, uwH, pCols[0]);
			UWORD uwCrossX = uwX + uwW * 2 / 5;
			UWORD uwMidY = uwY + uwH / 2;
			UWORD uwThick = uwH / 4 ? uwH / 4 : 1;
			flagFill(pBfr, uwX, uwMidY - uwThick / 2, uwW, uwThick, pCols[1]);
			flagFill(pBfr, uwCrossX - uwThick / 2, uwY, uwThick, uwH, pCols[1]);
			break;
		}
		case FLAG_NORDIC2: {
			// Norway: red bg, white cross with blue inner cross
			flagFill(pBfr, uwX, uwY, uwW, uwH, pCols[0]);
			UWORD uwCrossX = uwX + uwW * 2 / 5;
			UWORD uwMidY = uwY + uwH / 2;
			UWORD uwThick = uwH / 4 ? uwH / 4 : 1;
			UWORD uwInner = uwThick / 2 ? uwThick / 2 : 1;
			flagFill(pBfr, uwX, uwMidY - uwThick / 2, uwW, uwThick, pCols[1]);
			flagFill(pBfr, uwCrossX - uwThick / 2, uwY, uwThick, uwH, pCols[1]);
			flagFill(pBfr, uwX, uwMidY - uwInner / 2, uwW, uwInner, pCols[2]);
			flagFill(pBfr, uwCrossX - uwInner / 2, uwY, uwInner, uwH, pCols[2]);
			break;
		}
		case FLAG_CZ: {
			// White over red with blue triangle at hoist
			flagFill(pBfr, uwX, uwY, uwW, uwH / 2, pCols[0]);
			flagFill(pBfr, uwX, uwY + uwH / 2, uwW, uwH - uwH / 2, pCols[1]);
			// Triangle: column shrinking as it goes down
			UWORD uwHalfH = uwH / 2;
			for(UWORD uwRow = 0; uwRow < uwH; ++uwRow) {
				UWORD uwTri = (uwRow < uwHalfH) ? (uwRow * uwW / (2 * uwHalfH)) : ((uwH - uwRow - 1) * uwW / (2 * uwHalfH));
				if(uwTri) {
					blitRect(pBfr, uwX, uwY + uwRow, uwTri, 1, pCols[2]);
				}
			}
			break;
		}
		case FLAG_US: {
			// Simplified: 7 stripes + canton
			UWORD uwStripe = uwH / 7 ? uwH / 7 : 1;
			for(UWORD uwRow = 0; uwRow < 7; ++uwRow) {
				flagFill(pBfr, uwX, uwY + uwRow * uwStripe, uwW, uwStripe, (uwRow & 1) ? pCols[1] : pCols[0]);
			}
			flagFill(pBfr, uwX, uwY, uwW * 2 / 5, uwH / 2, pCols[2]);
			break;
		}
	}
}
