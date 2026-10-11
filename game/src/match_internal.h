/* XAHBall - internal shared declarations for the match state.
 * match.c (state shell), match_gameplay.c (rules/physics per-frame logic)
 * and match_render.c (sprites/HUD/banner/bobs) form one module split across
 * three files - this header is their private interface. NOT for other states.
 */
#ifndef _XAHBALL_MATCH_INTERNAL_H_
#define _XAHBALL_MATCH_INTERNAL_H_

#include <ace/managers/bob.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/font.h>
#include <fixmath/fixmath.h>
#include "flatbody.h"
#include "flatworld.h"
#include "game.h"
#include "input_slots.h"
#include "pitch.h"

//---------------------------------------------------------------------- DEFINES

#define PLAYER_R 8
#define BALL_R 5
#define SPRITE_BOX 16 // disc sprite box size (matches 16x16 assets)

// Physics constants - HaxBall-flavored, tuned for 50Hz PAL + 288px pitch
#define PLAYER_ACCEL F16(0.15f)
#define KICK_ACCEL F16(0.10f)   // accel while charging a kick (HaxBall kickingAccel)
#define PLAYER_DAMP F16(0.94f)  // no hard speed cap: top speed emerges ~2.5 px/f
#define BALL_DAMP F16(0.99f)
#define SAFETY_MAXV F16(6.0f)   // per-axis hard clamp, unreachable in normal play
#define DIAG_SCALE F16(0.7071f) // normalized diagonal input (no sqrt2 speed boost)
#define KICK_POWER F16(4.5f)    // HaxBall-ratio punchy shot
#define KICK_BACK F16(0.35f)    // shooter recoil
#define KICK_RANGE_EXTRA 4      // kick range: PLAYER_R + BALL_R + this
#define BALL_MASS 1   // player mass 2, ball mass 1 (HaxBall)
#define PLAYER_MASS 2
#define BALL_BOUNCE F16(1.0f)   // disc-pair restitution = bounceA * bounceB
#define PLAYER_BOUNCE F16(0.5f)
#define PLAYER_WALL_BOUNCE F16(0.0f) // players slide along walls, never bounce
#define BALL_WALL_BOUNCE F16(1.0f)   // * world WALL_BOUNCE 0.7 => 0.7 effective
#define BALL_MAX_SPEED F16(4.75f) // post-kick speed cap: bounds 50 Hz travel per frame
#define KICK_COOLDOWN 8
#define KICK_ANIM_FRAMES 8
#define KICK_PULSE_FRAMES 2     // AI kick press length within a 16-frame cycle

#define KICKOFF_FRAMES 150
#define GOAL_FRAMES 140
#define FULLTIME_FRAMES 130

//------------------------------------------------------------------------ TYPES

typedef enum tMatchPhase {
	PHASE_KICKOFF,
	PHASE_PLAY,
	PHASE_GOAL,
	PHASE_FULLTIME,
	PHASE_PAUSED
} tMatchPhase;

typedef struct _tMatchPlayer {
	tFlatBody sBody;
	UBYTE ubSlot;        // index into g_sMatchConfig.ubSlotDevice
	UBYTE ubSide;        // 0 left, 1 right
	UBYTE ubActive;
	UBYTE ubRole;        // AI: AI_ROLE_*
	UBYTE ubKickPrev;    // kick held last frame (edge detection)
	UBYTE ubKickConsumed; // current kick press already fired
	UBYTE ubKickPressed; // kick armed (held, not yet fired this press)
	UWORD uwKickCooldown;
	UWORD uwKickAnim;
	tBob sBob;
	tInputState sIn;
} tMatchPlayer;

//---------------------------------------------------------------------- GLOBALS
// Defined in match.c; gameplay and render both operate on this shared state.

extern tView *s_pView;
extern tVPort *s_pVPort;
extern tSimpleBufferManager *s_pBfr;
extern tTextBitMap *s_pTextBitMap;

extern tMatchPlayer s_pPlayers[SLOT_COUNT];
extern tFlatBody s_sBallBody;
extern tBob s_sBallBob;
extern tFlatWorld s_sWorld;

extern tMatchPhase s_ubPhase;
extern UWORD s_uwPhaseTimer;
extern ULONG s_ulFramesLeft;  // match time remaining in frames
extern UBYTE s_ubGoalScorer;  // side that scored the pending goal
extern UBYTE s_ubKickoffSide; // side that kicks off (conceding team after a goal)
extern UBYTE s_ubKickoffLocked; // 1 until the kickoff team first touches the ball
extern UWORD s_uwFrameTick;

// Active player slots: team A occupies 0..n-1, team B occupies 3..3+n-1
extern UBYTE s_pActiveSlots[2 * SLOTS_PER_SIDE];
extern UBYTE s_ubActiveCount;

//-------------------------------------------------------------------- FUNCTIONS
// match_gameplay.c

// Formations and resets
void matchResetPositions(void);

// Per-frame play-phase logic
void matchAssignAiRoles(void);
void matchUpdatePlayer(tMatchPlayer *pP);
void matchUpdateBallVelocity(void);
void matchCaptureBallBaseline(void);
void matchTryKicks(void);
void matchLimitBallVelocity(void);
void matchBallBounceSfx(void);
void matchCheckGoal(void);
void matchEnforceKickoffLock(void);

// Kickoff lock bookkeeping: true when the world step moved the resting ball
UBYTE matchKickoffTouchHappened(void);

// Goal celebration per-frame step (ball damping + world step + bounce sfx)
void matchGoalCelebrationStep(void);

// Match-start reset of gameplay-private bookkeeping
void matchGameplayReset(void);

// match_render.c

// Kit palette slots (with clash resolution) - call before viewLoad
void matchAssignKits(void);

// Sprite load/destroy for the numbered kit discs and the ball
void matchRenderLoadSprites(void);

// Frees all render-private resources (dbg bg + sprites)
void matchRenderDestroy(void);

// Bob registration (bobManagerCreate must run first) + buffer realloc
void matchRenderInitBobs(void);

// Per-page bookkeeping captures (physical buffer identities for trackers)
void matchRenderInitPages(void);
// Snapshot of the clean overlay-strip region (input debug bar) for restore
void matchRenderCaptureDbgBg(void);

// F1 overlay toggle: redraw/restore handled internally
void matchRenderToggleInputDebug(void);
UBYTE matchInputDebugActive(void);

// Per-frame rendering
void matchDrawHudStatic(tBitMap *pTarget); // once per page at create
void matchDrawHud(void);
void matchSyncBobPositions(void);
void matchDrawBanner(void);
void matchDrawInputDebug(void);

#endif // _XAHBALL_MATCH_INTERNAL_H_
