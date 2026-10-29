
#include "vgen/structure/BoundingBox.h"
#include "vgen/VgenTags.h"

namespace vgen {

Unique<IntArrayTag> BoundingBox::createTag() const {
	Unique<IntArrayTag> tag = make_unique<IntArrayTag>();
	int vals[6] = {x0, y0, z0, x1, y1, z1};
	tag->mValues.assign(vals, vals + 6);
	return tag;
}

} // namespace vgen

