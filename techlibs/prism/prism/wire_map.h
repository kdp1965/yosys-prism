#pragma once

#include <list>
#include "bitmask.h"
#include "input_mux.h"
#include "stew.h"

class WireMap {
	InputMux muxes;
	unsigned int nInput;
	unsigned int nVirtualOutput;
	unsigned int nMappings;
	unsigned int *map;
	unsigned int *fullMap;
	unsigned int *counts;

public:
	struct Config {
		InputMux::Config muxes;
		unsigned int nVirtualOutput;
		std::list<std::pair<unsigned int, unsigned int>> mappings;
	};

	WireMap(const Config &cfg);
	~WireMap(void);

	unsigned int lookup(unsigned int id) const;
	void bestFit(const Bitmask &outputs, unsigned int nparty,
			std::list<unsigned int> &out) const;
	void write(Bitmask &mask, const STEW &stew, unsigned int *outputMapping) const;
};
