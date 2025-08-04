#pragma once

#include <string>

#include "bitgroup.h"
#include "bitmask.h"
#include "strutil.h"
#include "unused.h"

class Expression {
public:
	virtual ~Expression(void) { }
	virtual Expression *clone(void) const = 0;
	virtual void collectInputs(Bitmask &nodes) const = 0;
	virtual void resolve(const Bitmask &inp, Bitmask &out) const = 0;

	virtual std::string to_str(void) const = 0;
};

class IdentifierExpression : public Expression {
	MappedBitGroup group;

public:
	IdentifierExpression(const BitGroup &grp)
	 : group(grp)
	{ }

	Expression *clone(void) const
	{
		return new IdentifierExpression(group);
	}

	void collectInputs(Bitmask &nodes) const
	{
		for (unsigned int bit = 0; bit < group.size(); ++bit)
			nodes.set(group.map(bit));
	}

	void resolve(const Bitmask &inp, Bitmask &out) const
	{
		for (unsigned int bit = 0; bit < group.size(); ++bit)
			out.write(bit, inp.get(group.map(bit)));
	}

	std::string to_str(void) const
	{
		if (group.size() == 1)
			return strutil::format("I%d", group.map(0));
		std::string out;

		for (unsigned int bit = 0; bit < group.size(); ++bit)
			out = strutil::format("%s,%d", out.c_str(), group.map(bit));

		return strutil::format("I{%s}", out.c_str());
	}
};

class LogicExpression : public Expression {
public:
	Expression *clone(void) const
	{
		return cloneLogic();
	}

	void resolve(const Bitmask &inp, Bitmask &out) const
	{
		out.write(0, resolveLogic(inp));
	}

	virtual bool constantSolve(bool &res) const = 0;

	virtual LogicExpression *cloneLogic(void) const = 0;
	virtual bool resolveLogic(const Bitmask &inp) const = 0;
};

class LogicTrueExpression : public LogicExpression {
public:
	LogicExpression *cloneLogic(void) const
	{
		return new LogicTrueExpression();
	}

	void collectInputs(Bitmask &nodes __unused) const
	{ }

	bool resolveLogic(const Bitmask &inp __unused) const
	{
		return true;
	}

	bool constantSolve(bool &res) const
	{
		res = true;
		return true;
	}

	std::string to_str(void) const
	{
		return "true";
	}
};

class LogicFalseExpression : public LogicExpression {
public:
	LogicExpression *cloneLogic(void) const
	{
		return new LogicFalseExpression();
	}

	void collectInputs(Bitmask &nodes __unused) const
	{ }

	bool resolveLogic(const Bitmask &inp __unused) const
	{
		return false;
	}

	bool constantSolve(bool &res) const
	{
		res = false;
		return true;
	}

	std::string to_str(void) const
	{
		return "false";
	}
};

class LogicReduceExpression : public LogicExpression {
protected:
	Expression *child;
public:
	LogicReduceExpression(Expression *p)
	 : child(p)
	{ }

	virtual ~LogicReduceExpression(void)
	{
		delete child;
	}

	void collectInputs(Bitmask &nodes) const
	{
		child->collectInputs(nodes);
	}

	bool resolveLogic(const Bitmask &inp) const
	{
		DynamicBitmask bitmask;

		child->resolve(inp, bitmask);

		return reduce(bitmask);
	}

	virtual bool reduce(const Bitmask &val) const = 0;
};

class LogicReduceOrExpression : public LogicReduceExpression {
public:
	LogicReduceOrExpression(Expression *p)
	 : LogicReduceExpression(p)
	{ }

	LogicExpression *cloneLogic(void) const
	{
		LogicExpression *lexpr = dynamic_cast<LogicExpression *>(child);
		if (lexpr != NULL)
			return lexpr->cloneLogic();
		return new LogicReduceOrExpression(child->clone());
	}

	bool reduce(const Bitmask &val) const
	{
		return val.count() != 0;
	}

	bool constantSolve(bool &res) const
	{
		const LogicExpression *lexpr = dynamic_cast<const LogicExpression *>(child);
		if (lexpr != NULL)
			return lexpr->constantSolve(res);
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("|%s", child->to_str().c_str());
	}
};

class LogicReduceAndExpression : public LogicReduceExpression {
public:
	LogicReduceAndExpression(Expression *p)
	 : LogicReduceExpression(p)
	{ }

	LogicExpression *cloneLogic(void) const
	{
		return new LogicReduceAndExpression(child->clone());
	}

	bool reduce(const Bitmask &val) const
	{
		return val.count() == val.size();
	}

	bool constantSolve(bool &res __unused) const
	{
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("&%s", child->to_str().c_str());
	}
};

class LogicReduceXorExpression : public LogicReduceExpression {
public:
	LogicReduceXorExpression(Expression *p)
	 : LogicReduceExpression(p)
	{ }

	LogicExpression *cloneLogic(void) const
	{
		return new LogicReduceXorExpression(child->clone());
	}

	bool reduce(const Bitmask &val) const
	{
		return val.count() & 1;
	}

	bool constantSolve(bool &res __unused) const
	{
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("^%s", child->to_str().c_str());
	}
};

class LogicNotExpression : public LogicExpression {
	LogicExpression *child;
public:
	LogicNotExpression(LogicExpression *p)
	 : child(p)
	{ }

	virtual ~LogicNotExpression(void)
	{
		delete child;
	}

	LogicExpression *cloneLogic(void) const
	{
		const LogicNotExpression *inv = this;
		const LogicNotExpression *next;
		int isinv = 1;

		while ((next = dynamic_cast<const LogicNotExpression *>(inv->child)) != NULL) {
			inv = next;
			isinv = !isinv;
		}

		if (isinv)
			return new LogicNotExpression(inv->child->cloneLogic());
		return inv->child->cloneLogic();
	}

	void collectInputs(Bitmask &nodes) const
	{
		child->collectInputs(nodes);
	}

	bool resolveLogic(const Bitmask &inp) const
	{
		return !child->resolveLogic(inp);
	}

	bool constantSolve(bool &res) const
	{
		if (child->constantSolve(res)) {
			res = !res;
			return true;
		}
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("!%s", child->to_str().c_str());
	}
};

class EqualityExpression : public LogicExpression {
	Expression *lhs;
	Expression *rhs;
public:
	EqualityExpression(Expression *l, Expression *r)
	 : lhs(l), rhs(r)
	{ }

	virtual ~EqualityExpression(void)
	{
		delete lhs;
		delete rhs;
	}

	void collectInputs(Bitmask &nodes) const
	{
		lhs->collectInputs(nodes);
		rhs->collectInputs(nodes);
	}

	LogicExpression *cloneLogic(void) const
	{
		LogicExpression *llhs = dynamic_cast<LogicExpression *>(lhs);
		LogicExpression *lrhs = dynamic_cast<LogicExpression *>(rhs);

		if (llhs != NULL && lrhs != NULL) {
			LogicExpression *out = NULL;
			bool res;

			if (llhs->constantSolve(res))
				out = lrhs->cloneLogic();
			else if (lrhs->constantSolve(res))
				out = llhs->cloneLogic();

			if (out != NULL) {
				if (!res)
					return LogicNotExpression(out).cloneLogic();
				return out;
			}
		}

		return new EqualityExpression(lhs->clone(), rhs->clone());
	}

	bool resolveLogic(const Bitmask &inp) const
	{
		DynamicBitmask lk;
		DynamicBitmask rk;

		lhs->resolve(inp, lk);
		rhs->resolve(inp, rk);

		unsigned int l = lk.ffs();
		unsigned int r = rk.ffs();

		while (l < lk.size() && r < rk.size() && l == r) {
			l = lk.fns(l);
			r = rk.fns(r);
		}

		return l >= lk.size() && r >= rk.size();
	}

	bool constantSolve(bool &res __unused) const
	{
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("(%s == %s)", lhs->to_str().c_str(), rhs->to_str().c_str());
	}
};

class BinaryLogicExpression : public LogicExpression {
protected:
	LogicExpression *lhs;
	LogicExpression *rhs;
public:
	BinaryLogicExpression(LogicExpression *l, LogicExpression *r)
	 : lhs(l), rhs(r)
	{ }

	virtual ~BinaryLogicExpression(void)
	{
		delete lhs;
		delete rhs;
	}

	void collectInputs(Bitmask &nodes) const
	{
		lhs->collectInputs(nodes);
		rhs->collectInputs(nodes);
	}

	bool resolveLogic(const Bitmask &inp) const
	{
		return resolveBinaryLogic(lhs->resolveLogic(inp), rhs->resolveLogic(inp));
	}

	virtual bool resolveBinaryLogic(bool lv, bool rv) const = 0;
};

class LogicOrExpression : public BinaryLogicExpression {
public:
	LogicOrExpression(LogicExpression *l, LogicExpression *r)
	 : BinaryLogicExpression(l, r)
	{ }

	LogicExpression *cloneLogic(void) const
	{
		bool res = false;

		if (lhs->constantSolve(res) && !res)
			return rhs->cloneLogic();
		if (rhs->constantSolve(res) && !res)
			return lhs->cloneLogic();
		return new LogicOrExpression(lhs->cloneLogic(), rhs->cloneLogic());
	}

	bool resolveBinaryLogic(bool lv, bool rv) const
	{
		return lv || rv;
	}

	bool constantSolve(bool &res) const
	{
		if (lhs->constantSolve(res) && res)
			return true;
		if (rhs->constantSolve(res) && res)
			return true;
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("(%s || %s)", lhs->to_str().c_str(), rhs->to_str().c_str());
	}
};

class LogicAndExpression : public BinaryLogicExpression {
public:
	LogicAndExpression(LogicExpression *l, LogicExpression *r)
	 : BinaryLogicExpression(l, r)
	{ }

	LogicExpression *cloneLogic(void) const
	{
		bool res = false;

		if (lhs->constantSolve(res) && res)
			return rhs->cloneLogic();
		if (rhs->constantSolve(res) && res)
			return lhs->cloneLogic();
		return new LogicAndExpression(lhs->cloneLogic(), rhs->cloneLogic());
	}

	bool resolveBinaryLogic(bool lv, bool rv) const
	{
		return lv && rv;
	}

	bool constantSolve(bool &res) const
	{
		if (lhs->constantSolve(res) && !res)
			return true;
		if (rhs->constantSolve(res) && !res)
			return true;
		return false;
	}

	std::string to_str(void) const
	{
		return strutil::format("(%s && %s)", lhs->to_str().c_str(), rhs->to_str().c_str());
	}
};

class BitwiseNotExpression : public Expression {
protected:
	Expression *child;
public:
	BitwiseNotExpression(Expression *c)
	 : child(c)
	{ }

	virtual ~BitwiseNotExpression(void)
	{
		delete child;
	}

	Expression *clone(void) const
	{
		return new BitwiseNotExpression(child->clone());
	}

	void collectInputs(Bitmask &nodes) const
	{
		child->collectInputs(nodes);
	}

	void resolve(const Bitmask &inp, Bitmask &out) const
	{
		DynamicBitmask val;

		child->resolve(inp, val);

		out.resize(val.size());
		for (unsigned int bit = 0; bit < val.size(); ++bit) {
			if (!val.get(bit))
				out.set(bit);
		}
	}

	std::string to_str(void) const
	{
		return strutil::format("~%s", child->to_str().c_str());
	}
};

class BitwiseExpression : public Expression {
protected:
	Expression *lhs;
	Expression *rhs;
public:
	BitwiseExpression(Expression *l, Expression *r)
	 : lhs(l), rhs(r)
	{ }

	virtual ~BitwiseExpression(void)
	{
		delete lhs;
		delete rhs;
	}

	void collectInputs(Bitmask &nodes) const
	{
		lhs->collectInputs(nodes);
		rhs->collectInputs(nodes);
	}

	void resolve(const Bitmask &inp, Bitmask &out) const
	{
		DynamicBitmask lk;
		DynamicBitmask rk;

		lhs->resolve(inp, lk);
		rhs->resolve(inp, rk);

		resolveBitwise(lk, rk, out);
		out.resize(lk.size() >= rk.size() ? lk.size() : rk.size());
	}

	virtual void resolveBitwise(const Bitmask &lk, const Bitmask &rk, Bitmask &out) const = 0;
};

class BitwiseAndExpression : public BitwiseExpression {
public:
	BitwiseAndExpression(Expression *l, Expression *r)
	 : BitwiseExpression(l, r)
	{ }

	Expression *clone(void) const
	{
		return new BitwiseAndExpression(lhs->clone(), rhs->clone());
	}

	void resolveBitwise(const Bitmask &lk, const Bitmask &rk, Bitmask &out) const
	{
		lk.setIntersection(out, rk);
	}

	std::string to_str(void) const
	{
		return strutil::format("(%s & %s)", lhs->to_str().c_str(), rhs->to_str().c_str());
	}
};

class BitwiseOrExpression : public BitwiseExpression {
public:
	BitwiseOrExpression(Expression *l, Expression *r)
	 : BitwiseExpression(l, r)
	{ }

	Expression *clone(void) const
	{
		return new BitwiseOrExpression(lhs->clone(), rhs->clone());
	}

	void resolveBitwise(const Bitmask &lk, const Bitmask &rk, Bitmask &out) const
	{
		lk.setUnion(out, rk);
	}

	std::string to_str(void) const
	{
		return strutil::format("(%s | %s)", lhs->to_str().c_str(), rhs->to_str().c_str());
	}
};

class BitwiseXorExpression : public BitwiseExpression {
public:
	BitwiseXorExpression(Expression *l, Expression *r)
	 : BitwiseExpression(l, r)
	{ }

	Expression *clone(void) const
	{
		return new BitwiseXorExpression(lhs->clone(), rhs->clone());
	}

	void resolveBitwise(const Bitmask &lk, const Bitmask &rk, Bitmask &out) const
	{
		if (&lk == &rk)
			return;

		if (lk.size() <= rk.size()) {
			for (unsigned int bit = 0; bit < lk.size(); ++bit) {
				if (rk.get(bit) != lk.get(bit))
					out.set(bit);
			}

			for (unsigned int bit = lk.size(); bit < rk.size(); ++bit) {
				if (rk.get(bit))
					out.set(bit);
			}
		} else {
			for (unsigned int bit = 0; bit < rk.size(); ++bit) {
				if (rk.get(bit) != lk.get(bit))
					out.set(bit);
			}

			for (unsigned int bit = rk.size(); bit < lk.size(); ++bit) {
				if (lk.get(bit))
					out.set(bit);
			}
		}
	}

	std::string to_str(void) const
	{
		return strutil::format("(%s ^ %s)", lhs->to_str().c_str(), rhs->to_str().c_str());
	}
};

class BitwiseXnorExpression : public BitwiseExpression {
public:
	BitwiseXnorExpression(Expression *l, Expression *r)
	 : BitwiseExpression(l, r)
	{ }

	Expression *clone(void) const
	{
		return new BitwiseXnorExpression(lhs->clone(), rhs->clone());
	}

	void resolveBitwise(const Bitmask &lk, const Bitmask &rk, Bitmask &out) const
	{
		if (lk.size() <= rk.size()) {
			for (unsigned int bit = 0; bit < lk.size(); ++bit) {
				if (rk.get(bit) == lk.get(bit))
					out.set(bit);
			}

			for (unsigned int bit = lk.size(); bit < rk.size(); ++bit) {
				if (!rk.get(bit))
					out.set(bit);
			}
		} else {
			for (unsigned int bit = 0; bit < rk.size(); ++bit) {
				if (rk.get(bit) == lk.get(bit))
					out.set(bit);
			}

			for (unsigned int bit = rk.size(); bit < lk.size(); ++bit) {
				if (!lk.get(bit))
					out.set(bit);
			}
		}
	}

	std::string to_str(void) const
	{
		return strutil::format("(%s ~^ %s)", lhs->to_str().c_str(), rhs->to_str().c_str());
	}
};

class ConstantExpression : public Expression {
	DynamicBitmask mask;

public:
	ConstantExpression(const Bitmask &mask)
	 : mask(mask)
	{ }

	Expression *clone(void) const
	{
		return new ConstantExpression(mask);
	}

	void collectInputs(Bitmask &nodes __unused) const
	{ }

	void resolve(const Bitmask &inp __unused, Bitmask &out) const
	{
		out.copy(mask);
	}

	std::string to_str(void) const
	{
		return mask.to_str();
	}
};
