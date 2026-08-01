//---------------------------------------------------------------------------
/*
	TVP2 ( T Visual Presenter 2 )  A script authoring tool
	Copyright (C) 2000 W.Dee <dee@kikyou.info> and contributors

	See details of license at "license.txt"
*/
//---------------------------------------------------------------------------
// Video Overlay support implementation
//---------------------------------------------------------------------------


#include "tjsCommHead.h"

#include <algorithm>
#include "MsgIntf.h"
#include "VideoOvlImpl.h"
#include "DebugIntf.h"
#include "LayerIntf.h"
#include "LayerBitmapIntf.h"
#include "SysInitIntf.h"
#include "StorageImpl.h"
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
#include "krmovie.h"
#endif
#include "PluginImpl.h"
#include "WaveImpl.h"  // for DirectSound attenuate <-> TVP volume
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
#include <evcode.h>
#endif

#include "Application.h"
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
#include "TVPVideoOverlay.h"
#else
#define TVPDSAttenuateToPan(x) x
#define TVPDSAttenuateToVolume(x) x
#endif
#ifdef __ANDROID__
#include <cstdlib>
#include <cstring>
#include <SDL.h>
// PL_MPEG_IMPLEMENTATION must be defined in exactly one translation unit;
// this is that unit.
#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"
// Audio playback does not use a second SDL audio device: Android's SDL
// aaudio/openslES backends each track only one open output device in a
// single global (see external/SDL/src/audio/aaudio/SDL_aaudio.c), so opening
// a second one here would silently clobber the one FAudio already opened
// for BGM/SE, breaking the app's background auto-pause. Instead this adds a
// source voice to the same shared FAudio engine via TVPGetSharedFAudioEngine()
// (defined in FAudioDevice.cpp).
#include <FAudio.h>
extern FAudio *TVPGetSharedFAudioEngine();
#endif

//---------------------------------------------------------------------------
static std::vector<tTJSNI_VideoOverlay *> TVPVideoOverlayVector;
//---------------------------------------------------------------------------
static void TVPAddVideOverlay(tTJSNI_VideoOverlay *ovl)
{
	TVPVideoOverlayVector.push_back(ovl);
}
//---------------------------------------------------------------------------
static void TVPRemoveVideoOverlay(tTJSNI_VideoOverlay *ovl)
{
	std::vector<tTJSNI_VideoOverlay*>::iterator i;
	i = std::find(TVPVideoOverlayVector.begin(), TVPVideoOverlayVector.end(), ovl);
	if(i != TVPVideoOverlayVector.end())
		TVPVideoOverlayVector.erase(i);
}
//---------------------------------------------------------------------------
static void TVPShutdownVideoOverlay()
{
	// shutdown all overlay object and release krmovie.dll / krflash.dll
	std::vector<tTJSNI_VideoOverlay*>::iterator i;
	for(i = TVPVideoOverlayVector.begin(); i != TVPVideoOverlayVector.end(); i++)
	{
		(*i)->Shutdown();
	}
}
static tTVPAtExit TVPShutdownVideoOverlayAtExit
	(TVP_ATEXIT_PRI_PREPARE, TVPShutdownVideoOverlay);
//---------------------------------------------------------------------------

#ifdef __ANDROID__
//---------------------------------------------------------------------------
// pl_mpeg decode callback trampolines
//---------------------------------------------------------------------------
// These must have the exact signature pl_mpeg expects (plm_t*, plm_frame_t*/
// plm_samples_t*, void*), so they cannot be tTJSNI_VideoOverlay member
// functions declared in VideoOvlImpl.h (plm_frame_t/plm_samples_t are
// anonymous-struct typedefs and cannot be forward-declared there).
static void TVPPlmVideoDecodeCallback(plm_t *plm, plm_frame_t *frame, void *user)
{
	static_cast<tTJSNI_VideoOverlay *>(user)->PlmWriteVideoFrame(frame);
}
//---------------------------------------------------------------------------
static void TVPPlmAudioDecodeCallback(plm_t *plm, plm_samples_t *samples, void *user)
{
	static_cast<tTJSNI_VideoOverlay *>(user)->PlmQueueAudioSamples(samples);
}
//---------------------------------------------------------------------------
// pl_mpeg's PLM_AUDIO_SAMPLES_PER_FRAME must match the ring slot size
// declared in VideoOvlImpl.h (kept as a literal there since that header
// stays pl_mpeg.h-free).
static_assert(PLM_AUDIO_SAMPLES_PER_FRAME == 1152,
	"tTJSNI_VideoOverlay::PlmAudioRing slot size must match PLM_AUDIO_SAMPLES_PER_FRAME");
//---------------------------------------------------------------------------
bool TVPPlmTickAll()
{
	// PlmTick() calls SetStatusAsync(Stop) on end-of-stream, which posts via
	// TVP_EPT_POST (see external/krkrz/base/EventIntf.cpp) and is therefore
	// delivered later on the event loop, not synchronously from here -- so
	// the onStatusChanged handler's Close() cannot reenter this loop. The
	// snapshot below is defensive insurance against that assumption ever
	// changing (or against a future caller of Close() during iteration),
	// not a fix for an observed reentrancy.
	std::vector<tTJSNI_VideoOverlay*> overlays(TVPVideoOverlayVector);
	bool anyPlaying = false;
	tjs_uint64 now = SDL_GetTicks64();
	for(std::vector<tTJSNI_VideoOverlay*>::iterator i = overlays.begin();
		i != overlays.end(); i++)
	{
		if((*i)->PlmTick(now)) anyPlaying = true;
	}
	return anyPlaying;
}
//---------------------------------------------------------------------------
void TVPPlmRenderAll(SDL_Renderer *renderer, const SDL_Rect &destRect,
	int innerWidth, int innerHeight)
{
	// see TVPPlmTickAll() for why this iterates a snapshot
	std::vector<tTJSNI_VideoOverlay*> overlays(TVPVideoOverlayVector);
	for(std::vector<tTJSNI_VideoOverlay*>::iterator i = overlays.begin();
		i != overlays.end(); i++)
	{
		(*i)->PlmRender(renderer, destRect, innerWidth, innerHeight);
	}
}
//---------------------------------------------------------------------------
#endif




//---------------------------------------------------------------------------
// tTJSNI_VideoOverlay
//---------------------------------------------------------------------------
tTJSNI_VideoOverlay::tTJSNI_VideoOverlay()
: EventQueue(this,&tTJSNI_VideoOverlay::WndProc)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	VideoOverlay = NULL;
#endif
	Rect.left = 0;
	Rect.top = 0;
	Rect.right = 320;
	Rect.bottom = 240;
	Visible = false;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	OwnerWindow = NULL;
#endif
	LocalTempStorageHolder = NULL;

	EventQueue.Allocate();

	Layer1 = NULL;
	Layer2 = NULL;
	Mode = vomOverlay;
	Loop = false;
	IsPrepare = false;
	SegLoopStartFrame = -1;
	SegLoopEndFrame = -1;
	IsEventPast = false;
	EventFrame = -1;

#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	Bitmap[0] = Bitmap[1] = NULL;
	BmpBits[0] = BmpBits[1] = NULL;
#endif
#ifdef __ANDROID__
	PlmDecoder = NULL;
	PlmTexture = NULL;
	PlmRgbBuffer = NULL;
	PlmAudioVoice = NULL;
	PlmAudioNeedsEngine = false;
	PlmAudioRingNext = 0;
	PlmPlaying = false;
	PlmFrameDirty = false;
	PlmLastTickMs = 0;
	PlmVideoWidth = 0;
	PlmVideoHeight = 0;
#endif
}
//---------------------------------------------------------------------------
tjs_error TJS_INTF_METHOD
tTJSNI_VideoOverlay::Construct(tjs_int numparams, tTJSVariant **param,
		iTJSDispatch2 *tjs_obj)
{
	tjs_error hr = inherited::Construct(numparams, param, tjs_obj);
	if(TJS_FAILED(hr)) return hr;

	return TJS_S_OK;
}
//---------------------------------------------------------------------------
void TJS_INTF_METHOD tTJSNI_VideoOverlay::Invalidate()
{
	inherited::Invalidate();

	Close();

	EventQueue.Deallocate();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Open(const ttstr &_name)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// open

	// first, close
	Close();


	// check window
	if(!Window) TVPThrowExceptionMessage(TVPWindowAlreadyMissing);

	// open target storage
	ttstr name(_name);
	ttstr param;

	const tjs_char * param_pos;
	int param_pos_ind;
	param_pos = TJS_strchr(name.c_str(), TJS_W('?'));
	param_pos_ind = (int)(param_pos - name.c_str());
	if(param_pos != NULL)
	{
		param = param_pos;
		name = ttstr(name, param_pos_ind);
	}

	IStream *istream = NULL;
	long size;
	ttstr ext = TVPExtractStorageExt(name).c_str();
	ext.ToLowerCase();

	{
		// prepate IStream
		tTJSBinaryStream *stream0 = NULL;
		try
		{
			stream0 = TVPCreateStream(name);
			size = (long)stream0->GetSize();
		}
		catch(...)
		{
			if(stream0) delete stream0;
			throw;
		}

		istream = new tTVPIStreamAdapter(stream0);
	}

	// 'istream' is an IStream instance at this point

	// create video overlay object
	try
	{
		{
			if(Mode == vomLayer)
				GetVideoLayerObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
			else if(Mode == vomMixer)
				GetMixingVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
			else if(Mode == vomMFEVR)
				GetMFVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
			else
				GetVideoOverlayObject(EventQueue.GetOwner(), istream, name.c_str(), ext.c_str(), size, &VideoOverlay);
		}

		if( (Mode == vomOverlay) || (Mode == vomMixer) || (Mode == vomMFEVR) )
		{
			ResetOverlayParams();
		}
		else
		{	// set font and back buffer to layerVideo
			long	width, height;
			long			size;
			VideoOverlay->GetVideoSize( &width, &height );
			
			if( width <= 0 || height <= 0 )
				TVPThrowExceptionMessage(TVPErrorInKrMovieDLL, (const tjs_char*)TVPInvalidVideoSize);

			size = width * height * 4;
			if( Bitmap[0] != NULL )
				delete Bitmap[0];
			if( Bitmap[1] != NULL )
				delete Bitmap[1];
			Bitmap[0] = new tTVPBaseBitmap( width, height, 32 );
			Bitmap[1] = new tTVPBaseBitmap( width, height, 32 );

			BmpBits[0] = static_cast<BYTE*>(Bitmap[0]->GetBitmap()->GetScanLine( Bitmap[0]->GetBitmap()->GetHeight()-1 ));
			BmpBits[1] = static_cast<BYTE*>(Bitmap[1]->GetBitmap()->GetScanLine( Bitmap[1]->GetBitmap()->GetHeight()-1 ));
			//BmpBits[0] = static_cast<BYTE*>(Bitmap[0]->GetBitmap()->GetScanLine( 0 ));
			//BmpBits[1] = static_cast<BYTE*>(Bitmap[1]->GetBitmap()->GetScanLine( 0 ));

			VideoOverlay->SetVideoBuffer( BmpBits[0], BmpBits[1], size );
		}
	}
	catch(...)
	{
		if(istream) istream->Release();
		Close();
		throw;
	}
	if(istream) istream->Release();

	// set Status
	ClearWndProcMessages();
	SetStatus(tTVPVideoOverlayStatus::Stop);
#elif defined(__ANDROID__)
	// first, close
	Close();

	tTJSBinaryStream *stream = TVPCreateStream(_name);
	// plm_create_with_memory(..., 1) below hands `data` to pl_mpeg, which
	// releases it with PLM_FREE() (plain free()) in plm_destroy(). It must
	// therefore be malloc'd here, not `new[]`'d, or the two allocators
	// mismatch.
	tjs_uint8 *data = NULL;
	tjs_uint64 size = 0;
	try
	{
		size = stream->GetSize();
		data = static_cast<tjs_uint8 *>(malloc(static_cast<size_t>(size)));
		if(!data) TVPThrowExceptionMessage(TVPInvalidVideoSize);
		stream->ReadBuffer(data, static_cast<tjs_uint>(size));
	}
	catch(...)
	{
		delete stream;
		free(data);
		throw;
	}
	delete stream;

	plm_t *plm = plm_create_with_memory(data, static_cast<size_t>(size), 1);
	if(!plm)
	{
		free(data);
		TVPThrowExceptionMessage(TVPInvalidVideoSize);
	}

	tjs_int width = plm_get_width(plm);
	tjs_int height = plm_get_height(plm);
	if(width <= 0 || height <= 0)
	{
		plm_destroy(plm); // also frees `data`
		TVPThrowExceptionMessage(TVPInvalidVideoSize);
	}

	PlmDecoder = plm;
	PlmVideoWidth = width;
	PlmVideoHeight = height;
	PlmRgbBuffer = new tjs_uint8[static_cast<size_t>(width) * height * 3];

	TVPAddVideOverlay(this);

	SetStatus(tTVPVideoOverlayStatus::Stop);
#endif
}
//---------------------------------------------------------------------------
#ifdef __ANDROID__
void tTJSNI_VideoOverlay::ReleasePlmResources()
{
	if(PlmAudioVoice)
	{
		// Only touch the voice while the shared engine is still alive.
		// TVPShutdownVideoOverlayAtExit (TVP_ATEXIT_PRI_PREPARE) always runs
		// before TVPUninitAudioDeviceAtExit (TVP_ATEXIT_PRI_RELEASE, see
		// external/krkrz/sound/QueueSoundBufferImpl.cpp) which destroys the
		// engine and every voice on it, so this is normally a no-op guard;
		// it exists so a dangling PlmAudioVoice can never be dereferenced if
		// that ordering assumption is ever broken.
		if(TVPGetSharedFAudioEngine())
		{
			// Stop+Flush only quiesces the voice; the ring buffer these
			// pointed at is a class member, not heap memory, so there is
			// nothing to free here regardless of whether FAudio still has a
			// buffer queued at this point.
			FAudioSourceVoice_Stop(PlmAudioVoice, 0, FAUDIO_COMMIT_NOW);
			FAudioSourceVoice_FlushSourceBuffers(PlmAudioVoice);
			FAudioVoice_DestroyVoice(PlmAudioVoice);
		}
		PlmAudioVoice = NULL;
	}
	PlmAudioNeedsEngine = false;
	PlmAudioRingNext = 0;
	if(PlmTexture)
	{
		SDL_DestroyTexture(PlmTexture);
		PlmTexture = NULL;
	}
	delete [] PlmRgbBuffer;
	PlmRgbBuffer = NULL;
	if(PlmDecoder)
	{
		// also frees the buffer handed to plm_create_with_memory (free_when_done=1)
		plm_destroy(static_cast<plm_t *>(PlmDecoder));
		PlmDecoder = NULL;
	}
	PlmPlaying = false;
	PlmFrameDirty = false;
}
#endif
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Close()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// close
	// release VideoOverlay object
	if(VideoOverlay)
	{
		VideoOverlay->Release(), VideoOverlay = NULL;
		::SetFocus(Window->GetWindowHandle());
	}
	if(LocalTempStorageHolder)
		delete LocalTempStorageHolder, LocalTempStorageHolder = NULL;
	ClearWndProcMessages();
	SetStatus(tTVPVideoOverlayStatus::Unload);

	if( Bitmap[0] )
		delete Bitmap[0];
	if( Bitmap[1] )
		delete Bitmap[1];

	Bitmap[0] = Bitmap[1] = NULL;
	BmpBits[0] = BmpBits[1] = NULL;
#elif defined(__ANDROID__)
	ReleasePlmResources();

	TVPRemoveVideoOverlay(this);

	SetStatus(tTVPVideoOverlayStatus::Unload);
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Shutdown()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// shutdown the system
	// this functions closes the overlay object, but must not fire any events.
	bool c = CanDeliverEvents;
	ClearWndProcMessages();
	SetStatus(tTVPVideoOverlayStatus::Unload);
	try
	{
		if(VideoOverlay) VideoOverlay->Release(), VideoOverlay = NULL;
	}
	catch(...)
	{
		CanDeliverEvents = c;
		throw;
	}
	CanDeliverEvents = c;
#elif defined(__ANDROID__)
	// Called from TVPShutdownVideoOverlay() while it iterates
	// TVPVideoOverlayVector, so this must not call TVPRemoveVideoOverlay()
	// (that would erase() from the vector mid-iteration) and, per the
	// contract above, must not fire onStatusChanged -- so this releases the
	// same resources as Close() but skips SetStatus() and the registry
	// removal.
	ReleasePlmResources();
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Disconnect()
{
	// disconnect the object
	Shutdown();

	Window = NULL;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Play()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// start playing
	if(VideoOverlay)
	{
		VideoOverlay->Play();
		ClearWndProcMessages();
		if( Mode != vomMFEVR ) SetStatus(tTVPVideoOverlayStatus::Play);
	}
#elif defined(__ANDROID__)
	if(!PlmDecoder || PlmPlaying) return;

	if(PlmAudioVoice)
	{
		// re-entering Play() after Stop(): the voice is kept around (only
		// stopped) so playback can resume without recreating it
		FAudioSourceVoice_Start(PlmAudioVoice, 0, FAUDIO_COMMIT_NOW);
	}
	else if(plm_get_samplerate(static_cast<plm_t *>(PlmDecoder)) <= 0)
	{
		// No usable audio stream in this .mpg. Guarded here, before any
		// voice/engine attempt, because a samplerate of 0 would also make
		// PlmTryCreateAudioVoice()'s plm_set_audio_lead_time() divide by
		// zero; this case is permanent, so PlmAudioNeedsEngine stays false
		// and PlmTick() will not keep retrying.
		plm_set_audio_enabled(static_cast<plm_t *>(PlmDecoder), 0);
		TVPAddLog(TJS_W("VideoOverlay: audio disabled (no audio stream)"));
	}
	else if(!PlmTryCreateAudioVoice())
	{
		// No shared FAudio engine yet (e.g. no BGM/SE has played this
		// session) or voice creation failed: play the video silently for
		// now. PlmAudioNeedsEngine is set so PlmTick() retries every tick;
		// TVPGetSharedFAudioEngine() is just a pointer read, so the retry
		// cost is negligible even while it keeps failing.
		plm_set_audio_enabled(static_cast<plm_t *>(PlmDecoder), 0);
		PlmAudioNeedsEngine = true;
		TVPAddLog(TJS_W("VideoOverlay: audio disabled (no FAudio engine yet, will retry)"));
	}

	plm_set_video_decode_callback(static_cast<plm_t *>(PlmDecoder), TVPPlmVideoDecodeCallback, this);
	plm_set_audio_decode_callback(static_cast<plm_t *>(PlmDecoder), TVPPlmAudioDecodeCallback, this);

	PlmPlaying = true;
	PlmLastTickMs = SDL_GetTicks64();
	SetStatus(tTVPVideoOverlayStatus::Play);
#endif
}
//---------------------------------------------------------------------------
#ifdef __ANDROID__
bool tTJSNI_VideoOverlay::PlmTryCreateAudioVoice()
{
	// Caller (Play() / PlmTick()) already checked plm_get_samplerate() > 0.
	FAudio *engine = TVPGetSharedFAudioEngine();
	if(!engine) return false;

	FAudioWaveFormatEx fmt;
	memset(&fmt, 0, sizeof(fmt));
	fmt.wFormatTag = FAUDIO_FORMAT_IEEE_FLOAT;
	fmt.nChannels = 2;
	fmt.nSamplesPerSec = static_cast<uint32_t>(plm_get_samplerate(static_cast<plm_t *>(PlmDecoder)));
	fmt.wBitsPerSample = 32;
	fmt.nBlockAlign = static_cast<uint16_t>((fmt.wBitsPerSample / 8) * fmt.nChannels);
	fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

	// pCallback=NULL: PlmQueueAudioSamples() submits directly from the
	// PlmAudioRing member array, so there is nothing for an OnBufferEnd
	// callback to free.
	FAudioSourceVoice *voice = NULL;
	uint32_t hr = FAudio_CreateSourceVoice(engine, &voice, &fmt, 0,
		FAUDIO_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
	if(hr != 0 || voice == NULL) return false;

	PlmAudioVoice = voice;
	// One MP2 frame (PLM_AUDIO_SAMPLES_PER_FRAME samples) is submitted per
	// audio decode callback, so that is how far ahead of the video the
	// audio decode runs; match the lead time to it (see pl_mpeg.h's
	// plm_set_audio_lead_time docs).
	plm_set_audio_lead_time(static_cast<plm_t *>(PlmDecoder),
		static_cast<double>(PLM_AUDIO_SAMPLES_PER_FRAME) / static_cast<double>(fmt.nSamplesPerSec));
	FAudioSourceVoice_Start(voice, 0, FAUDIO_COMMIT_NOW);
	// undo a previous attempt's plm_set_audio_enabled(.., 0), in case this
	// is a retry from PlmTick() that has now found an engine
	plm_set_audio_enabled(static_cast<plm_t *>(PlmDecoder), 1);
	PlmAudioNeedsEngine = false;
	return true;
}
#endif
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Stop()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// stop playing
	if(VideoOverlay)
	{
		VideoOverlay->Stop();
		ClearWndProcMessages();
		if( Mode != vomMFEVR ) SetStatus(tTVPVideoOverlayStatus::Stop);
	}
#elif defined(__ANDROID__)
	if(!PlmDecoder) return;

	PlmPlaying = false;
	if(PlmAudioVoice)
	{
		FAudioSourceVoice_Stop(PlmAudioVoice, 0, FAUDIO_COMMIT_NOW);
		FAudioSourceVoice_FlushSourceBuffers(PlmAudioVoice);
	}
	SetStatus(tTVPVideoOverlayStatus::Stop);
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::Pause()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// pause playing
	if(VideoOverlay)
	{
		VideoOverlay->Pause();
//		ClearWndProcMessages();
		if( Mode != vomMFEVR ) SetStatus(tTVPVideoOverlayStatus::Pause);
	}
#endif
}
void tTJSNI_VideoOverlay::Rewind()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// rewind playing
	if(VideoOverlay)
	{
		VideoOverlay->Rewind();
		ClearWndProcMessages();

		if( EventFrame >= 0 && IsEventPast )
			IsEventPast = false;
	}
#endif
}
void tTJSNI_VideoOverlay::Prepare()
{	// prepare movie
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if( VideoOverlay && (Mode == vomLayer) )
	{
		Pause();
		Rewind();
		IsPrepare = true;
		Play();
	}
#endif
}
void tTJSNI_VideoOverlay::SetSegmentLoop( int comeFrame, int goFrame )
{
	SegLoopStartFrame = comeFrame;
	SegLoopEndFrame = goFrame;
}
void tTJSNI_VideoOverlay::SetPeriodEvent( int eventFrame )
{
	EventFrame = eventFrame;

	if( eventFrame <= GetFrame() )
		IsEventPast = true;
	else
		IsEventPast = false;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetRectangleToVideoOverlay()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// set Rectangle to video overlay
	if(VideoOverlay && OwnerWindow)
	{
		tjs_int ofsx, ofsy;
		Window->GetVideoOffset(ofsx, ofsy);
		tjs_int l = Rect.left;
		tjs_int t = Rect.top;
		tjs_int r = Rect.right;
		tjs_int b = Rect.bottom;
		TVPAddLog(TJS_W("Video zoom: (") + ttstr(l) + TJS_W(",") + ttstr(t) + TJS_W(")-(") +
			ttstr(r) + TJS_W(",") + ttstr(b) + TJS_W(") ->"));
		Window->ZoomRectangle(l, t, r, b);
		TVPAddLog(TJS_W("(") + ttstr(l) + TJS_W(",") + ttstr(t) + TJS_W(")-(") +
			ttstr(r) + TJS_W(",") + ttstr(b) + TJS_W(")"));
		RECT rect = {l + ofsx, t + ofsy, r + ofsx, b + ofsy};
		VideoOverlay->SetRect(&rect);
	}
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetPosition(tjs_int left, tjs_int top)
{
	if( Mode == vomLayer )
	{
		if( Layer1 != NULL ) Layer1->SetPosition( left, top );
		if( Layer2 != NULL ) Layer2->SetPosition( left, top );
	}
	else
	{
		Rect.set_offsets(left, top);
		SetRectangleToVideoOverlay();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetSize(tjs_int width, tjs_int height)
{
	if( Mode == vomLayer ) return;

	Rect.set_size(width, height);
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetBounds(const tTVPRect & rect)
{
	if( Mode == vomLayer ) return;

	Rect = rect;
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetLeft(tjs_int l)
{
	if( Mode == vomLayer )
	{
		if( Layer1 != NULL ) Layer1->SetLeft( l );
		if( Layer2 != NULL ) Layer2->SetLeft( l );
	}
	else
	{
		Rect.set_offsets(l, Rect.top);
		SetRectangleToVideoOverlay();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetTop(tjs_int t)
{
	if( Mode == vomLayer )
	{
		if( Layer1 != NULL ) Layer1->SetTop( t );
		if( Layer2 != NULL ) Layer2->SetTop( t );
	}
	else
	{
		Rect.set_offsets(Rect.left, t);
		SetRectangleToVideoOverlay();
	}
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetWidth(tjs_int w)
{
	if( Mode == vomLayer ) return;

	Rect.right = Rect.left + w;
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetHeight(tjs_int h)
{
	if( Mode == vomLayer ) return;

	Rect.bottom = Rect.top + h;
	SetRectangleToVideoOverlay();
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetVisible(bool b)
{
	Visible = b;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		if( Mode == vomLayer )
		{
			if( Layer1 != NULL ) Layer1->SetVisible( Visible );
			if( Layer2 != NULL ) Layer2->SetVisible( Visible );
		}
		else
		{
			VideoOverlay->SetVisible(Visible);
		}
	}
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::ResetOverlayParams()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// retrieve new window information from owner window and
	// set video owner window / message drain window.
	// also sets rectangle and visible state.
	if(VideoOverlay && Window && (Mode == vomOverlay || Mode == vomMixer || Mode == vomMFEVR) )
	{
		OwnerWindow = Window->GetWindowHandle();
		VideoOverlay->SetWindow(OwnerWindow);

		VideoOverlay->SetMessageDrainWindow(Window->GetSurfaceWindowHandle());

		// set Rectangle
		SetRectangleToVideoOverlay();

		// set Visible
		VideoOverlay->SetVisible(Visible);
	}
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::DetachVideoOverlay()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay && Window && (Mode == vomOverlay || Mode == vomMixer || Mode == vomMFEVR) )
	{
		VideoOverlay->SetWindow(NULL);
		VideoOverlay->SetMessageDrainWindow(EventQueue.GetOwner());
			// once set to util window
	}
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetRectOffset(tjs_int ofsx, tjs_int ofsy)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		RECT r = {Rect.left + ofsx, Rect.top + ofsy,
			Rect.right + ofsx, Rect.bottom + ofsy};
		VideoOverlay->SetRect(&r);
	}
#endif
}
//---------------------------------------------------------------------------
//void __fastcall tTJSNI_VideoOverlay::WndProc(Messages::TMessage &Msg)
void tTJSNI_VideoOverlay::WndProc( NativeEvent& ev )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// EventQueue's message procedure
	if(VideoOverlay)
	{
		switch(ev.Message) {
		case WM_GRAPHNOTIFY:
		{
			long evcode;
			LONG_PTR p1, p2;
			bool got;
			do {
				VideoOverlay->GetEvent(&evcode, &p1, &p2, &got);
				if( got == false)
					return;

				switch( evcode )
				{
					case EC_COMPLETE:
						if( Status == tTVPVideoOverlayStatus::Play )
						{
							if( Loop )
							{
								Rewind();
								FirePeriodEvent(perLoop); // fire period event by loop rewind
							}
							else
							{
								// Graph manager seems not to complete playing
								// at this point (rewinding the movie at the event
								// handler called asynchronously from SetStatusAsync
								// makes continuing playing, but the graph seems to
								// be unstable).
								// We manually stop the manager anyway.
								VideoOverlay->Stop();
								SetStatusAsync(tTVPVideoOverlayStatus::Stop); // All data has been rendered
							}
						}
						break;
					case EC_UPDATE:
						if( Mode == vomLayer && Status == tTVPVideoOverlayStatus::Play )
						{
							int		curFrame = (int)p1;
							if( Layer1 == NULL && Layer2 == NULL )	// nothing to do.
								return;

							// 2フレーム以上差があるときはGetFrame() を現在のフレームとする
							int frame = GetFrame();
							if( (frame+1) < curFrame || (frame-1) > curFrame )
								curFrame = frame;

							if( (!IsPrepare) && (SegLoopEndFrame > 0) && (frame >= SegLoopEndFrame) ) {
								SetFrame( SegLoopStartFrame > 0 ? SegLoopStartFrame : 0 );
								FirePeriodEvent(perSegLoop); // fire period event by segment loop rewind
								return; // Updateを行わない
							}

							// get video image size
							long	width, height;
							VideoOverlay->GetVideoSize( &width, &height );

							tTJSNI_BaseLayer	*l1 = Layer1;
							tTJSNI_BaseLayer	*l2 = Layer2;

							// Check layer image size
							if( l1 != NULL )
							{
								if( (long)l1->GetImageWidth() != width || (long)l1->GetImageHeight() != height )
									l1->SetImageSize( width, height );
								if( (long)l1->GetWidth() != width || (long)l1->GetHeight() != height )
									l1->SetSize( width, height );
							}
							if( l2 != NULL )
							{
								if( (long)l2->GetImageWidth() != width || (long)l2->GetImageHeight() != height )
									l2->SetImageSize( width, height );
								if( (long)l2->GetWidth() != width || (long)l2->GetHeight() != height )
									l2->SetSize( width, height );
							}
							BYTE *buff;
							VideoOverlay->GetFrontBuffer( &buff );
							if( buff == BmpBits[0] )
							{
								if( l1 ) l1->AssignMainImage( Bitmap[0] );
								if( l2 ) l2->AssignMainImage( Bitmap[0] );
							}
							else	// 0じゃなかったら、1とみなす。
							{
								if( l1 ) l1->AssignMainImage( Bitmap[1] );
								if( l2 ) l2->AssignMainImage( Bitmap[1] );
							}
							if( l1 ) l1->Update();
							if( l2 ) l2->Update();
							FireFrameUpdateEvent( curFrame );

							// ! Prepare mode ?
							if( !IsPrepare )
							{
								// Send period event ?
								if( EventFrame >= 0 && !IsEventPast && curFrame >= EventFrame )
								{
									EventFrame = -1;
									FirePeriodEvent(perPeriod); // fire period event by setPeriodEvent()
								}
							}
							else
							{	// Prepare mode
								FirePeriodEvent(perPrepare); // fire period event by prepare()
								Pause();
								Rewind();
								IsPrepare = false;
							}
						}
						else if( Mode == vomMixer && Status == tTVPVideoOverlayStatus::Play )
						{
							int frame = GetFrame();
							if( (!IsPrepare) && (SegLoopEndFrame > 0) && (frame >= SegLoopEndFrame) ) {
								SetFrame( SegLoopStartFrame > 0 ? SegLoopStartFrame : 0 );
								FirePeriodEvent(perSegLoop); // fire period event by segment loop rewind
								return;
							}
							VideoOverlay->PresentVideoImage();
							FireFrameUpdateEvent( frame );
							// Send period event ?
							if( EventFrame >= 0 && !IsEventPast && frame >= EventFrame )
							{
								EventFrame = -1;
								FirePeriodEvent(perPeriod); // fire period event by setPeriodEvent()
							}
						}
						break;
				}
				VideoOverlay->FreeEventParams( evcode, p1, p2 );
			} while( got );
			return;
		}
		case WM_CALLBACKCMD:
		{
			// wparam : command
			// lparam : argument
			FireCallbackCommand((tjs_char*)ev.WParam, (tjs_char*)ev.LParam);
			return;
		}
		case WM_STATE_CHANGE:
			{
				switch( ev.WParam ) {
				case vsStopped:
					SetStatusAsync( tTVPVideoOverlayStatus::Stop );
					break;
				case vsPlaying:
					SetStatusAsync( tTVPVideoOverlayStatus::Play );
					break;
				case vsPaused:
					SetStatusAsync( tTVPVideoOverlayStatus::Pause );
					break;
				case vsReady:
					SetStatusAsync( tTVPVideoOverlayStatus::Ready );
					break;
				case vsEnded:
					if( Status == tTVPVideoOverlayStatus::Play )
					{
						if( Loop )
						{
							VideoOverlay->Play();
							FirePeriodEvent(perLoop); // fire period event by loop rewind
						}
						else
						{
							VideoOverlay->Stop();
							SetStatusAsync(tTVPVideoOverlayStatus::Stop); // All data has been rendered
						}
					}
					break;
				}
				return;
			}
		}
	}

	EventQueue.HandlerDefault(ev);
#endif
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::SetTimePosition( tjs_uint64 p )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetPosition( p );
	}
#endif
}
tjs_uint64 tTJSNI_VideoOverlay::GetTimePosition()
{
	tjs_uint64	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetPosition( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetFrame( tjs_int f )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetFrame( f );

		if( EventFrame >= f && IsEventPast )
			IsEventPast = false;
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetFrame()
{
	tjs_int	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetFrame( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetStopFrame( tjs_int f )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetStopFrame( f );
	}
#endif
}
void tTJSNI_VideoOverlay::SetDefaultStopFrame()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetDefaultStopFrame();
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetStopFrame()
{
	tjs_int	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetStopFrame( &result );
	}
#endif
	return result;
}
tjs_real tTJSNI_VideoOverlay::GetFPS()
{
	tjs_real	result = 0.0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetFPS( &result );
	}
#endif
	return result;
}
tjs_int tTJSNI_VideoOverlay::GetNumberOfFrame()
{
	tjs_int	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetNumberOfFrame( &result );
	}
#endif
	return result;
}
tjs_int64 tTJSNI_VideoOverlay::GetTotalTime()
{
	tjs_int64	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetTotalTime( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetLoop( bool b )
{
	Loop = b;
}
void tTJSNI_VideoOverlay::SetLayer1( tTJSNI_BaseLayer *l )
{
	Layer1 = l;
}
void tTJSNI_VideoOverlay::SetLayer2( tTJSNI_BaseLayer *l )
{
	Layer2 = l;
}
void tTJSNI_VideoOverlay::SetMode( tTVPVideoOverlayMode m )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// ビデオオープン後のモード変更は禁止
	if( !VideoOverlay )
	{
		Mode = m;
	}
#endif
}

tjs_real tTJSNI_VideoOverlay::GetPlayRate()
{
	tjs_real	result = 0.0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetPlayRate( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetPlayRate(tjs_real r)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetPlayRate( r );
	}
#endif
}

tjs_int tTJSNI_VideoOverlay::GetAudioBalance()
{
	long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetAudioBalance( &result );
	}
#endif
	return TVPDSAttenuateToPan( result );
}
void tTJSNI_VideoOverlay::SetAudioBalance(tjs_int b)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetAudioBalance( TVPPanToDSAttenuate( b ) );
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetAudioVolume()
{
	long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetAudioVolume( &result );
	}
#endif
	return TVPDSAttenuateToVolume( result );
}
void tTJSNI_VideoOverlay::SetAudioVolume(tjs_int b)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetAudioVolume( TVPVolumeToDSAttenuate( b ) );
	}
#endif
}
tjs_uint tTJSNI_VideoOverlay::GetNumberOfAudioStream()
{
	unsigned long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetNumberOfAudioStream( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SelectAudioStream(tjs_uint n)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SelectAudioStream( n );
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetEnabledAudioStream()
{
	long		result = -1;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetEnableAudioStreamNum( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::DisableAudioStream()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->DisableAudioStream();
	}
#endif
}

tjs_uint tTJSNI_VideoOverlay::GetNumberOfVideoStream()
{
	unsigned long	result = 0;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetNumberOfVideoStream( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SelectVideoStream(tjs_uint n)
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SelectVideoStream( n );
	}
#endif
}
tjs_int tTJSNI_VideoOverlay::GetEnabledVideoStream()
{
	long		result = -1;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetEnableVideoStreamNum( &result );
	}
#endif
	return result;
}
void tTJSNI_VideoOverlay::SetMixingLayer( tTJSNI_BaseLayer *l )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		if( l )
		{
			if( l->GetVisible() )
			{
				float	alpha = static_cast<float>(l->GetOpacity()) / 255.0f;
				RECT	dest;
				dest.left = l->GetLeft() + l->GetImageLeft();
				dest.top = l->GetTop() + l->GetImageTop();
				dest.right = dest.left + l->GetImageWidth();
				dest.bottom = dest.top + l->GetImageHeight();

				// tTVPBaseBitmap->tTVPBitmap
				tTVPBitmap *bmp = l->GetMainImage()->GetBitmap();
				if( bmp )
				{
					// 自前でDCを作る
					HDC hdc;
					HDC			ref = GetDC(0);
					HBITMAP		myDIB = CreateDIBitmap( ref, bmp->GetBITMAPINFOHEADER(), CBM_INIT, bmp->GetBits(), bmp->GetBITMAPINFO(), bmp->Is8bit() ? DIB_PAL_COLORS : DIB_RGB_COLORS );
					hdc = CreateCompatibleDC( NULL );
					HGDIOBJ		hOldBmp = SelectObject( hdc, myDIB );

					VideoOverlay->SetMixingBitmap( hdc, &dest, alpha );

					SelectObject( hdc, hOldBmp );
					DeleteObject( myDIB );
					DeleteDC( hdc );
				}
			}
			else
			{
				VideoOverlay->ResetMixingBitmap();
			}
		}
		else
		{
			VideoOverlay->ResetMixingBitmap();
		}
	}
#endif
}
void tTJSNI_VideoOverlay::ResetMixingBitmap()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->ResetMixingBitmap();
	}
#endif
}
void tTJSNI_VideoOverlay::SetMixingMovieAlpha( tjs_real a )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetMixingMovieAlpha( static_cast<float>(a) );
	}
#endif
}
tjs_real tTJSNI_VideoOverlay::GetMixingMovieAlpha()
{
	float	ret = 0.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetMixingMovieAlpha( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetMixingMovieBGColor( tjs_uint col )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetMixingMovieBGColor( col );
	}
#endif
}
tjs_uint tTJSNI_VideoOverlay::GetMixingMovieBGColor()
{
	unsigned long	ret;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetMixingMovieBGColor( &ret );
	}
#endif
	return static_cast<tjs_uint>(ret);
}



tjs_real tTJSNI_VideoOverlay::GetContrastRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrastStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrastStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetContrast()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetContrast( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetContrast( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetContrast( static_cast<float>(v) );
	}
#endif
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightnessStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightnessStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetBrightness()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetBrightness( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetBrightness( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetBrightness( static_cast<float>(v) );
	}
#endif
}

tjs_real tTJSNI_VideoOverlay::GetHueRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHueStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHueStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetHue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetHue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetHue( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetHue( static_cast<float>(v) );
	}
#endif
}

tjs_real tTJSNI_VideoOverlay::GetSaturationRangeMin()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationRangeMin( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationRangeMax()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationRangeMax( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationDefaultValue()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationDefaultValue( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturationStepSize()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturationStepSize( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
tjs_real tTJSNI_VideoOverlay::GetSaturation()
{
	float ret = -1.0f;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->GetSaturation( &ret );
	}
#endif
	return static_cast<tjs_real>(ret);
}
void tTJSNI_VideoOverlay::SetSaturation( tjs_real v )
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(VideoOverlay)
	{
		VideoOverlay->SetSaturation( static_cast<float>(v) );
	}
#endif
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_VideoOverlay::GetOriginalWidth()
{
	// retrieve original (coded in the video stream) width size
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	if(!VideoOverlay) return 0;
#endif

	long	width, height;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	VideoOverlay->GetVideoSize( &width, &height );
#else
	width = 0;
#endif

	return (tjs_int)width;
}
//---------------------------------------------------------------------------
tjs_int tTJSNI_VideoOverlay::GetOriginalHeight()
{
	// retrieve original (coded in the video stream) height size

	long	width, height;
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	VideoOverlay->GetVideoSize( &width, &height );
#else
	height = 0;
#endif

	return (tjs_int)height;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::ClearWndProcMessages()
{
#if defined(_WIN32) && defined(KRKRSDL2_USE_WIN32_EVENT_QUEUE) && defined(KRKRSDL2_ENABLE_VIDEOOVERLAY)
	// clear WndProc's message queue
	MSG msg;
	while(PeekMessage(&msg, EventQueue.GetOwner(), WM_GRAPHNOTIFY, WM_GRAPHNOTIFY+2, PM_REMOVE))
	{
		if(VideoOverlay)
		{
			long evcode;
			LONG_PTR p1, p2;
			bool got;
			VideoOverlay->GetEvent(&evcode, &p1, &p2, &got); // dummy call
			if( got )
				VideoOverlay->FreeEventParams( evcode, p1, p2 );
		}
	}
#endif
}
//---------------------------------------------------------------------------
#ifdef __ANDROID__
bool tTJSNI_VideoOverlay::PlmTick(tjs_uint64 nowMs)
{
	if(!PlmPlaying || !PlmDecoder) return false;

	if(PlmAudioNeedsEngine && !PlmAudioVoice)
	{
		// Retries every tick until an engine shows up (e.g. the game's
		// first BGM/SE starts after this video's Play()); a no-op read of
		// TVPGetSharedFAudioEngine() until then, so the per-tick cost of
		// retrying is negligible.
		PlmTryCreateAudioVoice();
	}

	double elapsed = static_cast<double>(nowMs - PlmLastTickMs) / 1000.0;
	// Clamp the step so that a stall (e.g. the app was backgrounded) does not
	// make plm_decode() try to catch up by decoding many seconds of frames
	// in a single call.
	if(elapsed > 0.25) elapsed = 0.25;
	if(elapsed < 0.0) elapsed = 0.0;
	PlmLastTickMs = nowMs;

	plm_decode(static_cast<plm_t *>(PlmDecoder), elapsed);

	if(plm_has_ended(static_cast<plm_t *>(PlmDecoder)))
	{
		PlmPlaying = false;
		if(PlmAudioVoice)
			FAudioSourceVoice_Stop(PlmAudioVoice, 0, FAUDIO_COMMIT_NOW);
		SetStatusAsync(tTVPVideoOverlayStatus::Stop);
		return false;
	}
	return true;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::PlmRender(SDL_Renderer *renderer, const SDL_Rect &destRect,
	int innerWidth, int innerHeight)
{
	if(!PlmDecoder || !PlmPlaying || !Visible) return;
	if(innerWidth <= 0 || innerHeight <= 0) return;
	if(PlmVideoWidth <= 0 || PlmVideoHeight <= 0) return;

	if(!PlmTexture)
	{
		PlmTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24,
			SDL_TEXTUREACCESS_STREAMING, PlmVideoWidth, PlmVideoHeight);
		if(!PlmTexture) return;
	}

	if(PlmFrameDirty)
	{
		SDL_UpdateTexture(PlmTexture, NULL, PlmRgbBuffer, PlmVideoWidth * 3);
		PlmFrameDirty = false;
	}

	// Map the overlay's game-resolution Rect into screen coordinates using
	// the same destRect/innerWidth/innerHeight transform TickBeat applies to
	// the main game texture, so the video lines up with the layer beneath it.
	SDL_Rect dst;
	dst.x = destRect.x + Rect.left * destRect.w / innerWidth;
	dst.y = destRect.y + Rect.top * destRect.h / innerHeight;
	dst.w = Rect.get_width() * destRect.w / innerWidth;
	dst.h = Rect.get_height() * destRect.h / innerHeight;

	SDL_RenderCopy(renderer, PlmTexture, NULL, &dst);
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::PlmWriteVideoFrame(void *frame)
{
	plm_frame_to_rgb(static_cast<plm_frame_t *>(frame), PlmRgbBuffer, PlmVideoWidth * 3);
	PlmFrameDirty = true;
}
//---------------------------------------------------------------------------
void tTJSNI_VideoOverlay::PlmQueueAudioSamples(void *samples)
{
	if(!PlmAudioVoice) return;
	plm_samples_t *s = static_cast<plm_samples_t *>(samples);

	// FAudioSourceVoice_SubmitSourceBuffer reads pAudioData in place rather
	// than copying it (see FAudioBuffer in FAudio.h), so the interleaved
	// samples need a buffer that outlives this call. A per-frame
	// malloc()/free() does not work here: FAudioSourceVoice_FlushSourceBuffers
	// (called from Stop()/Close()) only moves queued buffers to an internal
	// flush list without firing OnBufferEnd, and FAudioVoice_DestroyVoice
	// frees its own bookkeeping without firing it either -- so a
	// malloc-per-frame design leaks every buffer still queued at Close()
	// time. PlmAudioRing sidesteps this entirely: it is a fixed member
	// array, so there is nothing to leak and no OnBufferEnd is needed
	// (pCallback=NULL in PlmTryCreateAudioVoice()).
	const uint32_t ringSlots = static_cast<uint32_t>(sizeof(PlmAudioRing) / sizeof(PlmAudioRing[0]));

	FAudioVoiceState state;
	FAudioSourceVoice_GetState(PlmAudioVoice, &state, FAUDIO_VOICE_NOSAMPLESPLAYED);
	if(state.BuffersQueued >= ringSlots)
	{
		// Ring full: FAudio hasn't finished the oldest slot yet. Drop this
		// frame's audio rather than overwrite a slot FAudio may still be
		// reading from. Safe because submission is strictly round-robin and
		// FIFO: BuffersQueued < ringSlots can only happen once the buffer at
		// PlmAudioRingNext (the least recently submitted of the current set)
		// has been consumed, and PlmAudioRingNext only advances on a
		// successful submit below.
		return;
	}

	// s->count is documented as always PLM_AUDIO_SAMPLES_PER_FRAME, matching
	// the ring slot size exactly, but this is a runtime value, not something
	// the static_assert above can pin -- an oversized count here would
	// overflow a fixed-size member array (corrupting PlmAudioRingNext,
	// PlmPlaying, ...), not just leak, so it is checked explicitly rather
	// than trusted.
	size_t bytes = static_cast<size_t>(s->count) * 2 * sizeof(float);
	if(bytes > sizeof(PlmAudioRing[0])) return;

	float *slot = PlmAudioRing[PlmAudioRingNext];
	PlmAudioRingNext = (PlmAudioRingNext + 1) % static_cast<int>(ringSlots);
	memcpy(slot, s->interleaved, bytes);

	FAudioBuffer buf;
	memset(&buf, 0, sizeof(buf));
	buf.AudioBytes = static_cast<uint32_t>(bytes);
	buf.pAudioData = reinterpret_cast<const uint8_t *>(slot);
	FAudioSourceVoice_SubmitSourceBuffer(PlmAudioVoice, &buf, NULL);
}
//---------------------------------------------------------------------------
#endif



//---------------------------------------------------------------------------
// tTJSNC_VideoOverlay::CreateNativeInstance : returns proper instance object
//---------------------------------------------------------------------------
tTJSNativeInstance *tTJSNC_VideoOverlay::CreateNativeInstance()
{
	return new tTJSNI_VideoOverlay();
}
//---------------------------------------------------------------------------


//---------------------------------------------------------------------------
// TVPCreateNativeClass_VideoOverlay
//---------------------------------------------------------------------------
tTJSNativeClass * TVPCreateNativeClass_VideoOverlay()
{
	return new tTJSNC_VideoOverlay();
}
//---------------------------------------------------------------------------

