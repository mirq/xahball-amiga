/* XAHBall - match state shell: view lifecycle, phase machine, create/destroy.
 * The per-frame gameplay logic lives in match_gameplay.c, everything that
 * renders into the double-buffered display in match_render.c; the shared
 * interface between the three files is match_internal.h.
 *
 * Architecture notes (glitch-killers, see also ai-agent-instructions.md):
 * - phase texts render in a dedicated banner strip (y 16..31) that no sprite
 *   box can reach, drawn on pBack during normal compose
 * - the HUD redraws per page only when its values change
 * - the ball never leaves bounds (flatworld guarantees) so bob positions only
 *   need clamping against the screen edge itself
 */
#include "match.h"

#include <ace/managers/bob.h>
#include <ace/managers/game.h>
#include <ace/managers/key.h>
#include <ace/managers/joy.h>
#include <ace/managers/log.h>
#include <ace/managers/state.h>
#include <ace/managers/system.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/extview.h>
#include <ace/utils/font.h>
#include <stdio.h>
#include "game.h"
#include "ai.h"
#include "input_slots.h"
#include "match_internal.h"
#include "pitch.h"
#include "sfx.h"

//---------------------------------------------------------------------- GLOBALS
// Shared match state (declared in match_internal.h)

tView *s_pView;
tVPort *s_pVPort;
tSimpleBufferManager *s_pBfr;
tTextBitMap *s_pTextBitMap;

tMatchPlayer s_pPlayers[SLOT_COUNT];
tFlatBody s_sBallBody;
tBob s_sBallBob;
tFlatWorld s_sWorld;

tMatchPhase s_ubPhase;
UWORD s_uwPhaseTimer;
ULONG s_ulFramesLeft;  // match time remaining in frames
UBYTE s_ubGoalScorer;  // side that scored the pending goal
UBYTE s_ubKickoffSide; // side that kicks off (conceding team after a goal)
UBYTE s_ubKickoffLocked; // 1 until the kickoff team first touches the ball
UWORD s_uwFrameTick;

UBYTE s_pActiveSlots[2 * SLOTS_PER_SIDE];
UBYTE s_ubActiveCount;

//-------------------------------------------------------------------- FUNCTIONS

// Team A slots are 0..n-1, team B slots are 3..3+n-1 (SLOTS_PER_SIDE apart!)
static void matchBuildActiveSlots(void) {
	UBYTE ubCount = g_sMatchConfig.ubPlayersPerSide;
	s_ubActiveCount = 0;
	for(UBYTE ubIdx = 0; ubIdx < ubCount; ++ubIdx) {
		s_pActiveSlots[s_ubActiveCount++] = ubIdx;                    // team A
	}
	for(UBYTE ubIdx = 0; ubIdx < ubCount; ++ubIdx) {
		s_pActiveSlots[s_ubActiveCount++] = SLOTS_PER_SIDE + ubIdx;   // team B
	}
}

void gsMatchCreate(void) {
	logBlockBegin("gsMatchCreate");

	s_pView = viewCreate(0,
		TAG_VIEW_GLOBAL_PALETTE, 1,
		TAG_DONE
	);
	s_pVPort = vPortCreate(0,
		TAG_VPORT_VIEW, s_pView,
		TAG_VPORT_BPP, GAME_BPP,
		TAG_DONE
	);
	matchPaletteLoad(s_pVPort);

	s_pBfr = simpleBufferCreate(0,
		TAG_SIMPLEBUFFER_VPORT, s_pVPort,
		TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
		TAG_SIMPLEBUFFER_IS_DBLBUF, 1,
		TAG_DONE
	);

	s_pTextBitMap = fontCreateTextBitMap(320, g_pGameFont->uwHeight + 8);
	matchRenderInitPages();

	// Kits with clash resolution; palette writes land in the viewport
	// palette before viewLoad applies them
	matchAssignKits();
	matchRenderLoadSprites();

	// Init players + active slot list
	UBYTE ubCount = g_sMatchConfig.ubPlayersPerSide;
	matchBuildActiveSlots();
	for(UBYTE ubI = 0; ubI < SLOT_COUNT; ++ubI) {
		s_pPlayers[ubI].ubSlot = ubI;
		s_pPlayers[ubI].ubSide = ubI / SLOTS_PER_SIDE;
		s_pPlayers[ubI].ubActive = (ubI % SLOTS_PER_SIDE) < ubCount;
		s_pPlayers[ubI].ubRole = AI_ROLE_DEFEND;
		s_pPlayers[ubI].ubKickPrev = 0;
		s_pPlayers[ubI].ubKickConsumed = 0;
		s_pPlayers[ubI].ubKickPressed = 0;
		s_pPlayers[ubI].uwKickCooldown = 0;
		s_pPlayers[ubI].uwKickAnim = 0;
		flatBodyInit(
			&s_pPlayers[ubI].sBody, PITCH_CX, PITCH_CY, PLAYER_R, PLAYER_MASS,
			PLAYER_BOUNCE, PLAYER_WALL_BOUNCE, 0
		);
		s_pPlayers[ubI].sBody.ubWallMode = FLAT_WALL_PLAYER;
	}
	flatBodyInit(
		&s_sBallBody, PITCH_CX, PITCH_CY, BALL_R, BALL_MASS,
		BALL_BOUNCE, BALL_WALL_BOUNCE, 0
	);
	s_sBallBody.ubWallMode = FLAT_WALL_PLAY;

	// World owns integration + the full collision pass; only ACTIVE player
	// bodies take part (inactive slots must not collide with anything)
	flatWorldInit(&s_sWorld);
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		flatWorldAdd(&s_sWorld, &s_pPlayers[s_pActiveSlots[ubI]].sBody);
	}
	flatWorldAdd(&s_sWorld, &s_sBallBody);
	matchResetPositions();

	// BOB manager on both buffers, then register bobs
	bobManagerCreate(s_pBfr->pFront, s_pBfr->pBack, GAME_SCREEN_HEIGHT);
	matchRenderInitBobs();

	// Match state
	g_uwScoreLeft = 0;
	g_uwScoreRight = 0;
	s_ulFramesLeft = (ULONG)g_uwTimeOptions[g_sMatchConfig.ubTimeIdx] * 50;
	s_ubPhase = PHASE_KICKOFF;
	s_uwPhaseTimer = KICKOFF_FRAMES;
	s_ubGoalScorer = 0;
	s_ubKickoffSide = 0; // left team kicks off the match
	s_ubKickoffLocked = 1;
	s_uwFrameTick = 0;
	matchGameplayReset();
	// Fresh input world per game: the key state table is only ever written
	// on key events, so a release lost in a previous game (or menu) would
	// carry a stuck direction into this match until that key is pressed
	// again. Wipe it - anything physically held right now re-asserts on the
	// next event anyway. Bookkeeping (heal edges, joy latches) follows.
	keyReset();
	inputResetBookkeeping();

	// Static layers on both pages: pitch below, HUD + banner strip on top
	pitchDrawBg(s_pBfr->pBack);
	pitchDrawBg(s_pBfr->pFront);
	matchDrawHudStatic(s_pBfr->pBack);
	matchDrawHudStatic(s_pBfr->pFront);
	matchRenderCaptureDbgBg();

	viewLoad(s_pView);
	logBlockEnd("gsMatchCreate");
	systemUnuse();
}

void gsMatchLoop(void) {
	// Sync bob double-buffer index after simplebuffer page swap
	bobSetCurrentBuffer(s_pBfr->pBack);
	++s_uwFrameTick;

	// Input-state overlay toggle - works in every phase
	if(keyUse(KEY_F1)) {
		matchRenderToggleInputDebug();
	}

	// Pause handling during play
	if(s_ubPhase == PHASE_PLAY || s_ubPhase == PHASE_PAUSED) {
		if(keyUse(KEY_ESCAPE) || keyUse(KEY_P)) {
			if(s_ubPhase == PHASE_PLAY) {
				s_ubPhase = PHASE_PAUSED;
				sfxPlay(SFX_BACK, 64);
			}
			else {
				// ESC while paused quits to menu
				sfxPlay(SFX_BACK, 64);
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
				return;
			}
		}
		else if(s_ubPhase == PHASE_PAUSED && (keyUse(KEY_RETURN) || joyUse(JOY1_FIRE))) {
			s_ubPhase = PHASE_PLAY;
			sfxPlay(SFX_CONFIRM, 64);
		}
	}
	else if(keyUse(KEY_ESCAPE)) {
		// ESC during kickoff/goal/fulltime quits to menu
		sfxPlay(SFX_BACK, 64);
		stateChange(g_pGameStateManager, &g_pGameStates[STATE_MENU]);
		return;
	}

	switch(s_ubPhase) {
		case PHASE_KICKOFF:
			--s_uwPhaseTimer;
			if(!s_uwPhaseTimer) {
				sfxPlay(SFX_WHISTLE, 64);
				s_ubPhase = PHASE_PLAY;
			}
			break;
		case PHASE_PLAY: {
#ifdef XAHBALL_AUTOTEST
			// Automated time-up path probe: shorten the match clock once play
			// starts so FULL TIME -> winner happens naturally in ~6s, and draw
			// the kickoff-lock state + remaining clock in the banner strip.
			// L1 = clock held (ball untouched), L0 = clock running.
			{
				static UWORD s_uwAutoQuit = 0;
				++s_uwAutoQuit;
				if(s_uwAutoQuit == 10) {
					s_ulFramesLeft = 300;
				}
				static char szAuto[24];
				sprintf(szAuto, "L%u  C%u", s_ubKickoffLocked,
					(UWORD)(s_ulFramesLeft / 50));
				fontDrawStr(
					g_pGameFont, s_pBfr->pBack, 160, BANNER_Y0 + 2, szAuto,
					MCOLOR_AMBER, FONT_COOKIE | FONT_HCENTER, s_pTextBitMap
				);
			}
#endif
			matchAssignAiRoles();
			for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
				matchUpdatePlayer(&s_pPlayers[s_pActiveSlots[ubI]]);
			}
			matchUpdateBallVelocity();
			matchTryKicks();
			matchLimitBallVelocity();
			matchCaptureBallBaseline();

			// Full physics pass: integrate + walls/corners/nets + posts +
			// every body pair (mass-based impulses)
			flatWorldStep(&s_sWorld);
			matchBallBounceSfx();

			// Kickoff lock: release the circle exclusion on the first touch
			// (kick release happens in matchTryKicks already)
			if(s_ubKickoffLocked && matchKickoffTouchHappened()) {
				s_ubKickoffLocked = 0;
			}
			matchEnforceKickoffLock();

			// Goal: ball CENTER crossing the goal line inside the mouth
			matchCheckGoal();

			// Match clock: runs only in play AND only from the first ball touch
			// of each kickoff onward - the kickoff wait never eats match time.
			// s_ubKickoffLocked releases on any ball movement (kick or contact)
			// earlier this frame, so the clock starts the instant play begins.
			if(s_ubPhase == PHASE_PLAY && !s_ubKickoffLocked) {
				if(s_ulFramesLeft) {
					--s_ulFramesLeft;
				}
				if(!s_ulFramesLeft) {
					sfxPlay(SFX_WHISTLE, 64);
					s_ubPhase = PHASE_FULLTIME;
					s_uwPhaseTimer = FULLTIME_FRAMES;
				}
			}
			break;
		}
		case PHASE_GOAL:
			// Celebration: players freeze, ball rolls into the net
			matchGoalCelebrationStep();
			--s_uwPhaseTimer;
			if(!s_uwPhaseTimer) {
				// Conceding side kicks off
				s_sBallBody.ubWallMode = FLAT_WALL_PLAY;
				matchResetPositions();
				s_ubPhase = PHASE_KICKOFF;
				s_uwPhaseTimer = KICKOFF_FRAMES;
			}
			break;
		case PHASE_FULLTIME:
			--s_uwPhaseTimer;
			if(!s_uwPhaseTimer) {
				stateChange(g_pGameStateManager, &g_pGameStates[STATE_WINNER]);
				return;
			}
			break;
		default:
			break;
	}

	// Render: HUD first (sprite-free zone), then bobs, then banner text,
	// then the optional input overlay LAST - after the bob pass, so the
	// erase bar can't get baked into any bob save/undraw buffer
	matchDrawHud();
	matchSyncBobPositions();
	bobBegin(s_pBfr->pBack);
	for(UBYTE ubI = 0; ubI < s_ubActiveCount; ++ubI) {
		bobPush(&s_pPlayers[s_pActiveSlots[ubI]].sBob);
	}
	bobPush(&s_sBallBob);
	bobEnd();
	matchDrawBanner();
	if(matchInputDebugActive()) {
		matchDrawInputDebug();
	}

	viewProcessManagers(s_pView);
	copProcessBlocks();
	vPortWaitForEnd(s_pVPort);
}

void gsMatchDestroy(void) {
	systemUse();
	logBlockBegin("gsMatchDestroy");
	// NOTE: no sfx channel cleanup here. Samples are short (<=1.4s) and the
	// CIA interrupt keeps mixing them across state transitions by design -
	// AMIner (reference ACE game) never touches channels on exits either.
	// ptplayerSfxStopOnChannel() parks a channel with wordLength=1, which
	// makes chan_sfx_only() call startSfx() on EVERY tick forever: the
	// channel stays occupied at priority 1 and DMA-toggles 50x/s (audible on
	// real hardware). ptplayer itself stays up - it is app-global.
	bobManagerDestroy();
	matchRenderDestroy();
	fontDestroyTextBitMap(s_pTextBitMap);
	viewDestroy(s_pView);
	s_pView = 0;
	logBlockEnd("gsMatchDestroy");
}
