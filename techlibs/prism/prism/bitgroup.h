#pragma once

#include "assert.h"

class BitGroup {
public:
	virtual unsigned int map(unsigned int idx) const = 0;
	virtual unsigned int size(void) const = 0;
};

class OffsetBitGroup : public BitGroup {
	unsigned int nbit;
	unsigned int offset;
public:
	OffsetBitGroup(unsigned int off, unsigned int size)
	 : nbit(size), offset(off)
	{ }
	unsigned int map(unsigned int idx) const
	{
		ASSERT(idx < nbit,);
		return idx + offset;
	}
	unsigned int size(void) const
	{
		return nbit;
	}
};

class SliceBitGroup : public BitGroup {
	const BitGroup &parent;
	unsigned int nbit;
	unsigned int offset;

public:
	SliceBitGroup(const BitGroup &p, unsigned int off, unsigned int size)
	 : parent(p), nbit(size), offset(off)
	{ }
	unsigned int map(unsigned int idx) const
	{
		ASSERT(idx < nbit,);
		return parent.map(idx + offset);
	}
	unsigned int size(void) const
	{
		return nbit;
	}
};

class ConcatBitGroup : public BitGroup {
	const BitGroup &hi;
	const BitGroup &lo;
public:
	ConcatBitGroup(const BitGroup &high, const BitGroup &low)
	 : hi(high), lo(low)
	{ }
	unsigned int map(unsigned int idx) const
	{
		ASSERT(idx < hi.size() + lo.size(),);
		if (idx < lo.size())
			return lo.map(idx);
		return hi.map(idx - lo.size());
	}
	unsigned int size(void) const
	{
		return hi.size() + lo.size();
	}
};

class MappedBitGroup : public BitGroup {
	unsigned int *bits;
	unsigned int nbits;
public:
	MappedBitGroup(const BitGroup &grp)
	 : bits(NULL), nbits(0)
	{
		concat(grp);
	}

	MappedBitGroup(const unsigned int *map, unsigned int n)
	 : nbits(n)
	{
		bits = new unsigned int[(nbits + 31) & ~31];

		for (unsigned int i = 0; i < nbits; ++i)
			bits[i] = map[i];
	}

	MappedBitGroup(void)
	 : bits(NULL), nbits(0)
	{ }

	virtual ~MappedBitGroup(void)
	{
		if (bits)
			delete[] bits;
	}

	void concat(const BitGroup &grp)
	{
		unsigned int unbits = nbits + grp.size();

		if (unbits > ((nbits + 31) & ~31)) {
			unsigned int *ubits = new unsigned int[(unbits + 31) & ~31];

			for (unsigned int i = 0; i < nbits; ++i)
				ubits[i] = bits[i];

			if (bits)
				delete[] bits;
			bits = ubits;
		}

		for (unsigned int i = nbits; i < unbits; ++i)
			bits[i] = grp.map(i - nbits);

		nbits = unbits;
	}

	unsigned int map(unsigned int idx) const
	{
		ASSERT(idx < nbits,);
		return bits[idx];
	}
	unsigned int size(void) const
	{
		return nbits;
	}
};
