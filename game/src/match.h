/* XAHBall - match state */
#ifndef _XAHBALL_MATCH_H_
#define _XAHBALL_MATCH_H_

//---------------------------------------------------------------------- DEFINES

//------------------------------------------------------------------------ TYPES

//---------------------------------------------------------------------- GLOBALS

//-------------------------------------------------------------------- FUNCTIONS

void gsMatchCreate(void);
void gsMatchLoop(void);
void gsMatchDestroy(void);

// Attract mode: saves the player's match setup, switches to an all-AI match
// (random teams and team size, HARD bots, short clock) and changes state to
// the match. Any key / fire button or the final whistle returns to the menu
// with the saved setup restored.
void matchStartDemo(void);

#endif // _XAHBALL_MATCH_H_
