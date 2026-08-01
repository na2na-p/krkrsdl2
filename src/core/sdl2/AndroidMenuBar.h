/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

// Rendering/hit-rect geometry for the tap bar TVPWindowWindow shows in the
// game's letterbox band (see GetMenuBarRect()/DrawMenuBar() call sites in
// SDLApplication.cpp, which run this pair back-to-back each frame so the
// drawn bar and its hit-test rect never drift apart) and the safe-area-inset
// query that geometry is based on. Window geometry (renderer, the letterbox
// destRect's top/left) is taken as parameters rather than reaching into
// TVPWindowWindow, so this file has no dependency back on that class.
#pragma once

#ifdef __ANDROID__

#include <SDL.h>
#include "tjsCommHead.h"

// destTop/destLeft are TVPWindowWindow::LastSentDrawDeviceDestRect's
// top/left: the letterbox offset of the rendered game frame from the
// output's top-left origin.
bool TVPGetMenuBarRect(SDL_Renderer *renderer, tjs_int destTop, tjs_int destLeft, SDL_Rect &out);
void TVPDrawMenuBar(SDL_Renderer *renderer, const SDL_Rect &bar);

#endif // __ANDROID__
