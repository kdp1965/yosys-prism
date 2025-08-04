#pragma once

#include <string>

#include "decision_tree.h"
#include "stew.h"

struct PrismConfig {
	std::string title;
	std::string version;
	static std::string config;
//	std::string module_name;
	DecisionTree::Config tree;
	STEW stew;

	static int parse(const std::string &filename, PrismConfig &pc);
	static void fallback(PrismConfig &pc);
};
