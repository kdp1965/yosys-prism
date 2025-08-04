#pragma once

#include <vector>

// State Table Execution Word descriptor.  not an actual STEW, but a descriptor
// of how the bits are aligned.
struct STEW {
	enum Type {
		NIL, INC, MUX, JMP, OUT, CFG,
	};

	struct Item {
		Type type;
		unsigned int offset;
		unsigned int size;
	};

	unsigned int count;
	unsigned int size;
	std::vector<Item> items;

	// FIXME: theoretically slow.  in practice, the table of items will
	//   be relativly small, but that's no excuse to be lazy
	Item slice(Type type, unsigned int which = 0) const
	{
		for (Item item : items) {
			if (item.type != type)
				continue;
			if (which == 0)
				return item;
			--which;
		}
		return Item({NIL, 0, 0});
	}
};
