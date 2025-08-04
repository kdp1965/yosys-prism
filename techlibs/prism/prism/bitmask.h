#pragma once

#include <string>
#include "assert.h"
#include "strutil.h"
#include "bitops.h"
#include "bitgroup.h"

class Bitmask {
public:
	virtual void reset(void) = 0;
	virtual bool get(unsigned int bit) const = 0;
	virtual void set(unsigned int bit) = 0;
	virtual void clear(unsigned int bit) = 0;
	virtual unsigned int size(void) const = 0;
	virtual unsigned int count(void) const = 0;
	virtual unsigned int fns(unsigned int) const = 0;
	virtual unsigned int ffs(void) const = 0;
	virtual void resize(unsigned int count) = 0;

	void writeInteger(unsigned long value)
	{
		for (unsigned int bit = 0; value != 0; ++bit, value >>= 1)
			write(bit, value & 1);
	}

	unsigned char nibble(unsigned int bit) const
	{
		unsigned int ebit = bit + 4;
		unsigned int value = 0;

		if (ebit > size())
			ebit = size();

		for (unsigned int i = bit; i < ebit; ++i)
			value |= (get(i) << (i - bit));

		return value;
	}

	void write(unsigned int bit, bool value)
	{
		if (value)
			set(bit);
		else
			clear(bit);
	}

	void copyOnes(const Bitmask &from)
	{
		for (unsigned int bit = from.ffs(); bit < from.size(); bit = from.fns(bit))
			set(bit);
	}

	void copy(const Bitmask &from)
	{
		for (unsigned int bit = 0; bit < from.size(); ++bit)
			write(bit, from.get(bit));
	}

	void setIntersection(Bitmask &out, const Bitmask &other) const
	{
		if (&other == this) {
			if (&out == this || &out == &other)
				return;
			out.copy(*this);
			return;
		} else if (&out == &other) {
			other.setIntersection(out, *this);
			return;
		} else if (&out == this) {
			for (unsigned int bit = ffs(); bit < size(); bit = fns(bit)) {
				if (!other.get(bit))
					out.clear(bit);
			}
		} else {
			out.reset();

			for (unsigned int bit = ffs(); bit < size(); bit = fns(bit)) {
				if (other.get(bit))
					out.set(bit);
			}
		}
	}

	void setUnion(Bitmask &out, const Bitmask &other) const
	{
		if (&out == this) {
			out.copy(other);
			return;
		} else if (&out == &other) {
			out.copy(*this);
			return;
		}
		out.copy(other);

		for (unsigned int bit = ffs(); bit < size(); bit = fns(bit))
			out.set(bit);
	}

	std::string to_str(bool lenPrefix=true) const
	{
		char buffer[(size() >> 2) + 2];

		unsigned int nv = (size() + 3) & ~3;
		unsigned int ci = 0;

		while (nv > 0) {
			unsigned char c;
			nv -= 4;

			c = nibble(nv);
			if (c <= 9)
				c += '0';
			else
				c += 'a' - 0xa;
			buffer[ci++] = c;
		}
		if (ci == 0)
			buffer[ci++] = '0';
		buffer[ci] = 0;

		if (lenPrefix)
			return strutil::format("%d'h%s", size(), buffer);
		return std::string(buffer, buffer + ci);
	}
};

#define BITMASK_INDEX(bit) ((bit) / BITS_PER_LONG)
#define BITMASK_BIT(bit) (1ull << ((bit) % BITS_PER_LONG))
#define BITMASK_NLONGS(_nbits) \
	((((_nbits) / BITS_PER_LONG) + !!((_nbits) % BITS_PER_LONG)))

class AbstractBufferBitmask : public Bitmask {
protected:
	unsigned int nbits;
	unsigned long *bits;
public:
	void reset(void)
	{
		for (unsigned int i = 0; i < BITMASK_NLONGS(nbits); ++i)
			bits[i] = 0;
	}

	bool get(unsigned int bit) const
	{
		if (bit >= nbits)
			return false;

		return !!(bits[BITMASK_INDEX(bit)] & BITMASK_BIT(bit));
	}

	void set(unsigned int bit)
	{
		resize(bit + 1);
		bits[BITMASK_INDEX(bit)] |= BITMASK_BIT(bit);
	}

	void clear(unsigned int bit)
	{
		resize(bit + 1);
		bits[BITMASK_INDEX(bit)] &= ~BITMASK_BIT(bit);
	}

	unsigned int size(void) const
	{
		return nbits;
	}

	unsigned int count(void) const
	{
		unsigned int n = 0;

		for (unsigned int i = 0; i < BITMASK_NLONGS(nbits); ++i)
			n += bitops_cnt(bits[i]);

		return n;
	}

	static unsigned int array_ffs(unsigned long *ar, unsigned int nbits)
	{
		for (unsigned int i = 0; i < BITMASK_NLONGS(nbits); ++i) {
			if (ar[i] == 0)
				continue;
			return i * BITS_PER_LONG + bitops_ctz(ar[i]);
		}

		return nbits;
	}

	unsigned int fns(unsigned int cbit) const
	{
		unsigned int idx = BITMASK_INDEX(cbit);
		unsigned int off = BITS_PER_LONG * idx;
		unsigned long bit = BITMASK_BIT(cbit);
		unsigned long val = bits[idx] & ~(bit | (bit - 1));
		if (val != 0)
			return off + bitops_ctz(val);

		off += BITS_PER_LONG;
		if (off >= nbits)
			return nbits;

		return AbstractBufferBitmask::array_ffs(&bits[BITMASK_INDEX(off)], nbits - off) + off;
	}

	unsigned int ffs(void) const
	{
		return AbstractBufferBitmask::array_ffs(bits, nbits);
	}

	bool equals(const AbstractBufferBitmask &other) const
	{
		const AbstractBufferBitmask *small = this;
		const AbstractBufferBitmask *large = &other;

		if (BITMASK_NLONGS(large->nbits) < BITMASK_NLONGS(small->nbits)) {
			const AbstractBufferBitmask *tmp = large;
			large = small;
			small = tmp;
		}

		for (unsigned int i = 0; i < BITMASK_NLONGS(small->nbits); ++i) {
			if (small->bits[i] != large->bits[i])
				return false;
		}

		for (unsigned int i = BITMASK_NLONGS(small->nbits); i < BITMASK_NLONGS(large->nbits); ++i) {
			if (large->bits[i] != 0)
				return false;
		}

		return true;
	}
};

class BufferBitmask : public AbstractBufferBitmask {
public:
	BufferBitmask(unsigned int nbits_)
	{
		nbits = nbits_;
		bits = new unsigned long[BITMASK_NLONGS(nbits)];
		for (unsigned int i = 0; i < BITMASK_NLONGS(nbits); ++i)
			bits[i] = 0;
	}

	BufferBitmask(const BufferBitmask &from)
	{
		nbits = from.size();
		copyOnes(from);
	}

	BufferBitmask(const Bitmask &from)
	{
		nbits = from.size();
		copyOnes(from);
	}

	virtual ~BufferBitmask(void)
	{
		delete[] bits;
	}

	void resize(unsigned int count)
	{
		ASSERT(count <= nbits, "invalid attempt to resize buffer bitmask");
	}
};

class DynamicBitmask : public AbstractBufferBitmask {
	unsigned int nalloc;
public:
	DynamicBitmask(unsigned int nbits_ = 0)
	{
		nalloc = 0;
		nbits = 0;
		bits = NULL;
		resize(nbits_);
	}

	DynamicBitmask(const DynamicBitmask &from)
	{
		nalloc = 0;
		nbits = 0;
		bits = NULL;
		copyOnes(from);
	}

	DynamicBitmask(const Bitmask &from)
	{
		nalloc = 0;
		nbits = 0;
		bits = NULL;
		copyOnes(from);
	}

	virtual ~DynamicBitmask(void)
	{
		if (bits != NULL)
			delete[] bits;
	}

	void resize(unsigned int count)
	{
		unsigned int nwords;
		unsigned long *words;

		if (count <= nbits)
			return;

		nbits = count;

		if (count <= nalloc)
			return;

		nwords = BITMASK_NLONGS(count);
		words = new unsigned long[nwords];
		for (unsigned int i = 0; i < nwords; ++i) {
			if (i < BITMASK_NLONGS(nalloc))
				words[i] = bits[i];
			else
				words[i] = 0;
		}

		nalloc = BITS_PER_LONG * nwords;
		if (bits != NULL)
			delete[] bits;
		bits = words;
	}
};

class IntegerBitmask : public AbstractBufferBitmask {
	unsigned long value;
public:
	IntegerBitmask(unsigned long v, unsigned int nbits_)
	 : value(v)
	{
		nbits = nbits_;
		bits = &value;
	}

	void resize(unsigned int count)
	{
		ASSERT(count <= nbits, "invalid attempt to resize integer bitmask");
	}
};

class BitmaskSlice : public Bitmask {
	Bitmask &source;
	unsigned int offset;
	unsigned int nbits;

	void init(void)
	{
		if (offset > source.size())
			offset = source.size();
		if (offset + nbits > source.size())
			nbits = source.size() - offset;
	}

public:
	BitmaskSlice(BitmaskSlice &src, unsigned int off, unsigned int sz)
	 : source(src.source), offset(src.offset + off), nbits(sz)
	{
		init();
	}

	BitmaskSlice(Bitmask &src, unsigned int off, unsigned int sz)
	 : source(src), offset(off), nbits(sz)
	{
		init();
	}

	void reset(void)
	{
		for (unsigned int bit = ffs(); bit < size(); bit = fns(bit))
			clear(bit);
	}

	bool get(unsigned int bit) const
	{
		if (bit >= nbits)
			return false;

		return source.get(bit + offset);
	}

	void set(unsigned int bit)
	{
		ASSERT(bit < nbits,);
		source.set(bit + offset);
	}

	void clear(unsigned int bit)
	{
		ASSERT(bit < nbits,);
		source.clear(bit + offset);
	}

	unsigned int size(void) const
	{
		return nbits;
	}

	unsigned int count(void) const
	{
		unsigned int cnt = 0;

		for (unsigned int bit = ffs(); bit < size(); bit = fns(bit))
			++cnt;

		return cnt;
	}

	unsigned int fns(unsigned int cbit) const
	{
		unsigned int ret;

		ret = source.fns(cbit + offset) - offset;
		if (ret > nbits)
			return nbits;
		return ret;
	}

	unsigned int ffs(void) const
	{
		unsigned int ret;

		if (offset == 0)
			return source.ffs();

		ret = source.fns(offset - 1) - offset;
		if (ret > nbits)
			return nbits;
		return ret;
	}

	void resize(unsigned int count)
	{
		ASSERT(count <= nbits, "invalid attempt to resize bitmask slice");
	}
};

class MappedBitmask : public Bitmask {
	Bitmask &source;
	const BitGroup &group;
public:
	MappedBitmask(Bitmask &src, const BitGroup &grp)
	 : source(src), group(grp)
	{ }

	void reset(void)
	{
		for (unsigned int bit = ffs(); bit < size(); bit = fns(bit))
			clear(bit);
	}

	bool get(unsigned int bit) const
	{
		return source.get(group.map(bit));
	}

	void set(unsigned int bit)
	{
		source.set(group.map(bit));
	}

	void clear(unsigned int bit)
	{
		source.clear(group.map(bit));
	}

	unsigned int size(void) const
	{
		unsigned int ssz = source.size();
		unsigned int gsz = group.size();
		if (ssz < gsz)
			return ssz;
		return gsz;
	}

	unsigned int count(void) const
	{
		unsigned int cnt = 0;

		for (unsigned int bit = ffs(); bit < size(); bit = fns(bit))
			++cnt;

		return cnt;
	}

	unsigned int fns(unsigned int cbit) const
	{
		++cbit;
		while (cbit < size() && !get(cbit))
			++cbit;

		return cbit;
	}

	unsigned int ffs(void) const
	{
		unsigned int cbit = 0;

		while (cbit < size() && !get(cbit))
			++cbit;

		return cbit;
	}

	void resize(unsigned int count)
	{
		ASSERT(count <= size(), "invalid attempt to resize mapped bitmask");
	}
};
