#include "components.h"

void LUT::write(Bitmask &out, const BitGroup &grp, const LogicExpression &expr) const
{
	for (unsigned int bit = 0; bit < (1ul << inputSize); ++bit) {
		DynamicBitmask mask;
		MappedBitmask map(mask, grp);
		map.writeInteger(bit);
		out.write(bit, expr.resolveLogic(mask));
	}
	DEBUG("    LUT<%u> { %s } = %s\n", inputSize, expr.to_str().c_str(), out.to_str().c_str());
}
