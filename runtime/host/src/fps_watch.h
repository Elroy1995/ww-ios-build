#ifndef BLUEWAKE_FPS_WATCH_H
#define BLUEWAKE_FPS_WATCH_H

#include "core/cpu.h"

// A [fps-dip] line for every second of wall time in which fewer than 57 frames
// reached the screen: the frames shown, the game's speed, how many game frames
// got an in-between frame, the draws rejected or unmatched, the stage, room and
// Link's position, and why (the game below full speed, frames not
// interpolated, or presents late). BLUEWAKE_FPS_WATCH=0 turns it off.

void bluewake_fps_watch_attach(CPUState* cpu);
// Once per retrace.
void bluewake_fps_watch_retrace(void);

#endif
