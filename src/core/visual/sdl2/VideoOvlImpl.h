//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Video Overlay support implementation
//---------------------------------------------------------------------------
#ifndef VideoOvlImplH
#define VideoOvlImplH
//---------------------------------------------------------------------------
#include "tjsNative.h"
#include "WindowIntf.h"

#include "VideoOvlIntf.h"
#include "StorageIntf.h"
#include "UtilStreams.h"

#include "voMode.h"

#include "NativeEventQueue.h"

#ifdef __ANDROID__
// Forward declarations only: pl_mpeg.h and FAudio.h are included solely by
// VideoOvlImpl.cpp (the only translation unit that needs them), and SDL.h is
// kept out of this header to avoid pulling it into every file that includes
// VideoOvlIntf.h.
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Rect;
struct FAudioVoice; // audio playback goes through the shared FAudio engine's
                     // source voice API, not a second SDL audio device (see
                     // VideoOvlImpl.cpp for why)
#endif

//---------------------------------------------------------------------------
// tTJSNI_VideoOverlay : VideoOverlay Native Instance
//---------------------------------------------------------------------------
class iTVPVideoOverlay;
class tTJSNI_VideoOverlay : public tTJSNI_BaseVideoOverlay
{
	typedef tTJSNI_BaseVideoOverlay inherited;

	iTVPVideoOverlay *VideoOverlay;

	tTVPRect Rect;
	bool Visible;

#ifdef _WIN32
	HWND OwnerWindow;
#endif

	// HWND UtilWindow; // window which receives messages from video overlay object
	NativeEventQueue<tTJSNI_VideoOverlay> EventQueue;

	tTVPLocalTempStorageHolder *LocalTempStorageHolder;
	class tTJSNI_BaseLayer	*Layer1;
	class tTJSNI_BaseLayer	*Layer2;
	tTVPVideoOverlayMode	Mode;	//!< Modeの動的な変更は出来ない。open前にセットしておくこと
	bool	Loop;

#ifdef _WIN32
	class tTVPBaseBitmap	*Bitmap[2];	//!< Layer描画用バッファ用Bitmap
	BYTE			*BmpBits[2];
#endif

	bool	IsPrepare;			//!< 準備モードかどうか

	int		SegLoopStartFrame;	//!< セグメントループ開始フレーム
	int		SegLoopEndFrame;	//!< セグメントループ終了フレーム

	//! イベントが設定された時、現在フレームの方が進んでいたかどうか。
	//! イベントが設定されているフレームより前に現在フレームが移動した時、このフラグは解除される。
	bool	IsEventPast;
	int		EventFrame;		//!< イベントを発生させるフレーム

#ifdef __ANDROID__
	// The buffer handed to PlmDecoder is passed to plm_create_with_memory
	// with free_when_done=1, so plm_destroy() releases it; this class does
	// not keep its own pointer to it.
	void *PlmDecoder;			//!< plm_t*; kept as void* so this header stays pl_mpeg.h-free
	SDL_Texture *PlmTexture;	//!< render texture, lazily created on first draw
	tjs_uint8 *PlmRgbBuffer;
	// Not a second SDL audio device: Android's SDL aaudio/openslES backends
	// each track only one open output device in a single global, so opening
	// a second one here would break the app's background auto-pause
	// (SDL_PauseAudioDevice only stops the one it knows about). This adds a
	// source voice to the FAudio engine already opened for BGM/SE instead.
	FAudioVoice *PlmAudioVoice;
	bool PlmPlaying;
	bool PlmFrameDirty;
	tjs_uint64 PlmLastTickMs;
	tjs_int PlmVideoWidth;
	tjs_int PlmVideoHeight;
#endif

public:
	tTJSNI_VideoOverlay();
	tjs_error TJS_INTF_METHOD Construct(tjs_int numparams, tTJSVariant **param,
		iTJSDispatch2 *tjs_obj);
	void TJS_INTF_METHOD Invalidate();


public:
	void Open(const ttstr &name);
	void Close();
	void Shutdown();
	void Disconnect(); // tTJSNI_BaseVideoOverlay::Disconnect override

	void Play();
	void Stop();
	void Pause();
	void Rewind();
	void Prepare();

	void SetSegmentLoop( int comeFrame, int goFrame );
	void CancelSegmentLoop() { SegLoopStartFrame = -1; SegLoopEndFrame = -1; }
	void SetPeriodEvent( int eventFrame );

	void SetStopFrame( tjs_int f );
	void SetDefaultStopFrame();
	tjs_int GetStopFrame();

public:
	void SetRectangleToVideoOverlay();

	void SetPosition(tjs_int left, tjs_int top);
	void SetSize(tjs_int width, tjs_int height);
	void SetBounds(const tTVPRect & rect);

	void SetLeft(tjs_int l);
	tjs_int GetLeft() const { return Rect.left; }
	void SetTop(tjs_int t);
	tjs_int GetTop() const { return Rect.top; }
	void SetWidth(tjs_int w);
	tjs_int GetWidth() const { return Rect.get_width(); }
	void SetHeight(tjs_int h);
	tjs_int GetHeight() const { return Rect.get_height(); }

	void SetVisible(bool b);
	bool GetVisible() const { return Visible; }

	void SetTimePosition( tjs_uint64 p );
	tjs_uint64 GetTimePosition();

	void SetFrame( tjs_int f );
	tjs_int GetFrame();

	tjs_real GetFPS();
	tjs_int GetNumberOfFrame();
	tjs_int64 GetTotalTime();

	void SetLoop( bool b );
	bool GetLoop() const { return Loop; }

	void SetLayer1( tTJSNI_BaseLayer *l );
	tTJSNI_BaseLayer *GetLayer1() { return Layer1; }
	void SetLayer2( tTJSNI_BaseLayer *l );
	tTJSNI_BaseLayer *GetLayer2() { return Layer2; }

	void SetMode( tTVPVideoOverlayMode m );
	tTVPVideoOverlayMode GetMode() { return Mode; }

	tjs_real GetPlayRate();
	void SetPlayRate(tjs_real r);

	tjs_int GetSegmentLoopStartFrame() { return SegLoopStartFrame; }
	tjs_int GetSegmentLoopEndFrame() { return SegLoopEndFrame; }
	tjs_int GetPeriodEventFrame() { return EventFrame; }

	tjs_int GetAudioBalance();
	void SetAudioBalance(tjs_int b);
	tjs_int GetAudioVolume();
	void SetAudioVolume(tjs_int v);

	tjs_uint GetNumberOfAudioStream();
	void SelectAudioStream(tjs_uint n);
	tjs_int GetEnabledAudioStream();
	void DisableAudioStream();

	tjs_uint GetNumberOfVideoStream();
	void SelectVideoStream(tjs_uint n);
	tjs_int GetEnabledVideoStream();
	void SetMixingLayer( tTJSNI_BaseLayer *l );
	void ResetMixingBitmap();

	void SetMixingMovieAlpha( tjs_real a );
	tjs_real GetMixingMovieAlpha();
	void SetMixingMovieBGColor( tjs_uint col );
	tjs_uint GetMixingMovieBGColor();


	tjs_real GetContrastRangeMin();
	tjs_real GetContrastRangeMax();
	tjs_real GetContrastDefaultValue();
	tjs_real GetContrastStepSize();
	tjs_real GetContrast();
	void SetContrast( tjs_real v );

	tjs_real GetBrightnessRangeMin();
	tjs_real GetBrightnessRangeMax();
	tjs_real GetBrightnessDefaultValue();
	tjs_real GetBrightnessStepSize();
	tjs_real GetBrightness();
	void SetBrightness( tjs_real v );

	tjs_real GetHueRangeMin();
	tjs_real GetHueRangeMax();
	tjs_real GetHueDefaultValue();
	tjs_real GetHueStepSize();
	tjs_real GetHue();
	void SetHue( tjs_real v );

	tjs_real GetSaturationRangeMin();
	tjs_real GetSaturationRangeMax();
	tjs_real GetSaturationDefaultValue();
	tjs_real GetSaturationStepSize();
	tjs_real GetSaturation();
	void SetSaturation( tjs_real v );

	tjs_int GetOriginalWidth();
	tjs_int GetOriginalHeight();

	void ResetOverlayParams();
	void SetRectOffset(tjs_int ofsx, tjs_int ofsy);
	void DetachVideoOverlay();

#ifdef __ANDROID__
public:
	//! @brief Advances decode by the elapsed time since the previous call.
	//! @return true while playback is still ongoing (the caller uses this to
	//! decide whether a screen redraw is needed).
	bool PlmTick(tjs_uint64 nowMs);
	//! @brief Draws the current decoded frame. destRect/innerWidth/innerHeight
	//! come from the same transform TickBeat uses for the main game texture,
	//! mapping this overlay's game-resolution Rect into screen coordinates.
	void PlmRender(SDL_Renderer *renderer, const SDL_Rect &destRect, int innerWidth, int innerHeight);

	//! @brief Called by the pl_mpeg video decode callback (a file-scope
	//! trampoline in VideoOvlImpl.cpp). frame is really a plm_frame_t*, taken
	//! as void* because pl_mpeg.h typedefs it as an anonymous struct that
	//! cannot be forward-declared here.
	void PlmWriteVideoFrame(void *frame);
	//! @brief Called by the pl_mpeg audio decode callback; samples is really
	//! a plm_samples_t* (same reason as PlmWriteVideoFrame).
	void PlmQueueAudioSamples(void *samples);
#endif

private:
	void WndProc( NativeEvent& ev );
		// UtilWindow's window procedure
	void ClearWndProcMessages(); // clear WndProc's message queue

};
//---------------------------------------------------------------------------

#ifdef __ANDROID__
//! @brief Advances decode by one tick for every playing VideoOverlay instance.
//! @return true if any of them are still playing; the caller (TickBeat) uses
//! this to decide whether to set needsGraphicUpdate.
extern bool TVPPlmTickAll();
//! @brief Draws every playing, visible VideoOverlay instance.
extern void TVPPlmRenderAll(SDL_Renderer *renderer, const SDL_Rect &destRect,
	int innerWidth, int innerHeight);
#endif

#endif
