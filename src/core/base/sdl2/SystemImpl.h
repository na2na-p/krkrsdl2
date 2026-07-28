//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// "System" class implementation
//---------------------------------------------------------------------------
#ifndef SystemImplH
#define SystemImplH
//---------------------------------------------------------------------------
TJS_EXP_FUNC_DEF(bool, TVPGetAsyncKeyState, (tjs_uint keycode, bool getcurrent = true));
//---------------------------------------------------------------------------
extern void TVPPostApplicationActivateEvent();
extern void TVPPostApplicationDeactivateEvent();
extern bool TVPShellExecute(const ttstr &target, const ttstr &param);
//---------------------------------------------------------------------------
#ifdef __ANDROID__
// Android-only tap bar: System.setMenuBarCallback registration itself is
// cross-platform (see SystemImpl.cpp), but drawing/tap handling live in
// SDLApplication.cpp and need these to reach the registered closure.
extern bool TVPIsMenuBarCallbackRegistered();
extern void TVPPostMenuBarTapEvent();
// implemented in SDLApplication.cpp: forces a redraw on all windows so a
// newly registered/cleared bar becomes visible without waiting on the
// next unrelated screen update
extern void TVPRequestMenuBarRedraw();
#endif
//---------------------------------------------------------------------------
#endif
