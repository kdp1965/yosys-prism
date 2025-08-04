#pragma once

#include <list>
#include <string>
#include <memory>

#include "bitmask.h"
#include "strutil.h"
#include "filepos.h"
#include "expr.h"

struct StateCondition {
	LogicExpression *expr;

	StateCondition(LogicExpression *e)
	 : expr(e ? e : new LogicTrueExpression())
	{ }

	virtual ~StateCondition(void)
	{
		delete expr;
	}

	virtual std::string to_str(void) const = 0;
};

struct StateTransition : public StateCondition {
	DynamicBitmask output;
	unsigned int state;

	StateTransition(const Bitmask &data, LogicExpression *e, unsigned int state)
	 : StateCondition(e), output(data), state(state)
	{ }

	bool isFallthrough(unsigned int in)
	{
		if (state != in)
			return false;

		return dynamic_cast<LogicTrueExpression *>(expr) != NULL;
	}

	void writeOutput(Bitmask &out) const
	{
		for (unsigned int i = 0; i < out.size(); ++i)
			out.write(i, output.get(i));
	}

	void writeState(Bitmask &out, std::map<unsigned int, unsigned int> &stateMap) const
	{
		out.writeInteger(stateMap[state]);
	}

	std::string to_str(void) const
	{
		return strutil::format("XIT(state=%u, output=%s, expr=%s)", state,
				output.to_str().c_str(),
				expr ? expr->to_str().c_str() : "true");
	}
};

struct ConditionalOutput : public StateCondition {
	unsigned int output;

	ConditionalOutput(unsigned int output, LogicExpression *e)
	 : StateCondition(e), output(output)
	{ }

	std::string to_str(void) const
	{
		return strutil::format("COUT(output=%u, expr=%s)", output, expr->to_str().c_str());
	}
};

struct VirtualState {
	unsigned int index;
	FilePos filepos;
	bool partial;
	DynamicBitmask partialOutput;

	std::list<std::shared_ptr<StateTransition>> transitions;
	std::list<std::shared_ptr<ConditionalOutput>> conditionalOutputs;

	VirtualState(unsigned int id, const FilePos &pos)
	 : index(id), filepos(pos), partial(false)
	{ }

	void collectSteadyState(Bitmask &out)
	{
		unsigned int idx = 0;

		for (std::shared_ptr<StateTransition> x : transitions) {
			if (idx++ == 0) {
				out.copy(x->output);
			} else {
				out.setIntersection(out, x->output);
			}
		}
	}
};
