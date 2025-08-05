#include <array>

#include "decision_tree.h"

DecisionTree::DecisionTree(const Config &cfg)
 : wires(cfg.wires),
   components(cfg.staticComponents.size() + cfg.condComponents.size()),
   nStaticComponents(cfg.staticComponents.size()),
   nConditionalComponents(cfg.condComponents.size()),
   nInputs(1ul << cfg.wires.muxes.nBits),
   nVirtualInputs(0)
{
	unsigned int comp = 0;

	for (std::shared_ptr<Component> c : cfg.staticComponents) {
		components[comp++] = c;
		nVirtualInputs += c->inputSize;
	}

	for (std::shared_ptr<Component> c : cfg.condComponents) {
		components[comp++] = c;
		nVirtualInputs += c->inputSize;
	}
}

#define ASSERTVS(vs, cond, msg) \
	ASSERTF((vs).filepos, cond, msg)

void DecisionTree::splitState(std::list<std::shared_ptr<VirtualState>> &out,
		std::shared_ptr<VirtualState> vs,
		std::map<unsigned int, unsigned int> &stateMap) const
{
	stateMap[vs->index] = out.size();

	while (vs->transitions.size() > nStaticComponents) {
		std::list<std::shared_ptr<StateTransition>> &xits = vs->transitions;

		// if there's only the fallthrough left, no split is needed
		if (xits.size() == nStaticComponents + 1 && xits.back()->isFallthrough(vs->index))
      {
         if (vs->index+1 == xits.back()->state)
		      vs->partial = true;
			break;
      }

		auto it = xits.begin();
		std::advance(it, nStaticComponents);

		std::shared_ptr<VirtualState> lower =
				std::make_shared<VirtualState>(vs->index, vs->filepos);
		lower->transitions.splice(lower->transitions.begin(), xits, it, xits.end());
		lower->conditionalOutputs = vs->conditionalOutputs;

		lower->collectSteadyState(vs->partialOutput);

		vs->partial = true;
		out.push_back(vs);
		vs = lower;
	}
	out.push_back(vs);
}

void DecisionTree::writeState(Bitmask &out, const STEW &stew, const VirtualState &vs,
		std::map<unsigned int, unsigned int> &stateMap) const
{
	const unsigned int nComponents = nStaticComponents + nConditionalComponents;
	unsigned int wireMapping[nVirtualInputs];
	unsigned int compMapping[nVirtualInputs];
	LogicExpression *exprs[nComponents];
	unsigned int inputReqCount[nInputs];
	DynamicBitmask inputReq[nInputs];
	unsigned int comp;

	DEBUG("STATE %d%s: (%lu transitions, %lu conditional outputs)\n",
			vs.index, vs.partial ? " (partial)" : "",
			vs.transitions.size(), vs.conditionalOutputs.size());

	for (comp = 0; comp < nComponents; ++comp)
		exprs[comp] = NULL;

	DEBUG("  Breakdown:\n");

	// write component output values and jump targets
	comp = 0;
	for (std::shared_ptr<StateTransition> x : vs.transitions) {
		DEBUG("    %s\n", x->to_str().c_str());

		STEW::Item stew_out = stew.slice(STEW::OUT, comp);
		ASSERTVS(vs, stew_out.type != STEW::NIL,
				"STEW OUT configuration doesn't match"
				" decision-tree configuration");
		BitmaskSlice slice_out(out, stew_out.offset, stew_out.size);

		if (comp != nStaticComponents) {
			STEW::Item stew_jmp = stew.slice(STEW::JMP, comp);
			ASSERTVS(vs, stew_jmp.type != STEW::NIL,
					"STEW JMP configuration doesn't match"
					" decision-tree configuration");
			BitmaskSlice slice_jmp(out, stew_jmp.offset, stew_jmp.size);

			if (stateMap.find(x->state) == stateMap.end()) {
				ASSERTVS(vs, x->state == vs.index,
						"Invalid jump to undefined state");
				stateMap[vs.index] = x->state;
			}
			x->writeState(slice_jmp, stateMap);
			exprs[comp] = x->expr;
		}

		x->writeOutput(slice_out);
		++comp;
	}

	// set INC bit if needed
	if (vs.partial) {
		STEW::Item inc = stew.slice(STEW::INC, 0);
		STEW::Item stew_out = stew.slice(STEW::OUT, comp);

		BitmaskSlice(out, stew_out.offset, stew_out.size).copy(vs.partialOutput);

		out.set(inc.offset);
	}

	// assign the conditional output expressions according to mapping
	for (std::shared_ptr<ConditionalOutput> outp : vs.conditionalOutputs) {
		DEBUG("    %s\n", outp->to_str().c_str());
		exprs[nStaticComponents + outp->output] = outp->expr;
	}

	for (unsigned int i = 0; i < nInputs; ++i)
		inputReqCount[i] = 0;

	// collect necessary inputs for each expression, per input
	for (comp = 0; comp < nComponents; ++comp) {
		std::shared_ptr<Component> c = components[comp];
		DynamicBitmask bitmask;

		if (exprs[comp])
			exprs[comp]->collectInputs(bitmask);
		// TODO: there may be some conditions in which a state can
		// be optimized; check
		ASSERTVS(vs, bitmask.count() <= c->inputSize,
				"State condition requires too many inputs");

		for (unsigned int i = bitmask.ffs(); i < bitmask.size(); i = bitmask.fns(i)) {
			DynamicBitmask &req = inputReq[i];
			unsigned int offset = c->inputOffset;

			for (unsigned int bit = 0; bit < c->inputSize; ++bit)
				req.set(bit + offset);

			inputReqCount[i]++;
		}

		for (unsigned int bit = 0; bit < c->inputSize; ++bit)
			compMapping[bit + c->inputOffset] = comp;
	}

	DynamicBitmask used;
	for (unsigned int i = 0; i < nVirtualInputs; ++i) {
		wireMapping[i] = 0;
	}

	// map each system input to virtual component input
	for (unsigned int i = 0; i < nInputs; ++i) {
		unsigned int &count = inputReqCount[i];
		DynamicBitmask &req = inputReq[i];

		while (count > 0) {
			std::list<unsigned int> which;
			DynamicBitmask mask(req);

			// we can only use a virtual input once
			for (unsigned int bit = used.ffs(); bit < used.size(); bit = used.fns(bit))
				mask.clear(bit);

			wires.bestFit(mask, count, which);

			for (unsigned int bit : which) {
				std::shared_ptr<Component> c = components[compMapping[bit]];

				if (req.get(bit))
					count -= 1;

				// component is no longer interested in this input
				for (unsigned int ibit = 0; ibit < c->inputSize; ++ibit)
					req.clear(c->inputOffset + ibit);

				wireMapping[bit] = i;
				used.set(bit);
			}
		}
	}

	DEBUG("  Components:\n");
	// write component conditional configuration (e.g. LUT table)
	MappedBitGroup mgrp(wireMapping, nVirtualInputs);
	for (comp = 0; comp < nComponents; ++comp) {
		std::shared_ptr<Component> c = components[comp];
		SliceBitGroup sgrp(mgrp, c->inputOffset, c->inputSize);
		STEW::Item stewi = stew.slice(STEW::CFG, comp);
		BitmaskSlice slice(out, stewi.offset, stewi.size);

		if (exprs[comp])
			c->write(slice, sgrp, *exprs[comp]);
		else if (comp < nStaticComponents)
			c->write(slice, sgrp, LogicTrueExpression());
		else
			c->write(slice, sgrp, LogicFalseExpression());
	}

	DEBUG("  Wire mapping:\n");
	for (unsigned int i = 0; i < nVirtualInputs; ++i) {
		DEBUG("    WIRE (virtual -> real) = { %2d -> %2d }[ Component %2d ]\n",
				i, wireMapping[i], compMapping[i]);
	}
	// configure our input muxes by reverse mapping our virtual inputs
	wires.write(out, stew, wireMapping);
}
