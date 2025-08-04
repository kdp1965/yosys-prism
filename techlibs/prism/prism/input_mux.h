#pragma once

#include "bitmask.h"
#include "stew.h"

class InputMux {
	unsigned int nBits;
	unsigned int nMux;
public:
	struct Config {
		unsigned int nBits;
		unsigned int nMux;
	};

	InputMux(const Config &cfg)
	 : nBits(cfg.nBits), nMux(cfg.nMux)
	{ }

	void write(Bitmask &mask, const STEW &stew, unsigned int *mapping) const
	{
		STEW::Item cfg = stew.slice(STEW::MUX);

		for (unsigned int mux = 0; mux < nMux; ++mux) {
			BitmaskSlice slice(mask, mux * nBits + cfg.offset, nBits);
			slice.writeInteger(mapping[mux]);
		}
	}
};
