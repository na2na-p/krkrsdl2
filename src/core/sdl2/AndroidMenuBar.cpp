/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

#include "AndroidMenuBar.h"

#ifdef __ANDROID__

#include <jni.h>
#include "AndroidJNIStaticMethod.h"

namespace
{
	// left/top/right/bottom safe-area insets in px (display cutout ∪ system
	// bars), as reported by KirikiriSDL2Activity.getSafeAreaInsets().
	struct TVPSafeAreaInsets { int left = 0, top = 0, right = 0, bottom = 0; };

	// Resolves and calls Activity.getSafeAreaInsets() via JNI, using the
	// shared TVPJNIActivityMethodResolver (AndroidJNIStaticMethod.h) for
	// env/Activity/class resolution and fail-closed method lookup. Stays a
	// free function local to this file rather than exposed via
	// SystemImpl.h: it returns an int[4] rather than blocking on a dialog
	// result, and is only ever called from the render path below
	// (TVPGetCachedSafeAreaInsets), not from input handling.
	bool TVPFetchSafeAreaInsetsViaJNI(TVPSafeAreaInsets &out)
	{
		TVPJNIActivityMethodResolver resolver;
		if (!resolver.IsValid()) return false;
		JNIEnv *env = resolver.GetEnv();

		bool ok = false;
		jmethodID mid = resolver.GetStaticMethod("getSafeAreaInsets", "()[I");

		if (mid)
		{
			jintArray insets = (jintArray)env->CallStaticObjectMethod(resolver.GetActivityClass(), mid);
			if (env->ExceptionCheck())
			{
				env->ExceptionClear();
			}
			else if (insets && env->GetArrayLength(insets) >= 4)
			{
				jint buf[4];
				env->GetIntArrayRegion(insets, 0, 4, buf);
				out.left = buf[0];
				out.top = buf[1];
				out.right = buf[2];
				out.bottom = buf[3];
				ok = true;
			}
			if (insets) env->DeleteLocalRef(insets);
		}

		return ok;
	}

	// Caches the JNI result across TVPGetMenuBarRect() calls, which run once
	// per rendered frame while the bar is visible -- an unconditional JNI
	// round trip there would mean a JNI call every frame. The insets only
	// change on rotation or a cutout-mode change, and both of those already
	// change the letterbox geometry TVPGetMenuBarRect() computes from
	// (output size, or the destRect letterbox offsets), so keying the cache
	// on that same geometry re-fetches exactly when needed without a
	// dedicated Java-to-native invalidation callback.
	const TVPSafeAreaInsets &TVPGetCachedSafeAreaInsets(
		int output_w, int output_h, tjs_int destTop, tjs_int destLeft)
	{
		static TVPSafeAreaInsets cached;
		static bool cachedValid = false;
		static int cachedOutputW = -1, cachedOutputH = -1;
		static tjs_int cachedDestTop = -1, cachedDestLeft = -1;

		if (!cachedValid || cachedOutputW != output_w || cachedOutputH != output_h ||
			cachedDestTop != destTop || cachedDestLeft != destLeft)
		{
			TVPSafeAreaInsets fetched;
			if (TVPFetchSafeAreaInsetsViaJNI(fetched))
			{
				cached = fetched;
			}
			else if (!cachedValid)
			{
				cached = TVPSafeAreaInsets();
			}
			cachedOutputW = output_w;
			cachedOutputH = output_h;
			cachedDestTop = destTop;
			cachedDestLeft = destLeft;
			cachedValid = true;
		}
		return cached;
	}
}
bool TVPGetMenuBarRect(SDL_Renderer *renderer, tjs_int destTop, tjs_int destLeft, SDL_Rect &out)
{
	if (!renderer) return false;
	int output_w = 0, output_h = 0;
	SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
	if (output_w <= 0 || output_h <= 0) return false;

	// Thickness follows the letterbox band so the bar never overlaps the
	// rendered game frame, but is capped at 5% of the screen (floor 32px)
	// so it stays usable when the game fills the screen with no bars.
	auto cap = [](int screen_dim) {
		int c = screen_dim * 5 / 100;
		return c < 32 ? 32 : c;
	};

	// Fits the bar's thickness inside whatever band space is left once the
	// leading-edge safe-area inset is subtracted from the letterbox band
	// (bandAvailable), so that after the inset offset is applied to out.x/
	// out.y below, the bar's trailing edge still lands at or before the
	// band's own edge -- out.x + out.w <= destLeft for the landscape case,
	// out.y + out.h <= destTop for the portrait case -- preserving "never
	// overlaps the rendered game frame" from the comment above. When the
	// band itself is narrower than the cap floor (32px) -- the letterbox is
	// thinner than the safe-area inset, a rare device/orientation
	// combination -- honoring both "start past the inset" and "end within
	// the band" is impossible, so this keeps the 32px floor instead and
	// accepts a small overlap onto the game frame: a bar the user can still
	// find and tap outweighs one that stays clear of the game area but
	// sits fully under an obscuring cutout.
	auto fitWithinBand = [](int desired, int bandAvailable) {
		if (bandAvailable >= 32) return desired < bandAvailable ? desired : bandAvailable;
		return 32;
	};

	const TVPSafeAreaInsets &insets =
		TVPGetCachedSafeAreaInsets(output_w, output_h, destTop, destLeft);

	if (destTop > 0)
	{
		// Portrait letterbox (top/bottom black bars): bar sits at the screen
		// top, shifted below any cutout/status-bar inset so it isn't itself
		// clipped by the cutout; thickness is fit to what remains of the
		// band below that inset (see fitWithinBand above), so the bar still
		// never overlaps the rendered game frame except in that helper's
		// documented rare-device fallback.
		int h = fitWithinBand(cap(output_h), destTop - insets.top);
		out.x = 0;
		out.y = insets.top;
		out.w = output_w;
		out.h = h;
	}
	else if (destLeft > 0)
	{
		// Landscape letterbox (left/right black bars): bar sits at the
		// screen left edge -- not the right edge, so it stops colliding with
		// the right-edge back-gesture swipe area on right-handed grips --
		// shifted right past any left-edge cutout inset; width is fit to
		// what remains of the band past that inset (see fitWithinBand
		// above), with the same rare-device fallback.
		int w = fitWithinBand(cap(output_w), destLeft - insets.left);
		out.x = insets.left;
		out.y = 0;
		out.w = w;
		out.h = output_h;
	}
	else
	{
		// No letterbox: fall back to overlaying the screen top. There is no
		// black band to avoid overlapping here regardless of insets -- the
		// game already fills the screen, so this branch has always drawn
		// over it -- so this only clamps against running off the bottom
		// edge of the screen, not against a no-overlap invariant that does
		// not apply in this branch.
		int h = cap(output_h);
		if (insets.top + h > output_h) h = output_h - insets.top;
		if (h < 0) h = 0;
		out.x = 0;
		out.y = insets.top;
		out.w = output_w;
		out.h = h;
	}
	return true;
}
void TVPDrawMenuBar(SDL_Renderer *renderer, const SDL_Rect &bar)
{
	Uint8 pr, pg, pb, pa;
	SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);

	SDL_SetRenderDrawColor(renderer, 0x1E, 0x1E, 0x1E, 0xFF);
	SDL_RenderFillRect(renderer, &bar);

	// Hamburger icon (3 stacked bars). Computing the offset from the bar's
	// short axis and applying it to both x and y works for both
	// orientations without branching: it centers the icon on the short
	// axis and keeps it near the bar's leading edge on the long axis.
	int shortSide = bar.w < bar.h ? bar.w : bar.h;
	int iconSize = shortSide * 3 / 5;
	if (iconSize < 12) iconSize = shortSide < 12 ? shortSide : 12;
	if (iconSize > shortSide) iconSize = shortSide;
	int margin = (shortSide - iconSize) / 2;

	int lineThickness = iconSize / 6;
	if (lineThickness < 2) lineThickness = 2;
	int gap = (iconSize - lineThickness * 3) / 2;
	if (gap < 1) gap = 1;

	SDL_SetRenderDrawColor(renderer, 0xC8, 0xC8, 0xC8, 0xFF);
	for (int i = 0; i < 3; i++)
	{
		SDL_Rect line;
		line.x = bar.x + margin;
		line.y = bar.y + margin + i * (lineThickness + gap);
		line.w = iconSize;
		line.h = lineThickness;
		SDL_RenderFillRect(renderer, &line);
	}

	SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
}

#endif // __ANDROID__
