
#include "wire_map.h"

WireMap::WireMap(const Config &cfg)
 : muxes(cfg.muxes), nInput(cfg.muxes.nMux),
   nVirtualOutput(cfg.nVirtualOutput), nMappings(cfg.mappings.size())
{
	fullMap = new unsigned int[nVirtualOutput];
	map = new unsigned int[2 * nMappings];
	counts = new unsigned int[nInput];
	unsigned int index = 0;

	for (unsigned int i = 0; i < nInput; ++i)
		counts[i] = 1;

	for (std::pair<unsigned int, unsigned int> item : cfg.mappings) {
		map[index++] = item.first;
		map[index++] = item.second;
		counts[item.second]++;
	}

	for (unsigned int i = 0; i < nVirtualOutput; ++i)
		fullMap[i] = lookup(i);
}

WireMap::~WireMap(void)
{
	delete[] map;
	delete[] counts;
	delete[] fullMap;
}

unsigned int WireMap::lookup(unsigned int id) const
{
	unsigned int l = 0;
	unsigned int h = nMappings;

	while (l < h) {
		unsigned int m = (l + h) >> 1;
		int c = id - map[(m << 1) + 0];

		if (c > 0)
			l = m + 1;
		else if (c < 0)
			h = m;
		else
			return map[(m << 1) + 1];
	}

	return id - l;
}

void WireMap::bestFit(const Bitmask &outputs, unsigned int nparty,
		std::list<unsigned int> &out) const
{
	unsigned int table[nInput];
	unsigned int bestScore = 0;
	unsigned int bestInput = -1;

	for (unsigned int i = 0; i < nInput; ++i)
		table[i] = 0;

	for (unsigned int r = outputs.ffs(); r < outputs.size(); r = outputs.fns(r))
		table[fullMap[r]]++;

	for (unsigned int i = 0; i < nInput; ++i) {
		unsigned int score = 1;

		if (!table[i])
			continue;

		if (counts[i] == table[i]) {
			if (table[i] == nparty) {
				bestScore = 0xff;
				bestInput = i;
				break;
			}
			++score;
		}

		if (table[i] == nparty)
			++score;

		if ((nparty != 1) && (table[i] != 1))
			++score;

		if (score > bestScore) {
			bestScore = score;
			bestInput = i;
		}
	}

	ASSERT(bestScore != 0, "Unable to find suitable wire mapping for input");

	for (unsigned int i = 0; i < nVirtualOutput; ++i) {
		if (fullMap[i] == bestInput)
			out.push_back(i);
	}
}

void WireMap::write(Bitmask &mask, const STEW &stew, unsigned int *outputMapping) const
{
	unsigned int inputMapping[nInput];

	for (unsigned int i = 0; i < nVirtualOutput; ++i)
		inputMapping[fullMap[i]] = outputMapping[i];

	muxes.write(mask, stew, inputMapping);
}
