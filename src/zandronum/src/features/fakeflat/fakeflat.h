// [rc4l] A sector as the view sees it through a height transfer (gl_FakeFlat), whether the view area changes across a line (gl_CheckViewArea), and whether a line shuts the view (gl_CheckClip): moved here from gl/scene/gl_fakeflat.cpp so the backend's walks and bake read them without GL.

#ifndef ZX_FAKEFLAT_H
#define ZX_FAKEFLAT_H

#include "features/rendersettings/rendersettings.h"   // [rc4l] area_t and in_area

struct side_t;
struct sector_t;
struct vertex_t;

bool gl_CheckClip(side_t * sidedef, sector_t * frontsector, sector_t * backsector);
void gl_CheckViewArea(vertex_t *v1, vertex_t *v2, sector_t *frontsector, sector_t *backsector);

sector_t * gl_FakeFlat(sector_t * sec, sector_t * dest, area_t in_area, bool back);
inline sector_t * gl_FakeFlat(sector_t * sec, sector_t * dest, bool back)
{
	return gl_FakeFlat(sec, dest, in_area, back);
}


namespace zx { namespace fakeflat {

// [rc4l] Whether a sector takes another's heights (Transfer_Heights), whose planes then depend on where the viewer stands.
bool ViewDependent(const sector_t *s);

// [rc4l] The view area the backend draws height transfers for (a kViewArea, GL's area_t); true when it changed.
bool SetTransferArea(int area);

// [rc4l] s as the backend's view sees it: through a height transfer for that view area (gl_FakeFlat), else s itself.
sector_t *TransferSector(sector_t *s, sector_t *scratch, bool back);

}} // namespace zx::fakeflat

#endif // ZX_FAKEFLAT_H
