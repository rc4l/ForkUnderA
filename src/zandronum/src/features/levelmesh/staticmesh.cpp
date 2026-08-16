// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 rc4l

#include "gl/system/gl_system.h"
#include "features/levelmesh/staticmesh.h"

#include "gl/data/gl_vertexbuffer.h"
#include "gl/system/gl_interface.h"
#include "gl/shaders/gl_shader.h"
#include "gl/renderer/gl_renderstate.h"
#include "gl/renderer/gl_lightdata.h"
#include "tarray.h"
#include "c_dispatch.h"
#include "c_console.h"
#include "i_system.h"

EXTERN_CVAR(Int, gl_fogmode)

namespace zx { namespace levelmesh {

// [rc4l] Hard cap. An earlier attempt at this feature grew without bound and reached 47 GB before
// the process died, so this one refuses to grow instead of trusting the invalidation logic. Two
// million vertices is 40 MB and comfortably past what any measured map needs (Sunder MAP20's whole
// worst-case wall budget is 4.5 M vertices, and only the visible subset is ever baked).
static const unsigned int kMaxVertices = 2000000;

static TArray<FFlatVertex> g_verts;        // CPU mirror; the GPU copy is written from this
static unsigned int        g_used = 0;
static unsigned int        g_dirtyLo = 0;  // pending upload range, in vertices
static unsigned int        g_dirtyHi = 0;
static unsigned int        g_vbo = 0;
static unsigned int        g_vao = 0;
static bool                g_full = false;
static int                 g_pieces = 0, g_reallocs = 0;
static unsigned int        g_ibo = 0;   // declared here; the indexed path below fills it
static TArray<MeshPiece>   g_pieceList; // declared here; the piece registry below fills it
// [rc4l] range offset -> index into g_pieceList, so re-registering a piece is a lookup not a scan.
static TMap<unsigned int, unsigned int> g_pieceByOffset;

// [rc4l] See staticmesh.h. This exists so the backend never re-derives lighting: a second
// implementation of gl_SetColor/gl_SetFog drifted, silently losing rellight, extralight and
// desaturation, and produced a Vulkan render visibly brighter and flatter than GL's.
void CaptureShading(int lightlevel, int rellight, const FColormap &cm, MeshPiece &out, bool noFog)
{
	FColormap local = cm;
	gl_SetColor(lightlevel, rellight, local, 1.0f);
	if (noFog) gl_SetFog(255, 0, NULL, false);
	else       gl_SetFog(lightlevel, rellight, &local, false);

	gl_RenderState.GetColorRGB(out.colorR, out.colorG, out.colorB);
	const float *lp = gl_RenderState.GetLightParms();
	out.softLight    = lp[3];    // -1 unless lightmode 8
	out.fogDensity   = lp[2];    // already scaled by -log2(e)/64000, ready for exp2()
	out.fogColor     = gl_RenderState.GetFogColor().d & 0xffffff;
	out.desaturation = gl_RenderState.GetDesaturation();

	// FRenderState::Apply turns the enable plus the fog colour into uFogEnabled's sign.
	if (!gl_RenderState.IsFogEnabled()) out.fogMode = 0;
	else out.fogMode = (out.fogColor == 0) ? (int)gl_fogmode : -(int)gl_fogmode;

	// Opaque unless the caller says otherwise -- walls and flats always are.
	out.blendMode = 0;
	out.alpha = 1.f;
	out.sortX = out.sortY = out.sortZ = 0.f;
	out.baseTex = NULL;
	out.dynLightIndex = -1;
	out.normX = 0.f; out.normY = 1.f; out.normZ = 0.f;
}

// ---------------------------------------------------------------------------
// Dynamic stream -- see staticmesh.h
// ---------------------------------------------------------------------------

static TArray<FFlatVertex> g_dynVerts;
static TArray<MeshPiece>   g_dynPieces;

static unsigned int g_dynGen = 1;

void DynClear()
{
	// Clear, not free: the arrays settle at the frame's high-water mark after a few frames and stop
	// allocating entirely.
	g_dynVerts.Clear();
	g_dynPieces.Clear();
	g_dynGen++;
}

unsigned int DynGeneration() { return g_dynGen; }

void DynAppend(const FFlatVertex *verts, int count, MeshPiece proto)
{
	if (verts == NULL || count <= 0) return;
	// [rc4l] A sane ceiling, so a pathological frame cannot eat memory the way the first wall cache
	// did. 300k vertices is 100k triangles of sprites, far past anything a Doom frame produces.
	if (g_dynVerts.Size() + (unsigned)count > 300000) return;

	proto.range.offset = g_dynVerts.Size();
	proto.range.count = (unsigned)count;
	for (int i = 0; i < count; i++) g_dynVerts.Push(verts[i]);
	g_dynPieces.Push(proto);
}

const FFlatVertex *DynVertices(int &count)
{
	count = (int)g_dynVerts.Size();
	return count ? &g_dynVerts[0] : NULL;
}

const MeshPiece *DynPieces(int &count)
{
	count = (int)g_dynPieces.Size();
	return count ? &g_dynPieces[0] : NULL;
}

void MeshInitForLevel()
{
	MeshFreeLevel();
	g_verts.Clear();
	g_used = 0;
	g_dirtyLo = g_dirtyHi = 0;
	g_full = false;
	g_pieces = g_reallocs = 0;
	g_pieceList.Clear();
	g_pieceByOffset.Clear();
}

void MeshFreeLevel()
{
	if (g_ibo) { glDeleteBuffers(1, &g_ibo); g_ibo = 0; }
	if (g_vbo) { glDeleteBuffers(1, &g_vbo); g_vbo = 0; }
	if (g_vao) { glDeleteVertexArrays(1, &g_vao); g_vao = 0; }
	g_verts.Clear();
	g_used = 0;
	g_dirtyLo = g_dirtyHi = 0;
	g_full = false;
	g_pieces = g_reallocs = 0;
}

// [rc4l] Created lazily so a level with no baked geometry costs no GL objects, and so creation
// happens on the render thread with a context current.
static bool EnsureBuffer();
static bool EnsureBuffer()
{
	if (g_vbo) return true;

	glGenBuffers(1, &g_vbo);
	glGenVertexArrays(1, &g_vao);
	if (!g_vbo || !g_vao) return false;

	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glBufferData(GL_ARRAY_BUFFER, kMaxVertices * sizeof(FFlatVertex), NULL, GL_STATIC_DRAW);

	// [rc4l] Same attribute layout as FFlatVertexBuffer, so the existing shaders bind unchanged.
	glBindVertexArray(g_vao);
	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glVertexAttribPointer(VATTR_VERTEX,   3, GL_FLOAT, false, sizeof(FFlatVertex), &VTO->x);
	glVertexAttribPointer(VATTR_TEXCOORD, 2, GL_FLOAT, false, sizeof(FFlatVertex), &VTO->u);
	glEnableVertexAttribArray(VATTR_VERTEX);
	glEnableVertexAttribArray(VATTR_TEXCOORD);
	glBindVertexArray(0);
	return true;
}

bool MeshStore(MeshRange &range, const FFlatVertex *verts, int count)
{
	if (verts == NULL || count <= 0) return false;
	if (g_full) return false;

	// Reuse in place when the size is unchanged -- the normal case, since a wall's vertex count only
	// moves when its split topology does.
	if (range.count == (unsigned int)count)
	{
		memcpy(&g_verts[range.offset], verts, count * sizeof(FFlatVertex));
		if (range.offset < g_dirtyLo) g_dirtyLo = range.offset;
		if (range.offset + count > g_dirtyHi) g_dirtyHi = range.offset + count;
		return true;
	}

	if (range.count != 0) g_reallocs++;   // size changed: the old range is abandoned

	if (g_used + (unsigned int)count > kMaxVertices)
	{
		g_full = true;
		return false;
	}

	range.offset = g_used;
	range.count = (unsigned int)count;
	g_used += count;
	g_verts.Resize(g_used);
	memcpy(&g_verts[range.offset], verts, count * sizeof(FFlatVertex));

	if (range.offset < g_dirtyLo || g_dirtyHi == 0) g_dirtyLo = range.offset;
	g_dirtyHi = g_used;
	g_pieces++;
	return true;
}

void MeshFlush()
{
	if (g_dirtyHi <= g_dirtyLo) return;
	if (!EnsureBuffer()) { g_dirtyLo = g_dirtyHi = 0; return; }

	glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
	glBufferSubData(GL_ARRAY_BUFFER,
		(GLintptr)(g_dirtyLo * sizeof(FFlatVertex)),
		(GLsizeiptr)((g_dirtyHi - g_dirtyLo) * sizeof(FFlatVertex)),
		&g_verts[g_dirtyLo]);

	g_dirtyLo = kMaxVertices;
	g_dirtyHi = 0;
}

bool MeshBind()
{
	if (!g_vbo || g_used == 0) return false;
	glBindVertexArray(g_vao);
	return true;
}

void MeshDraw(const MeshRange &range)
{
	if (range.count == 0) return;
	glDrawArrays(GL_TRIANGLES, range.offset, range.count);
}

void MeshDrawMulti(const MeshRange *ranges, int n)
{
	if (ranges == NULL || n <= 0) return;

	// [rc4l] One call for the whole run. This is the shape an indirect draw command takes later, so
	// the batching decision made here is the one a GPU culling pass would be writing.
	static TArray<GLint>   firsts;
	static TArray<GLsizei> counts;
	firsts.Clear();
	counts.Clear();
	for (int i = 0; i < n; i++)
	{
		if (ranges[i].count == 0) continue;
		firsts.Push((GLint)ranges[i].offset);
		counts.Push((GLsizei)ranges[i].count);
	}
	if (firsts.Size() == 0) return;
	glMultiDrawArrays(GL_TRIANGLES, &firsts[0], &counts[0], (GLsizei)firsts.Size());
}

// ---------------------------------------------------------------------------
// Indexed path
// ---------------------------------------------------------------------------

static const unsigned int kMaxIndices = 4000000;   // 16 MB; refuses to grow, like the vertex arena

static TArray<unsigned int> g_indices;
static unsigned int         g_indexUsed = 0;
static unsigned int         g_runStart = 0;
static bool                 g_indexFull = false;

void MeshIndexBegin()
{
	g_indexUsed = 0;
	g_runStart = 0;
	g_indexFull = false;
	if (g_indices.Size() < kMaxIndices) g_indices.Resize(kMaxIndices);
}

bool MeshIndexAppend(const MeshRange &range)
{
	if (range.count == 0 || g_indexFull) return false;
	if (g_indexUsed + range.count > kMaxIndices) { g_indexFull = true; return false; }

	// [rc4l] The baked vertices are already a triangle list, so the indices are simply the range
	// walked in order. The point is not compression -- it is that a run of pieces becomes one
	// contiguous index span, which is what lets the whole run draw in a single call.
	unsigned int *dst = &g_indices[g_indexUsed];
	for (unsigned int i = 0; i < range.count; i++) dst[i] = range.offset + i;
	g_indexUsed += range.count;
	return true;
}

void MeshIndexEndRun(unsigned int &firstIndex, unsigned int &count)
{
	firstIndex = g_runStart;
	count = g_indexUsed - g_runStart;
	g_runStart = g_indexUsed;
}

void MeshIndexFlush()
{
	if (g_indexUsed == 0) return;
	if (!EnsureBuffer()) return;
	if (!g_ibo)
	{
		glGenBuffers(1, &g_ibo);
		if (!g_ibo) return;
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
		glBufferData(GL_ELEMENT_ARRAY_BUFFER, kMaxIndices * sizeof(unsigned int), NULL, GL_STREAM_DRAW);
		// [rc4l] The element buffer binding is VAO state, so record it in the mesh's VAO.
		glBindVertexArray(g_vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
		glBindVertexArray(0);
	}
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
	glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, g_indexUsed * sizeof(unsigned int), &g_indices[0]);
}

void MeshDrawIndexed(unsigned int firstIndex, unsigned int count)
{
	if (count == 0 || !g_ibo) return;
	glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_INT,
		(const void *)(size_t)(firstIndex * sizeof(unsigned int)));
}

void MeshRegisterPiece(const MeshPiece &piece)
{
	if (piece.range.count == 0) return;
	// [rc4l] A re-baked piece keeps its range, so replace the existing entry rather than appending --
	// otherwise a flickering-light sector would grow this list without bound, which is the exact
	// failure mode that killed an earlier version of the wall cache.
	//
	// Indexed by range offset rather than scanned. The scan was fine while the mesh only held what
	// the player had walked past, but a full-level bake registers tens of thousands of pieces in a
	// single frame and each one scanned every piece already registered -- quadratic, and the bake
	// frame is exactly when it hurts.
	unsigned *found = g_pieceByOffset.CheckKey(piece.range.offset);
	if (found != NULL && *found < g_pieceList.Size())
	{
		g_pieceList[*found] = piece;
		return;
	}
	const unsigned idx = g_pieceList.Push(piece);
	g_pieceByOffset.Insert(piece.range.offset, idx);
}

const MeshPiece *MeshPieces(int &count)
{
	count = (int)g_pieceList.Size();
	return count ? &g_pieceList[0] : NULL;
}

void MeshClearPieces()
{
	g_pieceList.Clear();
	g_pieceByOffset.Clear();
}

const FFlatVertex *MeshVertexData(int &count)
{
	count = (int)g_used;
	return (g_used > 0) ? &g_verts[0] : NULL;
}

void MeshGetStats(int &bakedPieces, int &vertices, int &bytes, int &reallocs)
{
	bakedPieces = g_pieces;
	vertices = (int)g_used;
	bytes = (int)(g_used * sizeof(FFlatVertex));
	reallocs = g_reallocs;
}

}} // namespace zx::levelmesh

//==========================================================================
//
// [rc4l] GL-side half of the backend benchmark.
//
// The Diligent scene bench and this one submit THE SAME baked geometry, the same number of times,
// and force a GPU sync before stopping the clock. Nothing else is equal between a Diligent frame and
// a real engine frame -- textures, lighting, sprites, the BSP walk -- so comparing those directly
// would be meaningless. Comparing pure submission of identical vertices is not.
//
//==========================================================================

CCMD( fua_gl_meshbench )
{
	const int frames = ( argv.argc( ) > 1 ) ? atoi( argv[1] ) : 400;
	int count = 0;
	if ( zx::levelmesh::MeshVertexData( count ) == NULL || count <= 0 )
	{
		Printf( "no baked geometry -- set gl_wallmesh 1 and walk the level first\n" );
		return;
	}
	if ( !zx::levelmesh::MeshBind( ) )
	{
		Printf( "mesh buffer not resident\n" );
		return;
	}

	for ( int i = 0; i < 20; i++ ) glDrawArrays( GL_TRIANGLES, 0, count );   // warmup
	glFinish( );

	const DWORD t0 = I_MSTime( );
	for ( int i = 0; i < frames; i++ ) glDrawArrays( GL_TRIANGLES, 0, count );
	glFinish( );
	const DWORD t1 = I_MSTime( );

	const double total = (double)( t1 - t0 );
	Printf( "GL mesh: %d draws, %d verts (%d tris), %.4f ms/draw\n",
		frames, count, count / 3, total / frames );
}
