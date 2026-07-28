// [rc4l] Implementation of the vertex-layout helpers. No engine deps.
#include "features/hwrender/computation/vertexformat_compute.h"

namespace hwrender
{

int ComputeAttribSizeBytes(const VertexAttrib &a)
{
	const int componentBytes = (a.type == AttribType::Float32) ? 4 : 1;
	return a.components * componentBytes;
}

int ComputeAttribOffsets(const VertexAttrib *attribs, int count, int *outOffsets)
{
	int offset = 0;
	for (int i = 0; i < count; i++)
	{
		outOffsets[i] = offset;
		offset += ComputeAttribSizeBytes(attribs[i]);
	}
	return offset;
}

int ComputeVertexStride(const VertexAttrib *attribs, int count)
{
	int stride = 0;
	for (int i = 0; i < count; i++)
		stride += ComputeAttribSizeBytes(attribs[i]);
	return stride;
}

} // namespace hwrender
