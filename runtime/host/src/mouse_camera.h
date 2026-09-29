#ifndef BLUEWAKE_MOUSE_CAMERA_H
#define BLUEWAKE_MOUSE_CAMERA_H

#include "core/cpu.h"
#include "gxruntime/platform.h"

// Mouse camera (the Mac host): click the game window to hand it the mouse,
// then moving it turns the camera around Link, left and right and up and
// down, and left click is A; Esc, or leaving the window, gives the mouse back. The mouse turns the
// game's own camera (its view angles), so walls, the stick's direction and the
// camera's easing behind Link all carry on as they do.
//
// In first person (C-stick up) and when aiming an item (bow, hookshot,
// boomerang, grappling hook, telescope, Picto Box) the mouse aims instead:
// it turns Link's own aim, one to one and within the game's limits, and the
// view follows it with no lag; the stick still aims as well.
//
// The wheel zooms: in third person it brings the follow camera in or out
// (0.5x to 2x its distance, kept until changed); in the telescope and the
// Picto Box it is their own 1x-9x zoom, a step a notch.
//
//   BLUEWAKE_MOUSE_CAMERA=0              off
//   BLUEWAKE_MOUSE_SENSITIVITY=1.0       degrees per point of mouse travel, scaled
//   BLUEWAKE_MOUSE_INVERT_Y=1            moving the mouse forward looks down
//   BLUEWAKE_MOUSE_TRACE=1               log the camera's angles (and the aim's)
//   BLUEWAKE_MOUSE_TEST=r:dx:dy:n[:wheel],...  testing: motion per retrace from r for n
//   BLUEWAKE_MOUSE_TEST_ITEM=0x27[@900]  testing: that item on X (see grant_test_item)

// Once, after the Aurora window exists.
void bluewake_mouse_camera_install(void);
// Once the guest is running (the camera's state is in its memory).
void bluewake_mouse_camera_attach(CPUState* cpu);
// Once per retrace.
void bluewake_mouse_camera_retrace(void);
// At every dispatch boundary (the chassis edge service): at camera_draw's
// entry the frame's camera is final, and the mouse's view and zoom are
// applied; at the player's update (daPy_Execute) the mouse's aim is.
void bluewake_mouse_camera_dispatch(CPUState* cpu, u32 address);
// On every pad read, on channel 0's live state: left click is A.
void bluewake_mouse_camera_pad(DolPadState* pad);
// Whether the mouse is the camera now (Esc gives it back), and giving it back.
bool bluewake_mouse_camera_captured(void);
void bluewake_mouse_camera_release(void);
// Reads BLUEWAKE_MOUSE_CAMERA, _SENSITIVITY and _INVERT_Y again (the options menu).
void bluewake_mouse_camera_reload(void);

#endif
