/* XAHBall - team select state
 * First human player picks left team, second human (or same player) picks
 * right team. Fire confirms, left/right cycles teams.
 */
#include "teamsel.h"

#include <ace/managers/blit.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/log.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/extview.h>
#include <ace/utils/font.h>
#include "game.h"
#include "input_slots.h"
#include "sfx.h"
#include "teams.h"

static tView *s_pView;
static tVPort *s_pVPort;
static tSimpleBufferManager *s_pBfr;
static tTextBitMap *s_pTextBitMap;

typedef enum tPickPhase {
	PICK_LEFT,
	PICK_RIGHT,
} tPickPhase;

static tPickPhase s_ubPhase;
static UBYTE s_ubPickLeft;  // team idx currently highlighted for left
static UBYTE s_ubPickRight;
static UBYTE s_ubDevicePicker;       // device cycling the left team
static UBYTE s_ubDevicePickerRight;  // device cycling the right team

static void teamselFindPickers(void) {
	UBYTE pHumans[SLOT_COUNT];
	UBYTE ubHumanCount = 0;
	for(UBYTE i = 0; i < SLOT_COUNT; ++i) {
		if(g_sMatchConfig.ubSlotDevice[i] != DEVICE_AI) {
			pHumans[ubHumanCount++] = g_sMatchConfig.ubSlotDevice[i];
		}
	}
	if(ubHumanCount == 0) {
		s_ubDevicePicker = DEVICE_KB1;
	}
	else {
		s_ubDevicePicker = pHumans[0];
	}
	// Right team: second human if there is one, else same picker
	s_ubDevicePickerRight = (ubHumanCount >= 2) ? pHumans[1] : s_ubDevicePicker;
}

static void teamselDrawPanel(
	UBYTE ubTeamIdx, UWORD uwCenterX, UBYTE ubIsActive
) {
	tBitMap *pBack = s_pBfr->pBack;
	UBYTE ubColorText = ubIsActive ? COLOR_AMBER : COLOR_GRAY;
	UBYTE ubColorName = ubIsActive ? COLOR_WHITE : COLOR_GRAY;

	// Flag
	UWORD uwFlagW = 120, uwFlagH = 80;
	teamDrawFlag(
		pBack, uwCenterX - uwFlagW / 2, 70, uwFlagW, uwFlagH, ubTeamIdx
	);
	// Frame around flag
	UWORD uwFx = uwCenterX - uwFlagW / 2 - 2, uwFy = 68;
	UBYTE ubFrame = ubIsActive ? COLOR_AMBER : COLOR_GRAY;
	blitRect(pBack, uwFx, uwFy, uwFlagW + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy + uwFlagH + 2, uwFlagW + 4, 2, ubFrame);
	blitRect(pBack, uwFx, uwFy, 2, uwFlagH + 4, ubFrame);
	blitRect(pBack, uwFx + uwFlagW + 2, uwFy, 2, uwFlagH + 4, ubFrame);

	fontDrawStr(
		g_pGameFont, pBack, uwCenterX, 166, g_pTeams[ubTeamIdx].szName,
		ubColorName, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	// NOTE: never draw empty strings - zero-width blit corrupts memory
	if(ubIsActive) {
		fontDrawStr(
			g_pGameFont, pBack, uwCenterX, 186, "PRESS FIRE",
			ubColorText, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
		);
	}
}

static void teamselDrawPage(tBitMap *pBack) {

	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, GAME_SCREEN_HEIGHT, COLOR_BG);
	blitRect(pBack, 0, 0, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);
	blitRect(pBack, 0, GAME_SCREEN_HEIGHT - 2, GAME_SCREEN_WIDTH, 2, COLOR_AMBER);

	fontDrawStr(
		g_pGameFont, pBack, GAME_SCREEN_WIDTH / 2, 24, "SELECT TEAMS", COLOR_AMBER,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, 80, 44, "LEFT", COLOR_WHITE,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);
	fontDrawStr(
		g_pGameFont, pBack, 240, 44, "RIGHT", COLOR_WHITE,
		FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
	);

	teamselDrawPanel(s_ubPickLeft, 80, s_ubPhase == PICK_LEFT);
	teamselDrawPanel(s_ubPickRight, 240, s_ubPhase == PICK_RIGHT);

	// VS
	fontDrawStr(
		g_pGameFont, pBack, GAME_SCREEN_WIDTH / 2, 100, "VS", COLOR_ACCENT,
		FONT_COOKIE | FONT_HCENTER | FONT_VCENTER, s_pTextBitMap
	);
}

void gsTeamSelCreate(void) {
	logBlockBegin("gsTeamSelCreate");

	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_DONE
	);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, GAME_BPP,
		TAG_DONE
	);
	gamePaletteLoad(s_pVPort);

	s_pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
		TAG_DONE
	);

	s_pTextBitMap = fontCreateTextBitMap(320, g_pGameFont->uwHeight + 8);

	s_ubPhase = PICK_LEFT;
	s_ubPickLeft = g_sMatchConfig.ubTeamLeft;
	s_ubPickRight = g_sMatchConfig.ubTeamRight;
	teamselFindPickers();

	teamselDrawPage(s_pBfr->pBack);
	teamselDrawPage(s_pBfr->pFront);

	viewLoad(s_pView);
	logBlockEnd("gsTeamSelCreate");
	systemUnuse();
}

void gsTeamSelLoop(void) {
	if(keyUse(KEY_ESCAPE)) {
		sfxPlay(SFX_BACK, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	// Per-frame off-page redraw - pages swap every frame
	teamselDrawPage(s_pBfr->pBack);

	UBYTE ubDevice = (s_ubPhase == PICK_LEFT) ? s_ubDevicePicker : s_ubDevicePickerRight;

	if(keyUse(KEY_LEFT) || keyUse(KEY_RIGHT) || keyUse(KEY_A) || keyUse(KEY_D) || joyUse(JOY1_LEFT) || joyUse(JOY1_RIGHT)) {
		UBYTE ubRight = keyCheck(KEY_RIGHT) || keyCheck(KEY_D) || joyCheck(JOY1_RIGHT);
		BYTE bDir = ubRight ? 1 : -1;
		UBYTE *pPick = (s_ubPhase == PICK_LEFT) ? &s_ubPickLeft : &s_ubPickRight;
		*pPick = (*pPick + bDir + TEAM_COUNT) % TEAM_COUNT;
		sfxPlay(SFX_MOVE, 48);
	}
	else if(keyUse(KEY_RETURN) || keyUse(KEY_X) || joyUse(JOY1_FIRE)) {
		if(s_ubPhase == PICK_LEFT) {
			g_sMatchConfig.ubTeamLeft = s_ubPickLeft;
			s_ubPhase = PICK_RIGHT;
			sfxPlay(SFX_CONFIRM, 64);
		}
		else {
			g_sMatchConfig.ubTeamRight = s_ubPickRight;
			sfxPlay(SFX_CONFIRM, 64);
			stateChange(g_pGameStateManager, &g_pGameStates[STATE_MATCH]);
			return;
		}
	}

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsTeamSelDestroy(void) {
	systemUse();
	logBlockBegin("gsTeamSelDestroy");
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsTeamSelDestroy");
}
