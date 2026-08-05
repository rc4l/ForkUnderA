// [rc4l] AudioQueue sink for the instant-replay audio pump on macOS. See fua_audiosink.h.
//
// AudioToolbox is a plain C API, so this is a .cpp rather than ObjC++ -- nothing here needs an
// Objective-C object, and keeping it C++ means it compiles with the rest of the feature.
//
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l
#include "fua_audiosink.h"
#include "computation/fua_audiomix_compute.h"

#include <AudioToolbox/AudioToolbox.h>
#include <cstring>

namespace zx
{

namespace
{

// Three buffers is the usual AudioQueue compromise: enough that a late callback does not underrun,
// few enough that the added latency stays below a frame of gameplay.
const int   kBufferCount = 3;
const UInt32 kBufferBytes = kFuaAudioFrames * kFuaAudioChannels * sizeof(short);

AudioQueueRef       s_queue = NULL;
AudioQueueBufferRef s_buffers[kBufferCount] = { NULL, NULL, NULL };
FuaAudioFill        s_fill = NULL;

void QueueCallback(void *, AudioQueueRef queue, AudioQueueBufferRef buffer)
{
	const int frames = FuaFramesForBytes(static_cast<int>(buffer->mAudioDataBytesCapacity));

	if (s_fill != NULL && frames > 0)
	{
		s_fill(static_cast<short *>(buffer->mAudioData), frames);
	}
	else
	{
		// Never hand the device an uninitialised buffer: that is audible noise, not silence.
		std::memset(buffer->mAudioData, 0, buffer->mAudioDataBytesCapacity);
	}

	buffer->mAudioDataByteSize = buffer->mAudioDataBytesCapacity;
	AudioQueueEnqueueBuffer(queue, buffer, 0, NULL);
}

} // namespace

bool FuaAudioSinkOpen(FuaAudioFill fill, const char **err)
{
	if (s_queue != NULL)
	{
		if (err != NULL) *err = "already open";
		return false;
	}

	s_fill = fill;

	AudioStreamBasicDescription fmt;
	std::memset(&fmt, 0, sizeof fmt);
	fmt.mSampleRate       = kFuaAudioRate;
	fmt.mFormatID         = kAudioFormatLinearPCM;
	fmt.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
	fmt.mChannelsPerFrame = kFuaAudioChannels;
	fmt.mBitsPerChannel   = 16;
	fmt.mFramesPerPacket  = 1;
	fmt.mBytesPerFrame    = kFuaAudioChannels * sizeof(short);
	fmt.mBytesPerPacket   = fmt.mBytesPerFrame;

	if (AudioQueueNewOutput(&fmt, QueueCallback, NULL, NULL, NULL, 0, &s_queue) != noErr)
	{
		s_queue = NULL;
		s_fill = NULL;
		if (err != NULL) *err = "AudioQueueNewOutput failed";
		return false;
	}

	// Prime every buffer before starting. Enqueueing them empty would play whatever the allocation
	// happened to contain.
	for (int i = 0; i < kBufferCount; ++i)
	{
		if (AudioQueueAllocateBuffer(s_queue, kBufferBytes, &s_buffers[i]) != noErr)
		{
			FuaAudioSinkClose();
			if (err != NULL) *err = "AudioQueueAllocateBuffer failed";
			return false;
		}
		QueueCallback(NULL, s_queue, s_buffers[i]);
	}

	if (AudioQueueStart(s_queue, NULL) != noErr)
	{
		FuaAudioSinkClose();
		if (err != NULL) *err = "AudioQueueStart failed";
		return false;
	}

	return true;
}

void FuaAudioSinkClose()
{
	if (s_queue != NULL)
	{
		// true == stop synchronously, so the callback is not still running when s_fill is cleared.
		AudioQueueStop(s_queue, true);
		AudioQueueDispose(s_queue, true);   // disposes its buffers with it
		s_queue = NULL;
	}

	for (int i = 0; i < kBufferCount; ++i)
	{
		s_buffers[i] = NULL;
	}

	s_fill = NULL;
}

const char *FuaAudioSinkName()
{
	return "AudioQueue";
}

} // namespace zx
