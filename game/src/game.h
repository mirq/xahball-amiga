/* XAHBall - HaxBall clone for Amiga 500, built with ACE (Amiga C Engine) */
#ifndef _XAHBALL_GAME_H_
#define _XAHBALL_GAME_H_

#include <ace/managers/state.h>
#include <ace/utils/extview.h>
#include <ace/utils/font.h>

//---------------------------------------------------------------------- DEFINES

#define GAME_BPP 4
#define GAME_SCREEN_WIDTH 320
#define GAME_SCREEN_HEIGHT 256

// Menu palette: flag/disc color indices (gamePaletteLoad) - used by menus,
// team select, winner screen and as RGB source for per-country ball bands
#define COLOR_BLACK 0
#define COLOR_WHITE 1
#define COLOR_PITCH 2
#define COLOR_PITCH_DARK 3
#define COLOR_BG 4
#define COLOR_GRAY 5
#define COLOR_RED 6
#define COLOR_MAROON 7
#define COLOR_ORANGE 8
#define COLOR_YELLOW 9
#define COLOR_GREEN 10
#define COLOR_AZURE 12
#define COLOR_NAVY 13
#define COLOR_ACCENT 14
#define COLOR_AMBER 15

// Match palette: dedicated indices (matchPaletteLoad). Slots 4..5 and 9..11
// are dynamic (written at match start / kickoff). Menu palette indices above
// MUST NOT be used by match-state drawing - they alias differently there.
// NOTE: bitmap_conv maps sprite pixels to palette entries by COLOR VALUE, so
// every dynamic slot uses a unique placeholder RGB in matchPaletteLoad -
// duplicates (like black/white) would collapse onto the static slots.
#define MCOLOR_BLACK 0
#define MCOLOR_WHITE 1
#define MCOLOR_PITCH 2
#define MCOLOR_STADIUM 3
#define MCOLOR_KIT_A 4       // dynamic: left team kit fill
#define MCOLOR_KIT_B 5       // dynamic: right team kit fill
#define MCOLOR_HUD_GRAY 6
#define MCOLOR_AMBER 7
#define MCOLOR_POST 8        // goal posts / goal frame
#define MCOLOR_ACCENT 12
#define MCOLOR_GRAY_D 13
#define MCOLOR_GRAY_L 14
#define MCOLOR_DARK 15

typedef enum tGameState {
	STATE_INTRO,
	STATE_MENU,
	STATE_SETUP,
	STATE_TEAMSEL,
	STATE_MATCH,
	STATE_WINNER,
	STATE_COUNT
} tGameState;

// Match time options (seconds)
#define TIME_OPTION_COUNT 4
extern const UWORD g_uwTimeOptions[TIME_OPTION_COUNT];

// Player slots: 0..2 = team A (left), 3..5 = team B (right)
#define SLOTS_PER_SIDE 3
#define SLOT_COUNT 6

typedef struct _tMatchConfig {
	UBYTE ubPlayersPerSide; // 1..3
	UBYTE ubTimeIdx;        // index into g_uwTimeOptions
	UBYTE ubSlotDevice[SLOT_COUNT];
	UBYTE ubTeamLeft;       // team index (see teams.h)
	UBYTE ubTeamRight;
	UBYTE ubBallFreeze;     // 1: ball freezes on goal instead of rolling in
	UBYTE ubKickoffLock;    // 1: non-kickoff team barred from center circle
} tMatchConfig;

//------------------------------------------------------------------------ TYPES

//---------------------------------------------------------------------- GLOBALS

extern tStateManager *g_pGameStateManager;
extern tState g_pGameStates[STATE_COUNT];
extern tMatchConfig g_sMatchConfig;
extern tFont *g_pGameFont; // silkscreen, shared by all states

// Last match results, used by winner state
extern UWORD g_uwScoreLeft;
extern UWORD g_uwScoreRight;

//-------------------------------------------------------------------- FUNCTIONS

void genericCreate(void);
void genericProcess(void);
void genericDestroy(void);

void createGameStates(void);
void destroyGameStates(void);

// Sets the 16-color menu palette on given viewport (menus, team select, winner)
void gamePaletteLoad(tVPort *pVPort);

// Sets the 16-color match palette on given viewport (pitch, kits, ball bands)
void matchPaletteLoad(tVPort *pVPort);


// Menu palette RGB table - RGB source for flag-derived dynamic colors
extern const UWORD g_pMenuPalette[16];

#endif // _XAHBALL_GAME_H_
