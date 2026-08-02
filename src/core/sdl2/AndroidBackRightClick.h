/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

// AC_BACK-to-right-click synthesis for touch devices without a physical
// right button: translates the Android back button into a right click at
// the last touch position (see TVPWindowWindow::window_receive_event_input()
// in SDLApplication.cpp, whose SDL_KEYDOWN/SDL_MOUSEBUTTONUP cases delegate
// to TVPBackRightClickPendingState below and to TVPPostBackRightClick()).
// Window state (last touch position, inner size, the native instance to
// post events to) is taken as parameters rather than reaching into
// TVPWindowWindow, so this file has no dependency back on that class. A
// plain function over a callback into TVPWindowWindow: every value it
// needs is read once at the call site before any of the clamping or
// event-posting logic runs, so there is no point mid-call where calling
// back into TVPWindowWindow would do anything a parameter couldn't.
#pragma once

#ifdef __ANDROID__

#include "tjsCommHead.h"

class tTJSNI_Window;

// Whether an AC_BACK-synthesized right click is waiting for the left mouse
// button (held during the touch drag that produced the AC_BACK) to release
// before it can fire -- see the doc comment at TVPPostBackRightClick()'s
// call sites in SDLApplication.cpp for why the click cannot post
// immediately in that case. A named wrapper around the pending flag rather
// than a bare bool so call sites read as intent (Arm/Reset) instead of raw
// assignment.
class TVPBackRightClickPendingState
{
public:
	bool IsPending() const { return pending; }
	void Arm() { pending = true; }
	void Reset() { pending = false; }

private:
	bool pending = false;
};

// Posts the Down -> Click -> Up sequence (in that order; see the doc
// comment at this function's call sites for why) that synthesizes a right
// click at the window's last touch position, clamped into
// [0, innerWidth) x [0, innerHeight) so a touch that landed in the
// letterbox black band still lands inside the game's hit-test area.
void TVPPostBackRightClick(tTJSNI_Window *nativeInstance, int lastMouseX, int lastMouseY,
	int innerWidth, int innerHeight, tjs_uint32 s);

#endif // __ANDROID__
