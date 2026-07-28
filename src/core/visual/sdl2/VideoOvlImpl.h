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
// Forward declarations only: pl_mpeg.h is included solely by VideoOvlImpl.cpp
// (the only translation unit that needs the decoder), and SDL.h is kept out
// of this header to avoid pulling it into every file that includes
// VideoOvlIntf.h.
struct SDL_Renderer;
struct SDL_Texture;
struct SDL_Rect;
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
	// PlmDecoderに渡したファイル全体のバッファはplm_create_with_memoryへ
	// free_when_done=1で渡しており、plm_destroy()が解放するためこのクラスでは
	// ポインタを保持しない
	void *PlmDecoder;			//!< plm_t*。このヘッダをpl_mpeg.h非依存に保つためvoid*で保持
	SDL_Texture *PlmTexture;	//!< 描画用テクスチャ。初回描画時に遅延生成
	tjs_uint8 *PlmRgbBuffer;
	unsigned int PlmAudioDevice;	//!< SDL_AudioDeviceID（SDL2ではUint32のtypedef）
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
	//! @brief 前回呼び出しからの経過時間分デコードを進める
	//! @return 再生継続中ならtrue（呼び出し側が画面再描画要否を判断するために使う）
	bool PlmTick(tjs_uint64 nowMs);
	//! @brief 現在のデコード済みフレームを描画する
	//! destRect/innerWidth/innerHeightはTickBeatがゲーム画面テクスチャに使うものと
	//! 同じ変換元で、ゲーム解像度で保持しているRectを画面座標へ写像するために使う
	void PlmRender(SDL_Renderer *renderer, const SDL_Rect &destRect, int innerWidth, int innerHeight);

	//! @brief pl_mpegの動画デコードコールバック（VideoOvlImpl.cpp内のファイルスコープの
	//! トランポリン関数）から呼ばれる。frameは実際にはplm_frame_t*だが、pl_mpeg.h内で
	//! 無名structとしてtypedefされ前方宣言できないためvoid*で受ける
	void PlmWriteVideoFrame(void *frame);
	//! @brief pl_mpegの音声デコードコールバックから呼ばれる。samplesは実際には
	//! plm_samples_t*（理由はPlmWriteVideoFrameと同じ）
	void PlmQueueAudioSamples(void *samples);
#endif

private:
	void WndProc( NativeEvent& ev );
		// UtilWindow's window procedure
	void ClearWndProcMessages(); // clear WndProc's message queue

};
//---------------------------------------------------------------------------

#ifdef __ANDROID__
//! @brief 再生中の全VideoOverlayインスタンスのデコードを1ティック進める
//! @return いずれかが再生中ならtrue。呼び出し側(TickBeat)はこれをneedsGraphicUpdateの
//! 判断材料にする
extern bool TVPPlmTickAll();
//! @brief 再生中かつvisibleな全VideoOverlayインスタンスを描画する
extern void TVPPlmRenderAll(SDL_Renderer *renderer, const SDL_Rect &destRect,
	int innerWidth, int innerHeight);
#endif

#endif
