// [rc4l] The ported backend reaches through globals our tree does not populate (zx_screen, its
// light/bone/viewpoint buffers, GLRenderer and its managers). A null one does not fault cleanly --
// the crash handler turns it into an uninterruptible spin, which is only findable by sampling a hung
// process. This turns that into a named error before anything draws.
#pragma once

// [rc4l] Which backend globals are populated. Kept as plain bools so the check is testable without a
// GL context or any of the real types.
struct BackendPrereqs
{
	bool frameBuffer;
	bool lightBuffer;
	bool boneBuffer;
	bool viewpointBuffer;
	bool vertexData;
	bool renderer;
	bool samplerManager;
	bool shaderManager;
};

// [rc4l] Name of the first unmet prerequisite, or nullptr when all are met. Order matches the order
// the backend touches them, so the name points at what to fix first.
const char *ComputeMissingPrereq(const BackendPrereqs &prereqs);
