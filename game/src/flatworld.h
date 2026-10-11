/* XAHBall - flat world: integrates bodies and runs the full collision set.
 * Same engine layout as amiga-shooter's FlatWorld, completed with real
 * collision response:
 *  - disc vs disc (mass-based impulse, restitution = product of bCoefs,
 *    positional correction split by inverse mass, static bodies immovable)
 *  - disc vs static disc (goal posts, stored in the world)
 *  - body vs pitch bounds: 1px walls, ROUNDED corner arcs, goal mouths and
 *    nets (per-body FLAT_WALL_* mode)
 *
 * Goal detection is deliberately NOT here - the match state watches the ball
 * center against the goal lines; the world only guarantees the ball never
 * leaves the pitch bounds.
 */
#ifndef _XAHBALL_FLATWORLD_H_
#define _XAHBALL_FLATWORLD_H_

#include "flatbody.h"
#include "pitch.h"

//---------------------------------------------------------------------- DEFINES

#define FLAT_WORLD_MAX_BODIES 8
#define FLAT_WORLD_POST_COUNT 4

//------------------------------------------------------------------------ TYPES

typedef struct tFlatWorld {
	tFlatBody *pBodies[FLAT_WORLD_MAX_BODIES];
	UBYTE ubBodyCount;
	tFlatBody pPosts[FLAT_WORLD_POST_COUNT]; // static discs at the mouth corners
} tFlatWorld;

//---------------------------------------------------------------------- FUNCTIONS

void flatWorldInit(tFlatWorld *pWorld);
void flatWorldAdd(tFlatWorld *pWorld, tFlatBody *pBody);

// One 50Hz tick: integrate all dynamic bodies, resolve bounds, posts and
// every body pair
void flatWorldStep(tFlatWorld *pWorld);

#endif // _XAHBALL_FLATWORLD_H_
