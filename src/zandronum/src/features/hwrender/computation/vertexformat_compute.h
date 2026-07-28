// [rc4l] Pure vertex attribute size/offset/stride math for glVertexAttribPointer and Vulkan input descriptions.
#ifndef ZX_HWRENDER_VERTEXFORMAT_COMPUTE_H
#define ZX_HWRENDER_VERTEXFORMAT_COMPUTE_H

namespace hwrender
{

// [rc4l] Component storage type of a vertex attribute.
enum class AttribType
{
	Float32, // 4 bytes per component (position, uv, ...)
	UNorm8,  // 1 byte per component (packed color)
};

// [rc4l] One vertex attribute: a small vector of `components` values of `type`.
struct VertexAttrib
{
	int components;
	AttribType type;
};

// [rc4l] Size in bytes of a single attribute (components * component-size).
int ComputeAttribSizeBytes(const VertexAttrib &a);

// [rc4l] Total vertex stride in bytes for a tightly-packed layout of `count` attributes.
int ComputeVertexStride(const VertexAttrib *attribs, int count);

// [rc4l] Fill outOffsets[count] with each attribute's byte offset; returns the stride.
int ComputeAttribOffsets(const VertexAttrib *attribs, int count, int *outOffsets);

} // namespace hwrender

#endif // ZX_HWRENDER_VERTEXFORMAT_COMPUTE_H
