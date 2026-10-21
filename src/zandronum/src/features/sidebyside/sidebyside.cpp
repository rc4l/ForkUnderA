// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/sidebyside/sidebyside.h"

#include "gl/system/gl_framebuffer.h"
#include "gl/renderer/gl_renderer.h"   // [rc4l] GLRenderer, which draws GL's 2D

#if defined(_WIN32) && defined(FUA_DILIGENT)
#include "features/draw2d/record.h"
#include "features/draw2d/record2d.h"      // [rc4l] Record2DCanvas
#include "features/hwrender/backendonly.h"   // [rc4l] FrameRouteNow

// [rc4l] GL's frame buffer, each 2D draw also recorded for the backend's window; GL leaves out one meant for the backend's window alone, and draws and presents nothing while its window is covered and not wanted.
class SideBySideFrameBuffer : public OpenGLFrameBuffer
{
	DECLARE_CLASS(SideBySideFrameBuffer, OpenGLFrameBuffer)
public:
	SideBySideFrameBuffer() {}
	SideBySideFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen)
		: OpenGLFrameBuffer(hMonitor, width, height, bits, refreshHz, fullscreen) {}

	// [rc4l] Whether GL's window shows next frame, decided at the end of this one, since the frame's draws come before it is presented.
	void Update()
	{
		OpenGLFrameBuffer::Update();
		mPresents = zx::hwrender::FrameRouteNow(true, true).glDraws;
		zx::sidebyside::WatchGLForChecks();
	}

	void STACK_ARGS DrawTextureV(FTexture *img, double x, double y, uint32 tag, va_list tags)
	{
		DrawParms parms;
		if (!ParseDrawTextureTags(img, x, y, tag, tags, &parms, true)) return;
		m2D.Texture(img, parms);
		if (GLDraws()) GLRenderer->DrawTexture(img, parms);
	}
	void DrawLine(int x1, int y1, int x2, int y2, int palcolor, uint32 color)
	{
		m2D.Line(x1, y1, x2, y2, palcolor, color);
		if (GLDraws()) GLRenderer->DrawLine(x1, y1, x2, y2, palcolor, color);
	}
	void DrawPixel(int x, int y, int palcolor, uint32 color)
	{
		m2D.Pixel(x, y, palcolor, color);
		if (GLDraws()) GLRenderer->DrawPixel(x, y, palcolor, color);
	}
	void Clear(int left, int top, int right, int bottom, int palcolor, uint32 color)
	{
		m2D.Clear(left, top, right, bottom, palcolor, color);
		if (GLDraws()) GLRenderer->Clear(left, top, right, bottom, palcolor, color);
	}
	// [rc4l] As GL's: the view's blend belongs to the view, so the screen dim ignores the colour.
	void Dim(PalEntry color) { OpenGLFrameBuffer::Dim(color); }
	void Dim(PalEntry color, float damount, int x1, int y1, int w, int h)
	{
		m2D.Dim(color, damount, x1, y1, w, h);
		if (GLDraws()) GLRenderer->Dim(color, damount, x1, y1, w, h);
	}
	void FlatFill(int left, int top, int right, int bottom, FTexture *src, bool local_origin)
	{
		m2D.FlatFill(left, top, right, bottom, src, local_origin);
		if (GLDraws()) GLRenderer->FlatFill(left, top, right, bottom, src, local_origin);
	}
	void FillSimplePoly(FTexture *tex, FVector2 *points, int npoints, double originx, double originy, double scalex, double scaley,
		angle_t rotation, FDynamicColormap *colormap, int lightlevel)
	{
		m2D.Poly(tex, points, npoints, originx, originy, scalex, scaley, rotation, colormap, lightlevel);
		if (GLDraws()) GLRenderer->FillSimplePoly(tex, points, npoints, originx, originy, scalex, scaley, rotation, colormap, lightlevel);
	}

private:
	bool GLDraws() const { return GLRenderer != NULL && mPresents && zx::draw2d::Get2DTarget() != zx::draw2d::k2DBackendOnly; }

	zx::draw2d::Record2DCanvas m2D;
};

IMPLEMENT_CLASS(SideBySideFrameBuffer)
#endif

namespace zx { namespace sidebyside {

DFrameBuffer *CreateGLFrameBuffer(void *hMonitor, int width, int height, int bits, int refreshHz, bool fullscreen)
{
#if defined(_WIN32) && defined(FUA_DILIGENT)
	return new SideBySideFrameBuffer(hMonitor, width, height, bits, refreshHz, fullscreen);
#else
	return new OpenGLFrameBuffer(hMonitor, width, height, bits, refreshHz, fullscreen);
#endif
}

}} // namespace zx::sidebyside
