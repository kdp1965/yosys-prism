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

		// A trailing "stay" or a trailing "else -> next state" fits without a
		// split.  "else -> next" is encoded with the INC bit, which in hardware
		// also starts (or continues) the automatic loop: the following states'
		// no-match path returns to the first INC state, so a set of conditions
		// spread over consecutive states keeps being evaluated.  (The same INC
		// joins the partial states created below.)
		if (xits.size() == nStaticComponents + 1 &&
		    (xits.back()->isStay(vs->index) || xits.back()->isNext(vs->index))) {
			if (xits.back()->isNext(vs->index))
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
		vs->row = out.size();
		out.push_back(vs);
		vs = lower;
	}
	vs->row = out.size();
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

	// A trailing unconditional "stay" always takes the default (no-match)
	// path, whatever the number of decision trees, so it never costs a tree
	// and a split lower state still loops back to its upper half.
	const bool trailingStay = !vs.transitions.empty() &&
			vs.transitions.back()->isStay(vs.index);

	// A trailing "else -> next state" behind at least one condition also
	// takes the default path plus the INC bit even when a tree is still
	// free, so that it starts / continues the automatic loop.  It must really
	// be the next STEW row; otherwise (states declared out of order) it is
	// left as an ordinary tree jump.  A lone unconditional jump has no
	// "else" and stays a tree jump too.
	bool trailingNext = false;
	if (!vs.partial && vs.transitions.size() >= 2 &&
	    vs.transitions.size() <= nStaticComponents &&
	    vs.transitions.back()->isNext(vs.index)) {
		auto it = stateMap.find(vs.transitions.back()->state);
		trailingNext = (it != stateMap.end() && it->second == vs.row + 1);
	}
	if (vs.partial && vs.transitions.size() == nStaticComponents + 1) {
		// INC chosen by splitState for a user "else -> next": check the row
		auto it = stateMap.find(vs.transitions.back()->state);
		ASSERTVS(vs, it != stateMap.end() && it->second == vs.row + 1,
				"'else' to the next state must target the next declared state");
	}

	// write component output values and jump targets
	comp = 0;
	for (std::shared_ptr<StateTransition> x : vs.transitions) {
		DEBUG("    %s\n", x->to_str().c_str());
		const bool isDefault = (comp == nStaticComponents) ||
				((trailingStay || trailingNext) && x == vs.transitions.back());
		STEW::Item stew_out = stew.slice(STEW::OUT,
				isDefault ? nStaticComponents : comp);
		ASSERTVS(vs, stew_out.type != STEW::NIL,
				"STEW OUT configuration doesn't match"
				" decision-tree configuration");
		BitmaskSlice slice_out(out, stew_out.offset, stew_out.size);
		if (!isDefault) {
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
	if (trailingNext) {
		STEW::Item inc = stew.slice(STEW::INC, 0);
		out.set(inc.offset);
	}
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

		// unused decision trees must never fire (their jump target would be
		// state 0); unused conditional outputs are simply low.  A row with no
		// transitions at all (a state the chroma never defines) keeps the old
		// behaviour of jumping to state 0 through tree 0, so a stray state
		// index resets the FSM instead of parking it.
		if (exprs[comp])
			c->write(slice, sgrp, *exprs[comp]);
		else if (comp == 0 && vs.transitions.empty())
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
