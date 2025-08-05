#include "parse_context.h"
#include "decision_tree.h"
#include "unused.h"
#include "state.h"

class StateExpression : public LogicExpression {
public:
	unsigned int state;

	StateExpression(unsigned int state_)
	 : state(state_)
	{ }

	LogicExpression *cloneLogic(void) const
	{
		return new StateExpression(state);
	}

	void collectInputs(Bitmask &nodes __unused) const
	{ }

	bool resolveLogic(const Bitmask &inp __unused) const
	{
		return false;
	}

	bool constantSolve(bool &res __unused) const
	{
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("[state=%u]", state);
	}
};

ParseContextTree::State::State(unsigned int state_,
		const std::map<unsigned int, ParseContextTree::CState> &cout,
		const FilePos &pos)
 : state(state_), filepos(pos)
{
	for (auto it : cout) {
		ParseContextTree::CState cs = it.second;
		std::shared_ptr<ConditionalOutput> cout = cs.second;
		ParseContextTree::CState n;

		n.first = cs.first;
		n.second = std::make_shared<ConditionalOutput>(cout->output, cout->expr->cloneLogic());
		condOut[cout->output] = n;
	}
}

void ParseContextTree::State::mergeConditionalOutput(unsigned int bit,
		LogicExpression *expr, bool value)
{
	auto it = condOut.find(bit);
	if (it != condOut.end()) {
		ParseContextTree::CState cs = it->second;
		std::shared_ptr<ConditionalOutput> cout = cs.second;

		if (value == cs.first) {
			cout->expr = new LogicOrExpression(cout->expr, expr);
		} else {
			expr = new LogicNotExpression(expr);
			cout->expr = new LogicAndExpression(cout->expr, expr);
		}
	} else {
		ParseContextTree::CState cs;

		cs.first = value;
		cs.second = std::make_shared<ConditionalOutput>(bit, expr);
		condOut[bit] = cs;
	}
}


void ParseContextTree::State::collectConditionalOutputs(
		std::list<std::shared_ptr<ConditionalOutput>> &out) const
{
	for (auto it : condOut) {
		ParseContextTree::CState cs = it.second;
		std::shared_ptr<ConditionalOutput> cout = cs.second;
		LogicExpression *expr = cout->expr->cloneLogic();

		if (!cs.first)
			expr = new LogicNotExpression(expr);
		cout = std::make_shared<ConditionalOutput>(cout->output, expr);
		out.push_back(cout);
	}
}

ParseContextTree::ParseContextTree(void)
 : activeState(NULL), defaultState(NULL), current(new Leaf()), parent(NULL)
{ m_ctrlReg = 0;}

ParseContextTree::~ParseContextTree(void)
{
	Node *p;

	for (p = current; p->parent != NULL; p = p->parent);

	delete p;
}

void ParseContextTree::split(LogicExpression *expr)
{
	Branch *oldparent = parent;

	parent = new Branch(current->parent, expr);
	current->parent = parent;

	parent->links[0] = current; // true
	parent->links[1] = current->clone(); // false

	if (oldparent != NULL)
		oldparent->links[oldparent->links[0] != current] = parent;

	switchSplit(true);
}

void ParseContextTree::switchSplit(bool istrue)
{
	current = parent->links[!istrue];
}

void ParseContextTree::join(void)
{
	if (parent->links[0]->equals(parent->links[1])) {
		// all of the children are identical, replace parent
		// with its first child

		// delete current's sibling; it's a duplicate
		delete parent->links[parent->links[0] == current];

		// remove parent's references to children
		parent->links[0] = NULL;
		parent->links[1] = NULL;

		parent = static_cast<Branch *>(parent->parent);

		// adjust grandparent's pointer to parent
		if (parent != NULL)
			parent->links[parent->links[0] != current->parent] = current;

		// get rid of the old parent and adjust current's parent pointer
		delete current->parent;
		current->parent = parent;
	} else {
		current = parent;
		parent = static_cast<Branch *>(current->parent);
	}
}

void ParseContextTree::assign(unsigned int bit, bool value)
{
	// is this a conditional output?
	// FIXME: this 0x10000 thing is hacky
	if (bit >= 0x10000 && bit < 0x20000) {
		LogicExpression *expr = NULL;
		// collapse conditional
		for (Branch *p = parent; p != NULL;
				p = static_cast<Branch *>(p->parent)) {
			StateExpression *eval =
					dynamic_cast<StateExpression *>(p->expr);
			if (eval != NULL)
				continue;
			LogicExpression *cexpr = p->expr->cloneLogic();
			if (expr == NULL)
				expr = cexpr;
			else
				expr = new LogicAndExpression(expr, cexpr);
		}

		bit -= 0x10000;

		if (expr == NULL)
			expr = new LogicTrueExpression();

		if (activeState == NULL) {
			for (auto &&state : states)
				state->mergeConditionalOutput(bit, expr->cloneLogic(), value);

			if (defaultState != NULL)
				defaultState->mergeConditionalOutput(bit, expr->cloneLogic(), value);

			globalState.mergeConditionalOutput(bit, expr, value);
		} else {
			activeState->mergeConditionalOutput(bit, expr, value);
		}
   } else if (bit >= 0x20000) {
      bit -= 0x20000;
      m_ctrlReg |= value << bit;
	} else {
		current->assign(bit, value);
	}
}

void ParseContextTree::setTargetState(unsigned int state)
{
	current->setTargetState(state);
}

void ParseContextTree::splitStateCase(unsigned int state, const FilePos &pos)
{
	// Check for duplicate state cases
	for (const auto &existingState : states) {
		if (existingState->state == state) {
			std::string msg = "Duplicate case for state " + std::to_string(state);
			throw Assertion(msg.c_str(), pos);
		}
	}

	activeState = std::make_shared<State>(state, globalState.condOut, pos);
	states.push_back(activeState);

	split(new StateExpression(state));
}

void ParseContextTree::defaultStateCase(const FilePos &pos)
{
	defaultState = std::make_shared<State>(-1, globalState.condOut, pos);
	activeState = defaultState;
}

void ParseContextTree::enterStateSwitch(std::string var __unused)
{
}

void ParseContextTree::exitStateSwitch(void)
{
	if (defaultState == NULL)
		defaultState = std::make_shared<State>(-1, globalState.condOut, FilePos());
	activeState = NULL;
}

void ParseContextTree::collectStateRecurse(std::list<std::shared_ptr<StateTransition>> &out,
		const Node *node, LogicExpression *pexpr, unsigned int state) const
{
	const Branch *branch = dynamic_cast<const Branch *>(node);

	if (branch != NULL) {
		const StateExpression *sexpr =
				dynamic_cast<const StateExpression *>(branch->expr);
		if (sexpr != NULL) {
			node = branch->links[sexpr->state != state];
			collectStateRecurse(out, node, pexpr, state);
			return;
		}
		LogicExpression *cxpr = branch->expr->cloneLogic();
		LogicExpression *expr;

		if (pexpr == NULL)
			expr = cxpr;
		else
			expr = new LogicAndExpression(pexpr->cloneLogic(), cxpr);

		collectStateRecurse(out, branch->links[0], expr, state);
		// true is always first in the comparison chain, so we don't
		// need to invert the true case here.  it helps to think of this
		// being the an 'elseif' case; the initial 'if' is already false.
		collectStateRecurse(out, branch->links[1], pexpr, state);
	} else {
		const Leaf *leaf = dynamic_cast<const Leaf *>(node);
		unsigned int targetState = leaf->targetState;

		if (targetState == (unsigned int)-1)
			targetState = state;

		out.push_back(std::make_shared<StateTransition>(leaf->output, pexpr, targetState));
	}
}

void ParseContextTree::writeStates(Bitmask &out, const STEW &stew, const DecisionTree &tree,
      uint32_t &ctrlReg) const
{
	std::list<std::shared_ptr<VirtualState>> outputStates;
	std::map<unsigned int, unsigned int> stateMap;
	unsigned int index;
	const Node *root;

   ctrlReg = m_ctrlReg;
	for (root = current; root->parent != NULL; root = root->parent);

	// collect and split all specified states
	for (auto &&state : states) {
		std::shared_ptr<VirtualState> vstate =
				std::make_shared<VirtualState>(state->state, state->filepos);
		collectStateRecurse(vstate->transitions, root, NULL, state->state);
		state->collectConditionalOutputs(vstate->conditionalOutputs);
		tree.splitState(outputStates, vstate, stateMap);
	}

	ASSERT(outputStates.size() <= stew.count,
			"Too many states for STEW configuration");

	// write all specified states
	index = 0;
	for (std::shared_ptr<VirtualState> vstate : outputStates) {
		BitmaskSlice word(out, (stew.count - index - 1) * stew.size, stew.size);
		tree.writeState(word, stew, *vstate, stateMap);

		++index;
	}

	// write all other possible states
	// TODO: we only need to collect the transitions once for all
	//  unspecified states, as there will be no difference between
	//  one unspecified state and another
	for (; index < stew.count; ++index) {
		BitmaskSlice word(out, (stew.count - index - 1) * stew.size, stew.size);
		VirtualState vstate(index, FilePos());

		defaultState->collectConditionalOutputs(vstate.conditionalOutputs);

		collectStateRecurse(vstate.transitions, root, NULL, index);
		tree.writeState(word, stew, vstate, stateMap);
	}
}
