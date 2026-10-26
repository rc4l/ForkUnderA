// [rc4l] Models as game data: the MD2, MD3 and voxel formats, their frames and surfaces and geometry on the CPU, and MODELDEF's mapping of sprite frames to models; moved here from gl/models/gl_models.h so both renderers read them, each drawing them its own way.

#ifndef ZX_MODELS_H
#define ZX_MODELS_H

#include "features/models/modelgeometry.h"
#include "p_pspr.h"
#include "r_data/voxels.h"
#include "xs_Float.h"
#include "textures/textures.h"


#define MAX_LODS			4

enum { VX, VZ, VY };

#define MD2_MAGIC			0x32504449
#define DMD_MAGIC			0x4D444D44
#define MD3_MAGIC			0x33504449
#define NUMVERTEXNORMALS	162
#define MD3_MAX_SURFACES	32

FTexture * LoadSkin(const char * path, const char * fn);

// [JM] Necessary forward declaration
typedef struct FSpriteModelFrame FSpriteModelFrame;

// [rc4l] One surface of a model frame as a renderer draws it: its skin, its two keyframes' first vertices in the geometry, and its triangles' first index and count.
struct FModelSurface
{
	FTexture *skin;
	unsigned int base1, base2;
	unsigned int firstIndex, count;
};

class FModel
{
public:

	FModel()
	{
	}
	virtual ~FModel();

	virtual bool Load(const char * fn, int lumpnum, const char * buffer, int length) = 0;
	virtual int FindFrame(const char * name) = 0;
	// [rc4l] The surfaces a frame pair draws, after BuildGeometry; how many, at most MD3_MAX_SURFACES.
	virtual int FrameSurfaces(FTexture * skin, int frame, int frame2, FModelSurface * out) = 0;
	// [rc4l] Fill mGeometry from the model's file, once; FrameSurfaces' offsets are into it.
	virtual void BuildGeometry() = 0;
	virtual float getAspectFactor() { return 1.f; }
	// [rc4l] Whether two keyframes blend (a voxel has one), and whether its skin is sampled unfiltered (a voxel's palette).
	virtual bool Interpolates() const { return true; }
	virtual bool NoFilter() const { return false; }

	// [rc4l] The geometry, on the CPU, which each renderer draws from its own way.
	FModelGeometry mGeometry;
	const FSpriteModelFrame *curSpriteMDLFrame;
	int curMDLIndex;
	void PushSpriteMDLFrame(const FSpriteModelFrame *smf, int index) { curSpriteMDLFrame = smf; curMDLIndex = index; };

	FString mFileName;
};

class FDMDModel : public FModel
{
protected:

	struct FTriangle
	{
		short           vertexIndices[3];
		short           textureIndices[3];
	};


	struct DMDHeader
	{
		int             magic;
		int             version;
		int             flags;
	};

	struct DMDModelVertex
	{
		float           xyz[3];
	};

	struct FTexCoord
	{
		short           s, t;
	};

	struct FGLCommandVertex
	{
		float           s, t;
		int             index;
	};

	struct DMDInfo
	{
		int             skinWidth;
		int             skinHeight;
		int             frameSize;
		int             numSkins;
		int             numVertices;
		int             numTexCoords;
		int             numFrames;
		int             numLODs;
		int             offsetSkins;
		int             offsetTexCoords;
		int             offsetFrames;
		int             offsetLODs;
		int             offsetEnd;
	};

	struct ModelFrame
	{
		char            name[16];
		unsigned int vindex;
	};

	struct ModelFrameVertexData
	{
		DMDModelVertex *vertices;
		DMDModelVertex *normals;
	};

	struct DMDLoDInfo
	{
		int             numTriangles;
		int             numGlCommands;
		int             offsetTriangles;
		int             offsetGlCommands;
	};

	struct DMDLoD
	{
		FTriangle		* triangles;
	};


	int				mLumpNum;
	DMDHeader	    header;
	DMDInfo			info;
	FTexture **		skins;
	ModelFrame  *	frames;
	bool			allowTexComp;  // Allow texture compression with this.

	// Temp data only needed for buffer construction
	FTexCoord *		texCoords;
	ModelFrameVertexData *framevtx;
	DMDLoDInfo		lodInfo[MAX_LODS];
	DMDLoD			lods[MAX_LODS];

public:
	FDMDModel()
	{
		mLumpNum = -1;
		frames = NULL;
		skins = NULL;
		for (int i = 0; i < MAX_LODS; i++)
		{
			lods[i].triangles = NULL;
		}
		info.numLODs = 0;
		texCoords = NULL;
		framevtx = NULL;
	}
	virtual ~FDMDModel();

	virtual bool Load(const char * fn, int lumpnum, const char * buffer, int length);
	virtual int FindFrame(const char * name);
	virtual int FrameSurfaces(FTexture * skin, int frame, int frame2, FModelSurface * out);
	virtual void LoadGeometry();
	void UnloadGeometry();
	void BuildGeometry();

};

// This uses the same internal representation as DMD
class FMD2Model : public FDMDModel
{
public:
	FMD2Model() {}
	virtual ~FMD2Model();

	virtual bool Load(const char * fn, int lumpnum, const char * buffer, int length);
	virtual void LoadGeometry();

};


class FMD3Model : public FModel
{
	struct MD3Tag
	{
		// Currently I have no use for this
	};

	struct MD3TexCoord
	{
		float s,t;
	};

	struct MD3Vertex
	{
		float x,y,z;
		float nx,ny,nz;
	};

	struct MD3Triangle
	{
		int VertIndex[3];
	};

	struct MD3Surface
	{
		int numVertices;
		int numTriangles;
		int numSkins;

		FTexture ** skins;
		MD3Triangle * tris;
		MD3TexCoord * texcoords;
		MD3Vertex * vertices;

		unsigned int vindex;	// contains numframes arrays of vertices
		unsigned int iindex;

		MD3Surface()
		{
			tris=NULL;
			vertices=NULL;
			texcoords=NULL;
			vindex = iindex = UINT_MAX;
		}

		~MD3Surface()
		{
			if (skins) delete [] skins;
			UnloadGeometry();
		}

		void UnloadGeometry()
		{
			if (tris) delete [] tris;
			if (vertices) delete [] vertices;
			if (texcoords) delete [] texcoords;
			tris = NULL;
			vertices = NULL;
			texcoords = NULL;
		}
	};

	struct MD3Frame
	{
		// The bounding box information is of no use in the Doom engine
		// That will still be done with the actor's size information.
		char Name[16];
		float origin[3];
	};

	int numFrames;
	int numTags;
	int numSurfaces;
	int mLumpNum;

	MD3Frame * frames;
	MD3Surface * surfaces;

public:
	FMD3Model() { }
	virtual ~FMD3Model();

	virtual bool Load(const char * fn, int lumpnum, const char * buffer, int length);
	virtual int FindFrame(const char * name);
	virtual int FrameSurfaces(FTexture * skin, int frame, int frame2, FModelSurface * out);
	void LoadGeometry();
	void BuildGeometry();
};

struct FVoxelVertexHash
{
	// Returns the hash value for a key.
	hash_t Hash(const FModelVertex &key)
	{
		int ix = xs_RoundToInt(key.x);
		int iy = xs_RoundToInt(key.y);
		int iz = xs_RoundToInt(key.z);
		return (hash_t)(ix + (iy<<9) + (iz<<18));
	}

	// Compares two keys, returning zero if they are the same.
	int Compare(const FModelVertex &left, const FModelVertex &right)
	{
		return left.x != right.x || left.y != right.y || left.z != right.z || left.u != right.u || left.v != right.v;
	}
};

struct FIndexInit
{
	void Init(unsigned int &value)
	{
		value = 0xffffffff;
	}
};

typedef TMap<FModelVertex, unsigned int, FVoxelVertexHash, FIndexInit> FVoxelMap;


class FVoxelModel : public FModel
{
protected:
	FVoxel *mVoxel;
	bool mOwningVoxel;	// if created through MODELDEF deleting this object must also delete the voxel object
	FTexture *mPalette;
	unsigned int mNumIndices;
	TArray<FModelVertex> mVertices;
	TArray<unsigned int> mIndices;

	void MakeSlabPolys(int x, int y, kvxslab_t *voxptr, FVoxelMap &check);
	void AddFace(int x1, int y1, int z1, int x2, int y2, int z2, int x3, int y3, int z3, int x4, int y4, int z4, BYTE color, FVoxelMap &check);
	unsigned int AddVertex(FModelVertex &vert, FVoxelMap &check);

public:
	FVoxelModel(FVoxel *voxel, bool owned);
	~FVoxelModel();
	bool Load(const char * fn, int lumpnum, const char * buffer, int length);
	void Initialize();
	virtual int FindFrame(const char * name);
	virtual int FrameSurfaces(FTexture * skin, int frame, int frame2, FModelSurface * out);
	FTexture *GetPaletteTexture() const { return mPalette; }
	void BuildGeometry();
	float getAspectFactor();
	bool Interpolates() const { return false; }
	bool NoFilter() const { return true; }
};



#define MAX_MODELS_PER_FRAME 4

//
// [BB] Model rendering flags.
//
enum
{
	// [BB] Color translations for the model skin are ignored. This is
	// useful if the skin texture is not using the game palette.
	MDL_IGNORETRANSLATION			= 1,
	MDL_PITCHFROMMOMENTUM			= 2,
	MDL_ROTATING					= 4,
	MDL_INTERPOLATEDOUBLEDFRAMES	= 8,
	MDL_NOINTERPOLATION				= 16,
	MDL_INHERITACTORPITCH			= 32,
	MDL_INHERITACTORROLL			= 64, // useless for now
	MDL_ALIGNANGLE					= 128,
	MDL_ALIGNPITCH					= 256,
	MDL_ROLLAGAINSTANGLE			= 512,
	MDL_DONTCULLBACKFACES			= 1024,	// [Doruk-Aksoy] UZDoom's 1<<8, renumbered past Zandronum's flags.
	MDL_FORCECULLBACKFACES			= 2048,	// [Doruk-Aksoy] UZDoom's 1<<14, renumbered likewise.
};

struct FSpriteModelFrame
{
	FModel * models[MAX_MODELS_PER_FRAME];
	FTexture * skins[MAX_MODELS_PER_FRAME];
	FTexture * surfaceskins[MAX_MODELS_PER_FRAME][MD3_MAX_SURFACES];
	int modelframes[MAX_MODELS_PER_FRAME];
	float xscale, yscale, zscale;
	// [BB] Added zoffset, rotation parameters and flags.
	// Added xoffset, yoffset
	float xoffset, yoffset, zoffset;
	float xrotate, yrotate, zrotate;
	float rotationCenterX, rotationCenterY, rotationCenterZ;
	float rotationSpeed;
	unsigned int flags;
	const PClass * type;
	short sprite;
	short frame;
	FState * state;	// for later!
	int hashnext;
	angle_t angleoffset;
	// added pithoffset, rolloffset.
	float pitchoffset, rolloffset; // I don't want to bother with type transformations, so I made this variables float.
};

void gl_InitModels();
FSpriteModelFrame * gl_FindModelFrame(const PClass * ti, int sprite, int frame, bool dropped);
bool gl_IsHUDModelFor (const PClass *weapon, FState *state);   // [rc4l] whether MODELDEF draws a weapon's state as a HUD model

// [rc4l] Every model MODELDEF and VOXELDEF loaded, for a renderer that keeps something per model.
int ModelCount();
FModel *ModelAt(int index);

#endif // ZX_MODELS_H
