#pragma once

#include <memory>
#include <list>
#include <map>

#include "decision_tree.h"
#include "bitmask.h"
#include "filepos.h"
#include "expr.h"
#include "assert.h"

class ParseContextTree {
	struct Node {
		Node *parent;

		Node(Node *parent_) : parent(parent_) { }
		virtual ~Node(void) { }

		virtual Node *clone(void) const = 0;
		virtual void setTargetState(unsigned int state) = 0;
		virtual void assign(unsigned int bit, bool value) = 0;
		virtual bool equals(const Node *other) const = 0;
	};

	struct Branch : public Node {
		LogicExpression *expr;
		Node *links[2];

		Branch(Node *parent_, LogicExpression *e)
		 : Node(parent_), expr(e)
		{ }

		virtual ~Branch(void)
		{
			delete expr;
			if (links[0])
				delete links[0];
			if (links[1])
				delete links[1];
		}

		Node *clone(void) const
		{
			Branch *n = new Branch(parent, expr->cloneLogic());

			n->links[0] = links[0]->clone();
			n->links[1] = links[1]->clone();

			return n;
		}

		void setTargetState(unsigned int state)
		{
			links[0]->setTargetState(state);
			links[1]->setTargetState(state);
		}

		void assign(unsigned int bit, bool value)
		{
			links[0]->assign(bit, value);
			links[1]->assign(bit, value);
		}

		bool equals(const Node *other) const
		{
			const Branch *branch = dynamic_cast<const Branch *>(other);

			if (branch == NULL)
				return false;
			if (!links[0]->equals(branch->links[0]))
				return false;
			return links[1]->equals(branch->links[1]);
		}
	};

	struct Leaf : public Node {
		DynamicBitmask output;
		unsigned int targetState;

		Leaf(void)
		 : Node(NULL), targetState(-1)
		{ }

		Leaf(Node *parent_, const Leaf &leaf)
		 : Node(parent_), output(leaf.output), targetState(leaf.targetState)
		{ }

		Node *clone(void) const
		{
			return new Leaf(parent, *this);
		}

		void setTargetState(unsigned int state)
		{
			targetState = state;
		}

		void assign(unsigned int bit, bool value)
		{
			output.write(bit, value);
		}

		bool equals(const Node *other) const
		{
			const Leaf *leaf = dynamic_cast<const Leaf *>(other);

			if (leaf == NULL)
				return false;
			if (targetState != leaf->targetState)
				return false;
			return output.equals(leaf->output);
		}
	};

	typedef std::pair<bool, std::shared_ptr<ConditionalOutput>> CState;

	struct State {
		std::map<unsigned int, CState> condOut;
		unsigned int state;
		FilePos filepos;

		State() { }
		State(unsigned int state_,
			const std::map<unsigned int, CState> &cout,
			const FilePos &pos);

		// expr == NULL means true
		void mergeConditionalOutput(unsigned int bit,
				LogicExpression *expr, bool value);

		void collectConditionalOutputs(std::list<std::shared_ptr<ConditionalOutput>> &out) const;
	};

	std::map<unsigned int, CState> condOut;
	std::list<std::shared_ptr<State>> states;
	std::shared_ptr<State> activeState;
	std::shared_ptr<State> defaultState;
	State globalState;
	Node *current;
	Branch *parent;
   uint32_t m_ctrlReg;

	void collectStateRecurse(std::list<std::shared_ptr<StateTransition>> &out,
			const Node *node, LogicExpression *pexpr,
			unsigned int state) const;

public:
	ParseContextTree(void);
	~ParseContextTree(void);

	// split branch on conditional
	void split(LogicExpression *expr);

	// switch to true/false branch
	void switchSplit(bool istrue);

	// join the branches back together
	void join(void);

	// set an output bit for all visible leaves
	void assign(unsigned int bit, bool value);

	// set a target state for all visible leaves
	void setTargetState(unsigned int state);

	// split branch on state==x true/false
	void splitStateCase(unsigned int state, const FilePos &pos);

	// mark as default case for state switch
	void defaultStateCase(const FilePos &pos);

	// state case switch begin
	void enterStateSwitch(std::string var);

	// state case switch end
	void exitStateSwitch(void);

	void writeStates(Bitmask &out, const STEW &stew, const DecisionTree &tree, uint32_t &ctrlReg) const;
};
