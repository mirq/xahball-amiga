/* XAHBall - menu music (ptplayer). The module ("triangles" by X-Type)
 * carries ~225 KB of chip samples, so it is loaded ONCE - on the
 * first musicPlay(), after the intro freed its 5bpp buffers - and stays
 * resident; reloading it from floppy on every menu entry would take seconds.
 */
#ifndef _XAHBALL_MUSIC_H_
#define _XAHBALL_MUSIC_H_

// Starts the menu tune from the top (loads it first if needed). Must be
// called while the OS is alive (state create) - the first call does disk I/O.
void musicPlay(void);

// Stops replay; the module stays loaded
void musicStop(void);

// Frees the module + chip samples (game exit)
void musicDestroy(void);

#endif // _XAHBALL_MUSIC_H_
