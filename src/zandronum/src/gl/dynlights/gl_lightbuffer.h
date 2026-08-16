#ifndef __GL_LIGHTBUFFER_H
#define __GL_LIGHTBUFFER_H

#include "tarray.h"
struct FDynLightData;

class FLightBuffer
{
	TArray<int> mIndices;
	unsigned int mBufferId;
	float * mBufferPointer;

	unsigned int mBufferType;
	unsigned int mIndex;
	unsigned int mUploadIndex;
	unsigned int mLastMappedIndex;
	unsigned int mBlockAlign;
	unsigned int mBlockSize;
	unsigned int mBufferSize;
	unsigned int mByteSize;

public:

	FLightBuffer();
	~FLightBuffer();
	void Clear();
	int UploadLights(FDynLightData &data);
	void Begin();
	void Finish();
	int BindUBO(unsigned int index);
	unsigned int GetBlockSize() const { return mBlockSize; }
	unsigned int GetBufferType() const { return mBufferType; }
	unsigned int GetIndexPtr() const { return mIndices.Size();	}
	void StoreIndex(int index) { mIndices.Push(index); }
	int GetIndex(int i) const { return mIndices[i];	}

	// [rc4l] features/hwrender: a CPU-side copy of exactly what UploadLights wrote to the GPU.
	//
	// The real buffer is a persistently-mapped (or glMapBufferRange'd) GL object, so a foreign
	// backend cannot read it and cannot reuse it. Mirroring the writes is a few memcpys against data
	// that is being assembled anyway, and it keeps the layout -- header vec4 then pairs of
	// (pos.xyz, radius) / (rgb, 0) -- defined in exactly one place. Indices handed out by
	// UploadLights are valid in both copies.
	const float *MirrorData(int &floats) const
	{
		floats = (int)mMirror.Size();
		return floats ? &mMirror[0] : NULL;
	}

private:
	TArray<float> mMirror;
};

#endif

