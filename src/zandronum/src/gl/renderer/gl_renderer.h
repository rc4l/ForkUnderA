#ifndef __GL_RENDERER_H
#define __GL_RENDERER_H

#include "r_defs.h"
#include "v_video.h"
#include "vectors.h"
#include "r_renderer.h"
#include "gl/data/gl_matrix.h"
#include "features/draw2d/computation/quad2d_compute.h"   // [rc4l] Quad2D
#include <vector>

struct particle_t;
class FCanvasTexture;
class FFlatVertexBuffer;
class FSkyVertexBuffer;
class OpenGLFrameBuffer;
struct FDrawInfo;
struct pspdef_t;
class FShaderManager;
class GLPortal;
class FLightBuffer;
class FSamplerManager;

enum SectorRenderFlags
{
	// This is used to avoid creating too many drawinfos
	SSRF_RENDERFLOOR = 1,
	SSRF_RENDERCEILING = 2,
	SSRF_RENDER3DPLANES = 4,
	SSRF_RENDERALL = 7,
	SSRF_PROCESSED = 8,
	SSRF_SEEN = 16,
};

struct GL_IRECT
{
	int left,top;
	int width,height;


	void Offset(int xofs,int yofs)
	{
		left+=xofs;
		top+=yofs;
	}
};


class FGLRenderer
{
public:

	OpenGLFrameBuffer *framebuffer;
	GLPortal *mCurrentPortal;
	int mMirrorCount;
	int mPlaneMirrorCount;
	int mLightCount;
	float mCurrentFoV;
	AActor *mViewActor;
	FShaderManager *mShaderManager;
	FSamplerManager *mSamplerManager;
	int gl_spriteindex;
	unsigned int mFBID;
	// [rc4l] video-scale: the current "screen" render target -- 0 (backbuffer) normally, or the
	// framebuffer's scale FBO when internal-resolution scaling is active. EndOffscreen restores to
	// this instead of hard-coding 0, so camera textures don't unbind the scale buffer.
	unsigned int mOutputFB;

	
	float mSky1Pos, mSky2Pos;

	FRotator mAngles;
	FVector2 mViewVector;

	FFlatVertexBuffer *mVBO;
	FSkyVertexBuffer *mSkyVBO;
	FLightBuffer *mLights;
	zx::draw2d::PolyDraw mPoly;                    // [rc4l] FillSimplePoly's points and texture coordinates, worked out by features/draw2d
	std::vector<zx::hwrender::Quad2D> mPolyTris;   // [rc4l] and its fan, which PreparePoly works out with them


	FGLRenderer(OpenGLFrameBuffer *fb);
	~FGLRenderer() ;

	angle_t FrustumAngle();
	void SetViewArea();
	void ResetViewport();
	void SetViewport(GL_IRECT *bounds);
	sector_t *RenderViewpoint (AActor * camera, GL_IRECT * bounds, float fov, float ratio, float fovratio, bool mainview, bool toscreen);
	void BeginFrame();
	void RenderView(player_t *player);
	void SetViewAngle(angle_t viewangle);
	void SetupView(fixed_t viewx, fixed_t viewy, fixed_t viewz, angle_t viewangle, bool mirror, bool planemirror);

	void Initialize();

	void CreateScene();
	void RenderScene(int recursion);
	void RenderTranslucent();
	// [rc4l] The same pass without the back-to-front sort, for the standalone path -- the backend
	// sorts the sprites it is handed, so sorting them here twice is work thrown away.
	void DrawScene(bool toscreen = false);
	void DrawBlend(sector_t * viewsector);

	void DrawPSprite (player_t * player,pspdef_t *psp,fixed_t sx, fixed_t sy, bool hudModelStep, int OverrideShader, bool alphatexture, int blend2d);
	void DrawPlayerSprites(sector_t * viewsector, bool hudModelStep);
	void DrawTargeterSprites();

	void Begin2D();
	void ClearBorders();
	void DrawTexture(FTexture *img, DCanvas::DrawParms &parms);
	void DrawLine(int x1, int y1, int x2, int y2, int palcolor, uint32 color);
	void DrawPixel(int x1, int y1, int palcolor, uint32 color);
	void Dim(PalEntry color, float damount, int x1, int y1, int w, int h);
	void FlatFill (int left, int top, int right, int bottom, FTexture *src, bool local_origin);
	void Clear(int left, int top, int right, int bottom, int palcolor, uint32 color);

	void ProcessLowerMiniseg(seg_t *seg, sector_t * frontsector, sector_t * backsector);
	void ProcessSprite(AActor *thing, sector_t *sector);
	void ProcessParticle(particle_t *part, sector_t *sector);
	void ProcessSector(sector_t *fakesector);
	void SetupLevel();

	void SetFixedColormap (player_t *player);
	void WriteSavePic (player_t *player, FILE *file, int width, int height);
	void EndDrawScene(sector_t * viewsector);
	void Flush() {}

	void SetProjection(float fov, float ratio, float fovratio, float eyeShift=0); // [BB] Added eyeShift from GZ3Doom.
	void SetProjection(VSMatrix matrix); // [rc4l] raw matrix input from stereo 3d modes
	void SetViewMatrix(fixed_t viewx, fixed_t viewy, fixed_t viewz, bool mirror, bool planemirror);
	void ProcessScene(bool toscreen = false);

	bool StartOffscreen();
	void EndOffscreen();

	void FillSimplePoly(FTexture *texture, FVector2 *points, int npoints,
		double originx, double originy, double scalex, double scaley,
		angle_t rotation, FDynamicColormap *colormap, int lightlevel);
};

// Global functions. Make them members of GLRenderer later?
void gl_RenderBSPNode (void *node);

#include "features/fakeflat/fakeflat.h"   // [rc4l] gl_FakeFlat, gl_CheckViewArea, gl_CheckClip, area_t and in_area

struct TexFilter_s
{
	int minfilter;
	int magfilter;
	bool mipmapping;
} ;


extern FGLRenderer *GLRenderer;

#endif