#pragma once

#include "decision_tree.h"

class LUT : public DecisionTree::Component {
public:
	LUT(unsigned int sz, unsigned int off)
	 : DecisionTree::Component(sz, off)
	{ }

	// grp maps from virtual input to real
	void write(Bitmask &out, const BitGroup &grp, const LogicExpression &expr) const;
};
