/* SPDX-License-Identifier: MIT */
/* Copyright (c) Kirikiri SDL2 Developers */

// Kept as its own header-only, Android-only file rather than folded into
// SystemImpl.cpp or SDLApplication.cpp (its two callers): a .cpp here would
// need an entry in sources.txt, which is upstream-tracked and so a needless
// future merge-conflict point for code with no upstream counterpart; and
// hosting the class in either caller's file would couple that file to the
// other for no reason beyond serving it.
#pragma once

#ifdef __ANDROID__

#include <jni.h>
#include <SDL.h>

//---------------------------------------------------------------------------
// Resolves JNIEnv / Activity / Activity-class and, on request, a static
// method on that class, failing closed (null Env, null ActivityClass, or a
// null jmethodID) at every step so a caller whose running Activity subclass
// lacks the expected method can fall back to a non-JNI path instead of
// crashing or leaking a pending exception into later JNI calls. Deletes the
// local refs it created (the Activity object and its class) once it goes
// out of scope.
//---------------------------------------------------------------------------
class TVPJNIActivityMethodResolver
{
public:
	TVPJNIActivityMethodResolver()
		: Env(static_cast<JNIEnv *>(SDL_AndroidGetJNIEnv())), ActivityClass(nullptr)
	{
		if (!Env) return;

		// SDL_AndroidGetActivity() calls CallStaticObjectMethod internally
		// and hands back a fresh local ref each time (see SDL_android.c);
		// this thread stays attached for the process lifetime rather than
		// returning to the JVM between calls, so the ref must be deleted
		// explicitly here or it never gets reclaimed.
		jobject activity = static_cast<jobject>(SDL_AndroidGetActivity());
		if (!activity) return;

		ActivityClass = Env->GetObjectClass(activity);
		Env->DeleteLocalRef(activity);
	}

	~TVPJNIActivityMethodResolver()
	{
		if (Env && ActivityClass) Env->DeleteLocalRef(ActivityClass);
	}

	TVPJNIActivityMethodResolver(const TVPJNIActivityMethodResolver &) = delete;
	TVPJNIActivityMethodResolver &operator=(const TVPJNIActivityMethodResolver &) = delete;

	bool IsValid() const { return Env != nullptr && ActivityClass != nullptr; }
	JNIEnv *GetEnv() const { return Env; }
	jclass GetActivityClass() const { return ActivityClass; }

	// Looks up a static method by name/signature, clearing (and discarding)
	// any pending exception -- e.g. NoSuchMethodError when the running
	// Activity subclass doesn't carry the method -- so the caller can fall
	// back to a non-JNI path instead of the exception surfacing later at an
	// unrelated JNI call. Returns nullptr immediately if IsValid() is false.
	jmethodID GetStaticMethod(const char *name, const char *sig) const
	{
		if (!IsValid()) return nullptr;

		jmethodID mid = Env->GetStaticMethodID(ActivityClass, name, sig);
		if (Env->ExceptionCheck())
		{
			Env->ExceptionClear();
			mid = nullptr;
		}
		return mid;
	}

private:
	JNIEnv *Env;
	jclass ActivityClass;
};

#endif // __ANDROID__
