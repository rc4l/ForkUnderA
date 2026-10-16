#ifndef __BACKENDFB_H__
#define __BACKENDFB_H__

// [rc4l] The frame buffer the backend's own derives from on this platform: the Cocoa window, made without its GL context.

#include "gl/system/gl_system.h"   // [rc4l] before sdlglvideo.h, whose GL headers lean on it
#include "sdlglvideo.h"

typedef SDLGLFB BackendFrameBufferBase;

#endif
