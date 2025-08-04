#pragma once

#include <vector>
#include <memory>
#include <map>

#include "bitmask.h"
#include "expr.h"
#include "wire_map.h"
#include "state.h"
#include "stew.h"

class DecisionTree {
public:
	class Component {
	public:
		unsigned int inputSize;
		unsigned int inputOffset;

		Component(unsigned int isz, unsigned int ioff)
		 : inputSize(isz), inputOffset(ioff)
		{ }

		virtual void write(Bitmask &out, const BitGroup &grp,
				const LogicExpression &expr) const = 0;
	};
private:
	WireMap wires;

	std::vector<std::shared_ptr<Component>> components;
	unsigned int nStaticComponents;
	unsigned int nConditionalComponents;
	unsigned int nInputs;
	unsigned int nVirtualInputs;
public:
	struct Config {
		WireMap::Config wires;
		std::list<std::shared_ptr<DecisionTree::Component>> staticComponents;
		std::list<std::shared_ptr<DecisionTree::Component>> condComponents;
	};

	DecisionTree(const Config &cfg);

	// split a virtual state into as many simplified states as necessary
	void splitState(std::list<std::shared_ptr<VirtualState>> &out,
		std::shared_ptr<VirtualState> vs,
		std::map<unsigned int, unsigned int> &stateMap) const;

	void writeState(Bitmask &out, const STEW &stew, const VirtualState &vs,
		std::map<unsigned int, unsigned int> &stateMap) const;
};
