/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "AndroidBackRightClick.h"

#ifdef __ANDROID__

#include "WindowImpl.h"

void TVPPostBackRightClick(tTJSNI_Window *nativeInstance, int lastMouseX, int lastMouseY,
	int innerWidth, int innerHeight, tjs_uint32 s)
{
	// lastMouseX/Y are last touch position translated into draw-area
	// (inner-resolution) coordinates; a touch that landed in the
	// letterbox black band translates to a coordinate outside
	// [0, innerWidth)x[0, innerHeight) (TranslateWindowToDrawArea does not
	// clamp), which the game's input layer -- sized to the inner
	// resolution -- never hit-tests against, silently swallowing the
	// synthesized click. Clamp a local copy only: TVPWindowWindow's
	// lastMouseX/Y themselves must keep reflecting the real last-known
	// pointer position for other callers (e.g. GetCursorPos()'s no-focus
	// fallback), not the letterbox-band value force-fit into the game area
	// for this one right-click synthesis.
	int x = lastMouseX;
	int y = lastMouseY;
	if (innerWidth > 0)
	{
		if (x < 0) x = 0;
		else if (x > innerWidth - 1) x = innerWidth - 1;
	}
	if (innerHeight > 0)
	{
		if (y < 0) y = 0;
		else if (y > innerHeight - 1) y = innerHeight - 1;
	}

	// Real single-touch releases go MouseMove -> Click -> MouseUp (see the
	// SDL_MOUSEBUTTONUP case in TVPWindowWindow::window_receive_event_input(),
	// SDLApplication.cpp), not Down -> Up -> Click: tTVPOnClickInputEvent's
	// PrimaryClick only fires while still holding the button's CaptureOwner,
	// i.e. before the matching Up runs. Ordering the synthesized press the
	// same way (here: Down -> Click -> Up) is required for the same reason,
	// not just for consistency with real presses.
	TVPPostInputEvent(new tTVPOnMouseDownInputEvent(nativeInstance, x, y, tTVPMouseButton::mbRight, s));
	TVPPostInputEvent(new tTVPOnClickInputEvent(nativeInstance, x, y));
	TVPPostInputEvent(new tTVPOnMouseUpInputEvent(nativeInstance, x, y, tTVPMouseButton::mbRight, s));
}

#endif // __ANDROID__
